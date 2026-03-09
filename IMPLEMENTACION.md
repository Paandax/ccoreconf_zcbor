# Implementación CORECONF — draft-ietf-core-comi-20

Este documento explica qué se ha implementado, por qué, y cómo se corresponde con el draft.

---

## 1. Qué es CORECONF

CORECONF es el protocolo de gestión de red para dispositivos IoT. Es el equivalente a NETCONF/RESTCONF pero ultra-compacto: usa **CoAP** en vez de HTTP y **CBOR** en vez de XML/JSON.

```
Cliente (CLI)   ←──── CoAP/UDP ────→   Servidor (dispositivo IoT)
                       puerto 5683
```

El servidor representa un **dispositivo real** (router, sensor, etc.) con sus datos internos. El cliente solo los consulta y modifica.

---

## 2. SIDs — Schema Item Identifiers

Los SIDs son números enteros que identifican de forma **globalmente única** cada nodo de un módulo YANG. En vez de enviar `"current-datetime"` como string (caro en IoT), se envía el número `1723`.

| SID | Identificador YANG | Módulo |
|-----|-------------------|--------|
| 1721 | `ietf-system/system/clock` | ietf-system (RFC 7317) |
| 1722 | `ietf-system/system/clock/boot-datetime` | ietf-system |
| 1723 | `ietf-system/system/clock/current-datetime` | ietf-system |
| 1533 | `ietf-interfaces/interfaces/interface` | ietf-interfaces (RFC 8343) |
| 1755 | `ietf-system/system/ntp/enabled` | ietf-system |
| 1756 | `ietf-system/system/ntp/server` | ietf-system |

Los SIDs son **fijos**: cualquier dispositivo del mundo que implemente `ietf-system` usará exactamente los mismos números.

Dentro de un container o lista, las claves se expresan como **deltas** respecto al SID padre:
- `1721` (clock) + delta `2` = `1723` (current-datetime)
- `1533` (interface) + delta `4` = `1537` (name/clave de lista)

---

## 3. Content-Formats (CF)

| CF | Nombre | Cuándo se usa |
|----|--------|---------------|
| 140 | `yang-data+cbor;id=sid` | PUT request / GET response (datastore completo) |
| 141 | `yang-identifiers+cbor-seq` | FETCH request (lista de SIDs a leer) |
| 142 | `yang-instances+cbor-seq` | FETCH response / iPATCH request (mapa SID→valor) |

---

## 4. Operaciones implementadas

### 4.1 GET (§3.3.1 del draft)

Lee el datastore completo.

```
REQ:  GET /c
      (sin payload)

RES:  2.05 Content  CF=140
      { 1721: {1:"2014-10-05T09:00:00Z", 2:"2016-10-26T12:16:31Z"},
        1533: [{4:"eth0", 1:"Ethernet adaptor", 5:1880, 2:true, 11:3}] }
```

En el CLI:
```
coreconf> get
```

### 4.2 FETCH (§3.1.3.1 del draft)

Lee **solo los SIDs pedidos**. El payload del request es una lista de SIDs (cbor-seq).

```
REQ:  FETCH /c  CF=141
      1755           ← pedir solo ntp/enabled

RES:  2.05 Content  CF=142
      { 1755: false }
```

En el CLI:
```
coreconf> fetch 1755
coreconf> fetch 1721 1533     ← varios SIDs a la vez
```

### 4.3 iPATCH — nodo simple (§3.2.3.1 del draft)

Actualiza **parcialmente** el datastore. Solo cambia los SIDs indicados; el resto queda intacto.

```
REQ:  iPATCH /c  CF=142
      { 1755: true }          ← activar NTP

RES:  2.04 Changed
      (sin payload)
```

En el CLI:
```
coreconf> ipatch 1755 true
coreconf> ipatch 1755 null    ← borrar SID 1755
```

### 4.4 iPATCH — instance-identifier (§3.2.3.1 del draft)

Para listas YANG, la clave del mapa es un **array** `[SID, "valor_clave"]` en vez de un uint.

```
REQ:  iPATCH /c  CF=142
      { [1756, "tac.nrc.ca"]: null }          ← borrar servidor NTP
      { [1756, "tic.nrc.ca"]: {3:"tic.nrc.ca",
                                4:true,
                                5:{1:"132.246.11.231"}} }  ← añadir servidor
```

En el CLI:
```
coreconf> ipatch [1756,tac.nrc.ca] null
coreconf> ipatch [1756,tic.nrc.ca] true
```

### 4.5 PUT (§3.3 del draft)

Reemplaza el datastore completo.

```
REQ:  PUT /c  CF=140
      { 1721: {...}, 1533: [...] }

RES:  2.01 Created   (si era nuevo)
      2.04 Changed   (si ya existía)
```

