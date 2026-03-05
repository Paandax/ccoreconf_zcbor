# Documentación ccoreconf_zcbor

Guías técnicas en profundidad sobre todo lo implementado en este proyecto.

## Índice

| Documento | Contenido |
|-----------|-----------|
| [01 — RFC 9254 CORECONF](01_RFC9254_CORECONF.md) | El protocolo en detalle: SIDs, Content-Format, las 6 operaciones, códigos de respuesta, qué falta implementar |
| [02 — Arquitectura del proyecto](02_ARQUITECTURA.md) | Estructura de ficheros, módulos de la biblioteca, flujo de datos completo, decisiones de diseño |
| [03 — CoAP y libcoap](03_COAP_LIBCOAP.md) | Protocolo CoAP (frames, opciones, block-wise), API de libcoap, CBOR, Wireshark |
| [04 — Docker IoT](04_DOCKER_IOT.md) | Dockerfile explicado, docker-compose, redes bridge, captura de tráfico, comandos esenciales |

## Orden de lectura recomendado

1. **01_RFC9254** → entiende QUÉ hace el sistema y por qué existe
2. **03_COAP_LIBCOAP** → entiende CÓMO se comunican los nodos
3. **02_ARQUITECTURA** → entiende CÓMO está organizado el código
4. **04_DOCKER_IOT** → entiende CÓMO se despliega

## Lo que falta implementar (trabajo futuro)

- **CoAP Observe** — notificaciones push cuando cambia un SID
- **Datastores NMDA** — running / intended / candidate / operational
- **YANG Library** — endpoint `/c/ietf-yang-library` con catálogo de módulos
- **DTLS** — seguridad en capa de transporte (obligatorio en producción)
- **Locking** — mutex para acceso concurrente al datastore
