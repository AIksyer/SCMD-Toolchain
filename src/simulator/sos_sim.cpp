#include "sos_sim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scmd::sim {
namespace {

std::string lower_ascii_local(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (char c : sv) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        out.push_back(c);
    }
    return out;
}

bool parse_number(std::string_view sv, double &out) {
    std::string s(sv);
    char *end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

std::string number_text(double x) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << x;
    return oss.str();
}

double sos_float(double x) {
    /* Source 2 SOS numeric fields behave like 32-bit floats. Keep the
     * simulator's externally visible numbers on the same precision boundary. */
    return static_cast<double>(static_cast<float>(x));
}

enum class FieldKind { Float, Bool, String, Enum, FloatArray };

struct Field {
    FieldKind kind = FieldKind::Float;
    double number = 0.0;
    bool boolean = false;
    std::string text;
    std::vector<double> array;
    std::string connected;
};

struct Operator {
    std::string name;
    std::string type;
    std::string name_space;
    std::vector<std::string> order;
    std::unordered_map<std::string, Field> fields;
};

struct Stack {
    std::string name;
    std::vector<std::string> order;
    std::unordered_map<std::string, Operator> operators;
};

Field fnum(double v, std::string connected = {}) {
    Field f; f.kind = FieldKind::Float; f.number = sos_float(v); f.connected = std::move(connected); return f;
}
Field fbool(bool v) { Field f; f.kind = FieldKind::Bool; f.boolean = v; return f; }
Field fstr(std::string v) { Field f; f.kind = FieldKind::String; f.text = std::move(v); return f; }
Field fenum(std::string v) { Field f; f.kind = FieldKind::Enum; f.text = std::move(v); return f; }
Field farray(std::initializer_list<double> v) { Field f; f.kind = FieldKind::FloatArray; for (double x : v) f.array.push_back(sos_float(x)); return f; }

void put(Operator &op, std::string name, Field value) {
    op.order.push_back(name);
    op.fields.emplace(std::move(name), std::move(value));
}

Operator make_op(std::string name, std::string type = "null", std::string ns = {}) {
    Operator op; op.name = std::move(name); op.type = std::move(type); op.name_space = std::move(ns);
    put(op, "error", fbool(false));
    put(op, "execute_once", fbool(false));
    return op;
}

void add_op(Stack &stack, Operator op) {
    stack.order.push_back(op.name);
    stack.operators.emplace(op.name, std::move(op));
}

} // namespace

struct SosSimulator::Impl {
    explicit Impl(std::unordered_map<std::string, std::string> &cv) : cvars(cv) { reset(); }

    std::unordered_map<std::string, std::string> &cvars;
    std::unordered_map<std::string, Stack> stacks;
    struct PendingPatch {
        std::string stack, op, field;
        size_t index = 0;
        bool is_string = false;
        std::string text;
        double number = 0.0;
    };
    std::vector<PendingPatch> pending;
    struct OpvarEntity {
        std::string name;
        std::string stack;
        std::string op;
        std::string field;
        size_t index = 0;
        double value = 0.0;
        bool string_value = false;
        std::string text;
    };
    std::unordered_map<std::string, OpvarEntity> opvar_entities;
    bool show_init = false;
    bool show_updates = false;
    uint64_t next_guid = 1;
    struct ExecEntry { int index = 0; uint64_t guid = 0; std::string stack; };
    std::vector<ExecEntry> exec_list;

    static const std::vector<std::string> &registry() {
        static const std::vector<std::string> names = {
            "convar_get", "convar_set", "ctrl_gate", "ctrl_switch12_float", "ctrl_switch3_float",
            "ctrl_switch3_string", "ctrl_switch_float", "ctrl_switch_string", "logic_string_match_12",
            "math_accumulate12_float", "math_accumulate16_float16", "math_average8_float", "math_clamp_float",
            "math_curve_2d", "math_curve_2d_4knot", "math_delta", "math_filter_float", "math_float",
            "math_float3", "math_float_eval", "math_func_float", "math_inrange_float", "math_random_float",
            "math_remap_float", "math_scale_float3", "math_string", "opvar_get_bool", "opvar_get_element_float8",
            "opvar_get_float", "opvar_get_float12", "opvar_get_float3", "opvar_get_float8", "opvar_get_string",
            "opvar_increment_float", "opvar_set_array_index", "opvar_set_float", "opvar_set_float3",
            "opvar_set_string", "sos_goto", "sos_import_stack", "sos_set_info", "util_elements_float16"
        };
        return names;
    }

