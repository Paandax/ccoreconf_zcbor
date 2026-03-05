# Contenedores Docker — simulación CORECONF en red real

Aquí explico cómo está montada la infraestructura Docker del proyecto, qué hace cada contenedor, cómo se comunican entre sí y qué se manda por la red en cada operación CORECONF.

---

## La idea general

La gracia de meter esto en Docker es simular una red IoT real sin necesitar hardware físico. Tenemos tres nodos:

- **Gateway** (`coreconf_server`): el servidor CORECONF. Mantiene el datastore de cada dispositivo y responde a todos los métodos del RFC 9254 (FETCH, GET, iPATCH, PUT, DELETE, POST).
- **Dispositivo 1** (`coreconf_cli temperature sensor-temp-001`): simula un sensor de temperatura que se conecta al gateway por CoAP/UDP.
- **Dispositivo 2** (`coreconf_cli humidity sensor-hum-001`): simula un sensor de humedad, mismo mecanismo.

Los tres viven en una red bridge de Docker llamada `iot_network`, con IPs fijas. El gateway tiene la 172.20.0.10, el dispositivo de temperatura la 172.20.0.11 y el de humedad la 172.20.0.12.

---

## Estructura de archivos

```
iot_containers/
├── Dockerfile              # imagen única para gateway y dispositivos
├── docker-compose.yml      # orquesta los 3 contenedores
├── logs/                   # volumen compartido para logs del gateway
└── iot_apps/
    ├── coreconf_server.c   # el gateway CORECONF
    ├── coreconf_cli.c      # el cliente interactivo (dispositivos)
    ├── [get|fetch|ipatch|put|delete]_server.c   # servidores de prueba individuales
    └── [get|fetch|ipatch|put|delete]_client.c   # clientes de prueba individuales
```

Los archivos `*_server.c` y `*_client.c` individuales son para pruebas aisladas de una sola operación. Para la demo completa se usan `coreconf_server.c` y `coreconf_cli.c`.

---

## El Dockerfile

Uso una sola imagen para todo — tanto el gateway como los dispositivos arrancan el mismo binario compilado, simplemente con argumentos distintos.

```dockerfile
FROM ubuntu:22.04
```

Ubuntu 22.04 LTS, base estable y bien soportada.

```dockerfile
RUN apt-get update && apt-get install -y \
    build-essential gcc make pkg-config \
    libcoap3-dev tcpdump ...
```

Instalo `libcoap3-dev` que es la implementación de CoAP para Ubuntu. Importante: en Ubuntu esta librería se llama `coap-3-notls` (sin TLS), a diferencia de macOS donde compilo contra libcoap desde source. También instalo `tcpdump` para poder capturar tráfico desde dentro del contenedor y piparlo a Wireshark.

```dockerfile
COPY include/ ./include/
COPY src/ ./src/
COPY coreconf_zcbor_generated/ ./coreconf_zcbor_generated/
COPY Makefile ./
COPY iot_containers/iot_apps/ ./iot_apps/
```

Copio el código desde la raíz del proyecto. Por eso el `docker build` hay que lanzarlo desde la raíz (`ccoreconf_zcbor/`), no desde `iot_containers/`. Si lo lanzas desde dentro, Docker no encuentra `include/` y `src/` y falla el build.

```dockerfile
RUN make clean && make
```

Compila la librería estática `ccoreconf.a` con toda la lógica CORECONF (serialización, hashmap, SIDs, fetch, ipatch...).

```dockerfile
RUN cd iot_apps && \
    sed -i 's|"../../include/|"|g' *.c && \
    sed -i 's|"../../coreconf_zcbor_generated/|"|g' *.c
```

Los `.c` de `iot_apps/` tienen includes con rutas relativas pensadas para compilar desde dentro de `iot_apps/` en local. En Docker, el WORKDIR es `/iot_device` y los headers están en `/iot_device/include/`, así que los `sed` ajustan las rutas antes de compilar.

