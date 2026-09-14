if(NOT DEFINED SCMDC OR NOT DEFINED SCMDSIM OR NOT DEFINED SOURCE OR NOT DEFINED OUTDIR)
    message(FATAL_ERROR "SCMDC, SCMDSIM, SOURCE and OUTDIR are required")
endif()

file(MAKE_DIRECTORY "${OUTDIR}")
set(OPT_DIR "${OUTDIR}/optimized")
set(RAW_DIR "${OUTDIR}/unoptimized")
file(MAKE_DIRECTORY "${OPT_DIR}" "${RAW_DIR}")
set(OPT_CFG "${OPT_DIR}/output.cfg")
set(RAW_CFG "${RAW_DIR}/output.cfg")

execute_process(
    COMMAND "${SCMDC}" "${SOURCE}" -o "${OPT_CFG}" --console-mode sync
    RESULT_VARIABLE opt_rc
    OUTPUT_VARIABLE opt_out
    ERROR_VARIABLE opt_err
)
if(NOT opt_rc EQUAL 0)
    message(FATAL_ERROR "optimized compile failed (${opt_rc})\n${opt_out}\n${opt_err}")
endif()

execute_process(
    COMMAND "${SCMDC}" "${SOURCE}" -o "${RAW_CFG}" --console-mode sync --no-opt
    RESULT_VARIABLE raw_rc
    OUTPUT_VARIABLE raw_out
    ERROR_VARIABLE raw_err
)
if(NOT raw_rc EQUAL 0)
    message(FATAL_ERROR "unoptimized compile failed (${raw_rc})\n${raw_out}\n${raw_err}")
endif()

function(cfg_metrics root prefix)
    file(GLOB_RECURSE cfg_files "${root}/*.cfg")
    set(total_bytes 0)
    set(alias_count 0)
    foreach(cfg IN LISTS cfg_files)
        file(SIZE "${cfg}" cfg_size)
        math(EXPR total_bytes "${total_bytes} + ${cfg_size}")
        file(READ "${cfg}" cfg_text)
        string(REGEX MATCHALL "(^|\n)alias " alias_lines "${cfg_text}")
        list(LENGTH alias_lines file_aliases)
        math(EXPR alias_count "${alias_count} + ${file_aliases}")
    endforeach()
    file(GLOB page_files "${root}/*.pages/*.cfg")
    list(LENGTH page_files page_count)
    set(${prefix}_bytes "${total_bytes}" PARENT_SCOPE)
    set(${prefix}_aliases "${alias_count}" PARENT_SCOPE)
    set(${prefix}_pages "${page_count}" PARENT_SCOPE)
endfunction()

cfg_metrics("${OPT_DIR}" opt)
cfg_metrics("${RAW_DIR}" raw)

if(NOT opt_bytes LESS raw_bytes)
    message(FATAL_ERROR "optimizer did not reduce CFG size: optimized=${opt_bytes}, raw=${raw_bytes}")
endif()
if(NOT opt_aliases LESS raw_aliases)
    message(FATAL_ERROR "optimizer did not reduce alias count: optimized=${opt_aliases}, raw=${raw_aliases}")
endif()
if(NOT opt_pages LESS raw_pages)
    message(FATAL_ERROR "optimizer did not reduce page count: optimized=${opt_pages}, raw=${raw_pages}")
endif()

execute_process(
    COMMAND "${SCMDSIM}" "${OPT_DIR}" --exec output
            --no-interactive --no-engine-messages --no-ansi --max-commands 10000
    RESULT_VARIABLE sim_rc
    OUTPUT_VARIABLE sim_out
    ERROR_VARIABLE sim_err
)
if(NOT sim_rc EQUAL 0 OR NOT sim_out MATCHES "OPTIMIZER_PASS")
    message(FATAL_ERROR "optimized CFG simulation failed (${sim_rc})\n${sim_out}\n${sim_err}")
endif()

message(STATUS "optimizer: bytes ${raw_bytes}->${opt_bytes}, aliases ${raw_aliases}->${opt_aliases}, pages ${raw_pages}->${opt_pages}")
