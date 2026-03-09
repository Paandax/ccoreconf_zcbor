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
