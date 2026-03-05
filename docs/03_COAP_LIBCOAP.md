# CoAP y libcoap — guía en profundidad

## ¿Qué es CoAP?

CoAP (Constrained Application Protocol, RFC 7252) es un protocolo de aplicación diseñado para dispositivos con recursos muy limitados en redes IoT. Es **semánticamente similar a HTTP** pero mucho más ligero:

| Característica | HTTP | CoAP |
|---------------|------|------|
| Transporte | TCP | UDP (opcionalmente TCP) |
| Overhead cabecera | ~800 bytes | 4 bytes mínimo |
| Métodos | GET/POST/PUT/DELETE | GET/POST/PUT/DELETE + FETCH/iPATCH/PATCH |
| Codes | 200/404/etc. | 2.05/4.04/etc. (clase.detalle) |
| Seguridad | TLS | DTLS |
| Multicast | No nativo | Sí (IPv6 multicast) |

---

## Estructura de un mensaje CoAP

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|Ver| T |  TKL  |      Code     |          Message ID           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Token (si TKL > 0) ...                                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Options ...                                                  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|1 1 1 1 1 1 1 1|    Payload ...                                |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- **Ver** (2 bits): siempre `01` (versión 1)
- **T** (2 bits): tipo de mensaje
  - `00` = CON (Confirmable) — requiere ACK
  - `01` = NON (Non-confirmable) — no requiere ACK
  - `10` = ACK — confirma un CON
  - `11` = RST — reset
- **TKL** (4 bits): longitud del token (0-8 bytes)
- **Code** (8 bits): `ccc.ddddd` → clase (0-7) y detalle (0-31)
  - `0.xx` = métodos de request (GET=0.01, POST=0.02, PUT=0.03, DELETE=0.04, FETCH=0.05, iPATCH=0.07)
  - `2.xx` = respuestas de éxito
  - `4.xx` = errores del cliente
  - `5.xx` = errores del servidor
- **Message ID** (16 bits): identificador para deduplicación y ACK
- **Token**: identificador request-response (el cliente elige, el servidor lo repite)

### El payload marker

El byte `0xFF` antes del payload es obligatorio si hay payload. Sin él, lo que sigue son opciones.

### Opciones CoAP

Las opciones van codificadas en TLV comprimido (delta coding):

| Número | Nombre | Uso en CORECONF |
|--------|--------|----------------|
| 3 | Uri-Host | hostname del servidor |
| 7 | Uri-Port | puerto |
| 11 | Uri-Path | `/c` — un option por segmento |
| 12 | Content-Format | 141, 142, 60 |
| 15 | Uri-Query | `id=sensor-001`, `k=20` — uno por parámetro |
| 17 | Accept | 142 en FETCH |

**Delta encoding de opciones**: cada opción se codifica como (delta_desde_anterior, longitud, valor). Como los números de opción van típicamente en orden creciente, los deltas son pequeños (1-2 bytes).

---

## Modelo de mensajes: CON/ACK

```
Cliente          Servidor
   |                 |
   |─── CON GET ────>|   Message ID: 1234, Token: 0xAB
   |                 |   (si no hay ACK en ~2s, retransmite)
   |<── ACK 2.05 ───|   Message ID: 1234 (mismo), Token: 0xAB
   |                 |
```

Esto es un **piggyback ACK**: el servidor responde con la respuesta directamente en el ACK. Es lo más común y eficiente.

Si el servidor tarda en procesar:
```
Cliente          Servidor
   |                 |
   |─── CON GET ────>|
   |<── ACK empty ──|   ACK vacío inmediato (confirma recepción)
   |                 |   (servidor procesa...)
   |<── CON 2.05 ───|   Respuesta como CON separado
   |─── ACK ────────>|
```

---

## Block-wise transfer (RFC 7959)

Cuando la respuesta no cabe en un solo paquete UDP (~1500 bytes con IP/UDP overhead):

```
Cliente                    Servidor
   |                           |
   |─── GET /c?id=X ──────────>|
   |<── 2.05 Block2: 0/More ──|   primer bloque, hay más
   |─── GET /c Block2: 1 ─────>|   pide bloque 1
   |<── 2.05 Block2: 1/More ──|
   |─── GET /c Block2: 2 ─────>|
   |<── 2.05 Block2: 2/Done ──|   último bloque
```

libcoap gestiona esto automáticamente con `coap_add_data_large_response()` (servidor) y el cliente lo reensambla transparentemente.

---

## libcoap: la biblioteca

