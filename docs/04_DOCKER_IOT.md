# Docker IoT — cómo funciona el setup

## Arquitectura de contenedores

```
┌─────────────────────────────────────────────────────────┐
│                  Red iot_network (bridge)                │
│                   172.20.0.0/16                          │
│                                                          │
│  ┌─────────────────┐      ┌─────────────────────────┐   │
│  │ coreconf_gateway │      │   coreconf_device_1     │   │
│  │  172.20.0.10    │<─CoAP│   172.20.0.11           │   │
│  │  PORT: 5683/udp │      │   ./coreconf_cli        │   │
│  │  ./coreconf_srv  │      │   temperature           │   │
│  └─────────────────┘      │   sensor-temp-001       │   │
│           ↑               └─────────────────────────┘   │
│           │                                              │
│           │               ┌─────────────────────────┐   │
│           └──────CoAP─────│   coreconf_device_2     │   │
│                           │   172.20.0.12           │   │
│                           │   ./coreconf_cli        │   │
│                           │   humidity              │   │
│                           │   sensor-hum-001        │   │
│                           └─────────────────────────┘   │
│                                                          │
└──────────────────────────┬──────────────────────────────┘
                           │
                    HOST MAC (5683:5683)
                    (puedes hacer coap-client desde Mac)
```

---

## El Dockerfile explicado línea a línea

```dockerfile
FROM ubuntu:22.04
```
Imagen base. Ubuntu 22.04 LTS tiene un repo apt con `libcoap3-dev` disponible.

```dockerfile
ENV DEBIAN_FRONTEND=noninteractive
```
Evita que `apt` pregunte por zona horaria u otras opciones interactivas durante el build.

```dockerfile
RUN apt-get update && apt-get install -y \
    build-essential \   ← gcc, make, binutils
    gcc \
    make \
    pkg-config \        ← para detectar flags de libcoap
    netcat-traditional \ ← nc, útil para debug
    iproute2 \          ← ip, ss
    iputils-ping \      ← ping
    curl \
    libcoap3-dev \      ← libcoap + headers
    tcpdump             ← captura de paquetes (para Wireshark pipe)
```

```dockerfile
WORKDIR /iot_device
```
Todos los COPY y RUN siguientes trabajan desde este directorio.

```dockerfile
COPY include/ ./include/
COPY src/ ./src/
COPY coreconf_zcbor_generated/ ./coreconf_zcbor_generated/
COPY Makefile ./
COPY iot_containers/iot_apps/ ./iot_apps/
```
Copia el código fuente del proyecto. El contexto de build es el **root del proyecto** (`ccoreconf_zcbor/`), por eso los paths son relativos a él.

```dockerfile
RUN make clean && make
```
Compila la biblioteca (`ccoreconf.a`). El Makefile detecta automáticamente el sistema y usa los flags correctos.

```dockerfile
RUN cd iot_apps && \
    sed -i 's|"../../include/|"|g' *.c && \
    sed -i 's|"../../coreconf_zcbor_generated/|"|g' *.c
```
Los `.c` de `iot_apps/` tienen `#include "../../include/coreconfTypes.h"` porque en el host están dos niveles abajo del root. Dentro del Docker la estructura es diferente (`/iot_device/include/`), así que se parchean los includes con `sed` antes de compilar.

```dockerfile
RUN COAP_LIBS=$(pkg-config --libs libcoap-3-notls 2>/dev/null || echo "-lcoap-3-notls") && \
    COAP_INC=$(pkg-config --cflags libcoap-3-notls 2>/dev/null || echo "") && \
    cd iot_apps && \
    gcc -Wall -Wextra -pedantic -std=c11 -Werror \
        -I../include -I../coreconf_zcbor_generated $COAP_INC \
        coreconf_server.c ../ccoreconf.a $COAP_LIBS -pthread -o coreconf_server && \
    gcc ... coreconf_cli.c ... -o coreconf_cli
```

- `pkg-config --libs libcoap-3-notls` → devuelve `-lcoap-3-notls -lm` (o lo que necesite)
- `|| echo "-lcoap-3-notls"` → fallback si pkg-config no está disponible
- `../ccoreconf.a` → enlaza nuestra biblioteca estática
- `-pthread` → libcoap usa threads internamente

