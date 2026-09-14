#ifndef SCMD_BYTECODE_H
#define SCMD_BYTECODE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCMD_SCB_ABI_VERSION 1

/* Compile every .cfg under cfg_root into a verified SCB package. */
bool scmd_bytecode_pack_cfg_root(const char *cfg_root,
                                 const char *output_scb,
                                 const char *profile);

#ifdef __cplusplus
}
#endif

#endif
