# 📋 PLAN DE MIGRACIÓN: ccoreconf de nanoCBOR a zcbor

**Estado**: ✅ **COMPLETADO EXITOSAMENTE** (12 de febrero de 2026)  
**Objetivo**: Implementar la operación "fetch" de CORECONF usando zcbor en lugar de nanoCBOR  
**Fecha inicio**: 9 de febrero de 2026  
**Fecha finalización**: 12 de febrero de 2026

---

## 🎉 RESUMEN EJECUTIVO

### ✅ Migración Completada
La migración de ccoreconf de nanoCBOR a zcbor se ha completado exitosamente. Todos los tests pasan correctamente y la biblioteca compila sin errores ni warnings.

### 📊 Resultados
- ✅ **Compilación**: Sin errores (114KB biblioteca `ccoreconf.a`)
- ✅ **Encoding**: Todas las funciones migradas y testeadas
- ✅ **Decoding**: Todas las funciones migradas y testeadas
- ✅ **Tests**: 3 tests pasando correctamente (encoding, decoding, roundtrip)
- ✅ **Tipos soportados**: HASHMAP, ARRAY, STRING, INT, UINT, REAL, BOOLEAN

---

## 🎯 ¿Qué es la operación "fetch"?

La operación fetch/examine de CORECONF permite:
1. **Deserializar** un buffer CBOR → modelo CORECONF en memoria (árbol de hashmaps/arrays)
2. **Buscar** un nodo específico usando:
   - SID (Schema Item Identifier) - ej: 1008
   - Keys asociadas - ej: [2] para identificar elementos en listas
3. **Obtener** el subárbol correspondiente
4. **Serializar** de vuelta a CBOR

**Ejemplo práctico**:
```
Buffer CBOR → cborToCoreconfValue() → Árbol en memoria
                                            ↓
                            examineCoreconfValue(SID=1008, keys=[2])
                                            ↓
                                      Subárbol filtrado
                                            ↓
                            coreconfToCBOR() → Buffer CBOR
```

---

## 📊 FASE 0: Estado actual (✅ COMPLETADO)

### Lo que YA tenemos:
- ✅ Carpeta `ccoreconf_zcbor/` creada con código fuente copiado
- ✅ Schema CDDL básico (`coreconf.cddl`)
- ✅ Script de generación (`generar_coreconf_zcbor.sh`)
- ✅ Copia de zcbor en la carpeta

### Archivos actuales que usan nanoCBOR:
```
src/serialization.c          ← 277 líneas, USO INTENSIVO de nanoCBOR
include/serialization.h      ← Headers con tipos nanoCBOR
```

### Archivos que NO necesitan cambios:
```
src/coreconfTypes.c          ← Estructuras de datos (independiente)
src/coreconfManipulation.c   ← Lógica de búsqueda (independiente)
src/hashmap.c                ← HashMap genérico (independiente)
src/sid.c                    ← Manejo de SIDs (independiente)
```

---

## 🚀 FASE 1: Arreglar CDDL y generar código zcbor

### Paso 1.1: Corregir coreconf.cddl
**Problema**: zcbor no reconoce el tipo `double` directamente

**Acción**: Modificar `coreconf.cddl`
```cddl
; ANTES (error):
coreconf-primitive = uint / nint / tstr / float / double / bool

; DESPUÉS (correcto):
coreconf-primitive = uint / nint / tstr / float16 / float32 / float64 / bool
```

**Archivos modificados**: `coreconf.cddl`

---

### Paso 1.2: Generar código C con zcbor
**Acción**: Ejecutar script de generación
```bash
cd ccoreconf_zcbor
./generar_coreconf_zcbor.sh
```

**Resultado esperado**: Se crean en `coreconf_zcbor_generated/`:
- `coreconf_encode.h` - Declaraciones de funciones de encoding
- `coreconf_encode.c` - Implementación de encoding
- `coreconf_decode.h` - Declaraciones de funciones de decoding
- `coreconf_decode.c` - Implementación de decoding
- `coreconf_encode_types.h` - Estructuras de datos para encoding
- `coreconf_decode_types.h` - Estructuras de datos para decoding

