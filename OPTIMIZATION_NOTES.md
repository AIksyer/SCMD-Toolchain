# SCMD / vCS-16 optimization pass (2026-09-14)

This snapshot contains one conservative compiler optimization pass, one vCS-16 A1 decode/fetch optimization pass, and an experimental Source 2 Sound Operator System (SOS) compatibility model in `scmdsim`.

## Compiler

The CS2 CFG backend now avoids unnecessary 8-bit temporary banks when an assignment can be lowered safely in place:

- direct `u8` copy and self-copy elimination;
- add/subtract immediate with carry/borrow latches;
- direction-aware constant shifts;
- multiply by 0/1/power-of-two;
- divide/modulo by power-of-two;
- lane-local bitwise expressions and safe shallow fixed-shift expressions.

Complex expressions keep the previous conservative snapshot fallback.

## vCS-16 A1

The ISA and self-test are unchanged. The implementation now uses:

- a 3-bit decision tree for register-file read/write selection;
- a balanced bit tree for ROM fetch instead of a linear `pc == address` chain;
- the compiler fast paths above for decode and ALU expressions.

Measured on the included A1 self-test, using the same Linux Release toolchain before/after:

| Metric | Original | Optimized | Change |
| --- | ---: | ---: | ---: |
| generated CFG files | 280 | 178 | -36.4% |
| generated CFG bytes | 400,775 | 270,065 | -32.6% |
| generated aliases | 7,410 | 4,632 | -37.5% |
| compile wall time (single sample) | 0.09 s | 0.03 s | ~3.0x |
| simulator wall time (single sample) | 0.18 s | 0.05 s | ~3.6x |

The wall-time samples are environment-dependent; the file/byte/alias counts are deterministic for this snapshot.

A1 still reports:

```text
CPU HALT: PASS
R0 = 0x0037 (55): PASS
R1 = 0x000B (11): PASS
PC = 0x000E after HALT: PASS
vCS-16 A1 CORE: PASS
```

## CS2 command-length safety

The optimizer now treats the CS2 console tokenizer limit as a hard backend constraint. A single generated console command may contain at most **510 UTF-8 bytes**. Alias-chain fusion stops before `alias NAME "BODY"` would cross that limit, and final CFG emission validates every alias definition before writing files. This prevents CS2 from dropping optimized aliases with `WARNING: Command too long... ignoring!`.

`scmdsim` models the same limit when loading CFG, so oversized commands are ignored with the same warning instead of silently working in simulation.

## `scmdsim` SOS compatibility model

`scmdsim` now models the SOS behavior that was verified in a live 2026 CS2 client, including:

- selected SOS debug commands and operator/stack dumps;
- float32-like field precision;
- `math_float`, filter, remap and switch operators;
- local opvar get/set/increment and indexed array reads;
- `diagnostic_globals/test_opvars` fixtures;
- numeric ConVar <-> SOS bridges;
- deferred `ent_create snd_opvar_set ... setOnSpawn 1` field patches;
- persistent `snd_opvar_set` entity inputs for runtime/deferred indexed stores;
- observed cross-stack write-context asymmetry;
- static/flattened `sos_import_stack` behavior after runtime `import_stack` mutation;
- the unported `cl_sos_test_*` FIXME behavior.

This is a compatibility fixture, not an audio renderer and not a claim to emulate all of Source 2 SOS. Unknown/unverified operators remain outside the model until their behavior is measured.

## Validation

The repository test suite passes **46/46**, including the new `scmdsim_sos_compat` regression test and the existing vCS-16 CFG + SCB bring-up tests.

## Fast-load pass for large CFG projects

A second conservative pass targets startup cost for generated projects with hundreds of public commands.

### Narrow inferred global state storage

Global `u8` values that are only initialized/assigned compile-time constants now use the minimum number of alias bits needed by their observed value range. High bits are compile-time false when the value is read. Any non-constant/compound assignment or uncertain local shadowing keeps the full 8-bit representation.

This is especially effective for generated menu/state-machine projects where most `u8` globals are really 2-6 bit enums.

### Static straight-line `main()` bootstrap

When optimized `main()` consists only of safe, semicolon-free `command.exec("...")` statements, the compiler emits them as raw commands in a generated `bootstrap.cfg` instead of constructing one continuation alias per statement. General `main()` functions keep the normal CFG lowering.