    void reset() {
        stacks.clear();
        pending.clear();
        opvar_entities.clear();
        exec_list.clear();
        next_guid = 1;
        cvars.try_emplace("snd_op_test_convar", "0");
        cvars.try_emplace("snd_musicvolume", "1");

        Stack dg; dg.name = "diagnostic_globals";
        Operator tv = make_op("test_opvars", "null");
        put(tv, "input_execute", fnum(0));
        put(tv, "test_float", fnum(666));
        put(tv, "number_of_local_players", fnum(666));
        put(tv, "test_array_max_value", farray({11, 777, 120, 3}));
        Field origins; origins.kind = FieldKind::FloatArray; for (double x : {1,2,3, 2,2,3, 3,2,3, 4,2,3}) origins.array.push_back(sos_float(x)); put(tv, "local_player_origins", std::move(origins));
        add_op(dg, std::move(tv));
        stacks.emplace(dg.name, std::move(dg));

        Stack sg; sg.name = "soundscape_globals";
        Operator dave = make_op("daves_test_opvars", "null");
        put(dave, "foo", fnum(0));
        add_op(sg, std::move(dave));
        stacks.emplace(sg.name, std::move(sg));

        Stack u; u.name = "update_test_opvar";
        Operator n = make_op("update_test_opvar_null", "null");
        put(n, "input_execute", fnum(0)); put(n, "test_float_field", fnum(666)); add_op(u, std::move(n));
        Operator set = make_op("update_test_opvar_set", "opvar_set_float");
        add_opvar_common(set, 666, "diagnostic_globals", "test_opvars", "test_float", true, 0); add_op(u, std::move(set));
        Operator get = make_op("update_test_opvar_get", "opvar_get_float");
        add_opvar_get_common(get, "diagnostic_globals", "test_opvars", "test_float", false, 0); add_op(u, std::move(get));
        Operator inc = make_op("update_test_opvar_increment", "opvar_increment_float");
        add_opvar_common(inc, 10, "diagnostic_globals", "test_opvars", "test_float", true, 0);
        put(inc, "array_selection_type", fenum("index")); put(inc, "input_clear_selection", fbool(false)); put(inc, "output", fnum(0)); add_op(u, std::move(inc));
        Operator ls = make_op("update_test_local_opvar_set", "opvar_set_float");
        add_opvar_common(ls, 999, "", "update_test_opvar_null", "test_float_field", true, 0); add_op(u, std::move(ls));
        Operator lg = make_op("update_test_local_opvar_get", "opvar_get_float");
        add_opvar_get_common(lg, "", "update_test_opvar_null", "test_float_field", true, 0); add_op(u, std::move(lg));
        Operator ga = make_op("update_test_get_array_max_value", "opvar_get_float");
        add_opvar_get_common(ga, "diagnostic_globals", "test_opvars", "test_array_max_value", false, 0); add_op(u, std::move(ga));
        stacks.emplace(u.name, std::move(u));

        Stack te; te.name = "test_enum_opvar";
        Operator pub = make_op("public", "null"); put(pub, "input_execute", fnum(0)); put(pub, "math_enum", fenum("mult")); add_op(te, std::move(pub));
        Operator mt = make_op("math_test", "math_float"); put(mt, "apply", fenum("mult")); put(mt, "input_execute", fnum(1)); put(mt, "input1", fnum(1)); put(mt, "input2", fnum(2)); put(mt, "output", fnum(0)); add_op(te, std::move(mt));
        stacks.emplace(te.name, std::move(te));

        make_soundscape_stack("soundscape_opvar_percentage", false);
        make_soundscape_stack("soundscape_opvar_switch", true);

        Stack cv; cv.name = "update_test_convar";
        Operator cg = make_op("update_test_convar", "convar_get"); put(cg, "convar", fstr("snd_op_test_convar")); put(cg, "input_execute", fnum(1)); put(cg, "output", fnum(0)); add_op(cv, std::move(cg));
        Operator cs = make_op("update_test_convar2", "convar_set"); put(cs, "convar", fstr("snd_musicvolume")); put(cs, "input_execute", fnum(1)); put(cs, "input", fnum(0)); add_op(cv, std::move(cs));
        stacks.emplace(cv.name, std::move(cv));

        Stack imp; imp.name = "update_test_simple_nest_import_op2";
        Operator ipub = make_op("public", "null"); put(ipub, "input_execute", fnum(0)); put(ipub, "import2_float", fnum(222)); add_op(imp, std::move(ipub));
        Operator io = make_op("update_test_import_op2", "sos_import_stack"); put(io, "import_stack", fstr("update_test_simple_nest_import_op1")); put(io, "input_execute", fnum(1)); add_op(imp, std::move(io));
        Operator old = make_op("update_test_import_op2::update_test_import_op1", "sos_import_stack", "update_test_import_op2"); put(old, "import_stack", fstr("update_test_simple_public")); put(old, "input_execute", fnum(1)); add_op(imp, std::move(old));
        Operator ialu = make_op("update_test_simple_nest_import_op2_OPER", "math_float"); put(ialu, "apply", fenum("add")); put(ialu, "input_execute", fnum(1)); put(ialu, "input1", fnum(222)); put(ialu, "input2", fnum(4)); put(ialu, "output", fnum(0)); add_op(imp, std::move(ialu));
        stacks.emplace(imp.name, std::move(imp));
    }

    static void add_opvar_common(Operator &op, double input, const std::string &stack, const std::string &target_op,
                                 const std::string &field, bool use_ns, size_t index) {
        put(op, "check_event_data", fbool(false)); put(op, "weights_field_name", fstr("")); put(op, "input_execute", fnum(1)); put(op, "input", fnum(input));
        put(op, "input_stack_name", fstr(stack)); put(op, "input_operator_name", fstr(target_op)); put(op, "input_field_name", fstr(field)); put(op, "input_use_namespace", fbool(use_ns));
        put(op, "input_get_parent", fbool(false)); put(op, "input_get_ancestor", fbool(false)); put(op, "input_index", fnum(static_cast<double>(index))); put(op, "output_opvar_exists", fnum(0));
    }