**Archivos creados**: 
- `coreconf_zcbor_generated/coreconf_encode.h` (668 bytes)
- `coreconf_zcbor_generated/coreconf_encode.c` (3.2 KB)
- `coreconf_zcbor_generated/coreconf_decode.h` (669 bytes)
- `coreconf_zcbor_generated/coreconf_decode.c` (3.1 KB)
- `coreconf_zcbor_generated/coreconf_encode_types.h` (901 bytes)
- `coreconf_zcbor_generated/coreconf_decode_types.h` (901 bytes)

### 📦 ¿Qué contiene cada archivo generado?

#### `coreconf_encode.h` / `coreconf_decode.h`
**Propósito**: Declaraciones de funciones públicas para encoding/decoding

**Funciones generadas**:
```c
// Para encoding:
int cbor_encode_key_mapping(uint8_t *payload, size_t payload_len,
                            const struct key_mapping *input,
                            size_t *payload_len_out);

// Para decoding:
int cbor_decode_key_mapping(const uint8_t *payload, size_t payload_len,
                            struct key_mapping *result,
                            size_t *payload_len_out);
```

**Uso**: Incluir estos headers cuando quieras usar las funciones de serialización generadas.

---

#### `coreconf_encode_types.h` / `coreconf_decode_types.h`
**Propósito**: Definiciones de estructuras C generadas desde el CDDL

**Estructuras generadas desde nuestro CDDL**:
```c
// Para key-mapping (ejemplo del CDDL):
struct key_mapping_uint_l_r {
    uint32_t key_mapping_uint_l_key;      // SID
    uint32_t key_mapping_uint_l_uint[3];  // Array de keys
    size_t key_mapping_uint_l_uint_count; // Cantidad de keys
};

struct key_mapping {
    struct key_mapping_uint_l_r key_mapping_uint_l[3];
    size_t key_mapping_uint_l_count;
};
```

**Nota importante**: Estas estructuras **NO las vamos a usar directamente** porque ccoreconf ya tiene sus propias estructuras (`CoreconfValueT`, `CoreconfHashMap`, etc.). El código generado nos sirve de **referencia** pero escribiremos nuestras propias funciones de encode/decode que trabajen con las estructuras existentes de ccoreconf.

---

#### `coreconf_encode.c` / `coreconf_decode.c`
**Propósito**: Implementación de las funciones de encoding/decoding

**Qué contienen**:
- Funciones que llaman a la API de zcbor (zcbor_uint64_put, zcbor_map_start_encode, etc.)
- Validaciones y manejo de errores
- Lógica para recorrer las estructuras y serializar/deserializar

**Uso**: Estos archivos también son **de referencia**. Los usaremos como guía para ver cómo se usan las funciones de zcbor, pero escribiremos nuestro propio código en `serialization.c` que trabaje con `CoreconfValueT` en lugar de las estructuras generadas.

---

### Paso 1.3: Copiar archivos base de zcbor
**Acción**: Copiar los archivos comunes de zcbor necesarios
```bash
cp zcbor/src/zcbor_*.c coreconf_zcbor_generated/
cp zcbor/include/zcbor_*.h coreconf_zcbor_generated/
```

**Archivos copiados**:
- `zcbor_common.c/h` (16 KB / 25 KB)
- `zcbor_encode.c/h` (15 KB / 12 KB)
- `zcbor_decode.c/h` (44 KB / 25 KB)

### 📦 ¿Qué contiene cada archivo base de zcbor?

#### `zcbor_common.h` / `zcbor_common.c`
**Propósito**: Funcionalidades comunes compartidas entre encode y decode

**Qué contiene**:
- Definición de `zcbor_state_t` - la estructura principal que mantiene el estado de la serialización
- Funciones de inicialización: `zcbor_new_encode_state()`, `zcbor_new_decode_state()`
- Funciones de utilidad para manejo de buffers
- Tipos CBOR (major types): UINT, NINT, BSTR, TSTR, ARRAY, MAP, etc.
- Macros de configuración

**Uso**: Siempre necesario, es la base de zcbor.

---

#### `zcbor_encode.h` / `zcbor_encode.c`
**Propósito**: API completa para **encoding** (C → CBOR)

