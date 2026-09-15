#include "vcs16/vcs16.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

struct vcs16_module {
    uint16_t entry = 0;
    uint16_t memory_bytes = 1024;
    std::vector<vcs16_instruction_t> code;
};

struct vcs16_vm {
    const vcs16_module_t *module = nullptr;
    std::array<uint16_t, VCS16_REGISTER_COUNT> regs{};
    uint16_t pc = 0;
    bool halted = false;
    std::vector<uint8_t> memory;
    std::vector<uint16_t> call_stack;
    size_t call_limit = 64;
    vcs16_host_t host{};
    std::string trap_message;
};

namespace {

struct PendingTarget {
    size_t instruction = 0;
    std::string label;
    size_t line = 0;
};

static void set_error(char *error, size_t error_size, const std::string &text) {
    if (!error || error_size == 0) return;
    const size_t n = std::min(error_size - 1u, text.size());
    std::memcpy(error, text.data(), n);
    error[n] = '\0';
}

static std::string trim(std::string s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1u]))) --b;
    return s.substr(a, b - a);
}

static std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::vector<std::string> tokens(std::string line) {
    for (char &c : line) if (c == ',' || c == '[' || c == ']' || c == '+') c = ' ';
    std::istringstream in(line);
    std::vector<std::string> out;
    std::string t;
    while (in >> t) out.push_back(t);
    return out;
}

static bool parse_u16(const std::string &text, uint16_t &value) {
    if (text.empty()) return false;
    errno = 0;
    char *end = nullptr;
    const unsigned long v = std::strtoul(text.c_str(), &end, 0);
    if (errno || !end || *end || v > 0xFFFFul) return false;
    value = static_cast<uint16_t>(v);
    return true;
}

static bool parse_reg(const std::string &text, uint8_t &reg) {
    if (text.size() != 2u || (text[0] != 'r' && text[0] != 'R') || text[1] < '0' || text[1] > '7') return false;
    reg = static_cast<uint8_t>(text[1] - '0');
    return true;
}

static vcs16_result_t fail_parse(char *error, size_t error_size, size_t line, const std::string &message) {
    set_error(error, error_size, "line " + std::to_string(line) + ": " + message);
    return VCS16_ERR_PARSE;
}

static bool valid_reg(uint8_t r) { return r < VCS16_REGISTER_COUNT; }

static uint16_t effective_address(const vcs16_vm_t *vm, uint8_t base, uint16_t offset) {
    return static_cast<uint16_t>(vm->regs[base] + offset);
}

static vcs16_result_t trap(vcs16_vm_t *vm, const std::string &message) {
    vm->trap_message = message;
    vm->halted = true;
    if (vm->host.trap) vm->host.trap(vm->host.userdata, vm, vm->trap_message.c_str());
    return VCS16_ERR_TRAP;
}

static bool write_u16(std::ofstream &out, uint16_t v) {
    const unsigned char b[2] = {static_cast<unsigned char>(v & 0xFFu), static_cast<unsigned char>(v >> 8u)};
    out.write(reinterpret_cast<const char *>(b), 2);
    return static_cast<bool>(out);
}

static bool read_u16(std::ifstream &in, uint16_t &v) {
    unsigned char b[2]{};
    in.read(reinterpret_cast<char *>(b), 2);
    if (!in) return false;
    v = static_cast<uint16_t>(b[0] | (static_cast<uint16_t>(b[1]) << 8u));
    return true;
}

} // namespace