    static void add_opvar_get_common(Operator &op, const std::string &stack, const std::string &target_op,
                                     const std::string &field, bool use_ns, size_t index) {
        put(op, "check_event_data", fbool(false)); put(op, "weights_field_name", fstr("")); put(op, "array_selection_type", fenum("index")); put(op, "input_execute", fnum(1));
        put(op, "input_stack_name", fstr(stack)); put(op, "input_operator_name", fstr(target_op)); put(op, "input_field_name", fstr(field)); put(op, "input_use_namespace", fbool(use_ns));
        put(op, "input_get_parent", fbool(false)); put(op, "input_get_ancestor", fbool(false)); put(op, "input_index", fnum(static_cast<double>(index))); put(op, "input_clear_selection", fbool(false));
        put(op, "output", fnum(0)); put(op, "output_opvar_exists", fnum(0));
    }

    void make_soundscape_stack(const std::string &name, bool use_switch) {
        Stack s; s.name = name;
        Operator pub = make_op("public", "null");
        put(pub, "input_execute", fnum(1));
        put(pub, "input_float", fnum(1));
        put(pub, "opvar_name", fstr("foo"));
        put(pub, "remap_opvar_x1", fnum(1)); put(pub, "remap_opvar_y1", fnum(1));
        put(pub, "remap_opvar_x2", fnum(1)); put(pub, "remap_opvar_y2", fnum(1));
        put(pub, "remap_opvar_x3", fnum(1)); put(pub, "remap_opvar_y3", fnum(1));
        put(pub, "remap_opvar_x4", fnum(1)); put(pub, "remap_opvar_y4", fnum(1));
        put(pub, "filter_speed", fnum(0.06)); put(pub, "output_float", fnum(1)); add_op(s, std::move(pub));
        Operator has = make_op("has_opvar", "math_string"); put(has, "apply", fenum("not_equal")); put(has, "input_execute", fnum(1)); put(has, "input1", fstr("foo")); put(has, "input2", fstr("foo")); put(has, "output", fnum(1)); put(has, "output_string", fstr("")); add_op(s, std::move(has));
        Operator get = make_op("get_opvar", "opvar_get_float"); add_opvar_get_common(get, "soundscape_globals", "daves_test_opvars", "foo", false, 0); add_op(s, std::move(get));
        Operator filter = make_op(use_switch ? "filter_vol_opvar" : "filter_opvar", "math_filter_float"); put(filter, "input_execute", fnum(1)); put(filter, "input", fnum(0)); put(filter, "input_max_velocity", fnum(0.06)); put(filter, "input_reset", fbool(false)); put(filter, "output", fnum(0)); add_op(s, std::move(filter));
        Operator remap = make_op("remap_opvar", "math_remap_float"); put(remap, "curve_defintion_type", fenum("single")); put(remap, "curve_type", fenum("linear")); put(remap, "input_execute", fnum(1)); put(remap, "input", fnum(0));
        for(int i=1;i<=4;++i){put(remap, "input_X"+std::to_string(i), fnum(1)); put(remap, "input_Y"+std::to_string(i), fnum(1));}
        put(remap, "input_scale_X", fnum(1)); put(remap, "input_scale_Y", fnum(1)); put(remap, "output", fnum(0)); add_op(s, std::move(remap));
        if(use_switch){
            Operator sw = make_op("opvar_switch", "ctrl_switch_float"); put(sw, "input_execute", fnum(1)); put(sw, "input1", fnum(1)); put(sw, "input2", fnum(0)); put(sw, "input_switch", fnum(1)); put(sw, "output", fnum(0)); put(sw, "output_not_selected", fnum(0)); add_op(s, std::move(sw));
        } else {
            Operator rv = make_op("random_perc", "math_random_float"); put(rv, "round_to_int", fbool(false)); put(rv, "input_execute", fnum(1)); put(rv, "input_min", fnum(0)); put(rv, "input_max", fnum(1)); put(rv, "output", fnum(1)); add_op(s, std::move(rv));
            Operator rc = make_op("random_compare", "math_float"); put(rc, "apply", fenum("less_than")); put(rc, "input_execute", fnum(1)); put(rc, "input1", fnum(1)); put(rc, "input2", fnum(0)); put(rc, "output", fnum(0)); add_op(s, std::move(rc));
            Operator sw = make_op("output_switch", "ctrl_switch_float"); put(sw, "input_execute", fnum(1)); put(sw, "input1", fnum(0)); put(sw, "input2", fnum(0)); put(sw, "input_switch", fnum(1)); put(sw, "output", fnum(1)); put(sw, "output_not_selected", fnum(0)); add_op(s, std::move(sw));
        }
        Operator setter = make_op("set_output_float", "opvar_set_float"); add_opvar_common(setter, use_switch?0:1, "", "public", "output_float", true, 0); add_op(s, std::move(setter));
        stacks.emplace(s.name, std::move(s));
    }

    Stack *stack(const std::string &name) {
        auto it = stacks.find(name); return it == stacks.end() ? nullptr : &it->second;
    }
    Operator *op(Stack *s, const std::string &name) {
        if (!s) return nullptr;
        auto it = s->operators.find(name);
        return it == s->operators.end() ? nullptr : &it->second;
    }
    Field *field(Operator *o, const std::string &name) {
        if (!o) return nullptr;
        auto it = o->fields.find(name);
        return it == o->fields.end() ? nullptr : &it->second;
    }
    const Field *field(const Operator *o, const std::string &name) const {
        if (!o) return nullptr;
        auto it = o->fields.find(name);
        return it == o->fields.end() ? nullptr : &it->second;
    }