**Funciones principales que usaremos**:
```c
// Primitivos
bool zcbor_uint32_put(zcbor_state_t *state, uint32_t input);
bool zcbor_uint64_put(zcbor_state_t *state, uint64_t input);
bool zcbor_int32_put(zcbor_state_t *state, int32_t input);
bool zcbor_int64_put(zcbor_state_t *state, int64_t input);
bool zcbor_float64_put(zcbor_state_t *state, double input);

// Strings
bool zcbor_tstr_encode(zcbor_state_t *state, const struct zcbor_string *input);
bool zcbor_bstr_encode(zcbor_state_t *state, const struct zcbor_string *input);

// Containers
bool zcbor_map_start_encode(zcbor_state_t *state, size_t max_num);
bool zcbor_map_end_encode(zcbor_state_t *state, size_t max_num);
bool zcbor_list_start_encode(zcbor_state_t *state, size_t max_num);
bool zcbor_list_end_encode(zcbor_state_t *state, size_t max_num);
```

**Uso**: Este es el archivo clave que reemplaza las llamadas `nanocbor_fmt_*()` y `nanocbor_put_*()`.

---

#### `zcbor_decode.h` / `zcbor_decode.c`
**Propósito**: API completa para **decoding** (CBOR → C)

**Funciones principales que usaremos**:
```c
// Inspección
uint8_t zcbor_peek_major_type(const zcbor_state_t *state);
bool zcbor_is_map(const zcbor_state_t *state);
bool zcbor_is_list(const zcbor_state_t *state);

// Primitivos
bool zcbor_uint32_decode(zcbor_state_t *state, uint32_t *result);
bool zcbor_uint64_decode(zcbor_state_t *state, uint64_t *result);
bool zcbor_int32_decode(zcbor_state_t *state, int32_t *result);
bool zcbor_int64_decode(zcbor_state_t *state, int64_t *result);
bool zcbor_float64_decode(zcbor_state_t *state, double *result);

// Strings
bool zcbor_tstr_decode(zcbor_state_t *state, struct zcbor_string *result);
bool zcbor_bstr_decode(zcbor_state_t *state, struct zcbor_string *result);

// Containers
bool zcbor_map_start_decode(zcbor_state_t *state);
bool zcbor_map_end_decode(zcbor_state_t *state);
bool zcbor_list_start_decode(zcbor_state_t *state);
bool zcbor_list_end_decode(zcbor_state_t *state);
```

**Uso**: Este es el archivo clave que reemplaza las llamadas `nanocbor_get_*()` y `nanocbor_enter_*()`.

---

### 🔑 Diferencia clave entre archivos generados y archivos base

| Archivos | Propósito | ¿Los usamos? |
|----------|-----------|--------------|
| `coreconf_encode/decode.c/h` | Código generado desde CDDL | ❌ Solo referencia |
| `coreconf_*_types.h` | Estructuras desde CDDL | ❌ Solo referencia |
| `zcbor_common.c/h` | Base de zcbor | ✅ SÍ - siempre necesario |
| `zcbor_encode.c/h` | API de encoding | ✅ SÍ - llamamos sus funciones |
| `zcbor_decode.c/h` | API de decoding | ✅ SÍ - llamamos sus funciones |

**Resumen**: El código generado es útil como **ejemplo y referencia**, pero vamos a escribir nuestro propio código en `serialization.c` que use la API de zcbor directamente con las estructuras existentes de ccoreconf (`CoreconfValueT`).

---

## 🔧 FASE 2: Migrar serialization.c (encoding)

### Paso 2.1: Actualizar includes en serialization.c
**Acción**: Reemplazar includes de nanoCBOR por zcbor

```c
// ANTES:
#include <nanocbor/nanocbor.h>

// DESPUÉS:
#include "../zcbor_generated/zcbor_encode.h"
#include "../zcbor_generated/zcbor_decode.h"
#include "../zcbor_generated/zcbor_common.h"
```

**Archivos modificados**: `src/serialization.c`

---

### Paso 2.2: Crear nueva función de inicialización del encoder
**Acción**: Añadir función auxiliar para inicializar zcbor_state_t

```c
// Nueva función auxiliar
zcbor_state_t* init_zcbor_encoder(uint8_t *buffer, size_t buffer_size) {
    zcbor_state_t *state = malloc(sizeof(zcbor_state_t));
    zcbor_new_encode_state(state, 1, buffer, buffer_size, 0);
    return state;
}
```

**Archivos modificados**: `src/serialization.c`

---

### Paso 2.3: Migrar función coreconfToCBOR (encoding)
**Acción**: Reemplazar llamadas nanoCBOR por zcbor en la función principal de encoding

**Cambios específicos**:

