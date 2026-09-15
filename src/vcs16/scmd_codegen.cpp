#include "vcs16/scmd.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

static void set_error(char *error, size_t error_size, const std::string &text) {
    if (!error || error_size == 0) return;
    const size_t n = std::min(error_size - 1u, text.size());
    std::copy_n(text.data(), n, error);
    error[n] = '\0';
}

static bool identifier(const std::string &s) {
    if (s.empty()) return false;
    const auto first = static_cast<unsigned char>(s.front());
    if (!(std::isalpha(first) || s.front() == '_')) return false;
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '_')) return false;
    }
    return true;
}

static std::string lo(const std::string &p, unsigned r) { return p + "_r" + std::to_string(r) + "_lo"; }
static std::string hi(const std::string &p, unsigned r) { return p + "_r" + std::to_string(r) + "_hi"; }
static unsigned low8(uint16_t v) { return static_cast<unsigned>(v & 0xffu); }
static unsigned high8(uint16_t v) { return static_cast<unsigned>((v >> 8u) & 0xffu); }

static bool module_uses_memory(const vcs16_module_t *m) {
    const auto *code = vcs16_module_instructions(m);
    for (size_t i = 0; i < vcs16_module_instruction_count(m); ++i) {
        if (code[i].opcode == VCS16_OP_LD8 || code[i].opcode == VCS16_OP_ST8) return true;
    }
    return false;
}

static void set_pc(std::ostringstream &o, const std::string &p, uint16_t pc, const char *indent = "        ") {
    o << indent << p << "_pc_lo = " << low8(pc) << "; " << p << "_pc_hi = " << high8(pc) << ";\n";
}

static void emit_effective_addr(std::ostringstream &o, const std::string &p, const vcs16_instruction_t &x) {
    o << "        " << p << "_addr_lo = " << lo(p, x.b) << "; " << p << "_addr_hi = " << hi(p, x.b) << ";\n";
    o << "        " << p << "_tmp = " << p << "_addr_lo; " << p << "_addr_lo += " << low8(x.imm) << ";\n";
    if (low8(x.imm) != 0u) o << "        if(" << p << "_addr_lo < " << p << "_tmp) { " << p << "_addr_hi += 1; }\n";
    if (high8(x.imm) != 0u) o << "        " << p << "_addr_hi += " << high8(x.imm) << ";\n";
}

static void emit_add_reg(std::ostringstream &o, const std::string &p, unsigned a, unsigned b) {
    o << "        " << p << "_tmp = " << lo(p,a) << "; " << lo(p,a) << " += " << lo(p,b) << ";\n";
    o << "        if(" << lo(p,a) << " < " << p << "_tmp) { " << hi(p,a) << " += 1; }\n";
    o << "        " << hi(p,a) << " += " << hi(p,b) << ";\n";
}

static void emit_sub_reg(std::ostringstream &o, const std::string &p, unsigned a, unsigned b) {
    o << "        " << p << "_tmp = " << lo(p,a) << "; " << lo(p,a) << " -= " << lo(p,b) << ";\n";
    o << "        if(" << p << "_tmp < " << lo(p,b) << ") { " << hi(p,a) << " -= 1; }\n";
    o << "        " << hi(p,a) << " -= " << hi(p,b) << ";\n";
}

static void emit_add_imm(std::ostringstream &o, const std::string &p, unsigned a, uint16_t imm) {
    o << "        " << p << "_tmp = " << lo(p,a) << "; " << lo(p,a) << " += " << low8(imm) << ";\n";
    if (low8(imm) != 0u) o << "        if(" << lo(p,a) << " < " << p << "_tmp) { " << hi(p,a) << " += 1; }\n";
    if (high8(imm) != 0u) o << "        " << hi(p,a) << " += " << high8(imm) << ";\n";
}

