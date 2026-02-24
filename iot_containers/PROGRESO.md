# Progreso: CoAP con CORECONF/CBOR en contenedores IoT

## ✅ Lo que hemos hecho

### iot_server.c — Migración TCP → CoAP (libcoap)

El servidor original usaba sockets TCP (`socket/bind/listen/accept/recv/send`).
Lo hemos reescrito completamente para usar **CoAP sobre UDP** con libcoap.

**Estructura nueva:**

| Función | Qué hace |
|---------|----------|
| `handle_post_store()` | Handler CoAP para `POST /c`. Extrae el payload CBOR con `coap_get_data()`, lo decodifica con `cborToCoreconfValue()`, almacena los datos del sensor en la base de datos mock y responde **2.04 Changed** |
| `handle_fetch_coreconf()` | Handler CoAP para `FETCH /c` (RFC 8132). Extrae la CBOR sequence de instance-identifiers, llama a `parse_fetch_request_iids()` + `create_fetch_response_iids()` y responde **2.05 Content** con el CBOR de los valores pedidos |
| `main()` | `coap_startup()` → bind en `0.0.0.0:5683` con `coap_new_context(&listen_addr)` → registra recurso `/c` con `coap_resource_init()` → registra los dos handlers → loop con `coap_io_process()` |

**Flujo STORE (POST /c):**
```
Cliente                    Servidor
  |  CON [POST /c]           |
  |  Content-Format: 60      |
  |  Payload: {1:id, 20:val} |──→ handle_post_store()
  |                          |    └─ store_device_data()  ← guarda SIDs >= 10
  |  ACK [2.04 Changed]      |
  |←─────────────────────────|
```

**Flujo FETCH (FETCH /c):**
```
Cliente                    Servidor
  |  CON [FETCH /c]          |
  |  Content-Format: 60      |
  |  Payload: [20, 21, 10]   |──→ handle_fetch_coreconf()
  |                          |    ├─ parse_fetch_request_iids()
  |                          |    └─ create_fetch_response_iids()
  |  ACK [2.05 Content]      |
  |  Payload: {20:18.3, ...} |
  |←─────────────────────────|
```

---

### iot_client.c — Migración TCP → CoAP (libcoap)

El cliente original usaba `socket/connect/send/recv` TCP.
Lo hemos reescrito con el patrón **asíncrono de libcoap**: enviar PDU + esperar respuesta con `coap_io_process()`.

**Estructura nueva:**

| Función | Qué hace |
|---------|----------|
| `response_handler()` | Callback registrado con `coap_register_response_handler()`. libcoap lo llama cuando llega la respuesta. Guarda el código (`coap_pdu_get_code()`) y el payload (`coap_get_data()`) en variables estáticas compartidas |
| `send_and_wait()` | Helper interno: llama a `coap_send()` y hace polling con `coap_io_process()` hasta 5s esperando que `response_handler` señalice que llegó respuesta |
| `do_store()` | Construye PDU `POST /c` con Content-Format CBOR (60) + payload CBOR del sensor. Llama a `send_and_wait()` y verifica que la respuesta sea clase 2.xx |
| `do_fetch()` | Construye PDU `FETCH /c` con la CBOR sequence de instance-identifiers. Llama a `send_and_wait()` y decodifica la respuesta con `cborToCoreconfValue()` |
| `main()` | `coap_startup()` → configura `dst_addr` con la IP del gateway → `coap_new_context()` + `coap_new_client_session()` → `coap_register_response_handler()` → 2 ciclos de `do_store()` + `do_fetch()` → limpieza |

**Patrón asíncrono libcoap (clave):**
```c
// En lugar de recv() bloqueante:
coap_mid_t mid = coap_send(session, pdu);   // envía
while (!response_received && waited < 5000) {
    coap_io_process(ctx, 100);               // procesa eventos 100ms
    waited += 100;
}
// response_handler() se ejecutó internamente y dejó los datos en last_response[]
```

**Resultado probado localmente:**
```bash
# Terminal 1
./iot_server

# Terminal 2
GATEWAY_HOST=127.0.0.1 ./iot_client temperature

# Salida:
# ✅ STORE aceptado por el gateway  (2.04 Changed)
# 🎯 SID 20: 18.31                  (2.05 Content)
# 🎯 SID 21: "celsius"
```

---

### Makefile (iot_apps/)

Añadidas las rutas de libcoap para compilar en macOS:

```makefile
LIBCOAP_INC = /Users/pablo/Desktop/coap_learning/libcoap/include
LIBCOAP_LIB = /Users/pablo/Desktop/coap_learning/libcoap/build
CFLAGS += -I$(LIBCOAP_INC) -I$(LIBCOAP_LIB)
LDFLAGS += -L$(LIBCOAP_LIB) -lcoap-3
```

---

### Dockerfile

Tres cambios respecto al original:

1. **`libcoap3-dev`** → instala libcoap en el contenedor Ubuntu 22.04
2. **`tcpdump`** → para capturar tráfico CoAP dentro del contenedor
3. **`pkg-config --libs libcoap-3-notls`** → Ubuntu 22.04 no tiene `libcoap-3.so` genérico, tiene variantes por backend TLS (`-notls`, `-gnutls`, `-openssl`). Usamos la versión sin TLS.

```dockerfile
RUN apt-get install -y ... libcoap3-dev tcpdump ...

RUN COAP_LIBS=$(pkg-config --libs libcoap-3-notls) && \
    gcc ... iot_server.c ../ccoreconf.a $COAP_LIBS -pthread -o iot_server
```

### docker-compose.yml

Puerto CoAP corregido a UDP (CoAP es UDP, no TCP):
```yaml
ports:
  - "5683:5683/udp"   # antes era "5683:5683" (TCP por defecto)
```

---

## ⏳ Lo que falta para Docker + Wireshark

### 1. Wireshark dentro del contenedor

En macOS, Docker Desktop corre en una VM. El tráfico entre contenedores
**no es visible** en las interfaces del Mac — Wireshark del host no lo capta.

**Solución pendiente:** capturar con `tcpdump` dentro del gateway y redirigir a Wireshark:

```bash
# Con los contenedores corriendo:
docker exec iot_gateway tcpdump -i any -U -w - udp port 5683 | wireshark -k -i -
```

Esto requiere que `tcpdump` esté en la imagen (ya lo pusimos) y que Wireshark
esté instalado en el Mac con soporte de pipe (`brew install --cask wireshark`).

### 2. Reconstruir la imagen con tcpdump

```bash
docker build -t iot-coreconf -f iot_containers/Dockerfile .
docker compose -f iot_containers/docker-compose.yml up
```

### 3. Verificar comunicación entre contenedores

Con los logs del compose deberías ver:
```
iot_gateway    | 📥 POST /c recibido (STORE)
iot_gateway    | ✨ Nuevo dispositivo 'sensor-temp-001' registrado
iot_gateway    | 📥 FETCH /c recibido
iot_gateway    | ✅ FETCH completado → 2.05 Content
iot_sensor_temp| ✅ STORE aceptado por el gateway
iot_sensor_temp| 🎯 SID 20: 22.14
```

### 4. Posibles problemas pendientes en Docker

| Problema | Causa probable | Solución |
|----------|---------------|----------|
| Cliente timeout en Docker | El gateway puede no estar listo cuando arranca el cliente | Añadir `sleep 2` al CMD del cliente o usar `healthcheck` |
| `iot_test_local.c` falla | Usa sockets TCP (no migrado) | Migrar o excluir del compose |

