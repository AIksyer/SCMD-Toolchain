#include "vcs16/vcs16.h"

#include <stdio.h>
#include <string.h>

typedef struct host_state {
    int saw_arg;
    int passed;
} host_state_t;

static uint16_t test_syscall(void *userdata, vcs16_vm_t *vm, uint16_t number) {
    host_state_t *state = (host_state_t *)userdata;
    if (number == 9u) {
        if (vcs16_vm_get_reg(vm, 0) == 42u) state->saw_arg = 1;
        return 123u;
    }
    if (number == 10u) {
        if (state->saw_arg && vcs16_vm_get_reg(vm, 0) == 1u) state->passed = 1;
        return 0u;
    }
    return 0xffffu;
}

int main(void) {
    static const char source[] =
        ".memory 32\n"
        ".entry start\n"
        "start:\n"
        "  ldi r0, 42\n"
        "  sys 9\n"
        "  ldi r1, 123\n"
        "  jne r0, r1, bad\n"
        "  ldi r0, 1\n"
        "  sys 10\n"
        "  halt\n"
        "bad:\n"
        "  halt\n";
    char error[256] = {0};
    vcs16_module_t *module = NULL;
    if (vcs16_assemble(source, &module, error, sizeof(error)) != VCS16_OK) {
        fprintf(stderr, "assemble: %s\n", error);
        return 1;
    }
    host_state_t state = {0, 0};
    vcs16_host_t host = {&state, test_syscall, NULL};
    vcs16_vm_t *vm = vcs16_vm_create(module, NULL, &host, error, sizeof(error));
    if (!vm) {
        fprintf(stderr, "vm: %s\n", error);
        vcs16_module_destroy(module);
        return 1;
    }
    const vcs16_result_t r = vcs16_vm_run(vm, 1000u);
    const int ok = r == VCS16_OK && state.passed && vcs16_vm_halted(vm);
    vcs16_vm_destroy(vm);
    vcs16_module_destroy(module);
    if (!ok) return 1;
    puts("VCS16_C_API_PASS");
    return 0;
}