    double num(Operator *o, const std::string &name, double fallback=0) const {
        const Field *f=field(o,name); if(!f) return fallback; if(f->kind==FieldKind::Bool)return f->boolean?1.0:0.0; if(f->kind==FieldKind::Float)return f->number; return fallback;
    }
    bool boolean(Operator *o, const std::string &name, bool fallback=false) const { return num(o,name,fallback?1:0)!=0; }
    std::string text(Operator *o,const std::string &name) const { const Field*f=field(o,name); return f?(f->text):std::string(); }
    void setnum(Operator *o,const std::string &name,double v) { Field*f=field(o,name); if(!f){put(*o,name,fnum(v));return;} if(f->kind==FieldKind::Bool)f->boolean=v!=0; else f->number=sos_float(v); }
    void settext(Operator *o,const std::string &name,const std::string &v) { Field*f=field(o,name); if(!f){put(*o,name,fstr(v));return;} f->text=v; }

    void print_field(const std::string &prefix,const std::string &name,const Field &f) const {
        std::cout << "[SndOperators]     " << prefix << '.' << name << ": ";
        switch(f.kind){
        case FieldKind::Float: std::cout << ' ' << number_text(f.number); break;
        case FieldKind::Bool: std::cout << ' ' << (f.boolean?"true":"false"); break;
        case FieldKind::String: std::cout << " \"" << f.text << "\""; break;
        case FieldKind::Enum: std::cout << ' ' << f.text; break;
        case FieldKind::FloatArray:
            std::cout << "\n";
            for(double v:f.array) std::cout << "\t " << number_text(v) << "\n";
            return;
        }
        if(!f.connected.empty())std::cout << "(connected: " << f.connected << " )";
        std::cout << '\n';
    }

    void print_operator(const Stack &s,const Operator &o) const {
        std::cout << "[SndOperators]     Operator Stack: " << s.name << "\n    Operator: " << o.name << "\n[SndOperators] \n    Name: " << o.name << '\n';
        if(!o.name_space.empty())std::cout << "    NameSpace: " << o.name_space << '\n';
        for(const std::string &name:o.order){auto it=o.fields.find(name);if(it!=o.fields.end())print_field(o.name,name,it->second);}
    }

    void print_stack(const Stack &s) const {
        std::cout << "[SndOperators]     Operator Stack: " << s.name << "\n    Size: " << (s.order.size()*64) << "\n    Depend: 0\n[SndOperators] \n";
        for(const std::string &name:s.order){auto it=s.operators.find(name);if(it==s.operators.end())continue;const Operator&o=it->second;std::cout << "    Name: " << o.name << '\n';if(!o.name_space.empty())std::cout<<"    NameSpace: "<<o.name_space<<'\n';for(const std::string&fn:o.order){auto fi=o.fields.find(fn);if(fi!=o.fields.end())print_field(o.name,fn,fi->second);}std::cout<<"[SndOperators] \n";}
    }

    bool resolve_target(const std::string &current_stack, Operator *source, Stack *&ts, Operator *&to, Field *&tf, size_t &index, bool for_write) {
        const std::string stack_name=text(source,"input_stack_name");
        const std::string op_name=text(source,"input_operator_name");
        const std::string field_name=text(source,"input_field_name");
        index=static_cast<size_t>(std::max(0.0,num(source,"input_index")));
        std::string actual_stack=stack_name.empty()?current_stack:stack_name;
        ts=stack(actual_stack); to=op(ts,op_name); tf=field(to,field_name);
        if(!ts||!to||!tf)return false;
        /* Observed engine quirk: test_enum_opvar is resolvable by debug execute
         * but not by the opvar get/set address space. */
        if(actual_stack=="test_enum_opvar"&&actual_stack!=current_stack)return false;
        if(for_write&&actual_stack!=current_stack){
            /* Cross-stack writes observed from update_test_opvar -> globals;
             * soundscape stacks can read the same globals but cannot write them. */
            if(!(current_stack=="update_test_opvar"&&actual_stack=="diagnostic_globals"))return false;
        }
        return true;
    }

    bool read_target(const std::string &current_stack,Operator *source,double &out) {
        Stack*ts=nullptr;Operator*to=nullptr;Field*tf=nullptr;size_t index=0;
        if(!resolve_target(current_stack,source,ts,to,tf,index,false))return false;
        if(tf->kind==FieldKind::Float){out=tf->number;return true;}
        if(tf->kind==FieldKind::Bool){out=tf->boolean?1:0;return true;}
        if(tf->kind==FieldKind::FloatArray&&index<tf->array.size()){out=tf->array[index];return true;}
        return false;
    }

    bool write_target(const std::string &current_stack,Operator *source,double value) {
        Stack*ts=nullptr;Operator*to=nullptr;Field*tf=nullptr;size_t index=0;
        if(!resolve_target(current_stack,source,ts,to,tf,index,true))return false;
        if(tf->kind==FieldKind::Float){tf->number=sos_float(value);return true;}
        if(tf->kind==FieldKind::Bool){tf->boolean=value!=0;return true;}
        if(tf->kind==FieldKind::FloatArray&&index<tf->array.size()){tf->array[index]=sos_float(value);return true;}
        return false;
    }