libcoap es la implementación de referencia de CoAP en C. Soporta:
- CoAP/UDP, CoAP/TCP (RFC 8323)
- DTLS con OpenSSL, GnuTLS, Mbed TLS o sin TLS (`-notls`)
- Block-wise transfer
- Observe
- CoAP over WebSockets

### Compilación

**macOS (desde source)**:
```bash
cd libcoap
./autogen.sh
./configure --disable-dtls --disable-tcp
make install
# instala libcoap-3 en /usr/local/lib
```
Enlazar: `-lcoap-3`

**Ubuntu 22.04 (apt)**:
```bash
apt install libcoap3-dev
```
Enlazar: `-lcoap-3-notls` (el sufijo indica qué backend TLS lleva compilado)

### Conceptos clave de la API

#### `coap_context_t` — el estado global

```c
coap_context_t *ctx = coap_new_context(NULL);
```

Contiene todos los sockets, recursos registrados, sesiones activas. Hay uno por proceso normalmente.

#### `coap_endpoint_t` — el socket

```c
coap_address_t addr;
coap_address_init(&addr);
addr.addr.sin.sin_family = AF_INET;
addr.addr.sin.sin_port = htons(5683);
inet_pton(AF_INET, "0.0.0.0", &addr.addr.sin.sin_addr);

coap_endpoint_t *ep = coap_new_endpoint(ctx, &addr, COAP_PROTO_UDP);
```

#### `coap_resource_t` — un recurso CoAP

```c
coap_resource_t *res = coap_resource_init(
    coap_make_str_const("c"),  // Uri-Path
    0                           // flags
);

// Registrar handlers por método
coap_register_handler(res, COAP_REQUEST_GET,    handle_get);
coap_register_handler(res, COAP_REQUEST_POST,   handle_post);
coap_register_handler(res, COAP_REQUEST_FETCH,  handle_fetch);
coap_register_handler(res, COAP_REQUEST_IPATCH, handle_ipatch);
coap_register_handler(res, COAP_REQUEST_PUT,    handle_put);
coap_register_handler(res, COAP_REQUEST_DELETE, handle_delete);

coap_add_resource(ctx, res);
```

#### Signature de un handler

```c
static void handle_get(
    coap_resource_t  *resource,   // el recurso que recibio la peticion
    coap_session_t   *session,    // la sesión (cliente que envió)
    const coap_pdu_t *request,    // PDU de la petición
    const coap_string_t *query,   // query string (?id=X&k=20)
    coap_pdu_t       *response    // PDU de respuesta (a rellenar)
) {
    // 1. Leer body del request
    size_t body_len;
    const uint8_t *body;
    coap_get_data(request, &body_len, &body);

    // 2. Leer opciones del request
    coap_opt_iterator_t oi;
    coap_opt_t *cf_opt = coap_check_option(request, COAP_OPTION_CONTENT_FORMAT, &oi);

    // 3. Construir respuesta
    coap_pdu_set_code(response, COAP_RESPONSE_CODE(205));  // 2.05

    // 4. Añadir Content-Format a la respuesta
    uint8_t cf_buf[2];
    coap_add_option(response, COAP_OPTION_CONTENT_FORMAT,
                    coap_encode_var_safe(cf_buf, sizeof(cf_buf), 142),
                    cf_buf);

    // 5. Añadir payload a la respuesta
    coap_add_data(response, payload_len, payload);
    // O para respuestas grandes con block-wise:
    coap_add_data_large_response(resource, session, request, response,
                                  query, 142, -1, 0,
                                  payload_len, payload, NULL, NULL);
}
```

#### El event loop

```c
while (1) {
    coap_io_process(ctx, COAP_IO_WAIT);  // COAP_IO_WAIT = bloquear hasta evento
}
```

`coap_io_process` es el corazón del servidor: espera eventos de red (usando `select`/`poll` internamente), los procesa, llama a los handlers y envía ACKs.

### Cliente con libcoap

```c
// 1. Crear contexto y sesión
coap_context_t *ctx = coap_new_context(NULL);
coap_address_t dst;
// ... resolver host ...
coap_session_t *sess = coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);

// 2. Registrar handler de respuesta
coap_register_response_handler(ctx, response_handler);

// 3. Crear PDU
coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, sess);

// 4. Añadir opciones
coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t*)"c");

// 5. Enviar
coap_send(sess, pdu);

// 6. Esperar respuesta (con timeout)
while (!response_received) {
    coap_io_process(ctx, 1000);  // 1s timeout
}

// 7. En el handler de respuesta
static void response_handler(coap_session_t *sess,
                              const coap_pdu_t *sent,
                              const coap_pdu_t *received,
                              const coap_mid_t mid) {
    coap_pdu_code_t code = coap_pdu_get_code(received);
    size_t len; const uint8_t *data;
    coap_get_data(received, &len, &data);
    // procesar respuesta...
}
```

