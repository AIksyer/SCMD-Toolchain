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
if(NOT EXISTS "${OUTDIR}/output_bootstrap.cfg")
    message(FATAL_ERROR "static main bootstrap file was not emitted")
endif()
file(READ "${OUTDIR}/output_bootstrap.cfg" boot)
if(NOT boot MATCHES "alias boot_value echoln BOOTSTRAP_PASS")
    message(FATAL_ERROR "bootstrap payload missing")
endif()
execute_process(
    COMMAND "${SCMDSIM}" "${OUTDIR}" --exec output --no-interactive --no-engine-messages --no-ansi --max-commands 100000
    RESULT_VARIABLE sim_rc OUTPUT_VARIABLE sim_out ERROR_VARIABLE sim_err
)
if(NOT sim_rc EQUAL 0 OR NOT sim_out MATCHES "BOOTSTRAP_PASS")
    message(FATAL_ERROR "bootstrap simulation failed (${sim_rc})\n${sim_out}\n${sim_err}")
endif()
