#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace scmd::sim {

/*
 * Compatibility model for the Source 2 Sound Operator System (SOS) surface
 * exercised by SCMD's 2026 CS2 experiments. This is intentionally not an
 * audio renderer. It models operator fields, opvar addressing, selected math
 * operators, convar bridges, debug commands, and the snd_opvar_set entity so
 * generated CFG experiments can be reproduced outside the game.
 */
class SosSimulator {
public:
    explicit SosSimulator(std::unordered_map<std::string, std::string> &cvars);
    ~SosSimulator();

    SosSimulator(const SosSimulator &) = delete;
    SosSimulator &operator=(const SosSimulator &) = delete;
    SosSimulator(SosSimulator &&) = delete;
    SosSimulator &operator=(SosSimulator &&) = delete;

    bool handles(std::string_view command) const;
    bool execute(const std::vector<std::string> &argv);
    void flush_deferred();
    std::vector<std::string> command_names() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace scmd::sim
