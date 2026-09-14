#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace scmd::bc {

constexpr uint16_t kAbiVersion = 1;
constexpr uint32_t kNoString = 0xffffffffu;
constexpr uint32_t kBlockModule = 1u;
constexpr size_t kRegisterCount = 16;

enum class Op : uint8_t {
    Nop = 0,
    KStr,
    KImm,
    AliasList,
    AliasQuery,
    AliasSet,
    AliasQueryI,
    AliasSetI,
    Exec,
    ExecIfExists,
    ExecAsync,
    ExecI,
    ExecIfExistsI,
    ExecAsyncI,
    Sleep,
    SleepI,
    Clear,
    Echo,
    EchoLn,
    Say,
    SayTeam,
    EchoI,
    EchoLnI,
    SayI,
    SayTeamI,
    SetInfo,
    SetInfoI,
    IncrementVar,
    MultVar,
    Toggle,
    Dispatch,
    Dispatch0,
    Dispatch1,
    DispatchRaw,
    Ret,
};

struct Instruction {
    uint8_t op = 0;
    uint8_t dst = 0;
    uint8_t a = 0;
    uint8_t b = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
};
static_assert(sizeof(Instruction) == 16);

struct Block {
    uint32_t name_sid = kNoString;
    uint32_t first = 0;
    uint32_t count = 0;
    uint32_t flags = 0;
};

struct Package {
    std::string profile = "cs2-2026";
    std::vector<std::string> strings;
    std::unordered_map<std::string, uint32_t> string_ids;
    std::vector<Block> blocks;
    std::vector<Instruction> code;
    std::unordered_map<std::string, uint32_t> modules;
    std::vector<std::string> module_names;

    uint32_t intern(std::string_view value);
    const std::string &str(uint32_t sid) const;
    uint32_t compile_text(std::string_view text, std::string_view block_name = {}, uint32_t flags = 0);
    bool compile_cfg_root(const std::filesystem::path &root, std::string &error);
    bool compile_cfg_module(const std::filesystem::path &path, std::string_view module_name, std::string &error);
    bool merge_from(const Package &other, std::string &error);
    bool save(const std::filesystem::path &path, std::string &error) const;
    bool load(const std::filesystem::path &path, std::string &error);
    bool verify(std::string &error) const;
    uint32_t find_module(std::string_view ref) const;
    std::vector<std::string> complete_exec(std::string_view prefix) const;
};

std::string trim(std::string_view sv);
std::string lower_ascii(std::string_view sv);
std::vector<std::string> split_commands(std::string_view text);
std::vector<std::string> tokenize(std::string_view line);
std::string join_tokens(const std::vector<std::string> &v, size_t first);
std::string normalize_exec_ref(std::string ref);

} // namespace scmd::bc
