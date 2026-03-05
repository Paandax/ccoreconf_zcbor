# Arquitectura del proyecto ccoreconf_zcbor

## Visión general

```
ccoreconf_zcbor/
│
├── include/              ← Headers públicos de la biblioteca
├── src/                  ← Implementación de la biblioteca
├── coreconf_zcbor_generated/  ← Código generado por zcbor
├── examples/             ← Tests unitarios de la biblioteca
├── iot_containers/       ← Aplicaciones IoT + Docker
│   ├── iot_apps/         ← Código fuente de apps IoT
│   ├── Dockerfile
│   └── docker-compose.yml
└── docs/                 ← Esta documentación
```

---

## Capas de abstracción

```
┌──────────────────────────────────────────────────────────┐
│              Capa de aplicación IoT                       │
│  coreconf_server.c │ coreconf_cli.c │ iot_server.c ...   │
├──────────────────────────────────────────────────────────┤
│              Capa de operaciones CORECONF                 │
│  get.c │ fetch.c │ ipatch.c │ put.c │ delete.c           │
├──────────────────────────────────────────────────────────┤
│              Capa de tipos y serialización                │
│  coreconfTypes.c │ serialization.c │ sid.c               │
├──────────────────────────────────────────────────────────┤
│              Capa de codificación CBOR (zcbor)            │
│  coreconf_decode.c │ coreconf_encode.c │ zcbor_*.c       │
├──────────────────────────────────────────────────────────┤
│              Capa de red (libcoap)                        │
│  CoAP sobre UDP                                           │
└──────────────────────────────────────────────────────────┘
```

---

## Módulos de la biblioteca

### `include/coreconfTypes.h` + `src/coreconfTypes.c`

El módulo más importante. Define las estructuras de datos centrales.

#### `CoreconfValueT` — valor genérico tipado

```c
typedef struct CoreconfValue {
    CoreconfTypeT type;    // qué tipo de dato es
    union {
        bool     bool_value;
        int64_t  int_value;
        uint64_t uint_value;
        double   real_value;
        char    *str_value;
        CoreconfHashMapT *map_value;
        CoreconfListT    *list_value;
        // ...
    } data;
} CoreconfValueT;
```

Un `CoreconfValueT` puede ser cualquier cosa: un entero, un string, un mapa (otro datastore), una lista... Es el bloque básico del sistema.

#### `CoreconfHashMapT` — mapa SID → valor

```c
typedef struct CoreconfHashMap {
    CoreconfHashMapNodeT *table[HASHMAP_SIZE];  // array de listas enlazadas
    int size;                                    // número de entradas
} CoreconfHashMapT;
```

Implementa un hashmap con **encadenamiento** (separate chaining). Cada bucket es una lista enlazada de nodos `{uint64_t key, CoreconfValueT* value, next}`.

**Hash function**: `key % HASHMAP_SIZE` (siendo HASHMAP_SIZE=64 en nuestra implementación).

Las funciones principales:
```c
CoreconfHashMapT* newCoreconfHashMap();
void insertCoreconfHashMap(CoreconfHashMapT*, uint64_t key, CoreconfValueT*);
CoreconfValueT*  getCoreconfHashMap(CoreconfHashMapT*, uint64_t key);
int deleteFromCoreconfHashMap(CoreconfHashMapT*, uint64_t key);  // ← añadida para DELETE
void freeCoreconfHashMap(CoreconfHashMapT*);
```

#### `CoreconfTypeT` — enum de tipos

```c
typedef enum {
    CORECONF_NULL     = 0,
    CORECONF_BOOL     = 1,
    CORECONF_INT      = 2,
    CORECONF_UINT     = 3,
    CORECONF_REAL     = 4,
    CORECONF_STRING   = 5,
    CORECONF_BYTES    = 6,
    CORECONF_MAP      = 7,
    CORECONF_LIST     = 8,
    CORECONF_TAG      = 9,
    // ...
} CoreconfTypeT;
```

---

### `include/serialization.h` + `src/serialization.c`

Convierte entre `CoreconfValueT` y bytes CBOR.

```c
// Serializa un valor a bytes CBOR
int serializeCoreconf(CoreconfValueT *val, uint8_t *buf, size_t buf_len);

// Deserializa bytes CBOR a un valor
CoreconfValueT* deserializeCoreconf(const uint8_t *buf, size_t len);
```