extern "C" {

const char *vcs16_result_string(vcs16_result_t result) {
    switch (result) {
        case VCS16_OK: return "ok";
        case VCS16_ERR_ARGUMENT: return "invalid argument";
        case VCS16_ERR_PARSE: return "parse error";
        case VCS16_ERR_VERIFY: return "verification error";
        case VCS16_ERR_IO: return "I/O error";
        case VCS16_ERR_OOM: return "out of memory";
        case VCS16_ERR_TRAP: return "trap";
        case VCS16_ERR_BUDGET: return "instruction budget exceeded";
    }
    return "unknown error";
}

const char *vcs16_opcode_name(vcs16_opcode_t opcode) {
    static const char *const names[] = {"nop","mov","ldi","add","sub","and","or","xor","addi","subi","ld8","st8","jmp","jeq","jne","jlt","jge","call","ret","sys","halt"};
    const unsigned i = static_cast<unsigned>(opcode);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "invalid";
}

vcs16_result_t vcs16_assemble(const char *source, vcs16_module_t **out_module, char *error, size_t error_size) {
    if (!source || !out_module) return VCS16_ERR_ARGUMENT;
    *out_module = nullptr;
    auto module = std::make_unique<vcs16_module_t>();
    std::unordered_map<std::string, uint16_t> labels;
    std::vector<PendingTarget> pending;
    std::string entry_label;

    std::istringstream in(source);
    std::string raw;
    size_t line_no = 0;
    while (std::getline(in, raw)) {
        ++line_no;
        const size_t comment = raw.find_first_of(";#");
        if (comment != std::string::npos) raw.resize(comment);
        std::string line = trim(raw);
        if (line.empty()) continue;

        if (line.back() == ':') {
            std::string name = lower(trim(line.substr(0, line.size() - 1u)));
            if (name.empty()) return fail_parse(error, error_size, line_no, "empty label");
            if (module->code.size() > 0xFFFFu) return fail_parse(error, error_size, line_no, "program exceeds 65535 instructions");
            if (!labels.emplace(name, static_cast<uint16_t>(module->code.size())).second)
                return fail_parse(error, error_size, line_no, "duplicate label '" + name + "'");
            continue;
        }

        const auto ts = tokens(line);
        if (ts.empty()) continue;
        const std::string op = lower(ts[0]);
        if (op == ".entry") {
            if (ts.size() != 2u) return fail_parse(error, error_size, line_no, ".entry expects one label");
            entry_label = lower(ts[1]);
            continue;
        }
        if (op == ".memory") {
            if (ts.size() != 2u) return fail_parse(error, error_size, line_no, ".memory expects one byte count");
            uint16_t bytes = 0;
            if (!parse_u16(ts[1], bytes) || bytes == 0) return fail_parse(error, error_size, line_no, "invalid .memory size");
            module->memory_bytes = bytes;
            continue;
        }

        vcs16_instruction_t ins{};
        auto need_rr = [&](vcs16_opcode_t code) -> vcs16_result_t {
            if (ts.size() != 3u || !parse_reg(ts[1], ins.a) || !parse_reg(ts[2], ins.b))
                return fail_parse(error, error_size, line_no, op + " expects two registers");
            ins.opcode = static_cast<uint8_t>(code); return VCS16_OK;
        };
        auto need_ri = [&](vcs16_opcode_t code) -> vcs16_result_t {
            if (ts.size() != 3u || !parse_reg(ts[1], ins.a) || !parse_u16(ts[2], ins.imm))
                return fail_parse(error, error_size, line_no, op + " expects register, immediate");
            ins.opcode = static_cast<uint8_t>(code); return VCS16_OK;
        };
        auto need_target = [&](vcs16_opcode_t code, size_t arg) -> vcs16_result_t {
            if (ts.size() != arg + 1u) return fail_parse(error, error_size, line_no, op + " has wrong operand count");
            ins.opcode = static_cast<uint8_t>(code);
            pending.push_back(PendingTarget{module->code.size(), lower(ts[arg]), line_no});
            return VCS16_OK;
        };

        vcs16_result_t rc = VCS16_OK;
        if (op == "nop") { if (ts.size()!=1u) rc=fail_parse(error,error_size,line_no,"nop takes no operands"); else ins.opcode=VCS16_OP_NOP; }
        else if (op == "mov") rc = need_rr(VCS16_OP_MOV);
        else if (op == "ldi") rc = need_ri(VCS16_OP_LDI);
        else if (op == "add") rc = need_rr(VCS16_OP_ADD);
        else if (op == "sub") rc = need_rr(VCS16_OP_SUB);
        else if (op == "and") rc = need_rr(VCS16_OP_AND);
        else if (op == "or") rc = need_rr(VCS16_OP_OR);
        else if (op == "xor") rc = need_rr(VCS16_OP_XOR);
        else if (op == "addi") rc = need_ri(VCS16_OP_ADDI);
        else if (op == "subi") rc = need_ri(VCS16_OP_SUBI);
        else if (op == "ld8" || op == "st8") {
            if (ts.size() != 4u || !parse_reg(ts[1], ins.a) || !parse_reg(ts[2], ins.b) || !parse_u16(ts[3], ins.imm))
                rc = fail_parse(error,error_size,line_no,op + " expects reg, base-reg, offset");
            else ins.opcode = static_cast<uint8_t>(op=="ld8"?VCS16_OP_LD8:VCS16_OP_ST8);
        }
        else if (op == "jmp") rc = need_target(VCS16_OP_JMP, 1u);
        else if (op == "call") rc = need_target(VCS16_OP_CALL, 1u);
        else if (op == "jeq" || op == "jne" || op == "jlt" || op == "jge") {
            if (ts.size() != 4u || !parse_reg(ts[1], ins.a) || !parse_reg(ts[2], ins.b)) rc=fail_parse(error,error_size,line_no,op+" expects reg, reg, label");
            else {
                ins.opcode = static_cast<uint8_t>(op=="jeq"?VCS16_OP_JEQ:op=="jne"?VCS16_OP_JNE:op=="jlt"?VCS16_OP_JLT:VCS16_OP_JGE);
                pending.push_back(PendingTarget{module->code.size(), lower(ts[3]), line_no});
            }
        }
        else if (op == "ret") { if(ts.size()!=1u) rc=fail_parse(error,error_size,line_no,"ret takes no operands"); else ins.opcode=VCS16_OP_RET; }
        else if (op == "sys") { if(ts.size()!=2u || !parse_u16(ts[1],ins.imm)) rc=fail_parse(error,error_size,line_no,"sys expects immediate syscall number"); else ins.opcode=VCS16_OP_SYS; }
        else if (op == "halt") { if(ts.size()!=1u) rc=fail_parse(error,error_size,line_no,"halt takes no operands"); else ins.opcode=VCS16_OP_HALT; }
        else return fail_parse(error, error_size, line_no, "unknown opcode '" + op + "'");
        if (rc != VCS16_OK) return rc;
        module->code.push_back(ins);
    }

    if (module->code.empty()) return fail_parse(error, error_size, 1, "program contains no instructions");
    for (const PendingTarget &p : pending) {
        const auto it = labels.find(p.label);
        if (it == labels.end()) return fail_parse(error, error_size, p.line, "unknown label '" + p.label + "'");
        module->code[p.instruction].target = it->second;
    }
    if (!entry_label.empty()) {
        const auto it = labels.find(entry_label);
        if (it == labels.end()) return fail_parse(error, error_size, 1, "unknown entry label '" + entry_label + "'");
        module->entry = it->second;
    }

    vcs16_result_t vr = vcs16_module_verify(module.get(), error, error_size);
    if (vr != VCS16_OK) return vr;
    *out_module = module.release();
    return VCS16_OK;
}

vcs16_result_t vcs16_module_verify(const vcs16_module_t *module, char *error, size_t error_size) {
    if (!module) return VCS16_ERR_ARGUMENT;
    if (module->code.empty()) { set_error(error,error_size,"empty module"); return VCS16_ERR_VERIFY; }
    if (module->code.size() > 0xFFFFu) { set_error(error,error_size,"module exceeds VXE2 instruction-count limit"); return VCS16_ERR_VERIFY; }
    if (module->entry >= module->code.size()) { set_error(error,error_size,"entry point out of range"); return VCS16_ERR_VERIFY; }
    for (size_t i=0;i<module->code.size();++i) {
        const auto &ins=module->code[i];
        const auto op=static_cast<vcs16_opcode_t>(ins.opcode);
        if (op > VCS16_OP_HALT) { set_error(error,error_size,"invalid opcode at instruction "+std::to_string(i)); return VCS16_ERR_VERIFY; }
        switch(op) {
            case VCS16_OP_MOV: case VCS16_OP_ADD: case VCS16_OP_SUB: case VCS16_OP_AND: case VCS16_OP_OR: case VCS16_OP_XOR:
            case VCS16_OP_JEQ: case VCS16_OP_JNE: case VCS16_OP_JLT: case VCS16_OP_JGE:
                if(!valid_reg(ins.a)||!valid_reg(ins.b)){set_error(error,error_size,"invalid register at instruction "+std::to_string(i));return VCS16_ERR_VERIFY;} break;
            case VCS16_OP_LDI: case VCS16_OP_ADDI: case VCS16_OP_SUBI:
                if(!valid_reg(ins.a)){set_error(error,error_size,"invalid register at instruction "+std::to_string(i));return VCS16_ERR_VERIFY;} break;
            case VCS16_OP_LD8: case VCS16_OP_ST8:
                if(!valid_reg(ins.a)||!valid_reg(ins.b)){set_error(error,error_size,"invalid memory register at instruction "+std::to_string(i));return VCS16_ERR_VERIFY;} break;
            default: break;
        }
        if ((op==VCS16_OP_JMP||op==VCS16_OP_JEQ||op==VCS16_OP_JNE||op==VCS16_OP_JLT||op==VCS16_OP_JGE||op==VCS16_OP_CALL) && ins.target>=module->code.size()) {
            set_error(error,error_size,"branch target out of range at instruction "+std::to_string(i)); return VCS16_ERR_VERIFY;
        }
    }
    return VCS16_OK;
}

vcs16_result_t vcs16_module_save(const vcs16_module_t *module, const char *path, char *error, size_t error_size) {
    if(!module||!path)return VCS16_ERR_ARGUMENT;
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out){set_error(error,error_size,"cannot open output file");return VCS16_ERR_IO;}
    out.write("VXE2",4);
    if(!write_u16(out,VCS16_ARCH_VERSION)||!write_u16(out,VCS16_ABI_VERSION)||!write_u16(out,module->entry)||!write_u16(out,module->memory_bytes)||!write_u16(out,static_cast<uint16_t>(module->code.size()))){set_error(error,error_size,"cannot write header");return VCS16_ERR_IO;}
    for(const auto &ins:module->code){
        out.put(static_cast<char>(ins.opcode));out.put(static_cast<char>(ins.a));out.put(static_cast<char>(ins.b));out.put(0);
        if(!write_u16(out,ins.imm)||!write_u16(out,ins.target)){set_error(error,error_size,"cannot write instruction");return VCS16_ERR_IO;}
    }
    if(!out){set_error(error,error_size,"cannot write module");return VCS16_ERR_IO;}
    return VCS16_OK;
}

