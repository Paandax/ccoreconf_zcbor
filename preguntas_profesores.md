# Preguntas para los profesores

## 1. Parámetros de query `c` y `d` en GET/FETCH (§3.1 del draft)

Actualmente **no están implementados** — el servidor ignora la query string en GET y FETCH y devuelve siempre todo.

**¿Es necesario implementarlos para el TFG o es suficiente con el comportamiento actual?**

### Parámetro `?d=` (with-defaults)

Controla si se devuelven nodos YANG que tienen un valor por defecto y nunca han sido escritos por ningún cliente.

Ejemplo: si un módulo YANG define `leaf enabled { default true; }` y nadie lo ha configurado:
- `d=t` (trim, **default del draft**) → el servidor **no devuelve** ese nodo. Asume que el cliente ya sabe el default del schema.
- `d=a` (all) → el servidor **sí devuelve** ese nodo con valor `true`.

En la implementación actual no hay valores por defecto definidos ni lógica para distinguirlos, así que `d=t` y `d=a` darían el mismo resultado de todas formas.

### Parámetro `?c=` (content)

En YANG cada nodo es "config true" (el cliente puede escribirlo) o "config false" (solo lectura, lo genera el dispositivo):
- **config true**: `ntp/enabled`, `ntp/server`, nombre de interfaz... (el admin los configura)
- **config false**: `oper-status`, estadísticas, timestamps... (el dispositivo los calcula solo)

El parámetro filtra qué tipo devuelve el servidor:
- `c=c` → solo nodos config true (configuración)
- `c=n` → solo nodos config false (estado operacional)
- `c=a` → todos mezclados (**comportamiento actual siempre**)

Implementarlo requeriría marcar en `sids.h` si cada SID es config o no-config, y filtrar al serializar la respuesta.

---

## 2. Event stream y CoAP Observe (§3.4 del draft)

**No implementado.** Me gustaría que me lo explicaran con más detalle.

El draft define un recurso `/s` (stream) al que el cliente se suscribe con `GET /s Observe(0)`. A partir de ahí el servidor avisa al cliente cada vez que ocurre un evento YANG (notificación), sin que el cliente tenga que preguntar (push en vez de polling).

El payload de cada notificación es CF=142: `{SID_evento: {campos}}`. Si hay varias llegan como CBOR sequence ordenadas de más reciente a más antigua.

También permite filtrar con FETCH+Observe: el cliente manda una lista de SIDs de eventos que le interesan (CF=141) y el servidor solo notifica esos.

**Preguntas:**
- ¿Es necesario implementar Observe para el TFG?
- ¿Cómo se gestiona en libcoap la suscripción y el envío de notificaciones asíncronas?
- ¿Qué pasa si el cliente se desconecta — cómo cancela el servidor la suscripción?

---

## 3. RPC y Action statements (§3.5 del draft)

**No implementado.** Me gustaría que me lo explicaran con más detalle.

Son operaciones que **ejecutan algo** en el dispositivo (no leen ni escriben datos del datastore). Se mapean a POST en `/c`.

- **RPC** (procedimiento global): la clave del mapa es el SID del RPC, el valor es el container `input`:
  ```
  POST /c  { 61000: {1: 77} }   →   2.04 { 61000: null }
  ```
- **Action** (procedimiento sobre una instancia de lista): la clave es un instance-identifier `[SID, "clave"]`:
  ```
  POST /c  { [60002, "myserver"]: {1: "2016-02-08T14:10:08Z"} }
           →   2.04 { [60002, "myserver"]: {2: "2016-02-08T14:10:11Z"} }
  ```

**Preguntas:**
- ¿Es necesario implementar RPC/Action para el TFG?
- ¿Cómo se diferencia en el servidor un POST de RPC de un POST de datos normales?

---

## 4. Application Discovery / `/.well-known/core` (§5 del draft)

**No implementado.** El servidor solo tiene registrado el recurso `/c`. No hay handler para `/.well-known/core`.

El draft define un mecanismo por el que un cliente que llega a un servidor desconocido puede descubrir qué recursos tiene sin saber nada de antemano. El flujo completo sería:

1. `GET /.well-known/core?rt=core.c.yl` → dónde está la YANG library
2. Descargar la YANG library → qué módulos YANG soporta el servidor
3. Buscar los ficheros `.sid` de esos módulos → obtener el mapeo SID ↔ nodo YANG
4. Ya se puede hacer FETCH/iPATCH con los SIDs correctos

En la implementación actual cliente y servidor conocen los SIDs de antemano (hardcodeados en `sids.h`) — es un escenario cerrado. El draft reconoce que los mecanismos de discovery son opcionales y que en dispositivos muy pequeños cliente y servidor simplemente acuerdan fuera de banda qué SIDs hay.

Los cuatro Resource Types definidos son:
- `?rt=core.c.yl` → localización de la YANG library
- `?rt=core.c.ds` → datastores disponibles (responde `</c>;rt="core.c.ds";ds=1029`)
- `?rt=core.c.dn` → cada nodo de datos individualmente con su ruta
- `?rt=core.c.es` → streams de eventos disponibles

**Preguntas:**
- ¿Es necesario implementar `/.well-known/core` para el TFG?
- ¿Hasta qué punto tiene sentido implementar discovery en un dispositivo IoT pequeño con pocos SIDs?
- ¿El escenario de cliente y servidor con SIDs acordados de antemano es válido para el TFG o se espera discovery dinámico?

---

## 5. Cosas que me faltan por implementar en la biblioteca (`src/`)

### Bug: deep copy incompleta en `apply_ipatch()` (`src/ipatch.c`)
`apply_one_sid()` hace `memcpy` y copia profunda de strings, pero si el valor es `CORECONF_HASHMAP` o `CORECONF_ARRAY` el puntero interno se copia tal cual — datastore y parche comparten la misma memoria. Si se libera el parche, el datastore queda con punteros colgantes. `apply_ipatch_raw()` no tiene este problema. ¿Merece la pena arreglarlo o con `apply_ipatch_raw()` es suficiente?

### Feature: FETCH con instance-identifiers (`src/fetch.c`)
`fetch.c` solo acepta una lista de SIDs enteros en el request. El draft permite también `[SID, "key"]` para pedir una entrada concreta de una lista (igual que en iPATCH). Lo implementé en iPATCH pero no en FETCH. ¿Es necesario para el TFG?

### Feature: parámetros `c`/`d` en `get.c` y `fetch.c`
Si en algún momento se quieren implementar los filtros `?c=` y `?d=` en el servidor, habría que modificar las firmas de `create_get_response()` y `create_fetch_response()` para que acepten esos parámetros. Actualmente devuelven siempre todo sin filtrar.