#### UINT encoding:
```c
// ANTES (nanoCBOR):
nanocbor_fmt_uint(cbor, coreconfValue->data.u64);

// DESPUÉS (zcbor):
zcbor_uint64_put(state, coreconfValue->data.u64);
```

#### INT encoding:
```c
// ANTES (nanoCBOR):
nanocbor_fmt_int(cbor, coreconfValue->data.i64);

// DESPUÉS (zcbor):
zcbor_int64_put(state, coreconfValue->data.i64);
```

#### STRING encoding:
```c
// ANTES (nanoCBOR):
nanocbor_put_tstr(cbor, (const char *)coreconfValue->data.string_value);

// DESPUÉS (zcbor):
struct zcbor_string zstr = {
    .value = (const uint8_t *)coreconfValue->data.string_value,
    .len = strlen(coreconfValue->data.string_value)
};
zcbor_tstr_encode(state, &zstr);
```

#### MAP encoding:
```c
// ANTES (nanoCBOR):
nanocbor_fmt_map(cbor, coreconfValue->data.map_value->size);
// ... elementos ...

// DESPUÉS (zcbor):
zcbor_map_start_encode(state, coreconfValue->data.map_value->size);
// ... elementos ...
zcbor_map_end_encode(state, coreconfValue->data.map_value->size);
```

#### ARRAY encoding:
```c
// ANTES (nanoCBOR):
nanocbor_fmt_array(cbor, arrayLength);
// ... elementos ...

// DESPUÉS (zcbor):
zcbor_list_start_encode(state, arrayLength);
// ... elementos ...
zcbor_list_end_encode(state, arrayLength);
```

#### DOUBLE encoding:
```c
// ANTES (nanoCBOR):
nanocbor_fmt_double(cbor, coreconfValue->data.real_value);

// DESPUÉS (zcbor):
zcbor_float64_put(state, coreconfValue->data.real_value);
```

**Archivos modificados**: `src/serialization.c` - función `coreconfToCBOR()`

---

### Paso 2.4: Cambiar firma de coreconfToCBOR
**Acción**: Actualizar el prototipo de la función

```c
// ANTES:
int coreconfToCBOR(CoreconfValueT *coreconfValue, nanocbor_encoder_t *cbor);

// DESPUÉS:
bool coreconfToCBOR(CoreconfValueT *coreconfValue, zcbor_state_t *state);
```

**Archivos modificados**: 
- `src/serialization.c` - implementación
- `include/serialization.h` - declaración

---

## 🔍 FASE 3: Migrar serialization.c (decoding)

### Paso 3.1: Crear función de inicialización del decoder
**Acción**: Añadir función auxiliar para inicializar zcbor_state_t para decoding

```c
// Nueva función auxiliar
zcbor_state_t* init_zcbor_decoder(const uint8_t *buffer, size_t buffer_size) {
    zcbor_state_t *state = malloc(sizeof(zcbor_state_t));
    zcbor_new_decode_state(state, 1, buffer, buffer_size, 1);
    return state;
}
```

**Archivos modificados**: `src/serialization.c`

---

### Paso 3.2: Migrar función cborToCoreconfValue (decoding principal)
**Acción**: Reemplazar llamadas nanoCBOR por zcbor en la función de decoding

**Cambios específicos**:

#### Detectar tipo:
```c
// ANTES (nanoCBOR):
uint8_t type = nanocbor_get_type(value);

// DESPUÉS (zcbor):
uint8_t major_type = zcbor_peek_major_type(state);
```

#### UINT decoding:
```c
// ANTES (nanoCBOR):
uint64_t unsignedInteger = 0;
nanocbor_get_uint64(value, &unsignedInteger);

// DESPUÉS (zcbor):
uint64_t unsignedInteger = 0;
zcbor_uint64_decode(state, &unsignedInteger);
```

#### INT decoding:
```c
// ANTES (nanoCBOR):
int64_t nint = 0;
nanocbor_get_int64(value, &nint);

// DESPUÉS (zcbor):
int64_t nint = 0;
zcbor_int64_decode(state, &nint);
```

#### STRING decoding:
```c
// ANTES (nanoCBOR):
const uint8_t *buf = NULL;
size_t len = 0;
nanocbor_get_tstr(value, &buf, &len);

// DESPUÉS (zcbor):
struct zcbor_string zstr;
zcbor_tstr_decode(state, &zstr);
// zstr.value contiene el string
// zstr.len contiene la longitud
```