---

## Opciones CoAP: cómo leerlas y escribirlas

### Escribir una opción

```c
// Content-Format: 142
uint8_t buf[2];
size_t len = coap_encode_var_safe(buf, sizeof(buf), 142);
coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, len, buf);

// Uri-Query: id=sensor-001
const char *q = "id=sensor-001";
coap_add_option(pdu, COAP_OPTION_URI_QUERY, strlen(q), (uint8_t*)q);

// Si tienes múltiples parámetros, añade un option por cada uno:
coap_add_option(pdu, COAP_OPTION_URI_QUERY, 3, (uint8_t*)"k=20");
coap_add_option(pdu, COAP_OPTION_URI_QUERY, 3, (uint8_t*)"k=21");
// Resultado en la URL: ?k=20&k=21
```

### Leer una opción

```c
coap_opt_iterator_t oi;
// Busca la primera ocurrencia de Content-Format
coap_opt_t *opt = coap_check_option(pdu, COAP_OPTION_CONTENT_FORMAT, &oi);
if (opt) {
    int cf = (int)coap_decode_var_bytes(coap_opt_value(opt), coap_opt_length(opt));
}

// Leer la query string completa (libcoap la concatena con &)
// query->s = "id=sensor-001&k=20"
// query->length = 18
```

---

## CBOR: el formato de serialización

CBOR (Concise Binary Object Representation, RFC 8949) es un formato binario similar a JSON pero mucho más compacto.

### Tipos CBOR y su encoding

| Tipo | Major type | Ejemplo |
|------|-----------|---------|
| Unsigned int | 0 | `0x18 0x64` = 100 |
| Negative int | 1 | `0x38 0x63` = -100 |
| Byte string | 2 | `0x43 0x01 0x02 0x03` = h'010203' |
| Text string | 3 | `0x63 0x61 0x62 0x63` = "abc" |
| Array | 4 | `0x82 0x01 0x02` = [1, 2] |
| Map | 5 | `0xa1 0x01 0x02` = {1: 2} |
| Tag | 6 | `0xc1 0x1a...` = timestamp |
| Float | 7 | `0xf9...` (half), `0xfa...` (single), `0xfb...` (double) |
| Bool/null | 7 | `0xf4`=false, `0xf5`=true, `0xf6`=null |

### Compacidad vs JSON

Representar `{"sensor": "temperature", "value": 24.5}` en JSON: **39 bytes**.

Con CORECONF usando SIDs y CBOR: `{10: "temperature", +10: 24.5}` → **~14 bytes** (~64% menos).

### Inspeccionar CBOR manualmente

```bash
# Ver bytes CBOR en hex
echo -n 'a2 0a 6b 74 65 6d 70 65 72 61 74 75 72 65 14 f9 4208' | xxd -r -p | python3 -c "
import sys, cbor2
data = sys.stdin.buffer.read()
print(cbor2.loads(data))
"

# O con la herramienta cbor-diag
apt install ruby-cbor-diag
cbor-diag.rb < payload.cbor
```

---

## Wireshark y CoAP

### Filtros útiles

```
coap                     # todo el tráfico CoAP
coap.code == 0.05        # solo FETCH requests
coap.code == 2.05        # solo 2.05 Content responses
coap.opt.uri_query       # mensajes con query params
coap.opt.content_format == 142  # CF=142
udp.port == 5683         # puerto CoAP estándar
```

### Disector CoAP en Wireshark

Wireshark disecta CoAP automáticamente si el tráfico va por el puerto 5683 UDP. Si usas otro puerto:
- Botón derecho en el paquete → "Decode As" → CoAP

Para ver el payload CBOR decodificado necesitas el plugin CBOR de Wireshark (incluido desde Wireshark 3.4).

### Capturar desde Docker en macOS

Docker en macOS corre en una VM. El tráfico entre contenedores queda dentro de la VM y no es visible en las interfaces del Mac. Solución: tcpdump desde dentro del contenedor, pipe a Wireshark:

```bash
docker exec coreconf_gateway tcpdump -i any -w - port 5683 2>/dev/null \
  | /Applications/Wireshark.app/Contents/MacOS/Wireshark -k -i -
```

Desglose:
- `docker exec coreconf_gateway` — ejecuta en el contenedor gateway
- `tcpdump -i any` — captura en todas las interfaces del contenedor
- `-w -` — escribe pcap a stdout
- `|` — pipe al Mac
- `Wireshark -k -i -` — Wireshark lee de stdin (-i -), empieza captura inmediatamente (-k)