### 4.6 DELETE (§3.3 del draft)

Borra el datastore completo.

```
REQ:  DELETE /c

RES:  2.02 Deleted
```

---

## 5. Datastores

En NETCONF/YANG existen tres datastores estándar (RFC 6241):

| Datastore | Descripción |
|-----------|-------------|
| **running** | Configuración activa ahora mismo |
| **startup** | Configuración que carga al arrancar |
| **candidate** | Staging antes de hacer commit |

En CORECONF (IoT simplificado) hay un **único datastore** `/c`. El servidor arranca con sus datos ya cargados (equivalente al `startup`), y el cliente los modifica con iPATCH/PUT (equivalente a `running`).

En esta implementación el servidor arranca directamente con el datastore del §3.3.1 del draft:

```c
/* en coreconf_server.c */
static void init_ietf_example_datastore(void) {
    /* clock: SID 1721 */
    /* interfaces: SID 1533 */
    /* ntp/enabled: SID 1755 */
    /* ntp/server: SID 1756 */
}
```

---

## 6. Bugs corregidos

### 6.1 iPATCH null (§3.2.3 del draft)

El draft dice que `{SID: null}` debe **borrar** ese SID del datastore. Había tres puntos rotos:

1. **Encoder** (`serialization.c`): no había case para `CORECONF_NULL` → el serializer caía al `default: return false`.
   - Fix: añadido `case CORECONF_NULL: res = zcbor_nil_put(state, NULL);`

2. **Decoder** (`serialization.c`): `cborToCoreconfValue()` no reconocía CBOR null (byte `0xf6`).
   - Fix: añadido `else if (zcbor_nil_expect(state, NULL))` que crea `CORECONF_NULL`.

3. **Apply** (`ipatch.c`): `apply_one_sid()` no comprobaba si el valor era null.
   - Fix: añadido `if (obj->value->type == CORECONF_NULL) { deleteFromCoreconfHashMap(...); return; }`

### 6.2 zcbor backups insuficientes

Todos los estados zcbor usaban `n_backups=0` (array de 5 estados con `n_states=5` → `n_states - 2 = 3` backups). El datastore del draft tiene 4 niveles de anidamiento (mapa→array→mapa→mapa), lo que requiere al menos 4 backups.

- Fix: aumentado todos los arrays de estados a `state[8]` (6 backups disponibles), suficiente para cualquier estructura razonablemente anidada.

Afecta a: `src/get.c`, `src/put.c`, `src/ipatch.c`, `iot_containers/iot_apps/coreconf_server.c`, `iot_containers/iot_apps/coreconf_cli.c`.

---

## 7. Instance-identifiers en iPATCH — implementación nueva

Para soportar `{ [SID, "key"]: valor }` se añadieron:

### 7.1 Cliente: `create_ipatch_iid_request()` (src/ipatch.c)

Serializa un mapa CBOR con clave array:
```
{ [1756, "tac.nrc.ca"]: null }
```

### 7.2 CLI: detección de `[SID,key]` en `cmd_ipatch()` (coreconf_cli.c)

Si el primer argumento empieza por `[`, parsea `[SID,clave]` y llama a `create_ipatch_iid_request()`.

### 7.3 Servidor: `apply_ipatch_raw()` (src/ipatch.c)

Decodifica directamente el buffer CBOR sin pasar por `parse_ipatch_request()`. Maneja:

- Clave `uint` → `insertCoreconfHashMap` / `deleteFromCoreconfHashMap`
- Clave `array [SID, "key"]` con `null` → busca en la lista el elemento con ese string y lo elimina
- Clave `array [SID, "key"]` con valor → busca y actualiza, o añade si no existe

La búsqueda del elemento en la lista es por **matching de string**: busca en todos los campos del entry de la lista hasta encontrar uno cuyo valor string coincida con la clave del instance-identifier. Esto funciona porque en YANG las claves de lista son leaves de tipo string.

---

## 8. Cómo usar

### Arrancar

```bash
# Terminal 1: servidor (ya tiene el datastore del draft cargado)
./coreconf_server

# Terminal 2: cliente
./coreconf_cli
```

### Secuencia de los ejemplos del draft

```
coreconf> get
```
→ §3.3.1: muestra clock(1721) + interfaces(1533) + ntp(1755/1756)

```
coreconf> fetch 1755
```
→ §3.1.3.1: devuelve `{1755: false}` (ntp/enabled)

```
coreconf> ipatch 1755 true
```
→ §3.2.3.1 (primera entrada): activa NTP

```
coreconf> ipatch [1756,tac.nrc.ca] null
```
→ §3.2.3.1 (segunda entrada): borra servidor NTP "tac.nrc.ca"

```
coreconf> get
```
→ verifica: 1755=true, 1756 ya no tiene "tac.nrc.ca"