**¿Por qué `-notls`?** Ubuntu 22.04 empaqueta múltiples variantes de libcoap:
- `libcoap-3-notls` — sin TLS (UDP puro)
- `libcoap-3-openssl` — con OpenSSL (DTLS)
- `libcoap-3-gnutls` — con GnuTLS (DTLS)

Usamos `-notls` para simplicidad. En producción usaríamos `-openssl` o `-gnutls` para DTLS.

---

## docker-compose.yml explicado

```yaml
version: '3.8'  # obsoleto pero funcional
```

### El gateway

```yaml
coreconf_gateway:
  image: iot-coreconf          # imagen construida con docker build
  container_name: coreconf_gateway
  hostname: gateway
  ports:
    - "5683:5683/udp"          # expone CoAP al host Mac
  networks:
    iot_network:
      ipv4_address: 172.20.0.10  # IP fija
  command: ./iot_apps/coreconf_server  # qué ejecutar
  volumes:
    - ./logs:/iot_device/logs  # montar directorio de logs
  restart: unless-stopped      # reiniciar si cae, excepto si se para a mano
```

`ports: "5683:5683/udp"` → el puerto 5683 UDP del contenedor se mapea al 5683 del Mac. Esto permite hacer desde el Mac:
```bash
coap-client coap://localhost:5683/c
```

### Los dispositivos

```yaml
coreconf_device_1:
  image: iot-coreconf
  stdin_open: true     # equivale a docker run -i (stdin abierto)
  tty: true            # equivale a docker run -t (pseudo-TTY)
  environment:
    - GATEWAY_HOST=172.20.0.10   # IP del gateway en la red de Docker
    - GATEWAY_PORT=5683
  command: ./iot_apps/coreconf_cli temperature sensor-temp-001
  depends_on:
    - coreconf_gateway  # espera a que el gateway esté running
```

`stdin_open + tty`: sin esto, el contenedor arrancaría el CLI pero no podría leer del teclado → terminaría inmediatamente porque stdin sería `/dev/null`.

`GATEWAY_HOST=172.20.0.10`: el CLI lee esta variable para saber a dónde conectarse. Dentro de la red bridge de Docker, los contenedores se ven entre sí por IP (también por hostname si están en la misma red).

---

## Comandos Docker esenciales para este proyecto

### Construir la imagen

```bash
cd ccoreconf_zcbor
docker build -t iot-coreconf -f iot_containers/Dockerfile .
#            ↑ nombre       ↑ dockerfile           ↑ contexto de build (root del proyecto)
```

**Por qué el contexto es el root**: el Dockerfile hace `COPY include/ ./include/` y `include/` está en el root del proyecto, no en `iot_containers/`. Si pusieras el contexto en `iot_containers/`, el COPY fallaría.

### Levantar todo

```bash
cd iot_containers
docker compose up -d
# -d = detached (background)
```

### Ver estado

```bash
docker compose ps
docker logs coreconf_gateway        # logs del servidor
docker logs -f coreconf_device_1    # logs en tiempo real
```

### Conectarse al CLI interactivo

```bash
docker attach coreconf_device_1
# Ahora writes van a stdin del coreconf_cli dentro del contenedor
```

**Salir sin matar el contenedor**: `Ctrl-P` seguido de `Ctrl-Q`
(Esto es la secuencia de detach heredada de `docker attach`. `Ctrl-C` pararía el CLI y el contenedor quedaría stopped.)

### Ejecutar un comando en un contenedor corriendo

```bash
# Ver procesos dentro del gateway
docker exec coreconf_gateway ps aux

# Abrir una shell en el gateway
docker exec -it coreconf_gateway bash

# Captura de tráfico
docker exec coreconf_gateway tcpdump -i any -n port 5683
```

### Parar y limpiar

```bash
docker compose down           # para y borra contenedores y red
docker compose down -v        # también borra volúmenes
docker rmi iot-coreconf       # borra la imagen
```

### Rebuild rápido (al cambiar código)

```bash
cd ccoreconf_zcbor
docker build -t iot-coreconf -f iot_containers/Dockerfile . && \
cd iot_containers && \
docker compose down && \
docker compose up -d
```

---

## Red bridge de Docker

Docker crea una red virtual llamada `iot_containers_iot_network` (prefijo = nombre del directorio donde está el compose).