This removes hundreds of one-shot alias definitions/dispatches from startup-heavy registration scripts without changing the source language.

### SCMD 2.0 page packing

The supplied large-project validation build keeps the 4096-byte page ceiling but raises its project-specific command ceiling from 40 to 64. The byte ceiling remains the hard safety bound; the 64-command setting mainly avoids unnecessary `exec` page boundaries.

Measured on the SCMD 2.0 source used during this pass:

| Metric | Previous command-limit-safe build | Fast-load build | Change |
| --- | ---: | ---: | ---: |
| CFG pages | 318 | 169 | -46.9% |
| CFG bytes | 860,495 | 645,900 | -24.9% |
| alias definitions | 12,217 | 9,583 | -21.6% |
| top-level non-comment commands | 12,540 | 9,759 | -22.2% |
| maximum individual command | 499 B | 499 B | within 510 B limit |

`scmdsim` startup accounting for this project also drops materially (about 14.2k -> 10.1k modeled commands in the measured run), while one-shot alias dispatches fall sharply because the registration-only `main()` no longer executes through a continuation chain.

## Mandatory demand-loaded function bodies

The CS2 backend now makes function-body demand loading part of normal code generation. It is not an optimization toggle and cannot be disabled. `--no-opt` still disables CFG peephole/size optimization, but it does not return to eager function-body loading.

The eager package contains runtime/global aliases, per-function return slots, `main()`, and one stable entry stub for every other function. A first call executes the function module; the module redefines that same entry alias to the real body and invokes it. Later calls therefore pay no module-load cost. Function modules use the same byte/command paging limits as the eager package.

To keep modules self-contained, alias ownership is tracked through CFG optimization and forward/dedup/fusion is prevented from crossing function-module boundaries. Return slots remain eager so a caller can install its continuation before the first call loads a callee.

Measured on the SCMD 2.0 Beta 3 project used above:

| Metric | Fast-load eager build | Demand-loaded eager startup | Change |
| --- | ---: | ---: | ---: |
| startup/eager CFG pages | 169 | 25 | -85.2% |
| startup non-comment commands incl. bootstrap | 9,755 | 2,184 | -77.6% |
| startup alias definitions incl. bootstrap | 9,583 | 2,156 | -77.5% |
| startup CFG bytes incl. bootstrap | 645,788 | 84,594 | -86.9% |
| `scmdsim` commands for `exec scmd` | 10,109 | 2,695 | -73.3% |
| `scmdsim` lazily compiled CFG modules for `exec scmd` | 172 | 32 | -81.4% |

The complete package now contains one lazily referenced page chain per function (the stub jumps straight to page 000, avoiding a redundant per-function entry file). The optimization is specifically about CS2 startup parsing and command-buffer pressure; total generated CFG size for this snapshot is about 740 KB while eager startup reads only about 85 KB.

## Validation update

The repository test suite passes **51/51**, including a regression that verifies demand loading both with optimization enabled and with `--no-opt`, plus the existing command-length, SOS compatibility, vCS-16 CFG and SCB tests.

## 2026-09-15 demand-load dependency bundling fix

The first mandatory demand-loader shipped function bodies one function per module. That was
semantically workable, but it was the wrong granularity for console UIs: a caller could begin
printing and then lazily load a tiny static helper, causing CS2's `[InputService] execing ...`
message to appear in the middle of the UI.

The backend now co-loads the transitive static call closure of helpers that have no local
storage. Stateful callees remain independent demand-loaded modules, so loading an unrelated
caller can never reset another function's locals. This keeps demand loading mandatory while
making ordinary helper calls silent after the caller module has loaded.

`scmdsim --profile cs2-2026` now also models synchronous `exec` messages as
`[InputService] execing <path>`. This exposed the original UI contamination in regression tests
and prevents the simulator from hiding this class of real-CS2 integration bug again.

SCMD 2.0 validation with the new simulator shows no `InputService` exec lines inside any of the
35 rendered menu blocks exercised by the menu sweep. For the Quick Commands page specifically,
the old build emitted four helper loads (`f0212`..`f0215`) between menu rows; the fixed build
emits zero loader messages inside the rendered menu. Loader messages, where unavoidable, are
confined to the pre-render load phase.

## downstream system / vCS-16/2 bring-up (2026-09-15)

This working tree adds the first downstream system system bring-up and refactors new vCS development away from the legacy A1 hardware-CPU model.

