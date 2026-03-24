# Checklist de cierre TFG (CORECONF + ietf-system)

## 0) Enfoque funcional (base del proyecto)
- [x] Mantener `ietf-system` como módulo principal de demostración
- [x] Mantener los ejemplos del draft con SIDs de `ietf-system` (clock, ntp)
- [x] Verificar que los deltas SID usados en mapas anidados coinciden con RFC 9254
- [x] Dejar `include/sids.h` como fuente única de constantes SID del proyecto

## 1) Seguridad DTLS (obligatorio para cierre)
- [ ] Servidor escuchando por `coaps://` (no solo `coap://`)
- [ ] Cliente enviando por `coaps://`
- [ ] Elegir modo DTLS para demo: `PSK` o `certificados`
- [ ] Configurar credenciales en cliente/servidor y documentarlas
- [ ] Rechazar cliente sin credenciales válidas
- [ ] Verificar handshake DTLS exitoso en logs
- [ ] Confirmar que payload no viaja en claro (captura de tráfico)
- [ ] Definir política mínima de autorización por método (lectura vs escritura)

## 2) Cumplimiento draft por operación
- [ ] GET: `Content-Format` y payload conforme al draft
- [ ] FETCH: request con CF de identifiers y response con instances
- [ ] iPATCH: soporte `{SID: valor}` y `{[SID,"key"]: valor}`
- [ ] iPATCH con `null` borra nodo/entrada (caso validado)
- [ ] PUT: reemplazo inicial/completo del datastore (idempotente)
- [ ] DELETE: elimina datastore y responde código esperado
- [ ] POST: limitar a RPC/acciones (no para cargar datastore)
- [ ] Casos de error por operación (CF inválido, payload vacío, SID inválido)
- [ ] Tabla final de conformidad: `Cumple / Parcial / No cumple`

## 3) Cerrar bien POST vs PUT
- [ ] Documentar regla oficial en código y memoria:
  - [ ] `PUT /c` para inicializar/reemplazar datastore
  - [ ] `POST /c` solo para RPC/acciones YANG
- [ ] Implementar al menos un ejemplo real de `POST` como acción/RPC
- [ ] Añadir test de no-regresión: `POST` para cargar datos debe fallar
- [ ] Añadir test de no-regresión: `PUT` para cargar datastore debe funcionar

## 4) Demo contenedor↔contenedor con cambio interno
- [ ] Escenario IoT definido (p.ej. temperatura/chorro)
- [ ] Contenedor A publica/expone estado (temperatura)
- [ ] Contenedor B consulta estado y toma decisión
- [ ] Si `temp > umbral`, contenedor B hace `iPATCH` sobre nodo de actuación
- [ ] Verificar que el cambio persiste en datastore del contenedor objetivo
- [ ] Añadir logs de trazabilidad extremo a extremo (A decide → B actúa → valor cambia)
- [ ] Grabar secuencia reproducible (script) para defensa del TFG

## 5) Calidad y validación final
- [ ] Ejecutar pruebas locales de operaciones CORECONF
- [ ] Validar funcionamiento en docker-compose (servicios levantan y se ven)
- [ ] Capturas de evidencia: comandos, respuestas CoAP, estado final
- [ ] Sección de limitaciones conocidas + trabajo futuro
- [ ] Preparar guion de demo de 5-10 minutos para la defensa

---

## Criterio de “TFG listo para defensa”
- [ ] DTLS activo y validado
- [ ] Operaciones CORECONF alineadas con draft
- [ ] Demo inter-contenedores funcionando de forma reproducible
- [ ] POST y PUT cerrados conceptualmente y validados con tests