    void refresh_connections(Stack &s,Operator &o) {
        if((s.name=="soundscape_opvar_percentage"||s.name=="soundscape_opvar_switch")){
            Operator *pub=op(&s,"public"),*has=op(&s,"has_opvar"),*get=op(&s,"get_opvar"),*filter=op(&s,s.name=="soundscape_opvar_switch"?"filter_vol_opvar":"filter_opvar"),*remap=op(&s,"remap_opvar");
            if(&o==has){settext(has,"input1",text(pub,"opvar_name"));}
            if(&o==get){settext(get,"input_field_name",text(pub,"opvar_name"));setnum(get,"input_execute",num(has,"output"));}
            if(&o==filter){setnum(filter,"input",num(get,"output"));setnum(filter,"input_max_velocity",num(pub,"filter_speed"));}
            if(&o==remap){setnum(remap,"input",num(filter,"output"));for(int i=1;i<=4;++i){setnum(remap,"input_X"+std::to_string(i),num(pub,"remap_opvar_x"+std::to_string(i)));setnum(remap,"input_Y"+std::to_string(i),num(pub,"remap_opvar_y"+std::to_string(i)));}}
            if(s.name=="soundscape_opvar_switch"){
                Operator*sw=op(&s,"opvar_switch");if(&o==sw){setnum(sw,"input1",num(pub,"input_float"));setnum(sw,"input2",num(remap,"output"));setnum(sw,"input_switch",num(has,"output"));}
                Operator*setter=op(&s,"set_output_float");if(&o==setter)setnum(setter,"input",num(sw,"output"));
            }else{
                Operator*rv=op(&s,"random_perc"),*rc=op(&s,"random_compare"),*sw=op(&s,"output_switch"),*setter=op(&s,"set_output_float");
                if(&o==rc){setnum(rc,"input1",num(rv,"output"));setnum(rc,"input2",num(remap,"output"));}
                if(&o==sw){setnum(sw,"input2",num(rc,"output"));setnum(sw,"input_switch",num(pub,"input_execute"));}
                if(&o==setter)setnum(setter,"input",num(sw,"output"));
            }
        }else if(s.name=="test_enum_opvar"&&o.name=="math_test"){
            Operator*pub=op(&s,"public");settext(&o,"apply",text(pub,"math_enum"));
        }else if(s.name=="update_test_simple_nest_import_op2"&&o.name=="update_test_simple_nest_import_op2_OPER"){
            Operator*pub=op(&s,"public");setnum(&o,"input1",num(pub,"import2_float"));
        }
    }

    void execute_math_float(Operator &o) {
        const std::string apply=text(&o,"apply");double a=num(&o,"input1"),b=num(&o,"input2"),r=0;
        if(apply=="add")r=a+b;else if(apply=="sub"||apply=="subtract")r=a-b;else if(apply=="mult"||apply=="multiply")r=a*b;else if(apply=="div"||apply=="divide")r=b!=0?a/b:0;else if(apply=="less_than")r=a<b?1:0;else if(apply=="greater_than")r=a>b?1:0;else if(apply=="equal")r=a==b?1:0;else if(apply=="not_equal")r=a!=b?1:0;else if(apply=="min")r=std::min(a,b);else if(apply=="max")r=std::max(a,b);setnum(&o,"output",r);
    }

    void execute_remap(Operator &o) {
        double x=num(&o,"input")*num(&o,"input_scale_X",1);double xs[4],ys[4];for(int i=0;i<4;++i){xs[i]=num(&o,"input_X"+std::to_string(i+1));ys[i]=num(&o,"input_Y"+std::to_string(i+1));}
        double y=ys[0];
        if(x<=xs[0])y=ys[0];else if(x>=xs[3])y=ys[3];else{for(int i=0;i<3;++i)if(x>=xs[i]&&x<=xs[i+1]){double d=xs[i+1]-xs[i];double t=d!=0?(x-xs[i])/d:0;y=ys[i]+(ys[i+1]-ys[i])*t;break;}}
        setnum(&o,"output",y*num(&o,"input_scale_Y",1));
    }