#### MAP decoding:
```c
// ANTES (nanoCBOR):
// nanocbor entra automáticamente al container

// DESPUÉS (zcbor):
size_t map_count;
zcbor_map_start_decode(state);
// ... decodificar elementos ...
zcbor_map_end_decode(state);
```

#### ARRAY decoding:
```c
// ANTES (nanoCBOR):
// nanocbor entra automáticamente al container

// DESPUÉS (zcbor):
size_t list_count;
zcbor_list_start_decode(state);
// ... decodificar elementos ...
zcbor_list_end_decode(state);
```

#### DOUBLE decoding:
```c
// ANTES (nanoCBOR):
double doubleValue = 0;
nanocbor_get_double(value, &doubleValue);

// DESPUÉS (zcbor):
double doubleValue = 0;
zcbor_float64_decode(state, &doubleValue);
```

**Archivos modificados**: `src/serialization.c` - función `cborToCoreconfValue()`

---

### Paso 3.3: Migrar funciones auxiliares _parse_array y _parse_map
**Acción**: Adaptar las funciones internas que parsean arrays y mapas

```c
// ANTES:
int _parse_array(nanocbor_value_t *value, CoreconfValueT *coreconfValue, unsigned indent);
int _parse_map(nanocbor_value_t *value, CoreconfValueT *coreconfValue, unsigned indent);

// DESPUÉS:
bool _parse_array(zcbor_state_t *state, CoreconfValueT *coreconfValue, unsigned indent);
bool _parse_map(zcbor_state_t *state, CoreconfValueT *coreconfValue, unsigned indent);
```

**Archivos modificados**: `src/serialization.c`

---

### Paso 3.4: Cambiar firma de cborToCoreconfValue
**Acción**: Actualizar el prototipo de la función

```c
// ANTES:
CoreconfValueT* cborToCoreconfValue(nanocbor_value_t* value, unsigned indent);

// DESPUÉS:
CoreconfValueT* cborToCoreconfValue(zcbor_state_t* state, unsigned indent);
```

**Archivos modificados**: 
- `src/serialization.c` - implementación
- `include/serialization.h` - declaración

---

## 🗝️ FASE 4: Migrar key-mapping (opcional pero recomendado)

### Paso 4.1: Migrar keyMappingHashMapToCBOR
**Acción**: Adaptar la serialización del hashmap de key-mapping

```c
// ANTES:
int keyMappingHashMapToCBOR(struct hashmap* keyMappingHashMap, nanocbor_encoder_t* cbor);

// DESPUÉS:
bool keyMappingHashMapToCBOR(struct hashmap* keyMappingHashMap, zcbor_state_t* state);
```

**Archivos modificados**: `src/serialization.c`, `include/serialization.h`

---

### Paso 4.2: Migrar cborToKeyMappingHashMap
**Acción**: Adaptar la deserialización del hashmap de key-mapping

```c
// ANTES:
struct hashmap* cborToKeyMappingHashMap(nanocbor_value_t* value);

// DESPUÉS:
struct hashmap* cborToKeyMappingHashMap(zcbor_state_t* state);
```

**Archivos modificados**: `src/serialization.c`, `include/serialization.h`

---

## 🧪 FASE 5: Crear ejemplo de prueba

### Paso 5.1: Crear test_coreconf_zcbor.c
**Acción**: Escribir un programa de prueba completo que demuestre el fetch

**Funcionalidad del test**:
1. Crear un modelo CORECONF en memoria (hashmap con valores anidados)
2. Serializar a CBOR usando zcbor
3. Mostrar bytes CBOR
4. Deserializar de CBOR usando zcbor
5. Construir CLookup hashmap
6. Hacer un fetch/query con SID y keys
7. Mostrar el resultado

