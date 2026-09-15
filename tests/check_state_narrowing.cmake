if(NOT DEFINED SCMDC OR NOT DEFINED SCMDSIM OR NOT DEFINED SOURCE OR NOT DEFINED OUTDIR)
    message(FATAL_ERROR "SCMDC, SCMDSIM, SOURCE and OUTDIR are required")
endif()
file(REMOVE_RECURSE "${OUTDIR}")
file(MAKE_DIRECTORY "${OUTDIR}")
set(OUTCFG "${OUTDIR}/output.cfg")
execute_process(
    COMMAND "${SCMDC}" "${SOURCE}" -o "${OUTCFG}" --console-mode sync
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "compile failed (${rc})\n${out}\n${err}")
endif()
file(GLOB_RECURSE cfg_files "${OUTDIR}/*.cfg")
set(cfg_text "")
foreach(f IN LISTS cfg_files)
    file(READ "${f}" t)
    string(APPEND cfg_text "\n${t}")
endforeach()
# mode is global 0 and only ever takes 0 or 5, so three bits are sufficient.
if(cfg_text MATCHES "alias __scmd_g0_b3 ")
    message(FATAL_ERROR "constant-state global was not narrowed to three bits")
endif()
if(NOT cfg_text MATCHES "alias __scmd_g0_b2 ")
    message(FATAL_ERROR "narrowed state is missing required bit 2")
endif()
# copied receives a runtime value, so it must retain full u8 storage.
if(NOT cfg_text MATCHES "alias __scmd_g2_b7 ")
    message(FATAL_ERROR "runtime-assigned u8 was narrowed unsafely")
endif()
execute_process(
    COMMAND "${SCMDSIM}" "${OUTDIR}" --exec output --no-interactive --no-engine-messages --no-ansi --max-commands 100000
    RESULT_VARIABLE sim_rc OUTPUT_VARIABLE sim_out ERROR_VARIABLE sim_err
)
if(NOT sim_rc EQUAL 0 OR NOT sim_out MATCHES "STATE_NARROW_PASS")
    message(FATAL_ERROR "simulation failed (${sim_rc})\n${sim_out}\n${sim_err}")
endif()
