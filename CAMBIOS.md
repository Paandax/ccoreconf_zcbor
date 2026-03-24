# Cambios realizados sobre la implementación base

Modificaciones necesarias para que los ejemplos del draft `draft-ietf-core-comi-20` funcionen correctamente.

---

## 1. Bug: iPATCH con `null` no funcionaba

**Ficheros:** `src/serialization.c`, `src/ipatch.c`

El draft dice que `iPATCH {SID: null}` debe **borrar** ese nodo. Fallaba en tres capas:

- **`serialization.c` → `coreconfToCBOR()`**: no tenía case para `CORECONF_NULL`. Añadido `zcbor_nil_put()`.
- **`serialization.c` → `cborToCoreconfValue()`**: no detectaba el byte CBOR `0xf6` (null). Añadido `zcbor_nil_expect()`.
- **`ipatch.c` → `apply_one_sid()`**: cuando el valor era null, no llamaba a `deleteFromCoreconfHashMap()`. Ahora sí.

---

## 2. Bug: crash con estructuras CBOR anidadas

**Ficheros:** `src/ipatch.c`, `src/get.c`, `src/put.c`, `iot_containers/iot_apps/coreconf_server.c`, `iot_containers/iot_apps/coreconf_cli.c`

Con mapas dentro de mapas (`{5:{1:"128.100.49.105"}}`), zcbor necesita backups de estado para poder retroceder en el parsing. `state[5]` con `n_states=5` solo proporciona 3 backups — insuficiente para estructuras con 4+ niveles de anidamiento. Cambiado a `state[8]` (`n_states=8`, 6 backups) en todos los encoders y decoders.

---

## 3. Nuevo: servidor arranca con datos del draft §3.3.1

**Fichero:** `iot_containers/iot_apps/coreconf_server.c`

Añadida función `init_ietf_example_datastore()` llamada desde `main()`. Carga el datastore al arrancar con los datos exactos del draft:

| SID  | Nodo YANG | Valor |
|------|-----------|-------|
| 1721 | `system-state/clock` | `{boot-datetime, current-datetime}` |
| 1533 | `interfaces/interface` | `[{eth0, Ethernet adaptor, ethernetCsmacd, enabled, oper-status=testing}]` |
| 1755 | `system/ntp/enabled` | `false` |
| 1756 | `system/ntp/server` | `[{tac.nrc.ca, prefer:false, udp:{128.100.49.105}}]` |

---

## 4. Nuevo: iPATCH con instance-identifiers `[SID,"key"]`

**Fichero:** `src/ipatch.c`, `include/ipatch.h`

El draft §3.2.3.1 entrada 2 usa `{[1756,"tac.nrc.ca"]: null}` para borrar una entrada concreta de una lista YANG. Implementadas:

- **`apply_ipatch_raw()`**: decodifica el CBOR directamente, reconoce tanto claves `uint` simples como arrays `[SID,"key"]` (instance-identifiers).
- **`create_ipatch_iid_request()`**: codifica `{[SID,"key"]: valor}` para enviar desde el cliente.
- **`find_list_entry_by_key()`**: busca en un array CORECONF la entrada que contiene una clave string concreta.
- **`remove_array_entry()`**: elimina un elemento del array por índice.

---

## 5. Nuevo: auto-wrap de lista al hacer iPATCH con clave uint

**Fichero:** `src/ipatch.c`

El draft §3.2.3.1 entrada 3 usa `{1756: {mapa}}` para reemplazar la lista de servidores NTP. El resultado debe ser `1756: [{mapa}]` porque SID 1756 es una lista YANG, no un contenedor.

Añadida lógica en `apply_ipatch_raw()`: si el SID existente en el datastore es `CORECONF_ARRAY` y el valor nuevo es `CORECONF_HASHMAP`, lo envuelve automáticamente en un array antes de almacenarlo.

---

## 6. Nuevo: parser recursivo de mapas en el CLI

**Fichero:** `iot_containers/iot_apps/coreconf_cli.c`