vcs16_result_t vcs16_module_load(const char *path, vcs16_module_t **out_module, char *error, size_t error_size) {
    if(!path||!out_module)return VCS16_ERR_ARGUMENT;*out_module=nullptr;
    std::ifstream in(path,std::ios::binary);if(!in){set_error(error,error_size,"cannot open module");return VCS16_ERR_IO;}
    char magic[4]{};in.read(magic,4);if(!in||std::memcmp(magic,"VXE2",4)!=0){set_error(error,error_size,"not a VXE2 module");return VCS16_ERR_IO;}
    uint16_t arch=0,abi=0,entry=0,mem=0,count=0;
    if(!read_u16(in,arch)||!read_u16(in,abi)||!read_u16(in,entry)||!read_u16(in,mem)||!read_u16(in,count)){set_error(error,error_size,"truncated VXE header");return VCS16_ERR_IO;}
    if(arch!=VCS16_ARCH_VERSION||abi!=VCS16_ABI_VERSION){set_error(error,error_size,"unsupported VXE architecture/ABI");return VCS16_ERR_IO;}
    auto m=std::make_unique<vcs16_module_t>();m->entry=entry;m->memory_bytes=mem;m->code.resize(count);
    for(auto &ins:m->code){int o=in.get(),a=in.get(),b=in.get(),r=in.get();(void)r;if(o<0||a<0||b<0||r<0||!read_u16(in,ins.imm)||!read_u16(in,ins.target)){set_error(error,error_size,"truncated VXE instruction stream");return VCS16_ERR_IO;}ins.opcode=static_cast<uint8_t>(o);ins.a=static_cast<uint8_t>(a);ins.b=static_cast<uint8_t>(b);ins.reserved=0;}
    vcs16_result_t vr=vcs16_module_verify(m.get(),error,error_size);if(vr!=VCS16_OK)return vr;*out_module=m.release();return VCS16_OK;
}