Internamente usa las funciones generadas por zcbor (`coreconf_encode`, `coreconf_decode`).

---

### `include/sid.h` + `src/sid.c`

Gestiona el delta encoding de SIDs.

```c
// Aplica delta encoding a un array de SIDs
// [10, 11, 20, 21] → [10, 1, 9, 1]
void encodeDeltas(uint64_t *sids, int n, int64_t *deltas_out);

// Recupera SIDs absolutos de deltas
void decodeDeltas(int64_t *deltas, int n, uint64_t base, uint64_t *sids_out);
```

---

### `include/hashmap.h` + `src/hashmap.c`

Estructura de datos auxiliar para el CLI — mapeo nombre↔SID para que el usuario pueda escribir `temperature` en vez de `20`.

---

### `include/fetch.h` + `src/fetch.c`

```c
// Parsea el body CBOR de una petición FETCH (lista de SIDs)
int parse_fetch_request(const uint8_t *buf, size_t len,
                        uint64_t *sids_out, int max_sids);

// Construye respuesta FETCH: filtra hashmap por SID list
int build_fetch_response(CoreconfHashMapT *map,
                         uint64_t *sids, int n_sids,
                         uint8_t *buf_out, size_t buf_len);
```

---

### `include/get.h` + `src/get.c`

```c
// Serializa todo el hashmap a CBOR (para respuesta GET)
int build_get_response(CoreconfHashMapT *map, uint8_t *buf, size_t len);
```

---

### `include/ipatch.h` + `src/ipatch.c`

```c
// Aplica un mapa de cambios CBOR sobre un CoreconfValueT
// El patch contiene {SID_delta: nuevo_valor, ...}
int apply_ipatch(CoreconfValueT *target, CoreconfValueT *patch);
```

Recorre el mapa del patch y para cada SID llama a `insertCoreconfHashMap` (upsert: si existe lo actualiza, si no lo crea).

---

### `include/put.h` + `src/put.c`

```c
// Reemplaza el contenido de target con el contenido de replacement
// (borra todo lo que había antes)
int apply_put(CoreconfValueT **target, CoreconfValueT *replacement);
```

---

### `include/delete.h` + `src/delete.c`

```c
// Parsea los parámetros ?k=SID de la query URI
int parse_delete_query(const char *query, size_t qlen,
                       uint64_t *sids_out, int max_sids);

// Borra SIDs específicos del hashmap
int apply_delete(CoreconfHashMapT *map, uint64_t *sids, int n_sids);
```

---

## Código generado por zcbor

zcbor (Zephyr CBOR) es un generador de código que toma un schema CDDL y produce codificadores/decodificadores C eficientes y con verificación de tipos en tiempo de compilación.

A partir de `coreconf.cddl`:
```
coreconf-map = {* sid => coreconf-value}
sid = int
coreconf-value = bool / int / float / tstr / bstr / coreconf-map / ...
```

zcbor genera:
```
coreconf_zcbor_generated/
├── coreconf_decode.h / .c   ← función coreconf_decode() 
├── coreconf_encode.h / .c   ← función coreconf_encode()
├── coreconf_decode_types.h  ← structs de salida del decoder
├── coreconf_encode_types.h  ← structs de entrada del encoder
└── zcbor_*.h / .c           ← runtime de zcbor (primitivos CBOR)
```

Para regenerar el código si cambias el CDDL:
```bash
./generar_coreconf_zcbor.sh
```

---

## Aplicaciones IoT

### `coreconf_server.c` — Servidor unificado

Es el **gateway CORECONF**. Acepta conexiones de cualquier dispositivo e implementa los 6 handlers sobre el recurso `/c`.

**Estructura de datos del servidor**:
```c
typedef struct {
    char device_id[64];    // "sensor-temp-001"
    CoreconfValueT *data;  // datastore del dispositivo
    int exists;            // slot ocupado
} Device;

Device devices[MAX_DEVICES];  // array de 32 dispositivos
```

Cada dispositivo tiene su propio `CoreconfValueT*` (que internamente es un `map_value` con el hashmap de SIDs).

**Flujo de un request**:
1. libcoap llama al handler registrado según el método CoAP
2. El handler extrae `?id=<device_id>` de la query URI
3. Busca el dispositivo en `devices[]`
4. Aplica la operación (get/fetch/ipatch/put/delete)
5. Construye la respuesta CoAP

