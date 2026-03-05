# RFC 9254 — CORECONF: Gestión de dispositivos IoT con CoAP + YANG + CBOR

## ¿Qué problema resuelve?

En redes de dispositivos IoT (sensores, actuadores, routers) necesitamos una forma estándar de:
- **Leer** la configuración y estado de un dispositivo
- **Modificar** parámetros (temperatura umbral, intervalo de muestreo...)
- **Borrar** datos obsoletos
- **Reemplazar** configuraciones completas

En Internet clásico esto se hace con NETCONF (XML sobre TCP/SSH) o RESTCONF (JSON/XML sobre HTTP). Pero en IoT:
- Los dispositivos tienen **poca RAM** (kilobytes)
- La red es **lossy** (se pierden paquetes)
- La energía es **limitada** (baterías)

CORECONF resuelve esto usando:
- **CoAP** en vez de HTTP (UDP, mensajes pequeños)
- **CBOR** en vez de JSON/XML (binario, compacto)
- **SIDs** (números enteros) en vez de nombres de nodos YANG

---

## La pila de protocolos

```
┌─────────────────────────────────────┐
│         CORECONF (RFC 9254)         │  ← gestión de dispositivos
├─────────────────────────────────────┤
│        YANG models (RFC 7950)        │  ← schema de datos
├─────────────────────────────────────┤
│    CBOR + SID mapping (RFC 9132)    │  ← serialización compacta
├─────────────────────────────────────┤
│          CoAP (RFC 7252)            │  ← transporte IoT
├─────────────────────────────────────┤
│            UDP / DTLS               │  ← red
└─────────────────────────────────────┘
```

---

## SIDs: el corazón de CORECONF

Un **SID (Schema Item iDentifier)** es un número entero de 64 bits que identifica de forma única un nodo del árbol YANG. En vez de escribir:

```
/ietf-interfaces:interfaces/interface[name='eth0']/ietf-ip:ipv4/address[ip='10.0.0.1']/prefix-length
```

Se escribe simplemente: **`58775`** (un número). Esto ahorra bytes en la red.

### ¿Cómo se asignan los SIDs?

Los SIDs se asignan en bloques por la IANA. El dueño de un módulo YANG solicita un rango (p.ej. 1000-1099) y asigna los números a sus nodos. El mapeo SID↔ruta YANG se publica en archivos `.sid` y en la YANG library del dispositivo.

En nuestro proyecto usamos SIDs didácticos:

| SID | Significado |
|-----|-------------|
| 1   | tipo de dispositivo (módulo) |
| 2   | versión |
| 10  | nombre del sensor (`/sensor/name`) |
| 11  | timestamp Unix |
| 20  | valor de medición (`/sensor/value`) |
| 21  | unidad de medición (`/sensor/unit`) |

### Delta encoding

En un mapa CBOR de SIDs, solo el primer SID se pone completo. Los demás se ponen como **delta** (diferencia respecto al anterior). Ejemplo:

```
SIDs: 10, 11, 20, 21
Codificado: {10: val, +1: val, +9: val, +1: val}
```

Esto ahorra 1-2 bytes por SID en mapas grandes.

---

## Content-Format: cómo diferencia el servidor el tipo de datos

El Content-Format es un número en la cabecera CoAP que indica cómo está codificado el cuerpo:

| CF | Tipo | Usar en |
|----|------|---------|
| 60 | `application/cbor` | POST de registro genérico |
| 141 | `application/yang-patch+cbor; id=sid` | Cuerpo de FETCH (lista de SIDs) e iPATCH (mapa de cambios) |
| 142 | `application/yang-data+cbor; id=sid` | Respuestas GET/FETCH, cuerpo de PUT |

Si el servidor recibe un CF incorrecto, responde **4.15 Unsupported Content-Format** (implementado en `coreconf_server.c`).

---

## Las 6 operaciones CORECONF

### 1. POST — Registrar dispositivo

No es estrictamente CORECONF pero sí necesario: el dispositivo se registra con sus metadatos.

```
Cliente                          Servidor
   |                                 |
   |── POST /c (CF=60, CBOR) ──────>|
   |                                 | Crea entrada en devices[]
   |<── 2.04 Changed ───────────────|
```

**Cuerpo enviado** (CBOR):
```cbor
{
  1: "temperature",      # tipo
  2: "1.0",             # versión
  10: "sensor-001",     # nombre
  11: 1741000000,       # timestamp
  20: 24.5,             # valor
  21: "celsius"         # unidad
}
```

---

### 2. GET — Leer datastore completo

Obtiene todos los SIDs de un dispositivo.

```
Cliente                          Servidor
   |                                 |
   |── GET /c?id=sensor-001 ────────>|
   |                                 | Serializa hashmap → CBOR
   |<── 2.05 Content (CF=142) ──────|
```

**Respuesta** (CBOR, CF=142):
```cbor
{10: "sensor-001", +1: 1741000000, +9: 24.5, +1: "celsius"}
```

Usa **block-wise transfer** (RFC 7959) para respuestas grandes — el servidor parte la respuesta en bloques de 512 bytes y el cliente los reensambla.

---

### 3. FETCH — Leer SIDs específicos

Como GET pero el cliente elige exactamente qué SIDs quiere. Más eficiente en red.

```
Cliente                          Servidor
   |                                 |
   |── FETCH /c?id=sensor-001 ──────>|
   |   CF=141, Accept=142            |
   |   Body: [20, 21]  (SID list)   |
   |                                 | Solo serializa SIDs 20 y 21
   |<── 2.05 Content (CF=142) ──────|
   |   Body: {20: 24.5, +1: "celsius"}
```