```dockerfile
RUN COAP_LIBS=$(pkg-config --libs libcoap-3-notls ...) && \
    gcc ... coreconf_server.c ../ccoreconf.a $COAP_LIBS -pthread -o coreconf_server && \
    gcc ... coreconf_cli.c    ../ccoreconf.a $COAP_LIBS -pthread -o coreconf_cli
```

Compila los binarios finales enlazando contra `ccoreconf.a` y libcoap. `pkg-config` me da los flags correctos para esa versión de libcoap automáticamente.

---

## El docker-compose.yml

```yaml
coreconf_gateway:
  image: iot-coreconf
  command: ./iot_apps/coreconf_server
  ports:
    - "5683:5683/udp"
  networks:
    iot_network:
      ipv4_address: 172.20.0.10
```

El gateway expone el puerto 5683/UDP al host, que es el puerto estándar de CoAP. Así se puede atacar desde fuera con `coap-client` o capturar en Wireshark.

```yaml
coreconf_device_1:
  image: iot-coreconf
  command: ./iot_apps/coreconf_cli temperature sensor-temp-001
  stdin_open: true
  tty: true
  environment:
    - GATEWAY_HOST=172.20.0.10
    - GATEWAY_PORT=5683
  depends_on:
    - coreconf_gateway
```

`stdin_open: true` y `tty: true` son necesarios para que el cliente sea interactivo — sin ellos el contenedor no acepta input del terminal. `depends_on` hace que el dispositivo espere a que el gateway esté arriba antes de arrancar. Las variables de entorno `GATEWAY_HOST` y `GATEWAY_PORT` le dicen al cliente dónde está el servidor. `coreconf_device_2` es exactamente igual pero para humedad.

---

## Cómo arrancar todo

```bash
# Desde la raíz del proyecto (ccoreconf_zcbor/)
docker build -t iot-coreconf .
docker compose -f iot_containers/docker-compose.yml up -d
```

Para conectarte a un dispositivo de forma interactiva:

```bash
docker attach coreconf_device_1
```

Para salir sin matar el contenedor: **Ctrl-P Ctrl-Q** (no Ctrl-C, eso lo mata).

Para ver los logs del gateway en tiempo real:

```bash
docker logs -f coreconf_gateway
```

---

## Cómo se comunican

Todos los mensajes van por **CoAP sobre UDP** al recurso `/c` del gateway.

```
coreconf_device_1 (172.20.0.11)  ──UDP/CoAP──▶  coreconf_gateway (172.20.0.10:5683)
coreconf_device_2 (172.20.0.12)  ──UDP/CoAP──▶  coreconf_gateway (172.20.0.10:5683)
```

El gateway identifica cada dispositivo por el `?id=<device_id>` del URI. Mantiene un datastore separado por device_id, así el sensor de temperatura y el de humedad no se pisan entre sí.

---

## Qué se manda en cada operación

### POST — cargar datos iniciales

El dispositivo manda todos sus SIDs con sus valores al arrancar.

```
→ POST coap://172.20.0.10:5683/c?id=sensor-temp-001
   Code: POST (0.02)
   Content-Format: 60  (application/cbor)
   Body CBOR: {10: "sensor-temp-001", 11: "temperature", 20: 22.5, 21: 45}

← 2.01 Created
```

El body es un CBOR map donde las claves son los SIDs (enteros cortos) y los valores son los datos del sensor. Sin POST previo el gateway no tiene datastore para ese dispositivo y cualquier GET devuelve 4.04.

Los SIDs que uso:

| SID | Nodo YANG | Tipo |
|-----|-----------|------|
| 10 | device-id | string |
| 11 | device-type | string |
| 20 | valor principal (temperatura o humedad) | float |
| 21 | battery-level | integer |

### GET — leer todo el datastore

Pide todos los SIDs de un dispositivo de golpe, sin filtrar nada.

