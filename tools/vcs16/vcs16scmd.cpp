#include "vcs16/scmd.h"
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
    if (argc != 6 || std::string(argv[2]) != "-o" || std::string(argv[4]) != "--prefix") {
        std::cerr << "Usage: vcs16scmd input.vcs|input.vxe -o output.scmd --prefix name\n";
        return 2;
    }
    char error[512]{};
    vcs16_module_t *module = nullptr;
    const std::string inpath = argv[1];
    vcs16_result_t r = VCS16_OK;
    if (inpath.size() >= 4u && inpath.substr(inpath.size()-4u) == ".vxe") {
        r = vcs16_module_load(argv[1], &module, error, sizeof(error));
    } else {
        const std::string source = read_all(argv[1]);
        r = source.empty() ? VCS16_ERR_IO : vcs16_assemble(source.c_str(), &module, error, sizeof(error));
    }
    if (r != VCS16_OK) {
        std::cerr << "vcs16scmd: " << (error[0] ? error : vcs16_result_string(r)) << '\n';
        return 1;
    }
    vcs16_scmd_config_t cfg{};
    cfg.prefix = argv[5];
    cfg.call_depth = 16;
    cfg.memory_limit = 256;
    r = vcs16_scmd_write_source(module, argv[3], &cfg, error, sizeof(error));
    vcs16_module_destroy(module);
    if (r != VCS16_OK) {
        std::cerr << "vcs16scmd: " << (error[0] ? error : vcs16_result_string(r)) << '\n';
        return 1;
    }
    return 0;
}