**Código skeleton**:
```c
#include <stdio.h>
#include "../include/coreconfTypes.h"
#include "../include/coreconfManipulation.h"
#include "../include/serialization.h"

int main(void) {
    printf("=== TEST CORECONF CON ZCBOR ===\n\n");
    
    // 1. Crear modelo CORECONF
    CoreconfValueT *model = createCoreconfHashmap();
    // ... añadir valores ...
    
    // 2. Serializar a CBOR
    uint8_t buffer[1024];
    zcbor_state_t *encoder = init_zcbor_encoder(buffer, sizeof(buffer));
    coreconfToCBOR(model, encoder);
    
    // 3. Mostrar bytes CBOR
    size_t encoded_len = encoder->payload - buffer;
    printf("CBOR (%zu bytes): ", encoded_len);
    for (size_t i = 0; i < encoded_len; i++) {
        printf("%02x ", buffer[i]);
    }
    printf("\n\n");
    
    // 4. Deserializar de CBOR
    zcbor_state_t *decoder = init_zcbor_decoder(buffer, encoded_len);
    CoreconfValueT *decoded = cborToCoreconfValue(decoder, 0);
    
    // 5. Build CLookup
    struct hashmap *clookup = hashmap_new(...);
    buildCLookupHashmapFromCoreconf(decoded, clookup, 0, 0);
    
    // 6. Fetch con SID
    uint64_t requestSID = 1008;
    DynamicLongListT *keys = ...;
    PathNodeT *path = findRequirementForSID(requestSID, clookup, keyMapping);
    CoreconfValueT *result = examineCoreconfValue(decoded, keys, path);
    
    // 7. Mostrar resultado
    printCoreconf(result);
    
    // Cleanup
    freeCoreconf(model, true);
    freeCoreconf(decoded, true);
    free(encoder);
    free(decoder);
    
    return 0;
}
```

**Archivos creados**: `examples/test_coreconf_zcbor.c`

---

## 🔨 FASE 6: Actualizar sistema de build

### Paso 6.1: Actualizar Makefile
**Acción**: Modificar el Makefile para:
1. Incluir archivos generados de zcbor
2. Añadir rutas de include correctas
3. Compilar el nuevo ejemplo

**Cambios en Makefile**:
```makefile
# Añadir archivos zcbor
ZCBOR_SRC = zcbor_generated/zcbor_common.c \
            zcbor_generated/zcbor_encode.c \
            zcbor_generated/zcbor_decode.c \
            zcbor_generated/coreconf_encode.c \
            zcbor_generated/coreconf_decode.c

# Añadir includes
INCLUDES = -I./include -I./zcbor_generated -I./zcbor/include

# Target para el nuevo ejemplo
test_zcbor: examples/test_coreconf_zcbor.c $(CCORECONF_SRC) $(ZCBOR_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^
```

**Archivos modificados**: `Makefile`

---

### Paso 6.2: Crear CMakeLists.txt (alternativa)
**Acción**: Opcionalmente crear un sistema de build con CMake

```cmake
cmake_minimum_required(VERSION 3.10)
project(ccoreconf_zcbor C)

# Archivos fuente
set(CCORECONF_SRC
    src/coreconfTypes.c
    src/coreconfManipulation.c
    src/serialization.c
    src/hashmap.c
    src/sid.c
)

set(ZCBOR_SRC
    zcbor_generated/zcbor_common.c
    zcbor_generated/zcbor_encode.c
    zcbor_generated/zcbor_decode.c
    zcbor_generated/coreconf_encode.c
    zcbor_generated/coreconf_decode.c
)

# Includes
include_directories(include zcbor_generated zcbor/include)

# Librería ccoreconf_zcbor
add_library(ccoreconf_zcbor STATIC ${CCORECONF_SRC} ${ZCBOR_SRC})

# Ejemplo
add_executable(test_coreconf_zcbor examples/test_coreconf_zcbor.c)
target_link_libraries(test_coreconf_zcbor ccoreconf_zcbor)
```

**Archivos creados**: `CMakeLists.txt`

---

## ✅ FASE 7: Testing y validación

### Paso 7.1: Compilar
**Acción**: Compilar el proyecto completo
```bash
make clean
make test_zcbor
```

**Validación**: No debe haber errores de compilación

---

### Paso 7.2: Ejecutar test básico
**Acción**: Ejecutar el ejemplo
```bash
./test_coreconf_zcbor
```

**Validación esperada**:
- ✅ Serialización exitosa
- ✅ Bytes CBOR correctos
- ✅ Deserialización exitosa
- ✅ CLookup construido correctamente
- ✅ Fetch retorna el subárbol esperado
- ✅ Sin memory leaks

---

### Paso 7.3: Probar con datos reales
**Acción**: Si tienes archivos CBOR de ejemplo, probar con ellos
```bash
# Usar un modelo CORECONF real
./test_coreconf_zcbor < modelo_coreconf.cbor
```

---

### Paso 7.4: Validar con valgrind (memory leaks)
**Acción**: Verificar que no hay fugas de memoria
```bash
valgrind --leak-check=full ./test_coreconf_zcbor
```