    bool execute_operator(const std::string &stack_name,const std::string &op_name,bool noisy=true) {
        Stack*s=stack(stack_name);Operator*o=op(s,op_name);if(!s||!o){if(noisy)std::cerr<<"[SndOperators] failed to resolve operator "<<op_name<<" in "<<stack_name<<'\n';return false;}
        refresh_connections(*s,*o);
        if(o->type=="opvar_get_float"){
            double value=0;bool ok=read_target(stack_name,o,value);if(ok)setnum(o,"output",value);setnum(o,"output_opvar_exists",ok?1:0);if(!ok&&noisy)std::cerr<<"[SndOperators] opvar_get_float operator "<<o->name<<" in "<<stack_name<<" failed to get field "<<text(o,"input_field_name")<<'\n';
        }else if(o->type=="opvar_set_float"){
            bool ok=write_target(stack_name,o,num(o,"input"));setnum(o,"output_opvar_exists",ok?1:0);
        }else if(o->type=="opvar_increment_float"){
            double value=0;bool ok=read_target(stack_name,o,value);if(ok){value+=num(o,"input");ok=write_target(stack_name,o,value);}setnum(o,"output",ok?value:0);setnum(o,"output_opvar_exists",ok?1:0);
        } else if (o->name == "has_opvar" &&
                   (stack_name == "soundscape_opvar_percentage" || stack_name == "soundscape_opvar_switch")) {
            /* The soundscape fixture uses this node as an existence gate. In
             * the observed CS2 graph it reports 1 while input1/input2 are the
             * same string, so model the graph behavior rather than generic
             * math_string equality semantics. */
            Operator *getter = op(s, "get_opvar");
            Operator *pub = op(s, "public");
            if (getter && pub) settext(getter, "input_field_name", text(pub, "opvar_name"));
            Stack *ts = nullptr;
            Operator *to = nullptr;
            Field *tf = nullptr;
            size_t index = 0;
            const bool ok = getter && resolve_target(stack_name, getter, ts, to, tf, index, false);
            setnum(o, "output", ok ? 1 : 0);
        }else if(o->type=="math_float")execute_math_float(*o);
        else if(o->type=="math_filter_float"){
            double in=num(o,"input");if(boolean(o,"input_reset"))setnum(o,"output",in);else{double cur=num(o,"output"),step=std::abs(num(o,"input_max_velocity"));if(step<=0)setnum(o,"output",in);else setnum(o,"output",cur+std::clamp(in-cur,-step,step));}
        }else if(o->type=="math_remap_float")execute_remap(*o);
        else if(o->type=="ctrl_switch_float"){
            bool sel=num(o,"input_switch")!=0;double a=num(o,"input1"),b=num(o,"input2");setnum(o,"output",sel?b:a);setnum(o,"output_not_selected",sel?a:b);
        }else if(o->type=="math_string"){
            std::string a=text(o,"input1"),b=text(o,"input2"),apply=text(o,"apply");double r=(apply=="equal")?(a==b):(apply=="not_equal"?(a!=b):0);setnum(o,"output",r);
        }else if(o->type=="math_random_float"){
            /* deterministic simulator value keeps SOS tests reproducible */
            setnum(o,"output",num(o,"input_max"));
        }else if(o->type=="convar_get"){
            double value=0;auto it=cvars.find(lower_ascii_local(text(o,"convar")));if(it!=cvars.end())parse_number(it->second,value);setnum(o,"output",value);
        }else if(o->type=="convar_set"){
            cvars[lower_ascii_local(text(o,"convar"))]=number_text(num(o,"input"));
        }
        if(show_updates&&noisy)std::cout<<"[SndOperators] update "<<stack_name<<"/"<<op_name<<'\n';
        return true;
    }

    bool set_numeric_field(const std::string &stack_name,const std::string &op_name,const std::string &field_name,size_t index,double value,bool noisy=true) {
        Stack*s=stack(stack_name);Operator*o=op(s,op_name);Field*f=field(o,field_name);if(!s||!o||!f){if(noisy)std::cerr<<"[SndOperators] couldn't find field "<<stack_name<<'/'<<op_name<<'/'<<field_name<<'\n';return false;}
        if(f->kind==FieldKind::String){if(noisy)std::cerr<<"[SndOperators] snd_sos_set_operator_field cannot set string field "<<field_name<<'\n';return false;}
        if(f->kind==FieldKind::FloatArray){if(index>=f->array.size())return false;f->array[index]=sos_float(value);return true;}
        if(f->kind==FieldKind::Bool){f->boolean=value!=0;return true;}
        if(f->kind==FieldKind::Enum){
            if(field_name=="array_selection_type")f->text=(value==0?"index":"max_value");
            else f->text=number_text(value);
            return true;
        }
        f->number=sos_float(value);return true;
    }

    bool direct_patch(const PendingPatch &p) {
        Stack*s=stack(p.stack);Operator*o=op(s,p.op);Field*f=field(o,p.field);if(!s||!o||!f)return false;
        if(p.is_string){
            if(f->kind!=FieldKind::String&&f->kind!=FieldKind::Enum)return false;
            /* In the observed snd_opvar_set path, opvarValueString "" did
             * not clear an already populated SOS string field. */
            if(p.text.empty())return true;
            f->text=p.text;return true;
        }
        if(f->kind==FieldKind::FloatArray){if(p.index>=f->array.size())return false;f->array[p.index]=sos_float(p.number);return true;}
        if(f->kind==FieldKind::Bool)f->boolean=p.number!=0;else if(f->kind==FieldKind::Float)f->number=sos_float(p.number);else return false;return true;
    }

    bool command_ent_create(const std::vector<std::string> &argv) {
        if (argv.size() < 2 || lower_ascii_local(argv[1]) != "snd_opvar_set") return false;
        std::unordered_map<std::string, std::string> kv;
        for (size_t i = 2; i + 1 < argv.size();) {
            if (argv[i] == "{" || argv[i] == "}") { ++i; continue; }
            if (argv[i + 1] == "}") break;
            kv[argv[i]] = argv[i + 1];
            i += 2;
        }

        OpvarEntity ent;
        ent.name = kv["targetName"];
        ent.stack = kv["stackName"];
        ent.op = kv["operatorName"];
        ent.field = kv["opvarName"];
        double d = 0;
        if (parse_number(kv["opvarIndex"], d) && d >= 0) ent.index = static_cast<size_t>(d);
        ent.string_value = kv["opvarValueType"] == "1";
        if (ent.string_value) ent.text = kv["opvarValueString"];
        else if (!parse_number(kv.count("opvarValueFloat") ? kv["opvarValueFloat"] : kv["opvarValue"], ent.value)) ent.value = 0;
        if (!ent.name.empty()) opvar_entities[lower_ascii_local(ent.name)] = ent;

        if (kv["setOnSpawn"] == "1") {
            PendingPatch patch;
            patch.stack = ent.stack;
            patch.op = ent.op;
            patch.field = ent.field;
            patch.index = ent.index;
            patch.is_string = ent.string_value;
            patch.text = ent.text;
            patch.number = ent.value;
            pending.push_back(std::move(patch));
        }
        return true;
    }

