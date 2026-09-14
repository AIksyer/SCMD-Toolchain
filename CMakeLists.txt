cmake_minimum_required(VERSION 3.22)
project(scmdc VERSION 0.10.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SCMD_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(SCMD_ENABLE_SANITIZERS "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# Keep CMake/Ninja internals under out/, and place user-facing executables in
# dist/<flavor>/.  This avoids mixing tools with object files, caches, and test
# scratch data.  Presets set SCMD_DIST_FLAVOR explicitly; the fallback keeps
# ad-hoc CMake builds useful as well.
set(SCMD_DIST_FLAVOR "" CACHE STRING "Distribution flavor directory (debug/release/sanitize)")
if(NOT SCMD_DIST_FLAVOR)
    if(CMAKE_BUILD_TYPE)
        string(TOLOWER "${CMAKE_BUILD_TYPE}" SCMD_DIST_FLAVOR)
    else()
        set(SCMD_DIST_FLAVOR "default")
    endif()
endif()
set(SCMD_DIST_DIR "${CMAKE_SOURCE_DIR}/dist/${SCMD_DIST_FLAVOR}" CACHE PATH
    "Directory for user-facing SCMD executables")

add_library(scmd_core STATIC
    src/frontend/ast.c
    src/common/common.c
    src/frontend/lexer.c
    src/frontend/keywords.c
    src/frontend/parser.c
    src/frontend/sema.c
    src/compiler/codegen.c
    src/compiler/loader.c
    src/compiler/project.c
)
target_include_directories(scmd_core PUBLIC include)

add_library(scmd_bytecode_core STATIC
    src/scb/bytecode.cpp
)
target_include_directories(scmd_bytecode_core PUBLIC include src/scb)

add_library(scmd_sim_core STATIC
    src/simulator/console_sim.cpp
)
target_include_directories(scmd_sim_core PUBLIC include src/scb)
target_link_libraries(scmd_sim_core PUBLIC scmd_bytecode_core)

# windows.h defines min/max macros unless NOMINMAX is set. They collide with
# std::numeric_limits<T>::max() and other standard C++ APIs. Keep those legacy
# macros out of the simulator on Windows and reduce Win32 header pollution.
if(WIN32)
    target_compile_definitions(scmd_sim_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_executable(scmdc src/compiler/main.c)
target_link_libraries(scmdc PRIVATE scmd_core scmd_bytecode_core)

add_executable(scmdsim src/simulator/main.cpp)
target_link_libraries(scmdsim PRIVATE scmd_sim_core)

# Both public tools live in dist/. Static libraries and every other build
# artifact remain in the CMake binary tree under out/. The per-config
# properties also make this deterministic if someone uses a multi-config
# generator instead of the supported Ninja presets.
foreach(tool scmdc scmdsim)
    set_target_properties(${tool} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${SCMD_DIST_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${SCMD_DIST_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${SCMD_DIST_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${SCMD_DIST_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${SCMD_DIST_DIR}"
    )
endforeach()

# An explicit aggregate target makes the intended public build products clear
# and lets build scripts fail if either tool cannot be produced.
add_custom_target(scmd_tools ALL DEPENDS scmdc scmdsim)

function(scmd_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(SCMD_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(SCMD_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

foreach(target scmd_core scmd_bytecode_core scmd_sim_core scmdc scmdsim)
    scmd_apply_warnings(${target})
endforeach()

if(SCMD_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "SCMD_ENABLE_SANITIZERS currently requires Clang/GCC-style flags")
    endif()
    foreach(target scmd_core scmd_bytecode_core scmd_sim_core scmdc scmdsim)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endforeach()
    target_link_options(scmdc PRIVATE -fsanitize=address,undefined)
    target_link_options(scmdsim PRIVATE -fsanitize=address,undefined)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT SCMD_IPO_SUPPORTED OUTPUT SCMD_IPO_ERROR)
if(SCMD_IPO_SUPPORTED)
    set_property(TARGET scmdc scmdsim scmd_core scmd_bytecode_core scmd_sim_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME scmdc_cli_help COMMAND $<TARGET_FILE:scmdc> --help)
    set_tests_properties(scmdc_cli_help PROPERTIES
        PASS_REGULAR_EXPRESSION "separate scmdsim tool"
        FAIL_REGULAR_EXPRESSION "console <cfg-root>"
    )

    add_test(NAME scmdsim_cli_help COMMAND $<TARGET_FILE:scmdsim> --help)
    set_tests_properties(scmdsim_cli_help PROPERTIES
        PASS_REGULAR_EXPRESSION "quit or exit to leave"
    )

    add_test(NAME scmd_tools_version_match
        COMMAND ${CMAKE_COMMAND}
                -DSCMDC=$<TARGET_FILE:scmdc>
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DEXPECTED=${PROJECT_VERSION}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_versions.cmake
    )

    add_test(
        NAME scmdsim_profile_cs2_2026
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --profile cs2-2026
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/case.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(scmdsim_profile_cs2_2026 PROPERTIES PASS_REGULAR_EXPRESSION "CASE_PASS")

    add_test(
        NAME scmdsim_unknown_profile_rejected
        COMMAND ${CMAKE_COMMAND}
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_bad_profile.cmake
    )

    function(scmd_compile_test name source)
        add_test(
            NAME ${name}
            COMMAND $<TARGET_FILE:scmdc>
                    ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                    -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.cfg
                    --console-mode sync
        )
    endfunction()

    scmd_compile_test(scmd_core tests/v06_core.scmd)
    scmd_compile_test(scmd_arithmetic tests/arithmetic.scmd)
    scmd_compile_test(scmd_ripple_runtime tests/ripple_runtime.scmd)
    scmd_compile_test(scmd_loops tests/loops.scmd)
    scmd_compile_test(scmd_block tests/block.scmd)
    scmd_compile_test(scmd_same_line_brace tests/style_same_line.scmd)
    scmd_compile_test(scmd_multifile tests/multifile/main.scmd)
    scmd_compile_test(scmd_unicode examples/unicode.scmd)
    scmd_compile_test(scmd_adder8 examples/adder8.scmd)

    add_test(
        NAME scmd_missing_if_parentheses_rejected
        COMMAND $<TARGET_FILE:scmdc>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/invalid_no_paren.scmd
                -o ${CMAKE_CURRENT_BINARY_DIR}/invalid.cfg
                --console-mode sync
    )
    set_tests_properties(scmd_missing_if_parentheses_rejected PROPERTIES WILL_FAIL TRUE)

    add_test(
        NAME scmd_recursion_rejected
        COMMAND $<TARGET_FILE:scmdc>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/recursive.scmd
                -o ${CMAKE_CURRENT_BINARY_DIR}/recursive.cfg
                --console-mode sync
    )
    set_tests_properties(scmd_recursion_rejected PROPERTIES WILL_FAIL TRUE)

    add_test(
        NAME sim_alias_trailing_args
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --exec alias
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/alias.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_alias_trailing_args PROPERTIES
        PASS_REGULAR_EXPRESSION "\\[ARG\\]"
        FAIL_REGULAR_EXPRESSION "\\[ARG\\] 12345"
    )

    add_test(
        NAME sim_pipe_is_literal
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/pipe.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_pipe_is_literal PROPERTIES
        PASS_REGULAR_EXPRESSION "12345 \\| setinfo vcs_pipe_value"
    )

    add_test(
        NAME sim_async_scheduler
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --exec async
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_async_scheduler PROPERTIES
        PASS_REGULAR_EXPRESSION "A\nB\nC"
    )

    add_test(
        NAME sim_incrementvar_wrap
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/increment.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_incrementvar_wrap PROPERTIES
        PASS_REGULAR_EXPRESSION "tv_window_size = 0"
    )

    add_test(
        NAME sim_loop_budget
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/loop.script
                --no-interactive --no-engine-messages --no-ansi --max-commands 1000
    )
    set_tests_properties(sim_loop_budget PROPERTIES WILL_FAIL TRUE)

    add_test(
        NAME scmd_runtime_smoke
        COMMAND ${CMAKE_COMMAND}
                -DSCMD=$<TARGET_FILE:scmdc>
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_smoke.scmd
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}/runtime-smoke
                -DNAME=runtime_smoke
                -DEXPECT=RUNTIME_SMOKE_PASS
                -DMODE=sync
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_compiled_sim.cmake
    )

    add_test(
        NAME scmd_runtime_async_smoke
        COMMAND ${CMAKE_COMMAND}
                -DSCMD=$<TARGET_FILE:scmdc>
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_async.scmd
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}/runtime-async
                -DNAME=runtime_async
                -DEXPECT=ASYNC_RUNTIME_PASS
                -DMODE=async
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_compiled_sim.cmake
    )

    add_test(
        NAME scmd_project_run_smoke
        COMMAND ${CMAKE_COMMAND}
                -DSCMD=$<TARGET_FILE:scmdc>
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/examples/adder8-project/adder8.scmdproj
                -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/project-smoke-adder8
                -DEXEC_NAME=adder8
                -DEXPECT=200.*100.*44.*PASS
                -DMAX_COMMANDS=5000000
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_project_sim.cmake
    )

    add_test(
        NAME sim_alias_state_rebinding
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --exec alias_state
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_alias_state_rebinding PROPERTIES
        PASS_REGULAR_EXPRESSION "ON\nOFF\nON"
    )

    add_test(
        NAME sim_quoted_semicolon
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --exec quote
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_quoted_semicolon PROPERTIES
        PASS_REGULAR_EXPRESSION "X;Y\nA\nB"
    )

    add_test(
        NAME sim_nested_exec
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --exec nested_root
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_nested_exec PROPERTIES
        PASS_REGULAR_EXPRESSION "ROOT_A\nCHILD\nROOT_B"
    )

    add_test(
        NAME sim_async_virtual_time_order
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --exec async_order
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_async_virtual_time_order PROPERTIES
        PASS_REGULAR_EXPRESSION "ROOT_A\nROOT_B\nFAST\nSLOW"
    )

    add_test(
        NAME sim_path_traversal_rejected
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/traversal.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_path_traversal_rejected PROPERTIES
        PASS_REGULAR_EXPRESSION "invalid cfg path"
        FAIL_REGULAR_EXPRESSION "TRAVERSAL_BAD"
    )

    add_test(
        NAME sim_case_insensitive_commands_aliases
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/case.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_case_insensitive_commands_aliases PROPERTIES
        PASS_REGULAR_EXPRESSION "CASE_PASS"
    )

    add_test(
        NAME sim_execifexists_missing_is_nonfatal
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/execifexists.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_execifexists_missing_is_nonfatal PROPERTIES
        PASS_REGULAR_EXPRESSION "EXECIFEXISTS_PASS"
        FAIL_REGULAR_EXPRESSION "couldn't exec"
    )

    add_test(
        NAME sim_script_meta_quit
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/meta_quit.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(sim_script_meta_quit PROPERTIES
        PASS_REGULAR_EXPRESSION "BEFORE_QUIT"
        FAIL_REGULAR_EXPRESSION "AFTER_QUIT_BAD"
    )

    add_test(
        NAME scmd_const_edge_semantics
        COMMAND ${CMAKE_COMMAND}
                -DSCMD=$<TARGET_FILE:scmdc>
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/const_edges.scmd
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}/const-edges
                -DNAME=const_edges
                -DEXPECT=CONST_EDGES_PASS
                -DMODE=sync
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_compiled_sim.cmake
    )

    add_test(
        NAME scmd_arithmetic_matrix
        COMMAND ${CMAKE_COMMAND}
                -DSCMD=$<TARGET_FILE:scmdc>
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/arithmetic_matrix.scmd
                -DOUTDIR=${CMAKE_CURRENT_BINARY_DIR}/arithmetic-matrix
                -DNAME=arithmetic_matrix
                -DEXPECT=ARITH_MATRIX_PASS
                -DMODE=sync
                -DMAX_COMMANDS=20000000
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_compiled_sim.cmake
    )
    set_tests_properties(scmd_arithmetic_matrix PROPERTIES TIMEOUT 60)

    add_test(
        NAME vcs16_a1_simulator_bringup
        COMMAND ${CMAKE_COMMAND}
                -DSCMD=$<TARGET_FILE:scmdc>
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/examples/vcs16-a1/vcs16-a1.scmdproj
                -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/project-smoke-vcs16
                -DEXEC_NAME=vcs16_a1
                -DEXPECT=vCS-16.A1.CORE:.PASS
                -DMAX_COMMANDS=2000000
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_project_sim.cmake
    )
    set_tests_properties(vcs16_a1_simulator_bringup PROPERTIES TIMEOUT 20)
endif()

install(TARGETS scmdc scmdsim RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    add_test(
        NAME scmd_bytecode_roundtrip
        COMMAND ${CMAKE_COMMAND}
                -DSCMDC=$<TARGET_FILE:scmdc>
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                -DOUT=${CMAKE_CURRENT_BINARY_DIR}/roundtrip.scb
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/bytecode_roundtrip.cmake
    )

    add_test(
        NAME scmd_bytecode_invalid_rejected
        COMMAND ${CMAKE_COMMAND}
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DBAD=${CMAKE_CURRENT_BINARY_DIR}/bad.scb
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/bad_scb.cmake
    )

    add_test(
        NAME scmdsim_exec_path_completion
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/complete_exec.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(scmdsim_exec_path_completion PROPERTIES
        PASS_REGULAR_EXPRESSION "nested/child"
    )

    add_test(
        NAME scmdsim_default_current_directory
        COMMAND $<TARGET_FILE:scmdsim>
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/case.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(scmdsim_default_current_directory PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
        PASS_REGULAR_EXPRESSION "CASE_PASS"
    )

    add_test(
        NAME vcs16_a1_bytecode_bringup
        COMMAND ${CMAKE_COMMAND}
                -DSCMD=$<TARGET_FILE:scmdc>
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/examples/vcs16-a1/vcs16-a1.scmdproj
                -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/project-bytecode-vcs16
                -DEXEC_NAME=vcs16_a1
                -DEXPECT=vCS-16.A1.CORE:.PASS
                -DMAX_COMMANDS=2000000
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_project_scb.cmake
    )
    set_tests_properties(vcs16_a1_bytecode_bringup PROPERTIES TIMEOUT 20)

    add_test(
        NAME scmdsim_plain_quit
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/quit_plain.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(scmdsim_plain_quit PROPERTIES
        FAIL_REGULAR_EXPRESSION "SHOULD_NOT_RUN"
    )

    add_test(
        NAME scmdsim_plain_exit
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/exit_plain.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(scmdsim_plain_exit PROPERTIES
        FAIL_REGULAR_EXPRESSION "SHOULD_NOT_RUN"
    )

    add_test(
        NAME scmdsim_lazy_cfg_mode
        COMMAND $<TARGET_FILE:scmdsim>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/cfg
                --script ${CMAKE_CURRENT_SOURCE_DIR}/tests/sim/lazy_stats.script
                --no-interactive --no-engine-messages --no-ansi
    )
    set_tests_properties(scmdsim_lazy_cfg_mode PROPERTIES
        PASS_REGULAR_EXPRESSION "source=cfg-lazy.*lazy_compiles=2"
    )

    add_test(
        NAME scmdsim_lazy_cache_reload
        COMMAND ${CMAKE_COMMAND}
                -DSCMDSIM=$<TARGET_FILE:scmdsim>
                -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/lazy-cache-test
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/lazy_cache.cmake
    )

endif()