void vcs16_module_destroy(vcs16_module_t *module){delete module;}
uint16_t vcs16_module_entry(const vcs16_module_t *module){return module?module->entry:0;}
uint16_t vcs16_module_memory_bytes(const vcs16_module_t *module){return module?module->memory_bytes:0;}
size_t vcs16_module_instruction_count(const vcs16_module_t *module){return module?module->code.size():0;}
const vcs16_instruction_t *vcs16_module_instructions(const vcs16_module_t *module){return module&&!module->code.empty()?module->code.data():nullptr;}

vcs16_vm_t *vcs16_vm_create(const vcs16_module_t *module,const vcs16_vm_config_t *config,const vcs16_host_t *host,char *error,size_t error_size){
    if(!module){set_error(error,error_size,"module is null");return nullptr;}if(vcs16_module_verify(module,error,error_size)!=VCS16_OK)return nullptr;
    auto vm=std::make_unique<vcs16_vm_t>();vm->module=module;const uint16_t mem=config&&config->memory_bytes?config->memory_bytes:module->memory_bytes;vm->memory.resize(mem?mem:1024u);vm->call_limit=config&&config->call_depth?config->call_depth:64u;if(host)vm->host=*host;if(vcs16_vm_reset(vm.get())!=VCS16_OK)return nullptr;return vm.release();
}
void vcs16_vm_destroy(vcs16_vm_t *vm){delete vm;}
vcs16_result_t vcs16_vm_reset(vcs16_vm_t *vm){if(!vm||!vm->module)return VCS16_ERR_ARGUMENT;vm->regs.fill(0);vm->pc=vm->module->entry;vm->halted=false;std::fill(vm->memory.begin(),vm->memory.end(),0);vm->call_stack.clear();vm->trap_message.clear();return VCS16_OK;}