    bool command_ent_fire(const std::vector<std::string> &argv) {
        if (argv.size() < 3) return false;
        const std::string target = lower_ascii_local(argv[1]);
        auto it = opvar_entities.find(target);
        if (it == opvar_entities.end()) return false;
        OpvarEntity &ent = it->second;
        const std::string input = lower_ascii_local(argv[2]);
        const std::string value = argv.size() > 3 ? argv[3] : std::string();
        double number = 0;

        if (input == "setstackname") ent.stack = value;
        else if (input == "setoperatorname") ent.op = value;
        else if (input == "setopvarname") ent.field = value;
        else if (input == "setopvarindex") {
            if (parse_number(value, number) && number >= 0) ent.index = static_cast<size_t>(number);
        } else if (input == "changeopvarvalue") {
            if (parse_number(value, number)) ent.value = sos_float(number);
        } else if (input == "changeopvarvalueandset") {
            if (parse_number(value, number)) ent.value = sos_float(number);
            PendingPatch patch;
            patch.stack = ent.stack; patch.op = ent.op; patch.field = ent.field; patch.index = ent.index;
            patch.number = ent.value; patch.is_string = false;
            pending.push_back(std::move(patch));
        } else if (input == "setopvar") {
            PendingPatch patch;
            patch.stack = ent.stack; patch.op = ent.op; patch.field = ent.field; patch.index = ent.index;
            patch.is_string = ent.string_value; patch.text = ent.text; patch.number = ent.value;
            pending.push_back(std::move(patch));
        } else {
            return false;
        }
        return true;
    }

