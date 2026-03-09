/**
 * sids.h — SID assignments for CORECONF modules
 *
 * Extracted from:
 *   - ietf-interfaces@2018-02-20.sid  (RFC 8343)
 *   - ietf-system@2014-08-06.sid      (RFC 7317)
 *
 * Format: SID file JSON → #define constants
 * Reference: draft-ietf-core-comi-20 §3.3.1 examples
 *
 * SID encoding in CBOR maps uses delta compression (RFC 9254 §3.2):
 *   delta = SID_child - SID_parent
 */

#ifndef SIDS_H
#define SIDS_H

/* ═══════════════════════════════════════════════════
 * module: ietf-interfaces (RFC 8343)
 * module SID base: 1500
 * ═══════════════════════════════════════════════════*/
#define SID_IETF_INTERFACES_MODULE          1500

/* /ietf-interfaces:interfaces/interface  (list) */
#define SID_IF_INTERFACE                    1533
/* children — delta relative to SID_IF_INTERFACE */
#define SID_IF_DESCRIPTION                  1534  /* delta  1 */
#define SID_IF_ENABLED                      1535  /* delta  2 */
#define SID_IF_NAME                         1537  /* delta  4  (key) */
#define SID_IF_TYPE                         1538  /* delta  5 */
#define SID_IF_OPER_STATUS                  1544  /* delta 11 */

/* identity: ethernetCsmacd */
#define SID_IF_IDENTITY_ETHERNET_CSMACD     1880

/* deltas dentro del mapa de una entrada interface */
#define DELTA_IF_DESCRIPTION    (SID_IF_DESCRIPTION  - SID_IF_INTERFACE)  /*  1 */
#define DELTA_IF_ENABLED        (SID_IF_ENABLED       - SID_IF_INTERFACE)  /*  2 */
#define DELTA_IF_NAME           (SID_IF_NAME          - SID_IF_INTERFACE)  /*  4 */
#define DELTA_IF_TYPE           (SID_IF_TYPE          - SID_IF_INTERFACE)  /*  5 */
#define DELTA_IF_OPER_STATUS    (SID_IF_OPER_STATUS   - SID_IF_INTERFACE)  /* 11 */

/* ═══════════════════════════════════════════════════
 * module: ietf-system (RFC 7317)
 * module SID base: 1700
 * ═══════════════════════════════════════════════════*/
#define SID_IETF_SYSTEM_MODULE              1700

/* /ietf-system:system/clock  (container) */
#define SID_SYS_CLOCK                       1721
/* children — delta relative to SID_SYS_CLOCK */
#define SID_SYS_CLOCK_BOOT_DATETIME         1722  /* delta 1 */
#define SID_SYS_CLOCK_CURRENT_DATETIME      1723  /* delta 2 */

#define DELTA_CLOCK_BOOT_DATETIME    (SID_SYS_CLOCK_BOOT_DATETIME    - SID_SYS_CLOCK)  /* 1 */
#define DELTA_CLOCK_CURRENT_DATETIME (SID_SYS_CLOCK_CURRENT_DATETIME - SID_SYS_CLOCK)  /* 2 */

/* /ietf-system:system/ntp/enabled  (leaf) */
#define SID_SYS_NTP_ENABLED                 1755

/* /ietf-system:system/ntp/server  (list) */
#define SID_SYS_NTP_SERVER                  1756
/* children — delta relative to SID_SYS_NTP_SERVER */
#define SID_SYS_NTP_SERVER_NAME             1759  /* delta 3  (key) */
#define SID_SYS_NTP_SERVER_PREFER           1760  /* delta 4 */
#define SID_SYS_NTP_SERVER_UDP              1761  /* delta 5 */
#define SID_SYS_NTP_SERVER_UDP_ADDRESS      1762  /* delta 1 relative to SID_SYS_NTP_SERVER_UDP */

#define DELTA_NTP_SERVER_NAME    (SID_SYS_NTP_SERVER_NAME    - SID_SYS_NTP_SERVER)  /* 3 */
#define DELTA_NTP_SERVER_PREFER  (SID_SYS_NTP_SERVER_PREFER  - SID_SYS_NTP_SERVER)  /* 4 */
#define DELTA_NTP_SERVER_UDP     (SID_SYS_NTP_SERVER_UDP     - SID_SYS_NTP_SERVER)  /* 5 */
#define DELTA_NTP_UDP_ADDRESS    (SID_SYS_NTP_SERVER_UDP_ADDRESS - SID_SYS_NTP_SERVER_UDP) /* 1 */

#endif /* SIDS_H */
