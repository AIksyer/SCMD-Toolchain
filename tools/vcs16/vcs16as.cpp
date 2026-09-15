#include "vcs16/vcs16.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string read_all(const char *path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

int main(int argc, char **argv) {
    if (argc != 4 || std::string(argv[2]) != "-o") {
        std::cerr << "Usage: vcs16as input.vcs -o output.vxe\n";
        return 2;
    }
    const std::string source = read_all(argv[1]);
    if (source.empty()) {
        std::cerr << "vcs16as: cannot read or empty input: " << argv[1] << '\n';
        return 1;
    }
    char error[512]{};
    vcs16_module_t *module = nullptr;
    vcs16_result_t r = vcs16_assemble(source.c_str(), &module, error, sizeof(error));
    if (r != VCS16_OK) {
        std::cerr << "vcs16as: " << (error[0] ? error : vcs16_result_string(r)) << '\n';
        return 1;
    }
    r = vcs16_module_save(module, argv[3], error, sizeof(error));
    vcs16_module_destroy(module);
    if (r != VCS16_OK) {
        std::cerr << "vcs16as: " << (error[0] ? error : vcs16_result_string(r)) << '\n';
        return 1;
    }
    return 0;
}