### Stable exported Console entries

SCMD now accepts `export function name()`. The compiler emits a stable public Console alias for the function while keeping its body under mandatory demand loading. Exported names are restricted to safe ASCII Console identifiers and cannot shadow modeled CS2/SCMD builtins. Exported callees are also lazy-bundle boundaries, preventing public module implementations from being copied into unrelated lazy bundles.

### vCS-16/2 libraries

`libvcs16` provides a C ABI assembler, verifier, VXE2 reader/writer and reference VM. vCS-16/2 is flags-free and CFG-oriented: conditional branches compare registers directly; `CALL/RET`, `LD8/ST8` and `SYS` are architectural operations rather than hardware-MMIO emulation.

`libvcs16_scmd` and `vcs16scmd` provide the first SCMD AOT backend. The backend preserves architectural register/PC semantics but removes runtime ROM fetch/opcode decode. Regression coverage exercises 16-bit ALU, CALL/RET, dynamic LD8/ST8, conditional branch and SYS through generated SCMD and `scmdsim`.

The previous `examples/vcs16-a1` remains as a legacy/reference CPU-simulation experiment and continues to pass both CFG and SCB bring-up tests.

### downstream system 0.1-dev

downstream system now boots through an AOT-lowered vCS-16/2 kernel module. SCMD firmware supplies TTY/AliasFS/Console host services. The first vCS user program (`vhello`) uses the same SYS ABI.

Current bring-up includes:

- 4 KiB logical TTY history (64 records) and 24-line redraw viewport;
- tokenized `command+` argv collection with `end/back/args/cancel`;
- 16-inode AliasFS with 1024 `u8` token cells (1 KiB logical token backing);
- create/read/write/remove file operations and root-level directory create/remove;
- cwd, `ls`, `pwd`, `df`, `ps`, `kill`, redraw and async counter task demo;
- vCS-16/2 kernel boot and vCS userland command path.

The simulator compatibility suite now explicitly proves that `|` remains literal text: it must not execute the command to its right, matching current CS2 behavior.

### Validation

The repository test suite passes **58/58** in Release, Clang `-Werror`, and Clang AddressSanitizer + UndefinedBehaviorSanitizer configurations. downstream system regression tests cover boot/TTY, file mutation/error paths, argv controls, and vCS userland execution.

## 0.11.1: control-flow dynamic array loads

SCMD 0.11.1 adds a CS2-backend fast path for direct dynamic fixed-array reads:

```scmd
u8 value = 0;
value = values[index];
```

Instead of materializing every candidate element through a full 8-bit combinational mux,
the backend builds a balanced index decision tree and copies only the selected leaf into the
destination. `bool` arrays use the same control-flow selection strategy. Constant-index reads
remain direct element accesses, and array reads nested inside larger expressions keep the
existing mux lowering.

For non-power-of-two arrays, unused leaves preserve the previous read semantics and produce
zero/false. A dedicated regression covers dynamic `u8`/`bool` reads, an unused leaf, and a
64-element array.

The optimization is especially effective for AliasOS state tables such as TTY history and
inode metadata, where the program naturally performs `dst = table[index]` reads.

### AliasOS downstream measurement

Using the same scripted `boot -> ls -> cd -> cat -> writetok` workload and the SCMD-native
(no-Python) AliasOS source:

| Metric | SCMD 0.11 baseline | 0.11.1 + source/backend pass | Change |
| --- | ---: | ---: | ---: |
| cold-boot modeled commands | 37,983 | 26,975 | -29.0% |
| cold-boot aliases | 25,510 | 21,471 | -15.8% |
| cold-boot lazy modules | 657 | 553 | -15.8% |
| cumulative commands after workload | 278,424 | 206,178 | -25.9% |
| cumulative lazy modules | 1,524 | 921 | -39.6% |
| generated CFG files | 18,274 | 11,934 | -34.7% |
| generated CFG bytes | 44,357,197 | 28,380,736 | -36.0% |

The AliasOS source pass also removes unnecessary exported aliases from a handful of hot
internal TTY/shell helpers while deliberately retaining export boundaries where they improve
demand-load granularity. Removing every internal export was tested and rejected because it
caused large first-use dependency bundles.

### Validation

SCMD 0.11.1 Release validation: **68/68 CTest PASS** before packaging.
