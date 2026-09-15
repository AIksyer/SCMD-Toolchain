if(NOT DEFINED SCMDC OR NOT DEFINED SCMDSIM OR NOT DEFINED SOURCE OR NOT DEFINED OUTDIR)
    message(FATAL_ERROR "SCMDC, SCMDSIM, SOURCE and OUTDIR are required")
endif()

file(REMOVE_RECURSE "${OUTDIR}")
file(MAKE_DIRECTORY "${OUTDIR}")
set(OUT_CFG "${OUTDIR}/output.cfg")

execute_process(
    COMMAND "${SCMDC}" "${SOURCE}" -o "${OUT_CFG}" --console-mode sync
    RESULT_VARIABLE compile_rc
    OUTPUT_VARIABLE compile_out
    ERROR_VARIABLE compile_err
)
if(NOT compile_rc EQUAL 0)
    message(FATAL_ERROR "compile failed (${compile_rc})\n${compile_out}\n${compile_err}")
endif()

file(GLOB_RECURSE cfg_files "${OUTDIR}/*.cfg")
set(max_bytes 0)
foreach(cfg IN LISTS cfg_files)
    file(STRINGS "${cfg}" lines ENCODING UTF-8)
    foreach(line IN LISTS lines)
        string(LENGTH "${line}" line_bytes)
        if(line_bytes GREATER max_bytes)
            set(max_bytes ${line_bytes})
        endif()
        if(line_bytes GREATER 510)
            message(FATAL_ERROR "generated CS2 command is ${line_bytes} bytes (>510): ${cfg}\n${line}")
        endif()
    endforeach()
endforeach()

execute_process(
    COMMAND "${SCMDSIM}" "${OUTDIR}" --exec output
            --no-interactive --no-engine-messages --no-ansi --max-commands 10000
    RESULT_VARIABLE sim_rc
    OUTPUT_VARIABLE sim_out
    ERROR_VARIABLE sim_err
)
if(NOT sim_rc EQUAL 0 OR NOT sim_out MATCHES "COMMAND_LIMIT_PASS")
    message(FATAL_ERROR "generated CFG simulation failed (${sim_rc})\n${sim_out}\n${sim_err}")
endif()

message(STATUS "CS2 command-length guard: max emitted line ${max_bytes} bytes")