vcs16_result_t vcs16_vm_step(vcs16_vm_t *vm){
    if(!vm||!vm->module)return VCS16_ERR_ARGUMENT;if(vm->halted)return VCS16_OK;if(vm->pc>=vm->module->code.size())return trap(vm,"PC out of range");
    const vcs16_instruction_t ins=vm->module->code[vm->pc++];const auto op=static_cast<vcs16_opcode_t>(ins.opcode);
    switch(op){
        case VCS16_OP_NOP: break;
        case VCS16_OP_MOV: vm->regs[ins.a]=vm->regs[ins.b]; break;
        case VCS16_OP_LDI: vm->regs[ins.a]=ins.imm; break;
        case VCS16_OP_ADD: vm->regs[ins.a]=static_cast<uint16_t>(vm->regs[ins.a]+vm->regs[ins.b]); break;
        case VCS16_OP_SUB: vm->regs[ins.a]=static_cast<uint16_t>(vm->regs[ins.a]-vm->regs[ins.b]); break;
        case VCS16_OP_AND: vm->regs[ins.a]=static_cast<uint16_t>(vm->regs[ins.a]&vm->regs[ins.b]); break;
        case VCS16_OP_OR: vm->regs[ins.a]=static_cast<uint16_t>(vm->regs[ins.a]|vm->regs[ins.b]); break;
        case VCS16_OP_XOR: vm->regs[ins.a]=static_cast<uint16_t>(vm->regs[ins.a]^vm->regs[ins.b]); break;
        case VCS16_OP_ADDI: vm->regs[ins.a]=static_cast<uint16_t>(vm->regs[ins.a]+ins.imm); break;
        case VCS16_OP_SUBI: vm->regs[ins.a]=static_cast<uint16_t>(vm->regs[ins.a]-ins.imm); break;
        case VCS16_OP_LD8:{const uint16_t addr=effective_address(vm,ins.b,ins.imm);if(addr>=vm->memory.size())return trap(vm,"LD8 address out of range");vm->regs[ins.a]=vm->memory[addr];break;}
        case VCS16_OP_ST8:{const uint16_t addr=effective_address(vm,ins.b,ins.imm);if(addr>=vm->memory.size())return trap(vm,"ST8 address out of range");vm->memory[addr]=static_cast<uint8_t>(vm->regs[ins.a]);break;}
        case VCS16_OP_JMP: vm->pc=ins.target; break;
        case VCS16_OP_JEQ: if(vm->regs[ins.a]==vm->regs[ins.b])vm->pc=ins.target; break;
        case VCS16_OP_JNE: if(vm->regs[ins.a]!=vm->regs[ins.b])vm->pc=ins.target; break;
        case VCS16_OP_JLT: if(vm->regs[ins.a]<vm->regs[ins.b])vm->pc=ins.target; break;
        case VCS16_OP_JGE: if(vm->regs[ins.a]>=vm->regs[ins.b])vm->pc=ins.target; break;
        case VCS16_OP_CALL: if(vm->call_stack.size()>=vm->call_limit)return trap(vm,"call stack overflow");vm->call_stack.push_back(vm->pc);vm->pc=ins.target;break;
        case VCS16_OP_RET: if(vm->call_stack.empty())return trap(vm,"return with empty call stack");vm->pc=vm->call_stack.back();vm->call_stack.pop_back();break;
        case VCS16_OP_SYS: if(vm->host.syscall)vm->regs[0]=vm->host.syscall(vm->host.userdata,vm,ins.imm);else return trap(vm,"unhandled syscall "+std::to_string(ins.imm));break;
        case VCS16_OP_HALT: vm->halted=true;break;
        default:return trap(vm,"invalid opcode");
    }
    return VCS16_OK;
}

