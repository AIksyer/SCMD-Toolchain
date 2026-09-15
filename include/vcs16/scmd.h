#ifndef VCS16_SCMD_H
#define VCS16_SCMD_H

#include "vcs16/vcs16.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vcs16_scmd_config {
    const char *prefix;       /* ASCII identifier prefix, default: vcs */
    uint16_t call_depth;      /* compile-time CALL stack slots, default: 16 */
    uint16_t memory_limit;    /* max byte-addressed backing for LD8/ST8, default: 256 */
} vcs16_scmd_config_t;

/* AOT-lower a vCS-16/2 module to SCMD source. The generated source exposes
 * `export function <prefix>_run()` and expects the embedding program to define
 * `function <prefix>_host_syscall()`.  Registers are visible as
 * <prefix>_rN_lo/<prefix>_rN_hi so a firmware/OS host can implement SYS.
 * The host writes the architectural SYS return value to
 * <prefix>_sysret_lo/<prefix>_sysret_hi; the AOT runtime commits it to r0.
 *
 * This backend intentionally removes runtime opcode fetch/decode. Architectural
 * PC/register state remains observable, but each instruction is emitted as
 * direct SCMD control/data flow. */
vcs16_result_t vcs16_scmd_write_source(const vcs16_module_t *module,
                                        const char *path,
                                        const vcs16_scmd_config_t *config,
                                        char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
