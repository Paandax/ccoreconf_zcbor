# NOTAS — 22 de marzo de 2026

## Resumen
Anotar la incidencia y siguientes pasos para arreglar la parte DTLS del demo CORECONF.

## Observación
- El servidor carga los SIDs y publica notificaciones del draft (notification_sid=60010).
- Al usar `Observe` el servidor publica/cola eventos que hacen referencia a la notificación 60010 (ej. `example-port-fault`).
- Al usar `GET /c` se devuelve el datastore estándar (SIDs del sistema/interfaces: 1721, 1533, 1755/1756, etc.), sin incluir la notificación 60010.

## Evidencia (resumen de logs)
- "[SID] Notificación cargada ../../sid/ietf-comi-notification.sid: notification_sid=60010"
- "[OBSERVE] queued event #1 (24 bytes, example-port-fault)"
- "Datastore ietf-example cargado: 4 SIDs (clock 1721, ifaces 1533, ntp 1755/1756)"

## Interpretación
- El flujo Observe utiliza SIDs de notificación (60010) para eventos; el `GET` retorna el contenido del datastore principal (SIDs de datos), por eso ves comportamientos distintos entre `Observe` y `GET`.
- El fallo actual de conexión parece relacionado con DTLS/handshake (certificados o mismatch de host), no con la lógica de SIDs en sí.

## Reproducción rápida
1. Iniciar servidor (DTLS):
   ```sh
   CORECONF_DTLS=1 ./coreconf_server
   ```
2. Iniciar cliente y ejecutar comandos `observe` / `get`.
3. Ver `server.log` para eventos y carga de SIDs.

## Siguientes pasos recomendados
- Depurar DTLS: verificar certificados CA/servidor/cliente (CN/SAN), probar con PSK o deshabilitar DTLS para pruebas UDP.
- Confirmar handshake en `server.log` y salida del cliente; capturar mensajes de error TLS.
- Luego validar que `Observe` publica SIDs de notificación (60010) y que `GET` devuelve datastore esperado.

---
Archivo creado automáticamente el 22-03-2026.