vcs16_result_t vcs16_vm_run(vcs16_vm_t *vm,uint64_t instruction_budget){if(!vm)return VCS16_ERR_ARGUMENT;if(instruction_budget==0)instruction_budget=1000000u;for(uint64_t i=0;i<instruction_budget&&!vm->halted;++i){const auto r=vcs16_vm_step(vm);if(r!=VCS16_OK)return r;}return vm->halted?VCS16_OK:VCS16_ERR_BUDGET;}
uint16_t vcs16_vm_get_reg(const vcs16_vm_t *vm,unsigned index){return vm&&index<VCS16_REGISTER_COUNT?vm->regs[index]:0;}
void vcs16_vm_set_reg(vcs16_vm_t *vm,unsigned index,uint16_t value){if(vm&&index<VCS16_REGISTER_COUNT)vm->regs[index]=value;}
uint16_t vcs16_vm_get_pc(const vcs16_vm_t *vm){return vm?vm->pc:0;}
int vcs16_vm_halted(const vcs16_vm_t *vm){return vm&&vm->halted?1:0;}
uint8_t *vcs16_vm_memory(vcs16_vm_t *vm){return vm&&!vm->memory.empty()?vm->memory.data():nullptr;}
size_t vcs16_vm_memory_size(const vcs16_vm_t *vm){return vm?vm->memory.size():0;}

} // extern C
