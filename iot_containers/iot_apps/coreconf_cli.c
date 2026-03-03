/**
 * coreconf_cli.c - Cliente CORECONF interactivo (RFC 9254)
 *
 * REPL: escribe comandos y los ejecuta contra el servidor.
 *
 * COMANDOS:
 *   store  <tipo> [valor]     → POST /c  — carga datos iniciales
 *   fetch  <SID> [SID...]     → FETCH /c — leer SIDs concretos
 *   get                       → GET /c   — leer datastore completo
 *   ipatch <SID> <valor>      → iPATCH /c — actualizar un SID
 *   put    <SID> <valor> ...  → PUT /c   — reemplazar datastore
 *   delete [SID [SID...]]     → DELETE /c — borrar SIDs o todo
 *   help                      → mostrar ayuda
 *   quit / exit               → salir
 *
 * COMPILAR:
 *   cd iot_containers/iot_apps && make coreconf_cli
 *
 * EJECUTAR (local):
 *   ./coreconf_cli temperature sensor-001
 *
 * DOCKER (interactivo):
 *   docker attach iot_device_1
 *   > store temperature 22.5
 *   > get
 *   > ipatch 20 99.9
 *   > delete 20
 */

#include <coap3/coap.h>
/* Compat: libcoap3-dev de Ubuntu 22.04 puede usar nombres distintos */
#ifndef COAP_LOG_WARN
#define COAP_LOG_WARN 4
#endif
#ifndef COAP_LOG_ERR
#define COAP_LOG_ERR  3
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <arpa/inet.h>
#include <ctype.h>

#include "../../include/coreconfTypes.h"
#include "../../include/serialization.h"
#include "../../include/fetch.h"
#include "../../include/get.h"
#include "../../include/ipatch.h"
#include "../../include/put.h"
#include "../../include/delete.h"
#include "../../coreconf_zcbor_generated/zcbor_encode.h"
#include "../../coreconf_zcbor_generated/zcbor_decode.h"

#define BUFFER_SIZE 4096
#define MAX_ARGS    32

/* ── Estado global CoAP ── */
static coap_context_t  *g_ctx     = NULL;
static coap_session_t  *g_session = NULL;
static char             g_device_id[64]   = "sensor-cli-001";
static char             g_device_type[64] = "temperature";

/* ── Respuesta ── */
static int             g_response_received = 0;
static uint8_t         g_response_buf[BUFFER_SIZE];
static size_t          g_response_len      = 0;
static coap_pdu_code_t g_response_code;

/* ══════════════════════════════════════════════════════════
 * Helpers CoAP
 * ════════════════════════════════════════════════════════*/
static coap_response_t response_handler(coap_session_t *sess,
                                          const coap_pdu_t *sent,
                                          const coap_pdu_t *recv,
                                          const coap_mid_t mid) {
    (void)sess; (void)sent; (void)mid;
    g_response_code = coap_pdu_get_code(recv);
    size_t len; const uint8_t *data;
    if (coap_get_data(recv, &len, &data)) {
        memcpy(g_response_buf, data, len);
        g_response_len = len;
    } else {
        g_response_len = 0;
    }
    g_response_received = 1;
    return COAP_RESPONSE_OK;
}

static int send_and_wait(coap_pdu_t *pdu) {
    g_response_received = 0;
    g_response_len      = 0;
    coap_mid_t mid = coap_send(g_session, pdu);
    if (mid == COAP_INVALID_MID) { printf("  ❌ Error enviando\n"); return 0; }
    int w = 0;
    while (!g_response_received && w < 5000) {
        coap_io_process(g_ctx, 100); w += 100;
    }
    if (!g_response_received) { printf("  ⏰ Timeout\n"); return 0; }
    printf("  📨 %d.%02d  ", COAP_RESPONSE_CLASS(g_response_code),
           g_response_code & 0x1F);
    return 1;
}

/* Añade query string con id del dispositivo */
static void add_device_query(coap_pdu_t *pdu) {
    char q[80];
    snprintf(q, sizeof(q), "id=%s", g_device_id);
    coap_add_option(pdu, COAP_OPTION_URI_QUERY, strlen(q), (uint8_t *)q);
}

/* ══════════════════════════════════════════════════════════
 * Mostrar datastore decodificado
 * ════════════════════════════════════════════════════════*/