```
→ GET coap://172.20.0.10:5683/c?id=sensor-temp-001
   Code: GET (0.01)
   Sin body, sin Content-Format

← 2.05 Content
   Content-Format: 142  (application/yang-data+cbor; id=sid)
   Body CBOR: {10: "sensor-temp-001", 11: "temperature", 20: 22.5, 21: 45}
```

Para un sensor con 4 SIDs son unos 37 bytes de respuesta.

### FETCH — leer SIDs concretos

Como GET pero filtrado. El cliente elige exactamente qué SIDs quiere recibir mandando una lista CBOR en el body.

```
→ FETCH coap://172.20.0.10:5683/c?id=sensor-temp-001
   Code: FETCH (0.05)
   Content-Format: 141  (application/yang-patch+cbor; id=sid)
   Accept: 142
   Body CBOR: [20]   ← lista de SIDs que quiero

← 2.05 Content
   Content-Format: 142
   Body CBOR: {20: 22.5}   ← solo el SID pedido
```

Si pides solo el SID 20, la respuesta son unos 14 bytes en vez de los 37 del GET completo. La diferencia escala mucho si el árbol YANG tiene decenas de nodos — aquí está una de las ventajas principales de FETCH frente a GET.

### iPATCH — actualización parcial

Modifica solo los SIDs que le mandes. El resto del datastore no se toca.

```
→ iPATCH coap://172.20.0.10:5683/c?id=sensor-temp-001
   Code: iPATCH (0.07)
   Content-Format: 141
   Body CBOR: {20: 24.5}   ← solo el SID que cambia

← 2.04 Changed
   Sin body
```

El body del request es un CBOR map `{SID: nuevo_valor}`. En este caso `{20: 24.5}` son 7 bytes para actualizar la temperatura. Si hubieras hecho un PUT tendrías que mandar los 4 SIDs completos. iPATCH solo manda el delta — eso es lo que lo hace útil para sensores que actualizan un valor cada pocos segundos.

### PUT — reemplazo total

Reemplaza el datastore entero del dispositivo con lo que mandes. Los SIDs que no vengan en el body se eliminan.

```
→ PUT coap://172.20.0.10:5683/c?id=sensor-temp-001
   Code: PUT (0.03)
   Content-Format: 142
   Body CBOR: {10: "sensor-temp-001", 11: "temperature", 20: 99.9, 21: 80}

← 2.04 Changed
   Sin body
```

### DELETE — borrar SIDs o el datastore completo

Con `?k=SID` borra un SID concreto. Sin el parámetro `k`, borra todo el datastore de ese dispositivo.

```
→ DELETE coap://172.20.0.10:5683/c?id=sensor-temp-001&k=20
   Code: DELETE (0.04)
   Sin body

← 2.02 Deleted
```

```
→ DELETE coap://172.20.0.10:5683/c?id=sensor-temp-001
   Code: DELETE (0.04)
   Sin body

← 2.02 Deleted
```

Después del DELETE total, cualquier GET o FETCH sobre ese device_id devuelve 4.04 Not Found.

---

## Capturar tráfico con Wireshark

### Desde Ubuntu (o Linux)

Docker crea una interfaz bridge que empieza por `br-`. Puedes capturar directamente:

```bash
# Ver qué interfaz es
ip link show | grep br-
# Ejemplo: br-a1b2c3d4e5f6

# En Wireshark, seleccionar esa interfaz y filtrar:
# udp.port == 5683
```

### Desde macOS

El tráfico entre contenedores no llega a las interfaces de red del Mac — pasa dentro de la VM de Docker Desktop. La solución es capturar desde dentro del gateway y piparlo a Wireshark:

```bash
docker exec coreconf_gateway tcpdump -i any -w - 'udp port 5683' | wireshark -k -i -
```

---

## Parar todo

```bash
docker compose -f iot_containers/docker-compose.yml down
```

Para limpiar también las imágenes:

```bash
docker rmi iot-coreconf
```