```
Host Mac
  eth0 (red real)
  docker0 (bridge default de Docker)
  br-xxxx (bridge de iot_network)
     ├── veth→ coreconf_gateway  (172.20.0.10)
     ├── veth→ coreconf_device_1 (172.20.0.11)
     └── veth→ coreconf_device_2 (172.20.0.12)
```

Cada contenedor tiene una interfaz virtual `eth0` conectada al bridge. El bridge hace de switch L2 entre ellos. El tráfico **no pasa por las interfaces físicas del Mac** — por eso Wireshark en `en0` no lo ve.

Para capturar desde Mac: `docker exec ... tcpdump | wireshark` o inspeccionar la interfaz `br-xxxx` si Docker Desktop lo expone.

---

## Por qué libcoap usa `-lcoap-3-notls` en Docker pero `-lcoap-3` en Mac

En macOS compilamos libcoap **desde source** con:
```bash
./configure --disable-dtls
make install
```
Esto instala `libcoap-3` sin sufijo de backend (porque el sufijo lo añade el empaquetador del SO, no upstream).

En Ubuntu 22.04, el mantenedor del paquete decidió usar sufijos para permitir instalar múltiples variantes simultáneamente:
- `/usr/lib/libcoap-3-notls.so`
- `/usr/lib/libcoap-3-openssl.so`

`libcoap3-dev` en Ubuntu instala los headers comunes y el `.pc` de `libcoap-3-notls` como default.

---

## Captura de tráfico Docker → Wireshark (macOS)

```bash
docker exec coreconf_gateway tcpdump -i any -w - port 5683 2>/dev/null \
  | /Applications/Wireshark.app/Contents/MacOS/Wireshark -k -i -
```

Pipeline explicado:
1. `docker exec coreconf_gateway` — ejecuta en el contenedor gateway (donde ocurre el tráfico)
2. `tcpdump -i any` — captura en TODAS las interfaces del contenedor (`lo` + `eth0`)
3. `-w -` — escribe formato pcap a stdout (en vez de a un archivo)
4. `2>/dev/null` — descarta los mensajes de log de tcpdump (para que no corrompan el pcap)
5. `|` — pipe: stdout de tcpdump → stdin de Wireshark en el Mac
6. `Wireshark -k -i -` — `-i -` = leer pcap de stdin; `-k` = empezar captura inmediatamente

Una vez abierto Wireshark, aplica el filtro `coap` para ver solo CoAP.

---

## Estructura multi-dispositivo del servidor

El gateway mantiene un array estático de dispositivos:

```c
#define MAX_DEVICES 32

typedef struct {
    char device_id[64];     // "sensor-temp-001"
    CoreconfValueT *data;   // datastore completo
    int exists;             // 1 si el slot está en uso
} Device;

Device devices[MAX_DEVICES];
```

Cuando llega una petición con `?id=sensor-temp-001`, el servidor busca linealmente en el array. Si no existe (POST nuevo), busca un slot libre.

Cada dispositivo tiene un `CoreconfValueT*` **independiente** — sus SIDs son privados. Dispositivo 1 puede tener SID 20 = 24.5 y dispositivo 2 puede tener SID 20 = 65.0 sin conflicto.

---

## Flujo de arranque del sistema

```
docker compose up -d
     │
     ├── Crea red bridge iot_network (172.20.0.0/16)
     │
     ├── Arranca coreconf_gateway
     │     └── ./coreconf_server
     │           ├── coap_new_context()
     │           ├── coap_new_endpoint() → bind :5683/UDP
     │           ├── coap_resource_init("c")
     │           ├── coap_register_handler() × 6
     │           └── while(1) coap_io_process()  ← esperando
     │
     ├── Arranca coreconf_device_1 (depends_on: gateway)
     │     └── ./coreconf_cli temperature sensor-temp-001
     │           ├── Lee GATEWAY_HOST=172.20.0.10
     │           ├── coap_resolve_address_info("172.20.0.10", 5683)
     │           ├── coap_new_client_session()
     │           └── Muestra prompt "sensor-temp-001> "
     │
     └── Arranca coreconf_device_2
           └── ./coreconf_cli humidity sensor-hum-001
                 └── Muestra prompt "sensor-hum-001> "
```