**Extracción del device_id**:
```c
static void get_query_device_id(coap_string_t *query, char *out, size_t sz) {
    // Busca "id=" en la query string
    // query: "id=sensor-temp-001&k=20"
    // out:   "sensor-temp-001"
}
```

### `coreconf_cli.c` — Cliente interactivo REPL

**Arquitectura**:
```
main()
  ├── Lee GATEWAY_HOST/GATEWAY_PORT del entorno
  ├── Lee tipo y device_id de argv
  ├── Crea contexto CoAP (coap_new_context)
  ├── Resuelve URI del gateway (coap_resolve_address_info)
  └── loop:
        ├── muestra prompt "sensor-temp-001> "
        ├── lee línea de stdin (fgets)
        ├── parsea comando
        └── según comando:
              ├── build_and_send_post()
              ├── build_and_send_get()
              ├── build_and_send_fetch()
              ├── build_and_send_ipatch()
              ├── build_and_send_put()
              └── build_and_send_delete()
```

Cada función `build_and_send_*()`:
1. Crea un PDU CoAP (`coap_new_pdu`)
2. Añade opciones (Uri-Path, Content-Format, Accept, Uri-Query)
3. Añade body CBOR si aplica
4. Envía con `coap_send`
5. Espera respuesta con `coap_io_process` (loop hasta recibir ACK)
6. Imprime resultado en pantalla

---

## Flujo de datos completo: ejemplo iPATCH

```
coreconf_cli.c                    coreconf_server.c
      │                                   │
      │  build_and_send_ipatch(20, 99.9)  │
      │                                   │
      │  1. Construir CBOR patch:         │
      │     {20: 99.9}  (8 bytes)         │
      │                                   │
      │  2. Crear PDU:                    │
      │     CON iPATCH /c                 │
      │     CF=141                        │
      │     Query: id=sensor-temp-001     │
      │     Body: {20: 99.9}              │
      │                                   │
      │──── UDP packet (73 bytes) ───────>│
      │                                   │  3. handle_ipatch()
      │                                   │     get_content_format() → 141 ✓
      │                                   │     get_query_device_id() → "sensor-temp-001"
      │                                   │     find_device() → &devices[0]
      │                                   │     deserializeCoreconf(body) → patch_value
      │                                   │     apply_ipatch(dev->data, patch_value)
      │                                   │       └─ insertCoreconfHashMap(map, 20, 99.9)
      │                                   │     freeCoreconfValue(patch_value)
      │<─── ACK 2.04 Changed (36 bytes) ─│
      │                                   │
      │  Imprime: "📨 2.04"               │
```

---

## El Makefile

```makefile
# Compilar la biblioteca
make              # compila ccoreconf.a
make clean        # limpia objetos y biblioteca

# Compilar apps IoT (en iot_containers/iot_apps/)
make coreconf_server
make coreconf_cli
make all          # todas las apps
```

La biblioteca se compila como `.a` (estática) porque:
- No hay linker dinámico en algunos targets embedded
- Facilita la distribución (un solo fichero)
- El Docker copia `ccoreconf.a` y lo enlaza directamente

---

## Decisiones de diseño importantes

### ¿Por qué hashmap y no array?

Los SIDs son enteros de 64 bits dispersos (1, 10, 11, 20, 21, ..., 58775, ...). Un array necesitaría 58775 entradas para llegar al SID 58775. El hashmap usa solo las entradas que existen, con O(1) acceso amortizado.

### ¿Por qué encadenamiento en el hashmap?

Las colisiones son inevitables. Encadenamiento (linked list por bucket) es simple de implementar y correcto. Para IoT con pocos SIDs (< 50) es perfectamente eficiente.

### ¿Por qué array de Device[] y no hashmap de dispositivos?

Simplicidad. 32 dispositivos máximo es suficiente para un proyecto académico. Un hashmap de `device_id → CoreconfValueT*` sería más escalable pero añade complejidad innecesaria.

### ¿Por qué CoreconfValueT* y no CoreconfValueT por valor?

Los valores pueden ser mapas anidados (structs de tamaño variable). Se pasan por puntero para:
- Evitar copias costosas
- Permitir `NULL` como "valor no existe"
- Facilitar la gestión de memoria con `freeCoreconfValue()`