static void print_datastore(const uint8_t *data, size_t len) {
    CoreconfValueT *ds = parse_get_response(data, len);
    if (!ds || ds->type != CORECONF_HASHMAP) {
        printf("(error decodificando)\n");
        if (ds) freeCoreconf(ds, true);
        return;
    }
    printf("\n");
    CoreconfHashMapT *map = ds->data.map_value;
    size_t shown = 0;
    for (size_t t = 0; t < HASHMAP_TABLE_SIZE; t++) {
        CoreconfObjectT *obj = map->table[t];
        while (obj) {
            printf("    SID %-8" PRIu64 " → ", obj->key);
            if (obj->value) {
                switch (obj->value->type) {
                    case CORECONF_REAL:    printf("%.4f",  obj->value->data.real_value); break;
                    case CORECONF_STRING:  printf("\"%s\"", obj->value->data.string_value); break;
                    case CORECONF_UINT_64: printf("%" PRIu64, obj->value->data.u64); break;
                    case CORECONF_INT_64:  printf("%" PRId64, obj->value->data.i64); break;
                    case CORECONF_UINT_32: printf("%" PRIu32, obj->value->data.u32); break;
                    case CORECONF_TRUE:    printf("true");  break;
                    case CORECONF_FALSE:   printf("false"); break;
                    case CORECONF_NULL:    printf("null");  break;
                    default: printf("(tipo %d)", obj->value->type); break;
                }
            }
            printf("\n");
            obj = obj->next;
            shown++;
        }
    }
    printf("    ── %zu SID(s) ──\n", shown);
    freeCoreconf(ds, true);
}

/* ══════════════════════════════════════════════════════════
 * Parsear valor desde string al tipo más adecuado
 * ════════════════════════════════════════════════════════*/
static CoreconfValueT *parse_value(const char *s) {
    /* true / false */
    if (strcmp(s, "true")  == 0) return createCoreconfBoolean(true);
    if (strcmp(s, "false") == 0) return createCoreconfBoolean(false);
    /* número con punto → real */
    char *ep;
    double d = strtod(s, &ep);
    if (ep != s && *ep == '\0') {
        if (strchr(s, '.') || strchr(s, 'e') || strchr(s, 'E'))
            return createCoreconfReal(d);
        /* entero */
        if (s[0] == '-') return createCoreconfInt64((int64_t)strtoll(s, NULL, 10));
        return createCoreconfUint64((uint64_t)strtoull(s, NULL, 10));
    }
    return createCoreconfString(s);
}

/* ══════════════════════════════════════════════════════════
 * Comandos
 * ════════════════════════════════════════════════════════*/

/* store <tipo> [valor] */
static void cmd_store(int argc, char **argv) {
    const char *tipo = (argc >= 1) ? argv[0] : g_device_type;
    double valor     = (argc >= 2) ? atof(argv[1]) : 22.5;

    strncpy(g_device_type, tipo, sizeof(g_device_type) - 1);

    CoreconfValueT  *data = createCoreconfHashmap();
    CoreconfHashMapT *map = data->data.map_value;
    insertCoreconfHashMap(map, 1,  createCoreconfString(g_device_id));
    insertCoreconfHashMap(map, 2,  createCoreconfString("cli"));
    insertCoreconfHashMap(map, 10, createCoreconfString(tipo));
    insertCoreconfHashMap(map, 11, createCoreconfUint64((uint64_t)time(NULL)));
    insertCoreconfHashMap(map, 20, createCoreconfReal(valor));
    insertCoreconfHashMap(map, 21, createCoreconfString("unit"));

    uint8_t buf[BUFFER_SIZE];
    zcbor_state_t enc[5];
    zcbor_new_encode_state(enc, 5, buf, BUFFER_SIZE, 0);
    coreconfToCBOR(data, enc);
    size_t len = (size_t)(enc[0].payload - buf);
    freeCoreconf(data, true);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_POST, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), 60);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    coap_add_data(pdu, len, buf);

    printf("  📤 POST /c (STORE, %zu bytes)... ", len);
    if (send_and_wait(pdu))
        printf("datastore creado con SIDs 10,11,20,21\n");
}

/* fetch <SID> [SID ...] */
static void cmd_fetch(int argc, char **argv) {
    if (argc == 0) { printf("  Uso: fetch <SID> [SID...]\n"); return; }

    uint64_t sids[64]; int n = 0;
    for (int i = 0; i < argc && i < 64; i++)
        sids[n++] = (uint64_t)strtoull(argv[i], NULL, 10);

    uint8_t buf[BUFFER_SIZE];
    size_t  len = create_fetch_request(buf, BUFFER_SIZE, sids, (size_t)n);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_FETCH, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    add_device_query(pdu);
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), COAP_MEDIA_TYPE_YANG_PATCH_CBOR);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    uint8_t acc[4]; size_t accl = coap_encode_var_safe(acc, sizeof(acc), COAP_MEDIA_TYPE_YANG_DATA_CBOR);
    coap_add_option(pdu, COAP_OPTION_ACCEPT, accl, acc);
    coap_add_data(pdu, len, buf);

    printf("  📤 FETCH /c (SIDs: ");
    for (int i = 0; i < n; i++) printf("%"PRIu64" ", sids[i]);
    printf(")... ");
    if (send_and_wait(pdu) && g_response_len > 0)
        print_datastore(g_response_buf, g_response_len);
    else if (g_response_len == 0)
        printf("sin payload\n");
}

