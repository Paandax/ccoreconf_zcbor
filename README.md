# ccoreconf_zcbor — Implementación de CORECONF (RFC 9254)

Este repositorio es parte de mi TFG. El objetivo es implementar el protocolo **CORECONF** (RFC 9254) para gestión de dispositivos IoT usando CoAP como transporte y CBOR como formato de serialización.

La idea es que en redes IoT no puedes usar HTTP+JSON como en internet normal porque los dispositivos tienen poca RAM, baterías limitadas y las redes son poco fiables. CORECONF resuelve esto usando CoAP (un HTTP ligero sobre UDP) y CBOR (un JSON binario mucho más compacto).

---

## ¿Qué he implementado?

Las 5 operaciones CORECONF del RFC más un POST de registro, todas verificadas con Wireshark:

| Operación | Método CoAP | CF entrada | CF salida | Código respuesta |
|-----------|------------|-----------|----------|-----------------|
| Registrar dispositivo | POST | 60 | — | 2.04 Changed |
| Leer datastore completo | GET | — | 142 | 2.05 Content |
| Leer SIDs específicos | FETCH | 141 | 142 | 2.05 Content |
| Modificar SIDs concretos | iPATCH | 141 | — | 2.04 Changed |
| Reemplazar datastore | PUT | 142 | — | 2.04 Changed |
| Borrar SIDs o datastore | DELETE | — | — | 2.02 Deleted |

Los Content-Format 141 y 142 son los definidos en el RFC para `application/yang-patch+cbor` y `application/yang-data+cbor` respectivamente. El servidor valida el CF de cada petición y devuelve 4.15 si es incorrecto.

### Diferencia entre FETCH y GET

Con GET recibes todo el datastore. Con FETCH mandas en el body un array CBOR con los SIDs que quieres y recibes solo esos. Si un dispositivo tiene 100 SIDs y solo necesitas 2, FETCH ahorra mucho ancho de banda.

### Diferencia entre iPATCH y PUT

iPATCH modifica solo los SIDs que le mandas, el resto no se toca. PUT reemplaza el datastore completo: lo que no aparezca en el body desaparece.

---

## Estructura del proyecto

```
ccoreconf_zcbor/
│
├── include/                  ← headers de la biblioteca
│   ├── coreconfTypes.h       ← tipos principales: CoreconfValueT, hashmap
│   ├── fetch.h / get.h
│   ├── ipatch.h / put.h
│   └── delete.h
│
├── src/                      ← implementación de la biblioteca
│   ├── coreconfTypes.c       ← hashmap SID→valor con MurmurHash
│   ├── serialization.c       ← conversión CoreconfValueT ↔ CBOR (usa zcbor)
│   ├── sid.c                 ← delta encoding de SIDs
│   ├── fetch.c / get.c
│   ├── ipatch.c / put.c
│   └── delete.c
│
├── coreconf_zcbor_generated/ ← código generado por zcbor a partir de coreconf.cddl
│
├── examples/                 ← tests de la biblioteca
│
├── iot_containers/           ← despliegue Docker
│   ├── Dockerfile
│   ├── docker-compose.yml    ← gateway + 2 sensores IoT
│   └── iot_apps/
│       ├── coreconf_server.c ← servidor CoAP unificado (todos los métodos sobre /c)
│       ├── coreconf_cli.c    ← cliente interactivo tipo REPL
│       └── [get|ipatch|put|delete]_[server|client].c
│
└── docs/                     ← documentación técnica detallada
```

### Estructura de datos interna

Los datos de cada dispositivo se guardan en un hashmap que mapea SIDs (enteros de 64 bits) a valores tipados:

```c
// Un valor puede ser int, float, string, bool, otro mapa...
typedef struct CoreconfValue {
    coreconf_type type;
    union {
        double   real_value;
        int64_t  i64;
        char    *string_value;
        struct CoreconfHashMap *map_value;
        // ...
    } data;
} CoreconfValueT;

// Hashmap con 100 buckets y encadenamiento (MurmurHash)
typedef struct CoreconfHashMap {
    CoreconfObjectT *table[HASHMAP_TABLE_SIZE];
    size_t size;
} CoreconfHashMapT;
```

Uso MurmurHash en vez de `key % 100` para que cuando tenga SIDs reales de la IANA (que pueden ser números como 59998, 60000...) no haya colisiones masivas en los mismos buckets.

---

## SIDs que uso (Provisionales)

Son SIDs didácticos que creé para el proyecto, no están asignados por la IANA:

| SID | Significado | Tipo |
|-----|-------------|------|
| 10 | nombre del sensor | string |
| 11 | timestamp Unix | uint64 |
| 20 | valor de medición | real |
| 21 | unidad | string |

