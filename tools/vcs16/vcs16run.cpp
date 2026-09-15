#include "vcs16/vcs16.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

struct HostState { int exit_code = 0; };

static uint16_t host_syscall(void *userdata, vcs16_vm_t *vm, uint16_t number) {
    auto *host = static_cast<HostState *>(userdata);
    switch (number) {
        case 0: host->exit_code = static_cast<int>(vcs16_vm_get_reg(vm, 0) & 0xFFu); return 0;
        case 1: std::cout << static_cast<char>(vcs16_vm_get_reg(vm, 0) & 0xFFu); return 1;
        case 2: std::cout << vcs16_vm_get_reg(vm, 0); return 1;
        case 3: std::cout << '\n'; return 1;
        default: return 0xFFFFu;
    }
}

static void host_trap(void *, vcs16_vm_t *, const char *message) {
    std::cerr << "vCS-16/2 trap: " << (message ? message : "unknown") << '\n';
}

static std::string read_all(const char *path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: vcs16run program.vxe|program.vcs [budget]\n";
        return 2;
    }
    char error[512]{};
    vcs16_module_t *module = nullptr;
    std::string path = argv[1];
    vcs16_result_t r;
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".vxe") {
        r = vcs16_module_load(argv[1], &module, error, sizeof(error));
    } else {
        const std::string source = read_all(argv[1]);
        r = source.empty() ? VCS16_ERR_IO : vcs16_assemble(source.c_str(), &module, error, sizeof(error));
    }
    if (r != VCS16_OK) {
        std::cerr << "vcs16run: " << (error[0] ? error : vcs16_result_string(r)) << '\n';
        return 1;
    }
    uint64_t budget = 1000000u;
    if (argc == 3) budget = std::stoull(argv[2]);
    HostState state{};
    vcs16_host_t host{&state, host_syscall, host_trap};
    vcs16_vm_t *vm = vcs16_vm_create(module, nullptr, &host, error, sizeof(error));
    if (!vm) {
        std::cerr << "vcs16run: " << error << '\n';
        vcs16_module_destroy(module);
        return 1;
    }
    r = vcs16_vm_run(vm, budget);
    if (r != VCS16_OK) std::cerr << "vcs16run: " << vcs16_result_string(r) << " at pc=" << vcs16_vm_get_pc(vm) << '\n';
    vcs16_vm_destroy(vm);
    vcs16_module_destroy(module);
    return r == VCS16_OK ? state.exit_code : 1;
}