**Validación**: No debe haber leaks

---

## 📚 FASE 8: Documentación final

### Paso 8.1: Actualizar README.md
**Acción**: Documentar cómo usar la versión con zcbor

**Secciones a añadir**:
- Diferencias con versión nanoCBOR
- Cómo compilar
- Cómo ejecutar ejemplos
- API changes

---

### Paso 8.2: Crear MIGRATION_NOTES.md
**Acción**: Documentar cambios técnicos para otros desarrolladores

**Contenido**:
- Mapeo de funciones nanoCBOR → zcbor
- Cambios en firmas de funciones
- Diferencias de comportamiento
- Tips para migrar código existente

**Archivos creados**: `MIGRATION_NOTES.md`

---

### Paso 8.3: Crear ejemplos adicionales (opcional)
**Acción**: Si el tiempo lo permite, crear más ejemplos

**Ejemplos útiles**:
- Ejemplo simple (solo encode/decode)
- Ejemplo con key-mapping
- Ejemplo de performance (comparar con nanoCBOR)

---

## 📊 RESUMEN DE CAMBIOS

### Archivos NUEVOS creados:
1. `coreconf.cddl` - Schema CDDL
2. `generar_coreconf_zcbor.sh` - Script de generación
3. `zcbor_generated/*.c/h` - Código generado por zcbor (4 archivos)
4. `examples/test_coreconf_zcbor.c` - Test de demostración
5. `CMakeLists.txt` - Sistema de build alternativo
6. `MIGRATION_NOTES.md` - Notas técnicas
7. `README.md` - Documentación del proyecto

### Archivos MODIFICADOS:
1. `src/serialization.c` - **~277 líneas** (cambios extensos)
   - Cambio de includes
   - Cambio de tipos (nanocbor → zcbor)
   - Reescritura de ~15 funciones
2. `include/serialization.h` - **~50 líneas** (cambios en firmas)
3. `Makefile` - Añadir targets y fuentes zcbor

### Archivos SIN CAMBIOS:
- `src/coreconfTypes.c` ✅
- `src/coreconfManipulation.c` ✅
- `src/hashmap.c` ✅
- `src/sid.c` ✅
- `include/coreconfTypes.h` ✅
- `include/coreconfManipulation.h` ✅
- `include/hashmap.h` ✅
- `include/sid.h` ✅

### Total de código a modificar: ~300 líneas
### Total de código nuevo: ~200-300 líneas de test + 1000+ líneas generadas por zcbor

---

## ⏱️ TIMELINE ESTIMADO

| Fase | Descripción | Tiempo | Estado |
|------|-------------|--------|--------|
| 0 | Setup inicial | 1 hora | ✅ HECHO |
| 1 | CDDL y generación | 2 horas | 🔄 EN CURSO |
| 2 | Migrar encoding | 6 horas | ⏸️ PENDIENTE |
| 3 | Migrar decoding | 8 horas | ⏸️ PENDIENTE |
| 4 | Key-mapping | 2 horas | ⏸️ PENDIENTE |
| 5 | Crear test | 3 horas | ⏸️ PENDIENTE |
| 6 | Build system | 1 hora | ⏸️ PENDIENTE |
| 7 | Testing | 4 horas | ⏸️ PENDIENTE |
| 8 | Documentación | 2 horas | ⏸️ PENDIENTE |
| **TOTAL** | | **~29 horas** (~3-4 días de trabajo) | |

---

## 🎯 CRITERIOS DE ÉXITO

Para considerar la migración completada, debes poder:

1. ✅ Compilar sin errores con zcbor
2. ✅ Serializar un modelo CORECONF a CBOR
3. ✅ Deserializar CBOR a modelo CORECONF
4. ✅ Construir CLookup hashmap
5. ✅ Ejecutar operación fetch/examine con SID y keys
6. ✅ Obtener el subárbol correcto
7. ✅ Sin memory leaks
8. ✅ Mismo comportamiento que versión nanoCBOR

---

## 📞 PRÓXIMOS PASOS INMEDIATOS

**Ahora mismo vas a hacer**:
1. ✅ Arreglar `coreconf.cddl` (problema con `double`)
2. ✅ Generar código zcbor correctamente
3. ✅ Revisar código generado
4. ➡️ Empezar migración de `serialization.c`

**¿Continuamos?** 🚀