**Cuerpo enviado** (CF=141): array CBOR de SIDs `[20, 21]`

**Diferencia con GET**: GET siempre devuelve todo. FETCH devuelve solo lo pedido. En un dispositivo con 200 SIDs, si solo necesitas 2, FETCH ahorra ~95% del ancho de banda.

---

### 4. iPATCH — Modificar SIDs específicos

Modifica uno o varios SIDs sin tocar el resto. "i" de *idempotent* — si se aplica dos veces, el resultado es el mismo.

```
Cliente                          Servidor
   |                                 |
   |── iPATCH /c?id=sensor-001 ─────>|
   |   CF=141                        |
   |   Body: {20: 99.9}              |
   |                                 | Solo actualiza SID 20
   |<── 2.04 Changed (sin body) ────|
```

**Cuerpo enviado** (CF=141): mapa CBOR `{SID_delta: valor, ...}`

**Sin body en la respuesta** — 2.04 Changed confirma sin repetir datos.

---

### 5. PUT — Reemplazar datastore completo

Reemplaza **toda** la configuración del dispositivo. Lo que no aparezca en el body, desaparece.

```
Cliente                          Servidor
   |                                 |
   |── PUT /c?id=sensor-001 ─────────>|
   |   CF=142                        |
   |   Body: {20: 55.0, +1: 70}     |
   |                                 | Borra todo, inserta nuevos SIDs
   |<── 2.04 Changed ───────────────|  (2.01 si era nuevo)
```

**Semántica exacta**: el servidor borra el datastore existente y lo sustituye por lo que viene en el body. Si antes había SIDs 10, 11, 20, 21 y el PUT solo trae 20 y 21, después solo quedan 20 y 21.

---

### 6. DELETE — Borrar SIDs o datastore

#### DELETE parcial (con `?k=SID`):
```
DELETE /c?id=sensor-001&k=20
→ 2.02 Deleted   (solo borra SID 20)
```

#### DELETE total (sin `?k`):
```
DELETE /c?id=sensor-001
→ 2.02 Deleted   (borra todo el datastore)
```

Tras borrar todo, un GET devuelve **4.04 Not Found**.

---

## Códigos de respuesta CoAP (equivalencias HTTP)

| CoAP | HTTP equiv. | Significado en CORECONF |
|------|------------|-------------------------|
| 2.01 Created | 201 | PUT creó un recurso nuevo |
| 2.02 Deleted | 204 | DELETE exitoso |
| 2.04 Changed | 204 | POST/iPATCH/PUT exitoso |
| 2.05 Content | 200 | GET/FETCH exitoso, body adjunto |
| 4.00 Bad Request | 400 | CBOR malformado |
| 4.04 Not Found | 404 | Dispositivo/SID no existe |
| 4.15 Unsupported CF | 415 | Content-Format incorrecto |

---

## Validación de Content-Format

El servidor debe rechazar peticiones con CF incorrecto. Implementado con `get_content_format()`:

```c
static int get_content_format(coap_pdu_t *pdu) {
    coap_opt_iterator_t oi;
    coap_opt_t *opt = coap_check_option(pdu, COAP_OPTION_CONTENT_FORMAT, &oi);
    if (!opt) return -1;
    return (int)coap_decode_var_bytes(coap_opt_value(opt), coap_opt_length(opt));
}

// En handle_ipatch():
int cf = get_content_format(request);
if (cf != 141) {
    coap_pdu_set_code(response, COAP_RESPONSE_CODE(415));
    return;
}
```

---

## YANG: el lenguaje de modelado

YANG (RFC 7950) es el lenguaje que describe la estructura de datos de un dispositivo. Es como un "schema" o "tipo" para la configuración.

Ejemplo de módulo YANG para nuestro sensor:
```yang
module ietf-sensor {
  namespace "urn:ietf:params:xml:ns:yang:ietf-sensor";
  prefix sensor;

  container sensor {
    leaf name { type string; }        // SID 10
    leaf timestamp { type uint64; }   // SID 11
    leaf value { type decimal64; }    // SID 20
    leaf unit { type string; }        // SID 21
  }
}
```

CORECONF usa YANG para que ambos extremos sepan qué significa cada SID — sin YANG, el SID 20 es solo un número; con YANG, sabemos que es `decimal64` y representa el valor de medición.

---

## Lo que NO implementamos (y por qué)

### CoAP Observe (RFC 7641)
El cliente se "suscribe" a un recurso y recibe notificaciones automáticas cuando cambia. Requiere mantener una lista de observadores en el servidor y enviar notificaciones push. Útil para alertas (temperatura supera umbral → notificación inmediata).

### Datastores NMDA (RFC 8342)
Un dispositivo puede tener `running` (activo), `intended` (deseado), `candidate` (borrador), `operational` (estado real). Requiere gestionar múltiples versiones del datastore y transacciones con commit/rollback.

### YANG Library (RFC 8525)
El servidor debe exponer en `/c/ietf-yang-library` un índice de todos sus módulos YANG, versiones y SID ranges. Permite a un cliente descubrir dinámicamente qué puede hacer con el servidor.

### DTLS (RFC 6347)
Seguridad en capa de transporte para CoAP. Equivalente a TLS pero para UDP. Sin DTLS, los mensajes viajan en claro. En producción **obligatorio**.

### Locking (RFC 8341)
Previene que dos clientes modifiquen el datastore simultáneamente. Se implementa con mutex o mecanismo de bloqueo explícito.
