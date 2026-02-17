# Guía de Implementación FETCH RFC 9254 - Tutorial para TFG

**Autor**: Sistema ccoreconf_zcbor  
**Fecha**: 17 de febrero de 2026  
**RFC**: 9254 Sección 3.1.3 (FETCH Operation)  
**Audiencia**: Programadores junior trabajando en TFG

---

## 📚 Índice

1. [¿Qué es FETCH y por qué existe?](#1-qué-es-fetch-y-por-qué-existe)
2. [Arquitectura de la Librería](#2-arquitectura-de-la-librería)
3. [¿Cómo se integra en la Librería?](#3-cómo-se-integra-en-la-librería)
4. [¿Cómo lo usan los Contenedores Docker?](#4-cómo-lo-usan-los-contenedores-docker)
5. [Análisis Detallado de fetch.h](#5-análisis-detallado-de-fetchh)
6. [Análisis Detallado de fetch.c](#6-análisis-detallado-de-fetchc)
7. [Flujo Completo de una Operación FETCH](#7-flujo-completo-de-una-operación-fetch)
8. [Casos de Uso Prácticos](#8-casos-de-uso-prácticos)

---

## 1. ¿Qué es FETCH y por qué existe?

### El Problema

Imagina que tienes un sensor IoT con 100 campos de datos:
- Temperatura
- Humedad
- Presión
- Ubicación GPS (latitud, longitud, altitud)
- Estado de batería
- ID del dispositivo
- ... (y 94 campos más)

**Problema**: Si cada vez que quieres consultar solo la temperatura, el dispositivo te envía los 100 campos, estás desperdiciando:
- Ancho de banda (costoso en redes IoT)
- Batería del dispositivo
- Tiempo de transmisión

### La Solución: FETCH

FETCH es una operación que permite **pedir solo los datos que necesitas**:

```
Cliente: "Dame solo temperatura (SID 20) y batería (SID 50)"
Servidor: {20: 25.5°C, 50: 87%}
```

### ¿Por qué RFC 9254?

RFC 9254 define CORECONF, un protocolo estándar para gestionar configuración y datos en dispositivos IoT usando CBOR (un formato binario compacto). La sección 3.1.3 especifica exactamente cómo debe funcionar FETCH para que todos los dispositivos sean interoperables.

---

## 2. Arquitectura de la Librería

### Estructura del Proyecto

```
ccoreconf_zcbor/
├── include/
│   ├── coreconfTypes.h      # Tipos de datos básicos (hashmaps, arrays, valores)
│   ├── serialization.h      # Conversión CBOR ↔ Coreconf
│   ├── fetch.h              # ⭐ API FETCH (NUEVO)
│   └── ...
├── src/
│   ├── coreconfTypes.c      # Implementación de tipos
│   ├── serialization.c      # Implementación serialización
│   ├── fetch.c              # ⭐ Implementación FETCH (NUEVO)
│   └── ...
├── coreconf_zcbor_generated/
│   ├── zcbor_encode.c       # Librería CBOR encoding
│   ├── zcbor_decode.c       # Librería CBOR decoding
│   └── ...
├── Makefile                 # Compilación
└── ccoreconf.a              # 📦 LIBRERÍA FINAL (incluye FETCH)
```

### ¿Qué hace cada componente?

1. **coreconfTypes**: Define las estructuras de datos (hashmaps con SIDs, arrays, valores)
2. **serialization**: Convierte entre formato binario CBOR y estructuras C
3. **fetch**: Crea y procesa peticiones FETCH según RFC 9254
4. **zcbor**: Librería de bajo nivel para codificar/decodificar CBOR

---

## 3. ¿Cómo se integra en la Librería?

### Paso 1: Añadir `fetch.h` al directorio `include/`

Este archivo declara la API pública que usarán los programadores:

```c
// include/fetch.h
#ifndef FETCH_H
#define FETCH_H

// Tipos de instance-identifiers
typedef enum {
    IID_SIMPLE,          // SID simple: 20
    IID_WITH_STR_KEY,    // Con clave string: [30, "temp1"]
    IID_WITH_INT_KEY     // Con índice: [30, 2]
} InstanceIdentifierType;

// Estructura para identificadores
typedef struct {
    InstanceIdentifierType type;
    uint64_t sid;
    union {
        char *str_key;
        int64_t int_key;
    } key;
} InstanceIdentifier;

// Funciones públicas
size_t create_fetch_request(...);
size_t create_fetch_response(...);
// ... más funciones
```

**¿Por qué en include/?** Porque cualquier aplicación que use la librería necesita incluir este header para usar FETCH.

### Paso 2: Implementar `fetch.c` en `src/`

Este archivo contiene la lógica real:

```c
// src/fetch.c
#include "fetch.h"
#include "serialization.h"

size_t create_fetch_request(uint8_t *buffer, ...) {
    // Código que genera la petición CBOR
    zcbor_uint64_put(state, sids[i]);  // Escribe SID en CBOR
    return offset;  // Retorna bytes escritos
}
```

**¿Por qué en src/?** Es el código de implementación, los usuarios no lo ven directamente.

### Paso 3: El Makefile compila automáticamente

```makefile
# Makefile
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
# ⬆️ Esto encuentra automáticamente src/fetch.c

OBJ_FILES = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC_FILES))
# ⬆️ Convierte fetch.c → obj/fetch.o

$(LIB_NAME).a: $(OBJ_FILES) $(ZCBOR_OBJ_FILES)
    ar rcs $@ $^
# ⬆️ Incluye fetch.o en ccoreconf.a
```

**Resultado**: `ccoreconf.a` ahora contiene todo el código FETCH.

### Paso 4: Dependencias

```
fetch.c depende de:
    ├── fetch.h (su propio header)
    ├── serialization.h (para coreconfToCBOR)
    ├── coreconfManipulation.h (para getCoreconfHashMap)
    └── zcbor_encode.h / zcbor_decode.h (para CBOR)
```

Todas estas dependencias ya existían, así que FETCH se integra limpiamente.

---

## 4. ¿Cómo lo usan los Contenedores Docker?

### Arquitectura Docker

```
┌─────────────────────────────────────────────────┐
│  Dockerfile (iot_containers/Dockerfile)         │
│                                                  │
│  1. Copia código fuente desde host              │
│  2. Compila ccoreconf.a                         │
│  3. Compila aplicaciones IoT enlazando con .a   │
│  4. Crea imagen con binarios listos             │
└─────────────────────────────────────────────────┘
           ↓
┌─────────────────────────────────────────────────┐
│  Imagen Docker: iot-coreconf                    │
│  Contiene:                                       │
│    - iot_server (binario)                       │
│    - iot_client (binario)                       │
│    - Ambos INCLUYEN ccoreconf.a con FETCH       │
└─────────────────────────────────────────────────┘
           ↓
┌─────────────────────────────────────────────────┐
│  docker-compose.yml                              │
│  Crea 5 contenedores de iot-coreconf:          │
│    - iot_gateway (ejecuta iot_server)           │
│    - iot_sensor_temp (ejecuta iot_client)       │
│    - iot_sensor_humidity (ejecuta iot_client)   │
│    - iot_actuator (ejecuta iot_client)          │
│    - iot_edge (ejecuta iot_client)              │
└─────────────────────────────────────────────────┘
```

### Dockerfile - Paso a Paso

```dockerfile
# 1. Copiar código fuente desde el host
COPY include/ ./include/
COPY src/ ./src/                           # ⬅️ Incluye src/fetch.c
COPY coreconf_zcbor_generated/ ./coreconf_zcbor_generated/
COPY Makefile ./

# 2. Compilar la librería
RUN make clean && make
# ⬆️ Genera ccoreconf.a con fetch.o dentro

# 3. Copiar aplicaciones IoT
COPY iot_containers/iot_apps/ ./iot_apps/

# 4. Compilar aplicaciones enlazando con la librería
RUN cd iot_apps && \
    gcc iot_server.c ../ccoreconf.a -o iot_server && \
    gcc iot_client.c ../ccoreconf.a -o iot_client
#                    ^^^^^^^^^^^^^ ⬅️ Aquí se incluye FETCH
```

### ¿Cómo usan las aplicaciones la librería?

**iot_client.c**:
```c
// 1. Incluir el header
#include "../../include/fetch.h"

int main() {
    // 2. Usar las funciones FETCH
    InstanceIdentifier iids[] = {
        {IID_SIMPLE, 20, {.str_key = NULL}},
        {IID_WITH_STR_KEY, 30, {.str_key = "temp1"}}
    };
    
    uint8_t buffer[256];
    size_t len = create_fetch_request_with_iids(buffer, 256, iids, 2);
    //           ^^^^^^^^^^^^^^^^^^^^^^^^^^^ Función de la librería
    
    send(socket, buffer, len, 0);  // Enviar al servidor
}
```

**iot_server.c**:
```c
#include "../../include/fetch.h"

void handle_request(uint8_t *cbor_data, size_t size) {
    // 1. Parsear la petición FETCH
    InstanceIdentifier *iids = NULL;
    size_t count = 0;
    parse_fetch_request_iids(cbor_data, size, &iids, &count);
    //^^^^^^^^^^^^^^^^^^^ Función de la librería
    
    // 2. Crear respuesta
    uint8_t response[512];
    size_t resp_len = create_fetch_response_iids(response, 512, 
                                                  device_data, iids, count);
    //                ^^^^^^^^^^^^^^^^^^^^^^^^^ Función de la librería
    
    send(socket, response, resp_len, 0);
    free_instance_identifiers(iids, count);
}
```

### Proceso de Compilación

```bash
# En el Dockerfile:
gcc -I../include -I../coreconf_zcbor_generated \
    iot_client.c ../ccoreconf.a -o iot_client

# Esto hace:
1. Encuentra #include "../../include/fetch.h" → Lee declaraciones
2. Compila iot_client.c → Genera iot_client.o
3. Enlaza con ccoreconf.a → Resuelve llamadas a create_fetch_request_with_iids()
4. Genera ejecutable iot_client con TODO incluido
```

**Resultado**: El binario `iot_client` es **autónomo** y contiene todo el código FETCH sin necesitar librerías externas.

---

## 5. Análisis Detallado de fetch.h

Vamos línea por línea explicando qué hace cada parte.

### 5.1 Guards de Header

```c
#ifndef FETCH_H
#define FETCH_H
```

**¿Qué es esto?** Guards de inclusión múltiple.

**¿Por qué?** Si varios archivos incluyen `fetch.h`, el compilador solo lo procesa una vez, evitando errores de "redefinición".

**Analogía**: Es como poner un candado en la puerta. Si ya está cerrada (definido), no intentas cerrarla de nuevo.

### 5.2 Includes Necesarios

```c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "coreconfTypes.h"
```

**¿Qué son?**
- `stdint.h`: Define tipos como `uint64_t`, `int64_t` (enteros de tamaño fijo)
- `stddef.h`: Define `size_t` (tipo para tamaños de memoria)
- `stdbool.h`: Define `bool`, `true`, `false`
- `coreconfTypes.h`: Nuestros tipos propios (CoreconfValueT, etc.)

**¿Por qué necesitamos estos?** Porque nuestras funciones usan estos tipos.

### 5.3 Enumeración de Tipos de Identificadores

```c
typedef enum {
    IID_SIMPLE,          // 0
    IID_WITH_STR_KEY,    // 1
    IID_WITH_INT_KEY     // 2
} InstanceIdentifierType;
```

**¿Qué es un enum?** Una lista de constantes con nombres.

**¿Para qué?** Para saber qué tipo de identificador tenemos:

```c
// Tipos de instance-identifiers según RFC 9254:
IID_SIMPLE:        20              // Solo un SID
IID_WITH_STR_KEY:  [30, "eth0"]   // SID + clave de búsqueda
IID_WITH_INT_KEY:  [30, 2]        // SID + índice de array
```

**¿Por qué enum y no #define?**
```c
// Mal: #define
#define IID_SIMPLE 0
#define IID_WITH_STR_KEY 1
// ❌ No hay type checking, puedes poner cualquier número

// Bien: enum
InstanceIdentifierType type = IID_SIMPLE;  // ✅ El compilador verifica el tipo
```

### 5.4 Estructura InstanceIdentifier

```c
typedef struct {
    InstanceIdentifierType type;  // Tipo de identificador
    uint64_t sid;                 // SID (número identificador)
    union {                       // ⬇️ Solo uno de estos se usa
        char *str_key;            //   Clave string
        int64_t int_key;          //   Índice numérico
    } key;
} InstanceIdentifier;
```

**¿Qué es una union?** Un espacio de memoria que puede contener SOLO UNO de los elementos.

**¿Por qué union y no struct normal?**

```c
// Opción 1: struct (desperdicia memoria)
struct {
    char *str_key;     // 8 bytes
    int64_t int_key;   // 8 bytes
};
// Total: 16 bytes, pero solo usas uno

// Opción 2: union (eficiente)
union {
    char *str_key;     // 8 bytes
    int64_t int_key;   // 8 bytes
};
// Total: 8 bytes (comparten el espacio)
```

**Ejemplo de uso**:

```c
InstanceIdentifier iid1 = {
    .type = IID_SIMPLE,
    .sid = 20,
    .key = {.str_key = NULL}  // No se usa
};

InstanceIdentifier iid2 = {
    .type = IID_WITH_STR_KEY,
    .sid = 30,
    .key = {.str_key = "temp1"}  // Se usa str_key
};

InstanceIdentifier iid3 = {
    .type = IID_WITH_INT_KEY,
    .sid = 30,
    .key = {.int_key = 2}  // Se usa int_key
};
```

### 5.5 Función: create_fetch_request()

```c
/**
 * @brief Create a FETCH request with simple SIDs
 * 
 * @param buffer Output buffer for the CBOR sequence
 * @param buffer_size Size of the output buffer
 * @param sids Array of SIDs to fetch
 * @param sid_count Number of SIDs in the array
 * @return Number of bytes written to buffer, or 0 on error
 */
size_t create_fetch_request(uint8_t *buffer, size_t buffer_size,
                             const uint64_t *sids, size_t sid_count);
```

**¿Qué hace?** Crea una petición FETCH con SIDs simples.

**Parámetros explicados**:
- `uint8_t *buffer`: Puntero al array donde escribir los bytes CBOR
- `size_t buffer_size`: Tamaño del buffer (para no escribir fuera de límites)
- `const uint64_t *sids`: Array de SIDs a pedir (const = no se modifica)
- `size_t sid_count`: Cuántos SIDs hay en el array

**Valor de retorno**: Número de bytes escritos (0 = error)

**Ejemplo de uso**:

```c
uint64_t sids[] = {20, 21, 30};  // Quiero estos SIDs
uint8_t buffer[256];

size_t len = create_fetch_request(buffer, sizeof(buffer), sids, 3);

if (len > 0) {
    // buffer contiene: 14 15 18 1e (CBOR para 20, 21, 30)
    send(socket, buffer, len, 0);
}
```

### 5.6 Función: create_fetch_request_with_iids()

```c
size_t create_fetch_request_with_iids(uint8_t *buffer, size_t buffer_size,
                                       const InstanceIdentifier *iids, 
                                       size_t iid_count);
```

**Diferencia con la anterior**: Esta acepta instance-identifiers completos (con keys).

**Ejemplo**:

```c
InstanceIdentifier iids[] = {
    {IID_SIMPLE, 20, {.str_key = NULL}},
    {IID_WITH_STR_KEY, 30, {.str_key = "eth0"}}
};

uint8_t buffer[256];
size_t len = create_fetch_request_with_iids(buffer, 256, iids, 2);

// buffer contiene:
// 14                    // SID 20
// 9f 18 1e 64 65746830 ff  // [30, "eth0"]
```

### 5.7 Función: fetch_value_by_iid()

```c
CoreconfValueT* fetch_value_by_iid(CoreconfValueT *data_source, 
                                    const InstanceIdentifier *iid);
```

**¿Qué hace?** Busca un valor usando un instance-identifier.

**¿Cómo funciona?**

1. Si `IID_SIMPLE`: busca directamente el SID en el hashmap
2. Si `IID_WITH_STR_KEY`: busca en un array el elemento que contiene esa string
3. Si `IID_WITH_INT_KEY`: accede al array por índice

**Ejemplo**:

```c
// Datos:
// {
//   30: [
//     {name: "eth0", ip: "192.168.1.1"},
//     {name: "wlan0", ip: "10.0.0.1"}
//   ]
// }

InstanceIdentifier iid = {IID_WITH_STR_KEY, 30, {.str_key = "eth0"}};
CoreconfValueT *result = fetch_value_by_iid(data, &iid);

// result apunta a: {name: "eth0", ip: "192.168.1.1"}
```

### 5.8 Funciones de Respuesta

```c
size_t create_fetch_response(uint8_t *buffer, size_t buffer_size,
                              CoreconfValueT *data_source,
                              const uint64_t *sids, size_t sid_count);

size_t create_fetch_response_iids(uint8_t *buffer, size_t buffer_size,
                                   CoreconfValueT *data_source,
                                   const InstanceIdentifier *iids, 
                                   size_t iid_count);
```

**¿Qué hacen?** Crean la respuesta del servidor en formato RFC 9254.

**Formato de salida**:
```
{SID1: valor1}, {SID2: valor2}, {SID3: valor3}
```

**Ejemplo**:

```c
// Cliente pidió SIDs 20 y 30
uint64_t requested[] = {20, 30};

// Servidor tiene datos:
// {20: 25.5, 30: "temperature", 40: true}

uint8_t response[512];
size_t len = create_fetch_response(response, 512, server_data, requested, 2);

// response contiene CBOR:
// bf 14 fb 40 39 80 00 00 00 00 00 ff  // {20: 25.5}
// bf 18 1e 6b 74656d7065726174757265 ff  // {30: "temperature"}
```

### 5.9 Parsing de Peticiones

```c
bool parse_fetch_request_iids(const uint8_t *cbor_data, size_t cbor_size,
                               InstanceIdentifier **out_iids, 
                               size_t *out_count);
```

**¿Qué hace?** Convierte bytes CBOR en un array de InstanceIdentifier.

**Parámetros especiales**:
- `**out_iids`: Puntero a puntero (porque la función crea el array con malloc)
- `*out_count`: Puntero simple (para devolver el tamaño)

**Ejemplo**:

```c
// Recibimos CBOR: 14 15 9f 0a 64 65746830 ff
uint8_t received[] = {0x14, 0x15, 0x9f, 0x0a, 0x64, 
                      0x65, 0x74, 0x68, 0x30, 0xff};

InstanceIdentifier *iids = NULL;
size_t count = 0;

if (parse_fetch_request_iids(received, sizeof(received), &iids, &count)) {
    // iids[0] = {IID_SIMPLE, 20, ...}
    // iids[1] = {IID_SIMPLE, 21, ...}
    // iids[2] = {IID_WITH_STR_KEY, 10, "eth0"}
    // count = 3
    
    // ⚠️ IMPORTANTE: Liberar memoria
    free_instance_identifiers(iids, count);
}
```

### 5.10 Gestión de Memoria

```c
void free_instance_identifiers(InstanceIdentifier *iids, size_t count);
```

**¿Por qué existe?** Porque los strings en `IID_WITH_STR_KEY` se crean con malloc.

**¿Qué hace?**

```c
// Dentro de la función:
for (size_t i = 0; i < count; i++) {
    if (iids[i].type == IID_WITH_STR_KEY && iids[i].key.str_key) {
        free(iids[i].key.str_key);  // Liberar string
    }
}
free(iids);  // Liberar array principal
```

**⚠️ Regla de oro**: Si una función devuelve un puntero con `**out_`, TÚ debes hacer free.

---

## 6. Análisis Detallado de fetch.c

Ahora vamos a la implementación. Este código es más complejo.

### 6.1 Includes

```c
#include "fetch.h"
#include "serialization.h"
#include "coreconfManipulation.h"
#include "../coreconf_zcbor_generated/zcbor_encode.h"
#include "../coreconf_zcbor_generated/zcbor_decode.h"
#include <stdlib.h>   // malloc, free
#include <stdint.h>   // uint64_t, etc.
#include <string.h>   // strcmp, memcpy
```

**¿Por qué tantos includes?**
- `fetch.h`: Nuestras propias declaraciones
- `serialization.h`: Para `coreconfToCBOR` (convertir valores a CBOR)
- `coreconfManipulation.h`: Para `getCoreconfHashMap` (buscar en hashmaps)
- `zcbor_*.h`: Funciones de bajo nivel para CBOR
- Standard library: Utilidades básicas de C

### 6.2 Implementación: create_fetch_request()

```c
size_t create_fetch_request(uint8_t *buffer, size_t buffer_size,
                             const uint64_t *sids, size_t sid_count) {
```

**Paso 1: Validación de entrada**

```c
    if (!buffer || !sids || sid_count == 0) {
        return 0;  // Error: parámetros inválidos
    }
```

**¿Por qué?** Evitar crashes. Si buffer es NULL, escribir en él causa segfault.

**Paso 2: Variables locales**

```c
    size_t offset = 0;        // Posición actual en el buffer
    zcbor_state_t state[5];   // Estado de zcbor (pila de estados)
```

**¿Qué es `zcbor_state_t`?** Una estructura que zcbor usa para recordar dónde está escribiendo.

**¿Por qué [5]?** Máxima profundidad de anidamiento CBOR que soportamos.

**Paso 3: Loop por cada SID**

```c
    for (size_t i = 0; i < sid_count; i++) {
```

CBOR sequence = concatenar múltiples elementos. No hay array envolvente.

**Paso 4: Inicializar estado de encoding**

```c
        zcbor_new_encode_state(state, 5, 
                                buffer + offset,           // Dónde escribir
                                buffer_size - offset,      // Espacio restante
                                0);                        // Flags
```

**Explicación**:
- `buffer + offset`: Aritmética de punteros. Si offset=10, empieza en buffer[10]
- `buffer_size - offset`: Espacio disponible (evita overflow)

**Paso 5: Escribir el SID**

```c
        if (!zcbor_uint64_put(state, sids[i])) {
            return 0;  // Error
        }
```

**¿Qué hace `zcbor_uint64_put`?**
```
SID 20 → CBOR: 0x14 (1 byte)
SID 256 → CBOR: 0x19 0x01 0x00 (3 bytes)
```

Elige la codificación más compacta automáticamente.

**Paso 6: Actualizar offset**

```c
        offset += (state[0].payload - (buffer + offset));
```

**¿Por qué esta fórmula?**
- `state[0].payload`: Puntero donde zcbor dejó el cursor
- `buffer + offset`: Puntero donde empezó
- Diferencia = bytes escritos

**Visualización**:
```
buffer:  [ ][ ][ ][X][X][ ][ ]
          ^offset  ^state.payload
               ⊢───⊣
          2 bytes escritos
offset += 2 → offset = 2
```

**Paso 7: Retornar total**

```c
    return offset;
}
```

### 6.3 Implementación: create_fetch_request_with_iids()

Similar a la anterior, pero más compleja:

```c
        switch (iids[i].type) {
            case IID_SIMPLE:
                // Escribir solo el SID
                if (!zcbor_uint64_put(state, iids[i].sid)) {
                    return 0;
                }
                break;
```

**Caso simple**: Igual que antes.

```c
            case IID_WITH_STR_KEY:
                // [uint, tstr]: encode as array
                if (!zcbor_list_start_encode(state, 2)) return 0;
                if (!zcbor_uint64_put(state, iids[i].sid)) return 0;
                if (!zcbor_tstr_put_term(state, iids[i].key.str_key, SIZE_MAX)) return 0;
                if (!zcbor_list_end_encode(state, 2)) return 0;
                break;
```

**¿Qué hace esto?**

1. `zcbor_list_start_encode(state, 2)`: Empieza array de 2 elementos
   - CBOR: `0x82` (array de 2)
2. `zcbor_uint64_put`: Escribe el SID
   - CBOR: `0x1e` (si SID=30)
3. `zcbor_tstr_put_term`: Escribe string terminado en null
   - `"eth0"` → CBOR: `0x64 0x65 0x74 0x68 0x30`
4. `zcbor_list_end_encode`: Cierra el array

**Resultado CBOR**: `82 1e 64 65746830` = `[30, "eth0"]`

**Caso con integer key**: Similar pero con `zcbor_int64_put`.

### 6.4 Implementación: fetch_value_by_iid()

Esta es la función más interesante.

**Paso 1: Validación**

```c
CoreconfValueT* fetch_value_by_iid(CoreconfValueT *data_source, 
                                    const InstanceIdentifier *iid) {
    if (!data_source || !iid || data_source->type != CORECONF_HASHMAP) {
        return NULL;
    }
```

**¿Por qué CORECONF_HASHMAP?** Porque los datos del dispositivo se guardan como hashmap: `{SID: value}`.

**Paso 2: Buscar el SID base**

```c
    CoreconfHashMapT *map = data_source->data.map_value;
    CoreconfValueT *value = getCoreconfHashMap(map, iid->sid);
    
    if (!value) {
        return NULL;  // SID no existe
    }
```

**getCoreconfHashMap**: Función del módulo coreconfManipulation que busca por SID.

**Paso 3: Caso simple**

```c
    if (iid->type == IID_SIMPLE) {
        return value;  // Devolver directamente
    }
```

**Paso 4: Búsqueda con string key**

```c
    if (iid->type == IID_WITH_STR_KEY) {
        if (value->type == CORECONF_ARRAY) {
            CoreconfArrayT *arr = value->data.array_value;
```

**Escenario**: El SID apunta a un array, necesitamos buscar dentro.

```c
            for (size_t i = 0; i < arr->size; i++) {
                CoreconfValueT *elem = &arr->elements[i];
                
                if (elem->type == CORECONF_HASHMAP) {
```

**¿Qué buscamos?** Hashmaps dentro del array que contengan la key.

```c
                    CoreconfHashMapT *elem_map = elem->data.map_value;
                    
                    // Iterate through hashmap looking for string match
                    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
                        CoreconfObjectT *obj = elem_map->table[t];
                        while (obj) {
```

**Estructura del hashmap**: Array de listas enlazadas (hash table).

```c
                            if (obj->value && obj->value->type == CORECONF_STRING) {
                                if (strcmp(obj->value->data.string_value, 
                                          iid->key.str_key) == 0) {
                                    return elem;  // ¡Encontrado!
                                }
                            }
                            obj = obj->next;  // Siguiente en la cadena
                        }
                    }
```

**Algoritmo de búsqueda**:
1. Para cada elemento del array
2. Si es un hashmap
3. Buscar en todos sus campos
4. Si algún campo es string y coincide → devolver ese elemento

**Ejemplo visual**:

```
Array en SID 30:
[
  0: {1: "eth0", 2: "192.168.1.1"},   ← Buscar aquí
  1: {1: "wlan0", 2: "10.0.0.1"}      ← Y aquí
]

Buscamos key="eth0"
→ Elemento 0 tiene campo con valor "eth0"
→ Retornar elemento 0 completo
```

**Paso 5: Búsqueda con integer key**

```c
    if (iid->type == IID_WITH_INT_KEY) {
        if (value->type == CORECONF_ARRAY) {
            CoreconfArrayT *arr = value->data.array_value;
            
            if (iid->key.int_key >= 0 && 
                (size_t)iid->key.int_key < arr->size) {
                return &arr->elements[iid->key.int_key];
            }
        }
        return NULL;
    }
```

**Más simple**: Acceso directo por índice.

```c
// [30, 2] → arr->elements[2]
```

### 6.5 Implementación: create_fetch_response_iids()

```c
size_t create_fetch_response_iids(uint8_t *buffer, size_t buffer_size,
                                   CoreconfValueT *data_source,
                                   const InstanceIdentifier *iids, 
                                   size_t iid_count) {
```

**Estructura de bucle**:

```c
    for (size_t i = 0; i < iid_count; i++) {
        zcbor_new_encode_state(state, 5, buffer + offset, 
                                buffer_size - offset, 0);
        
        // 1. Buscar valor
        CoreconfValueT *value = fetch_value_by_iid(data_source, &iids[i]);
        
        // 2. Crear mapa {SID: value}
        if (!zcbor_map_start_encode(state, 1)) return 0;
        
        if (!zcbor_uint64_put(state, iids[i].sid)) return 0;
        
        if (value) {
            if (!coreconfToCBOR(value, state)) return 0;
        } else {
            if (!zcbor_nil_put(state, NULL)) return 0;
        }
        
        if (!zcbor_map_end_encode(state, 1)) return 0;
        
        offset += (state[0].payload - (buffer + offset));
    }
```

**Formato de salida**:

```
Request:  20, [30, "eth0"]
Response: {20: 25.5}, {30: {name: "eth0", ip: "192.168.1.1"}}
          └──┬──┘     └────────────┬────────────────┘
         Mapa 1              Mapa 2
```

Cada mapa es un elemento de la CBOR sequence.

### 6.6 Implementación: parse_fetch_request_iids()

```c
bool parse_fetch_request_iids(const uint8_t *cbor_data, size_t cbor_size,
                                InstanceIdentifier **out_iids, 
                                size_t *out_count) {
```

**Estrategia de dos pasadas**:

**Primera pasada: Contar elementos**

```c
    size_t count = 0;
    size_t offset = 0;
    
    while (offset < cbor_size) {
        zcbor_new_decode_state(state, 5, cbor_data + offset, 
                                cbor_size - offset, 1, NULL, 0);
        
        uint64_t temp_sid;
        if (zcbor_uint64_decode(state, &temp_sid)) {
            count++;  // Es un SID simple
        } else {
            // Intentar decodificar como array
            if (zcbor_list_start_decode(state)) {
                count++;
                // Saltar hasta el final del array...
            }
        }
        offset += ...;
    }
```

**¿Por qué dos pasadas?** Para saber cuánta memoria necesitamos.

```c
    // Allocate array
    InstanceIdentifier *iids = (InstanceIdentifier *)calloc(count, 
                                                    sizeof(InstanceIdentifier));
```

**Segunda pasada: Decodificar valores**

```c
    offset = 0;
    for (size_t i = 0; i < count; i++) {
        zcbor_new_decode_state(state, 5, cbor_data + offset, 
                                cbor_size - offset, 1, NULL, 0);
        
        uint64_t sid;
        if (zcbor_uint64_decode(state, &sid)) {
            // SID simple
            iids[i].type = IID_SIMPLE;
            iids[i].sid = sid;
            iids[i].key.str_key = NULL;
```

**Caso array**:

```c
        } else {
            if (!zcbor_list_start_decode(state)) return false;
            if (!zcbor_uint64_decode(state, &sid)) return false;
            
            iids[i].sid = sid;
            
            // Try string key
            struct zcbor_string zstr;
            if (zcbor_tstr_decode(state, &zstr)) {
                iids[i].type = IID_WITH_STR_KEY;
                iids[i].key.str_key = (char *)malloc(zstr.len + 1);
                memcpy(iids[i].key.str_key, zstr.value, zstr.len);
                iids[i].key.str_key[zstr.len] = '\0';  // Null terminator
```

**Detalle importante**: `zcbor_string` no está null-terminated, nosotros lo hacemos.

**¿Por qué malloc?** Porque el string debe sobrevivir después de que esta función termine.

### 6.7 Implementación: free_instance_identifiers()

```c
void free_instance_identifiers(InstanceIdentifier *iids, size_t count) {
    if (!iids) return;
    
    for (size_t i = 0; i < count; i++) {
        if (iids[i].type == IID_WITH_STR_KEY && iids[i].key.str_key) {
            free(iids[i].key.str_key);  // Liberar string
        }
    }
    free(iids);  // Liberar array
}
```

**Orden importante**:
1. Primero liberar los strings dentro
2. Luego liberar el array

Si haces al revés, pierdes acceso a los punteros → memory leak.

---

## 7. Flujo Completo de una Operación FETCH

Veamos cómo fluye una petición FETCH desde el cliente al servidor.

### 7.1 Cliente (iot_client.c)

```c
// 1. Preparar identificadores
InstanceIdentifier iids[] = {
    {IID_SIMPLE, 20, {.str_key = NULL}},           // Temperatura
    {IID_WITH_STR_KEY, 30, {.str_key = "eth0"}}    // Interfaz específica
};

// 2. Crear petición CBOR
uint8_t buffer[256];
size_t len = create_fetch_request_with_iids(buffer, 256, iids, 2);

// Buffer ahora contiene:
// [0x14, 0x82, 0x1e, 0x64, 0x65, 0x74, 0x68, 0x30]
//  ↑SID  ↑─────────array [30, "eth0"]─────────┘

// 3. Enviar por socket TCP
send(socket, buffer, len, 0);
```

### 7.2 Red

```
Cliente (172.20.0.11) ──TCP──> Servidor (172.20.0.10:5683)
        8 bytes de CBOR
```

### 7.3 Servidor (iot_server.c)

```c
// 1. Recibir datos
uint8_t buffer[4096];
ssize_t received = recv(client_sock, buffer, 4096, 0);

// 2. Detectar que es FETCH (no es hashmap, es sequence)
if (!is_fetch_request(buffer, received)) {
    // Es STORE, procesarlo de otra forma
}

// 3. Parsear petición
InstanceIdentifier *iids = NULL;
size_t count = 0;
parse_fetch_request_iids(buffer, received, &iids, &count);

// iids ahora contiene:
// [0] = {IID_SIMPLE, 20, ...}
// [1] = {IID_WITH_STR_KEY, 30, "eth0"}

// 4. Obtener datos del dispositivo
CoreconfValueT *device_data = &devices[0].data;
// device_data = {20: 25.5, 30: [{name:"eth0", ip:"192.168.1.1"}, ...]}

// 5. Crear respuesta
uint8_t response[4096];
size_t resp_len = create_fetch_response_iids(response, 4096, 
                                              device_data, iids, count);

// response contiene CBOR sequence:
// {20: 25.5}, {30: {name: "eth0", ip: "192.168.1.1"}}

// 6. Enviar respuesta
send(client_sock, response, resp_len, 0);

// 7. Liberar memoria
free_instance_identifiers(iids, count);
```

### 7.4 Cliente recibe respuesta

```c
// 1. Recibir
uint8_t response[4096];
ssize_t received = recv(socket, response, 4096, 0);

// 2. Decodificar CBOR sequence
size_t offset = 0;
for (size_t i = 0; i < count; i++) {
    zcbor_state_t state[5];
    zcbor_new_decode_state(state, 5, response + offset, 
                           received - offset, 1, NULL, 0);
    
    CoreconfValueT *value = cborToCoreconfValue(state, 0);
    
    // value es un mapa {SID: valor}
    // Extraer el valor...
    
    offset += (state[0].payload - (response + offset));
}
```

---

## 8. Casos de Uso Prácticos

### 8.1 Caso 1: Monitoreo de Temperatura

**Escenario**: Un sensor envía 50 campos, pero solo quieres temperatura.

```c
// SIN FETCH (malgasta recursos)
send_all_data();  // 500 bytes

// CON FETCH (eficiente)
uint64_t sids[] = {20};  // Solo temperatura
uint8_t buffer[32];
size_t len = create_fetch_request(buffer, 32, sids, 1);
send(socket, buffer, len, 0);  // 2 bytes

// Respuesta: {20: 25.5} → 10 bytes
// Ahorro: 98% de datos
```

### 8.2 Caso 2: Configuración de Red

**Escenario**: Router con múltiples interfaces, quieres solo "eth0".

```c
InstanceIdentifier iid = {
    .type = IID_WITH_STR_KEY,
    .sid = 1533,  // SID de interfaces
    .key = {.str_key = "eth0"}
};

uint8_t buffer[64];
size_t len = create_fetch_request_with_iids(buffer, 64, &iid, 1);

// Servidor busca en:
// {1533: [
//   {name: "eth0", status: "up", ip: "192.168.1.1"},
//   {name: "wlan0", status: "down", ip: null}
// ]}
// 
// Retorna solo el elemento que coincide con name="eth0"
```

### 8.3 Caso 3: Logs de Sistema

**Escenario**: Acceder al tercer log de la lista.

```c
InstanceIdentifier iid = {
    .type = IID_WITH_INT_KEY,
    .sid = 9000,  // SID de logs
    .key = {.int_key = 2}  // Índice 2 (tercero)
};

// Servidor devuelve logs[2] directamente
```

---

## Conclusiones para tu TFG

### Conceptos Clave Aprendidos

1. **Separación de capas**:
   - API pública (fetch.h) vs implementación (fetch.c)
   - Abstracción: el usuario no necesita saber cómo funciona CBOR

2. **Gestión de memoria en C**:
   - malloc/free
   - Responsabilidad del caller cuando usas `**out_`
   - Liberar en orden correcto

3. **Protocolos binarios**:
   - CBOR es más eficiente que JSON
   - CBOR sequences: concatenar sin array envolvente
   - Encoding adaptativo (1 byte vs 3 bytes según el valor)

4. **Diseño de APIs**:
   - Validación de entrada siempre
   - Retornar tamaños/errores
   - const para parámetros que no se modifican

5. **Compilación de librerías**:
   - Headers públicos en include/
   - Implementación en src/
   - Makefile compila todo en .a
   - Docker copia y enlaza

### Mejoras Posibles (para futuro)

1. **Optimización**: Cache de búsquedas frecuentes
2. **Seguridad**: Límites estrictos en recursión
3. **Logging**: Debug traces para troubleshooting
4. **Tests**: Unit tests automatizados

### Recursos para Profundizar

- RFC 9254: https://www.rfc-editor.org/rfc/rfc9254.html
- RFC 8949 (CBOR): https://www.rfc-editor.org/rfc/rfc8949.html
- zcbor documentation: https://github.com/NordicSemiconductor/zcbor

---

**¡Éxito con tu TFG!** 🎓

Si tienes dudas sobre alguna parte específica, no dudes en profundizar más.