Para poder escribir desde el prompt:
```
ipatch 1756 {3:tic.nrc.ca,4:true,5:{1:132.246.11.231}}
```
Se necesita un parser que soporte mapas anidados. Añadidas:

- **`parse_value_ptr()`**: parsea un valor avanzando un puntero, detecta si es mapa `{...}` o escalar.
- **`parse_map_ptr()`**: parsea `{k:v, k:v, ...}` recursivamente, los valores pueden ser mapas anidados.

---

## 7. Nuevo: SIDs con nombres descriptivos

**Fichero:** `include/sids.h` (nuevo)

Creado `sids.h` con `#define` extraídos del fichero oficial `ietf-system@2014-08-06.sid` del repositorio `core-wg/yang-cbor`. El servidor ya no usa números mágicos:

```c
/* Antes */
insertCoreconfHashMap(ds->data.map_value, 1756, servers);
insertCoreconfHashMap(srv->data.map_value, 3, createCoreconfString("tac.nrc.ca"));

/* Ahora */
insertCoreconfHashMap(ds->data.map_value, SID_SYS_NTP_SERVER, servers);
insertCoreconfHashMap(srv->data.map_value, DELTA_NTP_SERVER_NAME, createCoreconfString("tac.nrc.ca"));
```

Los SIDs definidos cubren `ietf-system` (RFC 7317) e `ietf-interfaces` (RFC 8343).

---

## 8. Formato de salida del CLI

**Fichero:** `iot_containers/iot_apps/coreconf_cli.c`

Cambiado `print_value_r()` y `print_datastore()` para que la salida coincida con el formato del draft:

```
# Antes
SID 1756     → [
  [0]: {
    [delta 3]: "tac.nrc.ca"
  }
]

# Ahora
1756     : [
  {
    3 : "tac.nrc.ca"
  }
]
```

---

## 9. Endurecimiento DTLS en Docker (CERT/PSK)

**Ficheros:** `iot_containers/Dockerfile`, `iot_containers/docker-compose.yml`, `iot_containers/certs/generate_certs.sh`, `iot_containers/iot_apps/coreconf_server.c`, `iot_containers/iot_apps/coreconf_cli.c`

Se dejó la ejecución en contenedores preparada para CoAPS real:

- Imagen Docker copia `sid/` y `certs/` dentro del contenedor.
- Build de apps enlaza backend libcoap con prioridad DTLS (`openssl` -> `gnutls` -> `notls`).
- Exposición de puertos UDP `5683` y `5684`.
- `docker-compose` configurado con modo `CORECONF_TLS_MODE=cert` y cliente apuntando a `5684`.
- Generación de certificados mejorada (CA + server + client) con SAN/CN/KU/EKU válidos para entorno Docker.
- Cliente/servidor aceptan tanto modo certificados como PSK por variables de entorno.

Resultado: transporte DTLS operativo end-to-end entre `coreconf_client` y `coreconf_server`.

---

## 10. Carga runtime de SID files y verificación de compatibilidad CLI<->Server

**Ficheros:** `iot_containers/iot_apps/coreconf_server.c`, `iot_containers/iot_apps/coreconf_cli.c`, `sid/ietf-system.sid`, `sid/ietf-interfaces.sid`

Se eliminó dependencia de SIDs fijos compilados para el flujo principal:

- Server y CLI cargan en runtime `sid/ietf-system.sid` y `sid/ietf-interfaces.sid`.
- Se parsean SIDs críticos del ejemplo (`clock`, `interfaces`, `ntp`, etc.).
- Se calcula un fingerprint de diccionario SID (hash FNV-like) en ambos extremos.
- Endpoint `GET /sid` en servidor devuelve `sid-fingerprint=<u64>`.
- CLI compara fingerprint local vs remoto antes de operar; aborta si no coincide.

Objetivo: evitar que cliente y servidor trabajen con mapeos SID incompatibles sin detectarlo.

---