static void emit_sub_imm(std::ostringstream &o, const std::string &p, unsigned a, uint16_t imm) {
    o << "        " << p << "_tmp = " << lo(p,a) << "; " << lo(p,a) << " -= " << low8(imm) << ";\n";
    if (low8(imm) != 0u) o << "        if(" << p << "_tmp < " << low8(imm) << ") { " << hi(p,a) << " -= 1; }\n";
    if (high8(imm) != 0u) o << "        " << hi(p,a) << " -= " << high8(imm) << ";\n";
}

static void emit_compare_branch(std::ostringstream &o, const std::string &p, const vcs16_instruction_t &x,
                                const char *condition) {
    o << "        if(" << condition << ") { " << p << "_pc_lo = " << low8(x.target)
      << "; " << p << "_pc_hi = " << high8(x.target) << "; }\n";
}

} // namespace

extern "C" vcs16_result_t vcs16_scmd_write_source(const vcs16_module_t *module,
                                                    const char *path,
                                                    const vcs16_scmd_config_t *config,
                                                    char *error, size_t error_size) {
    if (!module || !path) return VCS16_ERR_ARGUMENT;
    vcs16_result_t vr = vcs16_module_verify(module, error, error_size);
    if (vr != VCS16_OK) return vr;

    const std::string p = config && config->prefix ? config->prefix : "vcs";
    if (!identifier(p)) {
        set_error(error, error_size, "SCMD prefix must be an ASCII identifier");
        return VCS16_ERR_ARGUMENT;
    }
    const unsigned call_depth = config && config->call_depth ? config->call_depth : 16u;
    const unsigned memory_limit = config && config->memory_limit ? config->memory_limit : 256u;
    if (call_depth == 0u || call_depth > 255u) {
        set_error(error, error_size, "SCMD call depth must be 1..255");
        return VCS16_ERR_ARGUMENT;
    }
    if (module_uses_memory(module) && vcs16_module_memory_bytes(module) > memory_limit) {
        set_error(error, error_size, "module memory exceeds SCMD AOT memory_limit");
        return VCS16_ERR_VERIFY;
    }
    if (module_uses_memory(module) && vcs16_module_memory_bytes(module) > 256u) {
        set_error(error, error_size, "SCMD AOT v1 currently supports at most 256 byte-addressed bytes");
        return VCS16_ERR_VERIFY;
    }

    std::ostringstream o;
    o << "// Generated by libvcs16_scmd. vCS-16/2 architectural state, direct SCMD AOT.\n";
    o << "// The embedding host must define: function " << p << "_host_syscall()\n";
    for (unsigned r = 0; r < VCS16_REGISTER_COUNT; ++r) {
        o << "u8 " << lo(p,r) << " = 0; u8 " << hi(p,r) << " = 0;\n";
    }
    o << "u8 " << p << "_pc_lo = 0; u8 " << p << "_pc_hi = 0;\n";
    o << "u8 " << p << "_sys_lo = 0; u8 " << p << "_sys_hi = 0; u8 " << p << "_sysret_lo = 0; u8 " << p << "_sysret_hi = 0;\n";
    o << "u8 " << p << "_call_sp = 0; bool " << p << "_halted = false; bool " << p << "_trapped = false;\n";
    o << "u8 " << p << "_tmp = 0; bool " << p << "_dispatched = false;\n";
    for (unsigned i = 0; i < call_depth; ++i) {
        o << "u8 " << p << "_call" << i << "_lo = 0; u8 " << p << "_call" << i << "_hi = 0;\n";
    }

    const unsigned memory_bytes = module_uses_memory(module) ? vcs16_module_memory_bytes(module) : 0u;
    if (memory_bytes) {
        o << "u8 " << p << "_addr_lo = 0; u8 " << p << "_addr_hi = 0; u8 " << p << "_mem_value = 0;\n";
        for (unsigned i = 0; i < memory_bytes; ++i) o << "u8 " << p << "_mem" << i << " = 0;\n";
        o << "\nfunction " << p << "_mem_read()\n{\n";
        o << "    " << p << "_mem_value = 0; if(" << p << "_addr_hi != 0) { " << p << "_trapped = true; return; }\n";
        for (unsigned i = 0; i < memory_bytes; ++i) o << "    if(" << p << "_addr_lo == " << i << ") { " << p << "_mem_value = " << p << "_mem" << i << "; return; }\n";
        o << "    " << p << "_trapped = true;\n}\n\n";
        o << "function " << p << "_mem_write()\n{\n";
        o << "    if(" << p << "_addr_hi != 0) { " << p << "_trapped = true; return; }\n";
        for (unsigned i = 0; i < memory_bytes; ++i) o << "    if(" << p << "_addr_lo == " << i << ") { " << p << "_mem" << i << " = " << p << "_mem_value; return; }\n";
        o << "    " << p << "_trapped = true;\n}\n\n";
    }

    o << "export function " << p << "_run()\n{\n";
    for (unsigned r = 0; r < VCS16_REGISTER_COUNT; ++r) o << "    " << lo(p,r) << " = 0; " << hi(p,r) << " = 0;\n";
    set_pc(o, p, vcs16_module_entry(module), "    ");
    o << "    " << p << "_call_sp = 0; " << p << "_halted = false; " << p << "_trapped = false;\n";
    for (unsigned i = 0; i < memory_bytes; ++i) o << "    " << p << "_mem" << i << " = 0;\n";
    o << "    while(!" << p << "_halted && !" << p << "_trapped)\n    {\n";
    o << "        " << p << "_dispatched = false;\n";

    const auto *code = vcs16_module_instructions(module);
    const size_t count = vcs16_module_instruction_count(module);
    for (size_t i = 0; i < count; ++i) {
        const auto &x = code[i];
        const auto op = static_cast<vcs16_opcode_t>(x.opcode);
        const uint16_t next = static_cast<uint16_t>(i + 1u);
        o << "        if(!" << p << "_dispatched && " << p << "_pc_hi == " << high8(static_cast<uint16_t>(i))
          << " && " << p << "_pc_lo == " << low8(static_cast<uint16_t>(i)) << ")\n        {\n";
        set_pc(o, p, next);
        switch (op) {
            case VCS16_OP_NOP: break;
            case VCS16_OP_MOV:
                o << "        " << lo(p,x.a) << " = " << lo(p,x.b) << "; " << hi(p,x.a) << " = " << hi(p,x.b) << ";\n"; break;
            case VCS16_OP_LDI:
                o << "        " << lo(p,x.a) << " = " << low8(x.imm) << "; " << hi(p,x.a) << " = " << high8(x.imm) << ";\n"; break;
            case VCS16_OP_ADD: emit_add_reg(o,p,x.a,x.b); break;
            case VCS16_OP_SUB: emit_sub_reg(o,p,x.a,x.b); break;
            case VCS16_OP_AND:
                o << "        " << lo(p,x.a) << " = " << lo(p,x.a) << " & " << lo(p,x.b) << "; " << hi(p,x.a) << " = " << hi(p,x.a) << " & " << hi(p,x.b) << ";\n"; break;
            case VCS16_OP_OR:
                o << "        " << lo(p,x.a) << " = " << lo(p,x.a) << " | " << lo(p,x.b) << "; " << hi(p,x.a) << " = " << hi(p,x.a) << " | " << hi(p,x.b) << ";\n"; break;
            case VCS16_OP_XOR:
                o << "        " << lo(p,x.a) << " = " << lo(p,x.a) << " ^ " << lo(p,x.b) << "; " << hi(p,x.a) << " = " << hi(p,x.a) << " ^ " << hi(p,x.b) << ";\n"; break;
            case VCS16_OP_ADDI: emit_add_imm(o,p,x.a,x.imm); break;
            case VCS16_OP_SUBI: emit_sub_imm(o,p,x.a,x.imm); break;
            case VCS16_OP_LD8:
                emit_effective_addr(o,p,x);
                o << "        " << p << "_mem_read(); if(!" << p << "_trapped) { " << lo(p,x.a) << " = " << p << "_mem_value; " << hi(p,x.a) << " = 0; }\n"; break;
            case VCS16_OP_ST8:
                emit_effective_addr(o,p,x);
                o << "        " << p << "_mem_value = " << lo(p,x.a) << "; " << p << "_mem_write();\n"; break;
            case VCS16_OP_JMP: set_pc(o,p,x.target); break;
            case VCS16_OP_JEQ: {
                const std::string c = hi(p,x.a)+" == "+hi(p,x.b)+" && "+lo(p,x.a)+" == "+lo(p,x.b);
                emit_compare_branch(o,p,x,c.c_str()); break;
            }
            case VCS16_OP_JNE: {
                const std::string c = hi(p,x.a)+" != "+hi(p,x.b)+" || "+lo(p,x.a)+" != "+lo(p,x.b);
                emit_compare_branch(o,p,x,c.c_str()); break;
            }
            case VCS16_OP_JLT: {
                const std::string c = hi(p,x.a)+" < "+hi(p,x.b)+" || ("+hi(p,x.a)+" == "+hi(p,x.b)+" && "+lo(p,x.a)+" < "+lo(p,x.b)+")";
                emit_compare_branch(o,p,x,c.c_str()); break;
            }
            case VCS16_OP_JGE: {
                const std::string c = hi(p,x.a)+" > "+hi(p,x.b)+" || ("+hi(p,x.a)+" == "+hi(p,x.b)+" && "+lo(p,x.a)+" >= "+lo(p,x.b)+")";
                emit_compare_branch(o,p,x,c.c_str()); break;
            }
            case VCS16_OP_CALL:
                o << "        if(" << p << "_call_sp >= " << call_depth << ") { " << p << "_trapped = true; }\n";
                for (unsigned d = 0; d < call_depth; ++d) {
                    o << "        " << (d ? "else " : "") << "if(" << p << "_call_sp == " << d << ") { "
                      << p << "_call" << d << "_lo = " << low8(next) << "; " << p << "_call" << d << "_hi = " << high8(next)
                      << "; " << p << "_call_sp = " << (d+1u) << "; }\n";
                }
                o << "        if(!" << p << "_trapped) { " << p << "_pc_lo = " << low8(x.target) << "; " << p << "_pc_hi = " << high8(x.target) << "; }\n";
                break;
            case VCS16_OP_RET:
                o << "        if(" << p << "_call_sp == 0) { " << p << "_trapped = true; }\n";
                for (unsigned d = 1; d <= call_depth; ++d) {
                    o << "        " << (d > 1 ? "else " : "else ") << "if(" << p << "_call_sp == " << d << ") { "
                      << p << "_pc_lo = " << p << "_call" << (d-1u) << "_lo; " << p << "_pc_hi = " << p << "_call" << (d-1u)
                      << "_hi; " << p << "_call_sp = " << (d-1u) << "; }\n";
                }
                break;
            case VCS16_OP_SYS:
                o << "        " << p << "_sys_lo = " << low8(x.imm) << "; " << p << "_sys_hi = " << high8(x.imm)
                  << "; " << p << "_sysret_lo = 0; " << p << "_sysret_hi = 0; " << p << "_host_syscall(); "
                  << lo(p,0) << " = " << p << "_sysret_lo; " << hi(p,0) << " = " << p << "_sysret_hi;\n"; break;
            case VCS16_OP_HALT:
                o << "        " << p << "_halted = true;\n"; break;
        }
        o << "        " << p << "_dispatched = true;\n";
        o << "        }\n";
    }
    o << "        if(!" << p << "_dispatched) { " << p << "_trapped = true; }\n";
    o << "    }\n}\n";

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { set_error(error,error_size,"cannot open SCMD AOT output"); return VCS16_ERR_IO; }
    out << o.str();
    if (!out) { set_error(error,error_size,"cannot write SCMD AOT output"); return VCS16_ERR_IO; }
    return VCS16_OK;
}