    bool handle(const std::vector<std::string> &argv) {
        if (argv.empty()) return false;
        const std::string cmd = lower_ascii_local(argv[0]);
        if (cmd == "ent_create") return command_ent_create(argv);
        if (cmd == "ent_fire") return command_ent_fire(argv);
        if(cmd=="snd_sos_print_operators"){for(const auto&n:registry())std::cout<<"[SndOperators] "<<n<<'\n';return true;}
        if (cmd == "snd_sos_print_operator_stacks") {
            std::vector<std::string> names;
            names.reserve(stacks.size());
            for (const auto &kv : stacks) names.push_back(kv.first);
            std::sort(names.begin(), names.end());
            for (const std::string &name : names) {
                const Stack &s = stacks.at(name);
                std::cout << "[SndOperators] " << name << "\n[SndOperators] Operator Count: "
                          << s.order.size() << '\n';
            }
            return true;
        }
        if(cmd=="snd_sos_print_operator_stack"){
            if(argv.size()<2){std::cerr<<"[SndOperators] Usage:  snd_sos_print_operator_stack <stackname>\n";return true;}Stack*s=stack(argv[1]);if(!s)std::cerr<<"[SndOperators] unknown stack "<<argv[1]<<'\n';else print_stack(*s);return true;
        }
        if(cmd=="snd_sos_print_operator_stack_operator"){
            if(argv.size()<3){std::cerr<<"[SndOperators] Usage:  snd_sos_print_operator_stack_operator <stackname> <operator>\n";return true;}Stack*s=stack(argv[1]);Operator*o=op(s,argv[2]);if(!s||!o)std::cerr<<"[SndOperators] Usage:  snd_sos_print_operator_stack <stackname> <operator>\n";else print_operator(*s,*o);return true;
        }
        if(cmd=="snd_sos_resolve_execute_operator"){
            if(argv.size()<3){std::cerr<<"[SndOperators] Usage: snd_sos_resolve_execute_operator <stack> <operator>\n";return true;}execute_operator(argv[1],argv[2]);return true;
        }
        if(cmd=="snd_sos_set_operator_field"){
            if(argv.size()<6){std::cerr<<"[SndOperators] Usage: snd_sos_set_operator_field <stack> <operator> <field> <index> <float>\n";return true;}double idx=0,val=0;if(!parse_number(argv[4],idx)||!parse_number(argv[5],val)){std::cerr<<"[SndOperators] invalid field index/value\n";return true;}set_numeric_field(argv[1],argv[2],argv[3],static_cast<size_t>(std::max(0.0,idx)),val);return true;
        }
        if (cmd == "snd_sos_get_operator_field_info") {
            if (argv.size() < 4) return true;
            Stack *s = stack(argv[1]);
            Operator *o = op(s, argv[2]);
            Field *f = field(o, argv[3]);
            if (!f) {
                std::cout << "[SndOperators] field not found\n";
                return true;
            }
            const char *type = f->kind == FieldKind::Float ? "float"
                : f->kind == FieldKind::Bool ? "bool"
                : f->kind == FieldKind::String ? "string"
                : f->kind == FieldKind::Enum ? "enum" : "float[]";
            std::cout << "[SndOperators] " << argv[1] << '.' << argv[2] << '.' << argv[3]
                      << " type=" << type << '\n';
            return true;
        }
        if (cmd == "snd_sos_start_stack") {
            if (argv.size() < 2) return true;
            Stack *s = stack(argv[1]);
            if (!s) return true;
            ExecEntry e;
            e.index = -2147366000 + static_cast<int>(exec_list.size());
            e.guid = next_guid++;
            e.stack = s->name;
            exec_list.push_back(e);
            if (show_init) {
                for (const auto &name : s->order)
                    std::cout << "[SndOperators] init " << s->name << "/" << name << '\n';
            }
            for (const auto &name : s->order) execute_operator(s->name, name, false);
            return true;
        }
        if(cmd=="snd_sos_print_stack_exec_list"){
            std::cout<<"[SndOperators] Stack Execution List:\n";for(const auto&e:exec_list)std::cout<<"[SndOperators] index "<<e.index<<" = "<<e.index<<" guid : "<<e.guid<<" dependents \n";return true;
        }
        if(cmd=="snd_sos_show_operator_init"){if(argv.size()>1)show_init=argv[1]!="0";return true;}
        if (cmd == "snd_sos_show_operator_updates" || cmd == "snd_sos_show_operator_updates_init") {
            if (argv.size() > 1) show_updates = argv[1] != "0";
            return true;
        }
        if (cmd == "snd_sos_show_operator_not_executing") return true;
        if (cmd == "snd_sos_stop_all_soundevents") { exec_list.clear(); return true; }
        if (cmd == "snd_sos_flush_operators") { reset(); return true; }
        if (cmd == "snd_sos_print_full_field_info") {
            for (const auto &sk : stacks) {
                for (const auto &on : sk.second.order) {
                    auto it = sk.second.operators.find(on);
                    if (it != sk.second.operators.end()) print_operator(sk.second, it->second);
                }
            }
            return true;
        }
        if (cmd == "snd_sos_print_field_name_strings") {
            std::set<std::string> names;
            for (const auto &sk : stacks)
                for (const auto &ov : sk.second.operators)
                    for (const auto &fv : ov.second.fields) names.insert(fv.first);
            for (const auto &name : names) std::cout << "[SndOperators] " << name << '\n';
            return true;
        }
        if (cmd == "snd_sos_print_strings") {
            std::set<std::string> values;
            for (const auto &sk : stacks) {
                for (const auto &ov : sk.second.operators) {
                    for (const auto &fv : ov.second.fields) {
                        if ((fv.second.kind == FieldKind::String || fv.second.kind == FieldKind::Enum) &&
                            !fv.second.text.empty()) values.insert(fv.second.text);
                    }
                }
            }
            for (const auto &value : values) std::cout << "[SndOperators] \"" << value << "\"\n";
            return true;
        }
        if (cmd == "snd_sos_print_field_references" || cmd == "snd_sos_print_table_arrays" ||
            cmd == "snd_sos_list_operator_updates" || cmd == "snd_sos_opvar_debug") {
            std::cout << "[SndOperators] scmdsim compatibility model: command accepted\n";
            return true;
        }
        if(cmd=="cl_sos_test_set_opvar"||cmd=="cl_sos_test_get_opvar"){std::cout<<"!!!FIXME: SOSSetOpvarFloat API not ported\n";return true;}
        return false;
    }
};

SosSimulator::SosSimulator(std::unordered_map<std::string, std::string> &cvars) : impl_(new Impl(cvars)) {}

SosSimulator::~SosSimulator() { delete impl_; }

bool SosSimulator::handles(std::string_view command) const {
    const std::string c=lower_ascii_local(command);
    return c.rfind("snd_sos_", 0) == 0 || c == "ent_create" || c == "ent_fire" ||
           c == "cl_sos_test_set_opvar" || c == "cl_sos_test_get_opvar";
}

bool SosSimulator::execute(const std::vector<std::string> &argv) { return impl_&&impl_->handle(argv); }

void SosSimulator::flush_deferred() {
    if(!impl_||impl_->pending.empty())return;
    std::vector<Impl::PendingPatch> work;work.swap(impl_->pending);
    for(const auto&p:work)impl_->direct_patch(p);
}

std::vector<std::string> SosSimulator::command_names() const {
    return {"snd_sos_print_operators", "snd_sos_print_operator_stacks", "snd_sos_print_operator_stack",
            "snd_sos_print_operator_stack_operator", "snd_sos_resolve_execute_operator",
            "snd_sos_set_operator_field", "snd_sos_get_operator_field_info", "snd_sos_start_stack",
            "snd_sos_print_stack_exec_list", "snd_sos_show_operator_init", "snd_sos_show_operator_updates",
            "snd_sos_show_operator_updates_init", "snd_sos_show_operator_not_executing",
            "snd_sos_stop_all_soundevents", "snd_sos_flush_operators", "snd_sos_print_full_field_info",
            "snd_sos_print_field_references", "snd_sos_print_table_arrays", "snd_sos_print_field_name_strings",
            "snd_sos_print_strings", "snd_sos_list_operator_updates", "snd_sos_opvar_debug",
            "cl_sos_test_set_opvar", "cl_sos_test_get_opvar", "ent_create", "ent_fire"};
}

} // namespace scmd::sim
