# Análisis de Funciones de fetch.c - Documentación TFG

**Archivo**: `src/fetch.c`  
**Propósito**: Implementación completa de operaciones FETCH según RFC 9254  
**Líneas totales**: 424

---

## 📑 Índice de Funciones

1. [create_fetch_request()](#1-create_fetch_request) - Crear petición FETCH simple
2. [create_fetch_request_with_iids()](#2-create_fetch_request_with_iids) - Crear petición con instance-identifiers
3. [create_fetch_response()](#3-create_fetch_response) - Crear respuesta simple
4. [fetch_value_by_iid()](#4-fetch_value_by_iid) - Búsqueda semántica de valores
5. [create_fetch_response_iids()](#5-create_fetch_response_iids) - Crear respuesta con resolución semántica
6. [parse_fetch_request()](#6-parse_fetch_request) - Parsear petición simple
7. [is_fetch_request()](#7-is_fetch_request) - Detectar si es FETCH o STORE
8. [parse_fetch_request_iids()](#8-parse_fetch_request_iids) - Parsear petición con instance-identifiers
9. [free_instance_identifiers()](#9-free_instance_identifiers) - Liberar memoria

---

## 1. create_fetch_request()

**Líneas**: 10-30  
**Propósito**: Crear una petición FETCH con SIDs simples

### Firma

```c
size_t create_fetch_request(uint8_t *buffer, size_t buffer_size,
                             const uint64_t *sids, size_t sid_count);
```

### Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `buffer` | `uint8_t *` | Buffer de salida donde escribir CBOR |
| `buffer_size` | `size_t` | Tamaño del buffer (prevenir overflow) |
| `sids` | `const uint64_t *` | Array de SIDs a solicitar |
| `sid_count` | `size_t` | Número de SIDs en el array |

### Retorno

- **Éxito**: Número de bytes escritos en el buffer
- **Error**: 0

### ¿Qué hace?

Codifica un array de SIDs en formato **CBOR sequence** (RFC 8949):
- No hay array envolvente
- Cada SID es un elemento CBOR independiente
- Se concatenan directamente

### Ejemplo de entrada/salida

```c
// Entrada
uint64_t sids[] = {20, 21, 30};
uint8_t buffer[256];

// Llamada
size_t len = create_fetch_request(buffer, 256, sids, 3);

// Salida en buffer (en hexadecimal)
// 0x14 0x15 0x1E
// └─┘  └─┘  └─┘
//  20   21   30
```

### Algoritmo

1. **Validar** parámetros (NULL checks)
2. **Para cada SID**:
   - Inicializar estado de encoding zcbor
   - Codificar el SID como uint64 CBOR
   - Actualizar offset con bytes escritos
3. **Retornar** total de bytes

### Notas técnicas

- Usa `zcbor_uint64_put()` para encoding eficiente
- CBOR escoge automáticamente 1, 2, 3, 5 o 9 bytes según el valor
- Ejemplos:
  - `20` → 1 byte: `0x14`
  - `256` → 3 bytes: `0x19 0x01 0x00`
  - `70000` → 5 bytes: `0x1A 0x00 0x01 0x11 0x70`

---

## 2. create_fetch_request_with_iids()

**Líneas**: 32-78  
**Propósito**: Crear petición FETCH con instance-identifiers completos (incluyendo keys)

### Firma

```c
size_t create_fetch_request_with_iids(uint8_t *buffer, size_t buffer_size,
                                       const InstanceIdentifier *iids, 
                                       size_t iid_count);
```

### Diferencia con create_fetch_request()

Esta función maneja **tres tipos** de identificadores:
1. `IID_SIMPLE`: Solo SID → `20`
2. `IID_WITH_STR_KEY`: SID + string → `[30, "eth0"]`
3. `IID_WITH_INT_KEY`: SID + índice → `[30, 2]`

### Ejemplo de entrada/salida

```c
// Entrada
InstanceIdentifier iids[] = {
    {IID_SIMPLE, 20, {.str_key = NULL}},
    {IID_WITH_STR_KEY, 30, {.str_key = "eth0"}},
    {IID_WITH_INT_KEY, 40, {.int_key = 2}}
};

// Salida en buffer (CBOR)
// 20
// [30, "eth0"]
// [40, 2]
```

### Codificación CBOR por tipo

#### IID_SIMPLE
```c
zcbor_uint64_put(state, sid);
// Resultado: 0x14 (si sid=20)
```

#### IID_WITH_STR_KEY
```c
zcbor_list_start_encode(state, 2);    // Start array de 2 elementos
zcbor_uint64_put(state, sid);         // SID
zcbor_tstr_put_term(state, str_key);  // String
zcbor_list_end_encode(state, 2);      // End array

// Resultado: 0x82 0x1E 0x64 "eth0"
//            └─┘  └─┘  └─────────┘
//          array  30   string(4) "eth0"
```

#### IID_WITH_INT_KEY
```c
zcbor_list_start_encode(state, 2);
zcbor_uint64_put(state, sid);
zcbor_int64_put(state, int_key);
zcbor_list_end_encode(state, 2);

// Resultado: 0x82 0x18 0x28 0x02
//            └─┘  └────┘  └─┘
//          array   40      2
```

### Switch case

```c
switch (iids[i].type) {
    case IID_SIMPLE:
        // Código para simple
        break;
    case IID_WITH_STR_KEY:
        // Código para string key
        break;
    case IID_WITH_INT_KEY:
        // Código para int key
        break;
    default:
        return 0;  // Tipo desconocido
}
```

---

## 3. create_fetch_response()

**Líneas**: 80-118  
**Propósito**: Crear respuesta FETCH del servidor con SIDs simples

### Firma

```c
size_t create_fetch_response(uint8_t *buffer, size_t buffer_size,
                              CoreconfValueT *data_source,
                              const uint64_t *sids, size_t sid_count);
```

### Formato de salida

RFC 9254 especifica:
```
Response = CBOR sequence de mapas:
{SID1: value1}
{SID2: value2}
{SID3: value3}
```

### Ejemplo

```c
// Cliente pidió: [20, 30, 99]
// Servidor tiene:
// {20: 25.5, 30: "temperature", 40: true}

// Respuesta generada:
// {20: 25.5}         ← Encontrado
// {30: "temperature"} ← Encontrado
// {99: null}         ← No encontrado
```

### Algoritmo

1. **Para cada SID solicitado**:
   - Buscar valor con `getCoreconfHashMap()`
   - Crear mapa CBOR `{SID: ...}`
   - Si encontrado: codificar valor
   - Si no encontrado: codificar `null`
   - Cerrar mapa
2. **Retornar** total de bytes

### Código clave

```c
// Buscar valor
CoreconfValueT *value = NULL;
if (data_source && data_source->type == CORECONF_HASHMAP) {
    value = getCoreconfHashMap(data_source->data.map_value, sids[i]);
}

// Crear mapa
zcbor_map_start_encode(state, 1);  // 1 par clave-valor
zcbor_uint64_put(state, sids[i]);  // Clave = SID

if (value) {
    coreconfToCBOR(value, state);  // Valor encontrado
} else {
    zcbor_nil_put(state, NULL);    // null si no existe
}

zcbor_map_end_encode(state, 1);
```

### CBOR generado

```
{20: 25.5}
↓
0xBF            // Map start (indefinite length)
0x14            // Key: 20
0xFB 0x40...    // Value: double 25.5
0xFF            // Map end

{99: null}
↓
0xBF            // Map start
0x18 0x63       // Key: 99
0xF6            // Value: null
0xFF            // Map end
```

---

## 4. fetch_value_by_iid()

**Líneas**: 120-195  
**Propósito**: ⭐ **FUNCIÓN MÁS IMPORTANTE** - Búsqueda semántica de valores usando instance-identifiers

### Firma

```c
CoreconfValueT* fetch_value_by_iid(CoreconfValueT *data_source, 
                                    const InstanceIdentifier *iid);
```

### ¿Por qué es tan importante?

Esta función implementa la **resolución semántica** del RFC 9254:
- No solo decodifica `[SID, key]` del wire format
- **Busca activamente** el elemento que coincide con la key
- Soporta búsqueda por string en arrays
- Soporta acceso por índice

### Casos de uso

#### Caso 1: IID_SIMPLE (SID solo)

```c
// Datos: {20: 25.5, 30: "temperature"}
// IID: {IID_SIMPLE, 20, ...}
// Resultado: 25.5
```

**Algoritmo**: Lookup directo en hashmap.

#### Caso 2: IID_WITH_STR_KEY (búsqueda por string)

```c
// Datos:
// {1533: [
//   {name: "eth0", status: "up", ip: "192.168.1.1"},
//   {name: "wlan0", status: "down", ip: "10.0.0.1"}
// ]}
//
// IID: {IID_WITH_STR_KEY, 1533, "eth0"}
// Resultado: {name: "eth0", status: "up", ip: "192.168.1.1"}
```

**Algoritmo**:
1. Obtener array en SID 1533
2. Para cada elemento del array
3. Si es un hashmap
4. Buscar en TODOS sus campos
5. Si algún campo STRING coincide con "eth0"
6. Retornar ese elemento completo

**Código clave**:

```c
if (value->type == CORECONF_ARRAY) {
    CoreconfArrayT *arr = value->data.array_value;
    
    for (size_t i = 0; i < arr->size; i++) {
        CoreconfValueT *elem = &arr->elements[i];
        
        if (elem->type == CORECONF_HASHMAP) {
            CoreconfHashMapT *elem_map = elem->data.map_value;
            
            // Buscar en toda la hash table
            for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
                CoreconfObjectT *obj = elem_map->table[t];
                while (obj) {  // Recorrer lista enlazada
                    if (obj->value->type == CORECONF_STRING) {
                        if (strcmp(obj->value->data.string_value, 
                                  iid->key.str_key) == 0) {
                            return elem;  // ¡MATCH!
                        }
                    }
                    obj = obj->next;
                }
            }
        }
    }
}
```

#### Caso 3: IID_WITH_INT_KEY (acceso por índice)

```c
// Datos:
// {30: ["sensor1", "sensor2", "sensor3"]}
//
// IID: {IID_WITH_INT_KEY, 30, 1}
// Resultado: "sensor2"
```

**Algoritmo**: Acceso directo a `arr->elements[index]` con bounds checking.

**Código**:

```c
if (value->type == CORECONF_ARRAY) {
    CoreconfArrayT *arr = value->data.array_value;
    
    // Verificar límites
    if (iid->key.int_key >= 0 && 
        (size_t)iid->key.int_key < arr->size) {
        return &arr->elements[iid->key.int_key];
    }
}
return NULL;  // Fuera de límites
```

### Retorno

- **Éxito**: Puntero al `CoreconfValueT` encontrado
- **No encontrado**: `NULL`
- **Error de parámetros**: `NULL`

### Comparación con implementaciones incorrectas

❌ **Implementación básica (no RFC compliant)**:
```c
// Solo retorna el array completo
if (iid->type == IID_WITH_STR_KEY) {
    return value;  // Devuelve todo el array
}
```

✅ **Implementación correcta (RFC 9254)**:
```c
// Busca DENTRO del array y retorna el elemento específico
if (iid->type == IID_WITH_STR_KEY) {
    for (size_t i = 0; i < arr->size; i++) {
        // Buscar coincidencia
        if (match(arr->elements[i], key)) {
            return &arr->elements[i];  // Solo el elemento
        }
    }
}
```

---

## 5. create_fetch_response_iids()

**Líneas**: 197-230  
**Propósito**: Crear respuesta FETCH usando resolución semántica de instance-identifiers

### Diferencia con create_fetch_response()

| Función | Búsqueda | Soporte keys |
|---------|----------|--------------|
| `create_fetch_response()` | Directa por SID | ❌ No |
| `create_fetch_response_iids()` | Semántica con `fetch_value_by_iid()` | ✅ Sí |

### Código clave

```c
for (size_t i = 0; i < iid_count; i++) {
    // Búsqueda semántica
    CoreconfValueT *value = fetch_value_by_iid(data_source, &iids[i]);
    
    // Crear mapa {SID: valor_resuelto}
    zcbor_map_start_encode(state, 1);
    zcbor_uint64_put(state, iids[i].sid);
    
    if (value) {
        coreconfToCBOR(value, state);
    } else {
        zcbor_nil_put(state, NULL);
    }
    
    zcbor_map_end_encode(state, 1);
}
```

### Ejemplo completo

```c
// Cliente pide:
InstanceIdentifier iids[] = {
    {IID_SIMPLE, 20, ...},                  // Temperatura
    {IID_WITH_STR_KEY, 1533, "eth0"},      // Interfaz eth0
    {IID_WITH_INT_KEY, 30, 2}              // Tercer sensor
};

// Servidor responde (CBOR sequence):
// {20: 25.5}                                    ← Valor directo
// {1533: {name:"eth0", ip:"192.168.1.1"}}      ← Busca y resuelve
// {30: {id:3, value:45.2}}                     ← Acceso por índice
```

---

## 6. parse_fetch_request()

**Líneas**: 232-282  
**Propósito**: Decodificar petición FETCH simple (solo SIDs)

### Firma

```c
bool parse_fetch_request(const uint8_t *cbor_data, size_t cbor_size,
                          uint64_t **out_sids, size_t *out_count);
```

### Estrategia: Dos pasadas

#### Primera pasada: Contar SIDs

```c
size_t count = 0;
size_t offset = 0;

while (offset < cbor_size) {
    // Intentar decodificar uint64
    if (zcbor_uint64_decode(state, &temp_sid)) {
        count++;
    } else {
        break;  // No hay más SIDs
    }
    offset += bytes_consumed;
}
```

**¿Por qué contar primero?** Para saber cuánta memoria asignar con malloc.

#### Segunda pasada: Decodificar valores

```c
uint64_t *sids = malloc(sizeof(uint64_t) * count);

for (size_t i = 0; i < count; i++) {
    zcbor_uint64_decode(state, &sids[i]);
    // ...
}

*out_sids = sids;
*out_count = count;
```

### Gestión de memoria

⚠️ **IMPORTANTE**: El caller debe hacer `free(out_sids)` después.

```c
uint64_t *sids = NULL;
size_t count = 0;

if (parse_fetch_request(cbor, size, &sids, &count)) {
    // Usar sids...
    
    free(sids);  // ¡No olvidar!
}
```

---

## 7. is_fetch_request()

**Líneas**: 284-294  
**Propósito**: Distinguir entre FETCH y STORE

### Firma

```c
bool is_fetch_request(const uint8_t *cbor_data, size_t cbor_size);
```

### Lógica de detección

```
FETCH:  [SID1, SID2, ...]  ← Empieza con uint (SID)
STORE:  {SID: value, ...}  ← Empieza con mapa
```

### Implementación

```c
zcbor_state_t state[5];
zcbor_new_decode_state(state, 5, cbor_data, cbor_size, 1, NULL, 0);

uint64_t temp_sid;
return zcbor_uint64_decode(state, &temp_sid);
//     └─────────────┬────────────────┘
//           true si empieza con uint → FETCH
//           false si empieza con map → STORE
```

### Uso en servidor

```c
if (is_fetch_request(buffer, received)) {
    handle_fetch(buffer, received);
} else {
    handle_store(buffer, received);
}
```

---

## 8. parse_fetch_request_iids()

**Líneas**: 296-418  
**Propósito**: Decodificar petición FETCH completa con instance-identifiers

### Complejidad

Esta es la función de parsing más compleja porque debe manejar:
1. SIDs simples: `20`
2. Arrays con string: `[30, "eth0"]`
3. Arrays con int: `[30, 2]`
4. Todo mezclado en una CBOR sequence

### Estrategia: Dos pasadas (como parse_fetch_request)

#### Primera pasada: Contar elementos

```c
while (offset < cbor_size) {
    // Intentar SID simple
    if (zcbor_uint64_decode(state, &temp_sid)) {
        count++;
    } else {
        // Intentar array [SID, key]
        if (zcbor_list_start_decode(state)) {
            count++;
            // Saltar al final del array
            // (código complejo para navegar CBOR)
        }
    }
}
```

#### Segunda pasada: Decodificar valores

**Alojar memoria**:

```c
InstanceIdentifier *iids = calloc(count, sizeof(InstanceIdentifier));
```

**Para cada elemento**:

```c
for (size_t i = 0; i < count; i++) {
    if (zcbor_uint64_decode(state, &sid)) {
        // Caso 1: SID simple
        iids[i].type = IID_SIMPLE;
        iids[i].sid = sid;
        iids[i].key.str_key = NULL;
    } else {
        // Caso 2: Array [SID, key]
        zcbor_list_start_decode(state);
        zcbor_uint64_decode(state, &sid);
        
        iids[i].sid = sid;
        
        // Intentar string
        struct zcbor_string zstr;
        if (zcbor_tstr_decode(state, &zstr)) {
            iids[i].type = IID_WITH_STR_KEY;
            // Copiar string (con malloc)
            iids[i].key.str_key = malloc(zstr.len + 1);
            memcpy(iids[i].key.str_key, zstr.value, zstr.len);
            iids[i].key.str_key[zstr.len] = '\0';  // Null terminator
        } else {
            // Intentar int
            int64_t int_key;
            if (zcbor_int64_decode(state, &int_key)) {
                iids[i].type = IID_WITH_INT_KEY;
                iids[i].key.int_key = int_key;
            }
        }
        
        zcbor_list_end_decode(state);
    }
}
```

### Punto crítico: Copiar strings

⚠️ **Problema**: `zcbor_string` NO está null-terminated.

```c
struct zcbor_string zstr;
zcbor_tstr_decode(state, &zstr);
// zstr.value pointing to CBOR buffer
// zstr.len = 4 (ejemplo)
// NO hay '\0' al final
```

✅ **Solución**: Copiar a nuevo buffer con null terminator.

```c
char *str = malloc(zstr.len + 1);  // +1 para '\0'
memcpy(str, zstr.value, zstr.len);
str[zstr.len] = '\0';  // Añadir terminador
```

### Gestión de errores

Si algo falla a mitad de camino:

```c
if (error) {
    free_instance_identifiers(iids, i);  // Liberar lo que llevamos
    return false;
}
```

---

## 9. free_instance_identifiers()

**Líneas**: 420-429  
**Propósito**: Liberar memoria de instance-identifiers

### Firma

```c
void free_instance_identifiers(InstanceIdentifier *iids, size_t count);
```

### ¿Qué libera?

1. Strings en `IID_WITH_STR_KEY` (asignados con malloc)
2. Array de estructuras (asignado con calloc)

### Implementación

```c
void free_instance_identifiers(InstanceIdentifier *iids, size_t count) {
    if (!iids) return;
    
    // 1. Liberar strings dentro
    for (size_t i = 0; i < count; i++) {
        if (iids[i].type == IID_WITH_STR_KEY && iids[i].key.str_key) {
            free(iids[i].key.str_key);
        }
    }
    
    // 2. Liberar array principal
    free(iids);
}
```

### ⚠️ Orden importante

```c
// ❌ INCORRECTO (memory leak)
free(iids);  // Pierdes acceso a iids[i].key.str_key
for (...) {
    free(iids[i].key.str_key);  // ¡Ya no puedes acceder!
}

// ✅ CORRECTO
for (...) {
    free(iids[i].key.str_key);  // Primero liberar contenido
}
free(iids);  // Luego liberar contenedor
```

### Uso típico

```c
InstanceIdentifier *iids = NULL;
size_t count = 0;

// Parsear
if (parse_fetch_request_iids(cbor, size, &iids, &count)) {
    // Procesar...
    
    // Limpiar
    free_instance_identifiers(iids, count);
}
```

---

## 📊 Resumen de Funciones

| Función | LOC | Complejidad | Propósito |
|---------|-----|-------------|-----------|
| `create_fetch_request()` | 21 | Baja | Encoding simple |
| `create_fetch_request_with_iids()` | 47 | Media | Encoding con keys |
| `create_fetch_response()` | 39 | Baja | Response simple |
| `fetch_value_by_iid()` | ⭐ 76 | **Alta** | **Búsqueda semántica** |
| `create_fetch_response_iids()` | 34 | Media | Response con resolución |
| `parse_fetch_request()` | 51 | Media | Parsing simple |
| `is_fetch_request()` | 11 | Muy baja | Detección |
| `parse_fetch_request_iids()` | ⭐ 123 | **Muy alta** | **Parsing completo** |
| `free_instance_identifiers()` | 10 | Baja | Cleanup |

---

## 🎓 Conceptos Clave para TFG

### 1. CBOR Sequences vs Arrays

```
Array CBOR:     [20, 21, 30]     → 0x83 0x14 0x15 0x1E
CBOR Sequence:  20, 21, 30       → 0x14 0x15 0x1E
                                     └─── 1 byte menos
```

### 2. Two-Pass Parsing

Patrón común en C cuando no sabes el tamaño:
1. Primera pasada: contar
2. `malloc(count * sizeof(...))`
3. Segunda pasada: llenar

### 3. Búsqueda Semántica

La clave del RFC 9254:
- `[1533, "eth0"]` NO es solo formato
- Es una **instrucción de búsqueda**
- El servidor debe **buscar** en el array
- Retornar el elemento que coincide

### 4. Memory Management

Reglas:
- Si una función usa `**out_`, hace malloc → caller hace free
- Liberar "de dentro hacia fuera"
- NULL checks siempre

### 5. Estado de zcbor

```c
zcbor_state_t state[5];  // Stack de 5 niveles
// [0] = nivel actual
// [1-4] = para anidamiento (arrays, maps dentro de maps)
```

---

## 🔍 Debugging Tips

### Ver bytes CBOR

```c
for (size_t i = 0; i < len; i++) {
    printf("%02X ", buffer[i]);
}
```

### Verificar memoria

```bash
valgrind --leak-check=full ./programa
```

### Logging

```c
#ifdef DEBUG
printf("[FETCH] Parsed %zu identifiers\n", count);
#endif
```

---

**Fin del análisis técnico** ✅