/* get */
static void cmd_get(void) {
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    add_device_query(pdu);

    printf("  📤 GET /c... ");
    if (send_and_wait(pdu)) {
        if (g_response_len > 0)
            print_datastore(g_response_buf, g_response_len);
        else
            printf("sin payload\n");
    }
}

/* ipatch <SID> <valor> */
static void cmd_ipatch(int argc, char **argv) {
    if (argc < 2) { printf("  Uso: ipatch <SID> <valor>\n"); return; }

    uint64_t sid = strtoull(argv[0], NULL, 10);
    CoreconfValueT *val = parse_value(argv[1]);

    CoreconfValueT  *patch = createCoreconfHashmap();
    insertCoreconfHashMap(patch->data.map_value, sid, val);

    uint8_t buf[BUFFER_SIZE];
    size_t  len = create_ipatch_request(buf, BUFFER_SIZE, patch);
    freeCoreconf(patch, true);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_IPATCH, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    add_device_query(pdu);
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), COAP_MEDIA_TYPE_YANG_PATCH_CBOR);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    coap_add_data(pdu, len, buf);

    printf("  📤 iPATCH /c (SID %"PRIu64" = %s)... ", sid, argv[1]);
    if (send_and_wait(pdu)) printf("\n");
}

/* put <SID> <valor> [SID valor ...] */
static void cmd_put(int argc, char **argv) {
    if (argc < 2 || argc % 2 != 0) {
        printf("  Uso: put <SID> <valor> [SID valor ...]\n"); return;
    }

    CoreconfValueT  *ds  = createCoreconfHashmap();
    CoreconfHashMapT *map = ds->data.map_value;
    for (int i = 0; i < argc; i += 2) {
        uint64_t sid = strtoull(argv[i], NULL, 10);
        insertCoreconfHashMap(map, sid, parse_value(argv[i + 1]));
    }

    uint8_t buf[BUFFER_SIZE];
    size_t  len = create_put_request(buf, BUFFER_SIZE, ds);
    freeCoreconf(ds, true);

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_PUT, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    add_device_query(pdu);
    uint8_t cf[4]; size_t cfl = coap_encode_var_safe(cf, sizeof(cf), COAP_MEDIA_TYPE_YANG_DATA_CBOR);
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, cfl, cf);
    coap_add_data(pdu, len, buf);

    printf("  📤 PUT /c (%d SIDs)... ", argc / 2);
    if (send_and_wait(pdu)) printf("\n");
}

/* delete [SID [SID...]] */
static void cmd_delete(int argc, char **argv) {
    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_DELETE, g_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
    add_device_query(pdu);

    if (argc > 0) {
        /* Construir query: id=X&k=SID1&k=SID2 */
        char q[256];
        int  pos = snprintf(q, sizeof(q), "id=%s", g_device_id);
        for (int i = 0; i < argc && pos < (int)sizeof(q) - 16; i++)
            pos += snprintf(q + pos, sizeof(q) - pos, "&k=%s", argv[i]);
        /* Reemplazar la opción query: necesitamos un nuevo pdu */
        coap_pdu_t *pdu2 = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_DELETE, g_session);
        coap_add_option(pdu2, COAP_OPTION_URI_PATH, 1, (uint8_t *)"c");
        /* Añadir id= y cada k= como opciones separadas */
        char id_q[80]; snprintf(id_q, sizeof(id_q), "id=%s", g_device_id);
        coap_add_option(pdu2, COAP_OPTION_URI_QUERY, strlen(id_q), (uint8_t *)id_q);
        for (int i = 0; i < argc; i++) {
            char kq[32]; snprintf(kq, sizeof(kq), "k=%s", argv[i]);
            coap_add_option(pdu2, COAP_OPTION_URI_QUERY, strlen(kq), (uint8_t *)kq);
        }
        /* Liberar el primero, usar el segundo */
        /* coap_delete_pdu(pdu); — no necesario, send libera en error */
        printf("  📤 DELETE /c?%s... ", q);
        if (send_and_wait(pdu2)) printf("\n");
        return;
    }

    printf("  📤 DELETE /c (datastore completo)... ");
    if (send_and_wait(pdu)) printf("\n");
}