En CBOR los mapas usan **delta encoding**: en vez de mandar el SID completo cada vez, se manda la diferencia respecto al SID anterior. `{10: ..., 11: ..., 20: ..., 21: ...}` se codifica con deltas `{10, +1, +9, +1}`, lo que ahorra bytes.

---

## Cómo compilar

### macOS

```bash
# Primero compilar libcoap desde source (solo una vez)
cd libcoap
./autogen.sh && ./configure --disable-dtls && make && sudo make install

# Compilar la biblioteca
cd ccoreconf_zcbor
make

# Compilar las apps IoT
cd iot_containers/iot_apps
make all
```

### Ubuntu

```bash
sudo apt install libcoap3-dev build-essential
make
cd iot_containers/iot_apps && make all
```

---

## Cómo probarlo

### Opción 1 — local (dos terminales)

```bash
# Terminal 1
cd iot_containers/iot_apps
./coreconf_server

# Terminal 2
./coreconf_cli temperature sensor-test-001
```

Dentro del prompt `sensor-test-001>` puedes escribir:

```
store temperature 24.5   → POST, registra el dispositivo
get                       → GET, devuelve todo el datastore
fetch 20                  → FETCH, solo el SID 20
ipatch 20 99.9            → iPATCH, modifica el SID 20
put 20 55.0 21 70         → PUT, reemplaza el datastore completo
delete 20                 → DELETE, borra solo el SID 20
delete                    → DELETE, borra todo el datastore
quit
```

### Opción 2 — Docker (recomendado para ver dos dispositivos a la vez) (Simulación en RED)

```bash
# Construir imagen (desde el root del proyecto)
docker build -t iot-coreconf -f iot_containers/Dockerfile .

# Levantar gateway + device_1 (temperatura) + device_2 (humefind_or_create_devicedad)
cd iot_containers
docker compose up -d

# Conectarse a cada dispositivo en terminales separadas
docker attach coreconf_device_1   # sensor-temp-001
docker attach coreconf_device_2   # sensor-hum-001
```

Salir sin matar el contenedor: **Ctrl-P Ctrl-Q**

### Ver tráfico en Wireshark

Desde Ubuntu puedo capturar directamente en la interfaz bridge de Docker:
```bash
sudo tcpdump -i br+ -w captura.pcap port 5683
```

Desde macOS, pipe de tcpdump al Wireshark:
```bash
docker exec coreconf_gateway tcpdump -i any -w - port 5683 2>/dev/null \
  | /Applications/Wireshark.app/Contents/MacOS/Wireshark -k -i -
```

Filtro en Wireshark: `coap`

---

## Cumplimiento RFC 9254

Lo que sí cumple:

- Content-Format 141/142 en todas las operaciones
- Validación de CF entrante con 4.15 si es incorrecto
- Accept: 142 en FETCH
- Códigos de respuesta correctos (2.01/2.02/2.04/2.05/4.04/4.15)
- Block-wise transfer en GET para respuestas grandes (RFC 7959)
- Delta encoding de SIDs en mapas CBOR

Lo que queda para trabajo futuro:

- **DTLS** — seguridad en capa de transporte, obligatorio en producción
- **CoAP Observe** — notificaciones push cuando cambia un SID
- **Datastores NMDA** — running / intended / candidate / operational (RFC 8342)
- **YANG Library** — endpoint `/c/ietf-yang-library` con catálogo de módulos
- **Locking** — mutex para acceso concurrente al datastore

---

## Documentación

Tengo guías más detalladas en `docs/`:

- [01 — RFC 9254 en detalle](docs/01_RFC9254_CORECONF.md) — SIDs, delta encoding, las 6 operaciones con diagramas
- [02 — Arquitectura](docs/02_ARQUITECTURA.md) — cada módulo explicado, flujo completo de un iPATCH
- [03 — CoAP y libcoap](docs/03_COAP_LIBCOAP.md) — estructura de frames CoAP, API de libcoap, CBOR
- [04 — Docker](docs/04_DOCKER_IOT.md) — Dockerfile línea a línea, red bridge, comandos

---

## Referencias

- [RFC 9254](https://www.rfc-editor.org/rfc/rfc9254) — CORECONF
- [RFC 7252](https://www.rfc-editor.org/rfc/rfc7252) — CoAP
- [RFC 8949](https://www.rfc-editor.org/rfc/rfc8949) — CBOR
- [RFC 9132](https://www.rfc-editor.org/rfc/rfc9132) — SIDs
- [RFC 7950](https://www.rfc-editor.org/rfc/rfc7950) — YANG 1.1
- [libcoap](https://libcoap.net)
- [zcbor](https://github.com/zephyrproject-rtos/zcbor)
