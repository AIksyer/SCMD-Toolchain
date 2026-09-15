if(NOT DEFINED SCMDSIM OR NOT DEFINED ROOT)
    message(FATAL_ERROR "SCMDSIM and ROOT are required")
endif()
execute_process(
    COMMAND "${SCMDSIM}" "${ROOT}" --exec command_limit
            --no-interactive --no-engine-messages --no-ansi --max-commands 1000
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "simulator failed (${rc})\n${out}\n${err}")
endif()
if(NOT err MATCHES "WARNING: Command too long... ignoring!")
    message(FATAL_ERROR "simulator did not model CS2 long-command rejection\nstdout:\n${out}\nstderr:\n${err}")
endif()
if(NOT out MATCHES "SIM_COMMAND_LIMIT_PASS")
    message(FATAL_ERROR "command-limit fixture did not finish\n${out}\n${err}")
endif()