/* ══════════════════════════════════════════════════════════
 * REPL
 * ════════════════════════════════════════════════════════*/
static void print_help(void) {
    printf("\n  Comandos disponibles:\n");
    printf("  ─────────────────────────────────────────────────────\n");
    printf("  store  [tipo] [valor]        POST /c — cargar datos\n");
    printf("  fetch  <SID> [SID...]        FETCH /c — leer SIDs\n");
    printf("  get                          GET /c — datastore completo\n");
    printf("  ipatch <SID> <valor>         iPATCH — actualizar un SID\n");
    printf("  put    <SID> <v> [SID <v>]   PUT — reemplazar datastore\n");
    printf("  delete [SID [SID...]]        DELETE — borrar SIDs o todo\n");
    printf("  info                         mostrar configuración actual\n");
    printf("  quit / exit                  salir\n");
    printf("  ─────────────────────────────────────────────────────\n");
    printf("  Ejemplos:\n");
    printf("    store temperature 22.5\n");
    printf("    get\n");
    printf("    fetch 10 20\n");
    printf("    ipatch 20 99.9\n");
    printf("    put 30 humidity 31 50.0\n");
    printf("    delete 20 21\n");
    printf("    delete\n\n");
}

static void tokenize(char *line, char **argv, int *argc) {
    *argc = 0;
    char *p = line;
    while (*p && *argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\n') break;
        argv[(*argc)++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        if (*p) *p++ = '\0';
    }
}

/* ════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════*/
int main(int argc, char *argv[]) {
    if (argc >= 2) strncpy(g_device_type, argv[1], sizeof(g_device_type) - 1);
    if (argc >= 3) strncpy(g_device_id,   argv[2], sizeof(g_device_id)   - 1);

    const char *host     = getenv("GATEWAY_HOST");
    const char *port_str = getenv("GATEWAY_PORT");
    if (!host) host = "127.0.0.1";
    int port = port_str ? atoi(port_str) : 5683;

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   CORECONF CLI — cliente interactivo RFC 9254               ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("  Device:  %s (%s)\n", g_device_id, g_device_type);
    printf("  Gateway: %s:%d\n", host, port);
    printf("  Escribe 'help' para ver los comandos.\n\n");

    coap_startup();
    coap_set_log_level(COAP_LOG_ERR);

    coap_address_t dst;
    coap_address_init(&dst);
    dst.size = sizeof(struct sockaddr_in);
    dst.addr.sin.sin_family = AF_INET;
    dst.addr.sin.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &dst.addr.sin.sin_addr) != 1) {
        fprintf(stderr, "IP inválida: %s\n", host); return 1;
    }

    g_ctx     = coap_new_context(NULL);
    g_session = g_ctx ? coap_new_client_session(g_ctx, NULL, &dst, COAP_PROTO_UDP) : NULL;
    if (!g_ctx || !g_session) {
        fprintf(stderr, "Error creando sesión CoAP\n"); return 1;
    }
    coap_register_response_handler(g_ctx, response_handler);

    /* ── REPL ── */
    char line[512];
    while (1) {
        printf("%s> ", g_device_id);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break; /* EOF / Ctrl-D */

        /* Trim */
        size_t ll = strlen(line);
        while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r')) line[--ll] = '\0';
        if (ll == 0) continue;

        char *args[MAX_ARGS]; int nargs = 0;
        tokenize(line, args, &nargs);
        if (nargs == 0) continue;

        const char *cmd = args[0];

        if      (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
            print_help();
        else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0)
            break;
        else if (strcmp(cmd, "info") == 0)
            printf("  device_id=%s  type=%s  gateway=%s:%d\n",
                   g_device_id, g_device_type, host, port);
        else if (strcmp(cmd, "store")  == 0) cmd_store (nargs-1, args+1);
        else if (strcmp(cmd, "fetch")  == 0) cmd_fetch (nargs-1, args+1);
        else if (strcmp(cmd, "get")    == 0) cmd_get   ();
        else if (strcmp(cmd, "ipatch") == 0) cmd_ipatch(nargs-1, args+1);
        else if (strcmp(cmd, "put")    == 0) cmd_put   (nargs-1, args+1);
        else if (strcmp(cmd, "delete") == 0) cmd_delete(nargs-1, args+1);
        else
            printf("  Comando desconocido: '%s'  (escribe 'help')\n", cmd);
    }

    printf("\nSaliendo...\n");
    coap_session_release(g_session);
    coap_free_context(g_ctx);
    coap_cleanup();
    return 0;
}
