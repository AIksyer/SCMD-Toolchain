#ifndef VCS16_VCS16_H
#define VCS16_VCS16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VCS16_ARCH_VERSION 2u
#define VCS16_ABI_VERSION 1u
#define VCS16_REGISTER_COUNT 8u

typedef struct vcs16_module vcs16_module_t;
typedef struct vcs16_vm vcs16_vm_t;

typedef enum vcs16_result {
    VCS16_OK = 0,
    VCS16_ERR_ARGUMENT = 1,
    VCS16_ERR_PARSE = 2,
    VCS16_ERR_VERIFY = 3,
    VCS16_ERR_IO = 4,
    VCS16_ERR_OOM = 5,
    VCS16_ERR_TRAP = 6,
    VCS16_ERR_BUDGET = 7
} vcs16_result_t;

typedef enum vcs16_opcode {
    VCS16_OP_NOP = 0,
    VCS16_OP_MOV,
    VCS16_OP_LDI,
    VCS16_OP_ADD,
    VCS16_OP_SUB,
    VCS16_OP_AND,
    VCS16_OP_OR,
    VCS16_OP_XOR,
    VCS16_OP_ADDI,
    VCS16_OP_SUBI,
    VCS16_OP_LD8,
    VCS16_OP_ST8,
    VCS16_OP_JMP,
    VCS16_OP_JEQ,
    VCS16_OP_JNE,
    VCS16_OP_JLT,
    VCS16_OP_JGE,
    VCS16_OP_CALL,
    VCS16_OP_RET,
    VCS16_OP_SYS,
    VCS16_OP_HALT
} vcs16_opcode_t;

typedef struct vcs16_instruction {
    uint8_t opcode;
    uint8_t a;
    uint8_t b;
    uint8_t reserved;
    uint16_t imm;
    uint16_t target;
} vcs16_instruction_t;

typedef uint16_t (*vcs16_syscall_fn)(void *userdata, vcs16_vm_t *vm, uint16_t number);
typedef void (*vcs16_trap_fn)(void *userdata, vcs16_vm_t *vm, const char *message);

typedef struct vcs16_host {
    void *userdata;
    vcs16_syscall_fn syscall;
    vcs16_trap_fn trap;
} vcs16_host_t;

typedef struct vcs16_vm_config {
    uint16_t memory_bytes; /* 0 => module/default size. Max 65535. */
    uint16_t call_depth;   /* 0 => 64. */
} vcs16_vm_config_t;

/* Module / assembler API. */
vcs16_result_t vcs16_assemble(const char *source, vcs16_module_t **out_module,
                               char *error, size_t error_size);
vcs16_result_t vcs16_module_verify(const vcs16_module_t *module,
                                    char *error, size_t error_size);
vcs16_result_t vcs16_module_save(const vcs16_module_t *module, const char *path,
                                  char *error, size_t error_size);
vcs16_result_t vcs16_module_load(const char *path, vcs16_module_t **out_module,
                                  char *error, size_t error_size);
void vcs16_module_destroy(vcs16_module_t *module);

uint16_t vcs16_module_entry(const vcs16_module_t *module);
uint16_t vcs16_module_memory_bytes(const vcs16_module_t *module);
size_t vcs16_module_instruction_count(const vcs16_module_t *module);
const vcs16_instruction_t *vcs16_module_instructions(const vcs16_module_t *module);

/* Reference VM. The architecture deliberately has no FLAGS register: branches
 * compare operands directly, which maps much better to CFG/alias runtimes. */
vcs16_vm_t *vcs16_vm_create(const vcs16_module_t *module, const vcs16_vm_config_t *config,
                             const vcs16_host_t *host, char *error, size_t error_size);
void vcs16_vm_destroy(vcs16_vm_t *vm);
vcs16_result_t vcs16_vm_reset(vcs16_vm_t *vm);
vcs16_result_t vcs16_vm_step(vcs16_vm_t *vm);
vcs16_result_t vcs16_vm_run(vcs16_vm_t *vm, uint64_t instruction_budget);

uint16_t vcs16_vm_get_reg(const vcs16_vm_t *vm, unsigned index);
void vcs16_vm_set_reg(vcs16_vm_t *vm, unsigned index, uint16_t value);
uint16_t vcs16_vm_get_pc(const vcs16_vm_t *vm);
int vcs16_vm_halted(const vcs16_vm_t *vm);
uint8_t *vcs16_vm_memory(vcs16_vm_t *vm);
size_t vcs16_vm_memory_size(const vcs16_vm_t *vm);

const char *vcs16_result_string(vcs16_result_t result);
const char *vcs16_opcode_name(vcs16_opcode_t opcode);

#ifdef __cplusplus
}
#endif

#endif