## 11. Nuevo recurso de stream `/s` con CoAP Observe

**Ficheros:** `iot_containers/iot_apps/coreconf_server.c`, `iot_containers/iot_apps/coreconf_cli.c`

Se implementó el stream de eventos CORECONF en línea con draft (`rt="core.c.es"`):

- Recurso `/s` registrado en servidor como observable.
- `GET /s` soporta Observe (`Observe=0`) y devuelve `CF=142`.
- Cola circular de eventos en memoria (`STREAM_MAX_EVENTS=16`), más nuevo primero.
- Notificaciones push al cambiar datastore (PUT/iPATCH/DELETE).
- CLI añade comando `observe [segundos]` para suscripción y recepción de notificaciones.

Se validó que tras un `iPATCH` el cliente recibe notificación asíncrona con código `2.05` y payload de evento.

---

## 12. Ajuste de semántica de stream: de snapshot a cambio real

**Fichero:** `iot_containers/iot_apps/coreconf_server.c`

Refactor para acercar `/s` al modelo de notificación del draft:

- Antes: se encolaban snapshots del datastore completo para cada cambio.
- Ahora:
  - `iPATCH` encola el payload recibido (cambio aplicado).
  - `PUT` encola el body aplicado.
  - `DELETE` encola evento vacío `{}`.

Con esto, el stream representa mejor "instancias de notificación" y no únicamente estado total repetido.

---

## 13. `FETCH /s` con filtro real por SID (CF=141)

**Fichero:** `iot_containers/iot_apps/coreconf_server.c`

Implementación nueva para filtros en stream:

- Se parsea payload `CF=141` como cbor-seq de IIDs (SID simple o `[SID, key]`).
- Se extraen SIDs base y se filtran eventos de la cola por presencia de esas claves SID.
- Si payload con filtro es inválido, se responde `4.00` con error payload CORECONF.
- Si `CF` no es `141` en `FETCH /s` con body, se responde `4.15`.

Nota: para IID compuesto `[SID,key]` el filtrado actual usa el SID base (no matching fino por key todavía).

---

## 14. Nuevo comando CLI `sfetch` para validar stream filtrado

**Fichero:** `iot_containers/iot_apps/coreconf_cli.c`

Comando añadido:

- `sfetch <SID> [SID...] [[SID,clave]...]`

Comportamiento:

- Envía `FETCH /s` con payload `CF=141` (IIDs) y `Accept=142`.
- Muestra cbor-seq devuelto por stream igual que otros comandos de lectura.

Motivo: facilitar pruebas reproducibles de filtro de stream sin herramientas externas.

---

## 15. Validaciones ejecutadas durante la sesión

### Build y despliegue

- Build de imagen Docker completado sin errores.
- Recreación de `coreconf_server` y `coreconf_client` correcta.

### Prueba funcional Observe

- Suscripción con `observe 8` / `observe 10`.
- Tras enviar `iPATCH`, recepción de notificación en cliente confirmada.

### Prueba funcional de filtro `FETCH /s`

Secuencia probada:

1. `ipatch 1755 true`
2. `ipatch 20 42`
3. `sfetch 1755` -> devuelve solo evento con SID `1755`.
4. `sfetch 9999` -> respuesta `2.05` sin payload (sin coincidencias).

Conclusión: filtro de stream por SID operativo.

---

## 16. Estado actual (resumen rápido)

### Hecho

- DTLS por certificados funcionando en Docker.
- Carga runtime de SID files + comprobación de compatibilidad cliente/servidor.
- Stream `/s` observable implementado.
- Filtro `FETCH /s` por SID base implementado y validado.
- CLI con `observe` y `sfetch` para demo y pruebas.

### Parcial / pendiente para "clavado" total del draft

- Matching estricto de IID compuesto por key (`[SID,key]`) en filtro de stream.
- Refinar el formato final de "notification-instance" para eventos complejos.
- Tabla de conformidad final por secciones del draft (si se quiere dejar cerrada en memoria TFG).
