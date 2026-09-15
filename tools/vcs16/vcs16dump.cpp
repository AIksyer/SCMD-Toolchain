#include "vcs16/vcs16.h"

#include <iomanip>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 2) { std::cerr << "Usage: vcs16dump program.vxe\n"; return 2; }
    char error[512]{};
    vcs16_module_t *m = nullptr;
    const auto r = vcs16_module_load(argv[1], &m, error, sizeof(error));
    if (r != VCS16_OK) { std::cerr << "vcs16dump: " << (error[0] ? error : vcs16_result_string(r)) << '\n'; return 1; }
    std::cout << "vCS-16/2 VXE2 entry=" << vcs16_module_entry(m) << " memory=" << vcs16_module_memory_bytes(m)
              << " instructions=" << vcs16_module_instruction_count(m) << '\n';
    const auto *code = vcs16_module_instructions(m);
    for (size_t i=0;i<vcs16_module_instruction_count(m);++i) {
        const auto &x=code[i];
        std::cout << std::setw(4) << i << "  " << std::left << std::setw(6)
                  << vcs16_opcode_name(static_cast<vcs16_opcode_t>(x.opcode)) << std::right
                  << " a=" << unsigned(x.a) << " b=" << unsigned(x.b)
                  << " imm=" << x.imm << " target=" << x.target << '\n';
    }
    vcs16_module_destroy(m);
    return 0;
}
