if(NOT DEFINED SCMDSIM OR NOT DEFINED ROOT OR NOT DEFINED SCRIPT)
    message(FATAL_ERROR "SCMDSIM, ROOT and SCRIPT are required")
endif()
execute_process(
    COMMAND "${SCMDSIM}" "${ROOT}" --script "${SCRIPT}"
            --no-interactive --no-engine-messages --no-ansi
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "scmdsim failed (${rc})\n${out}\n${err}")
endif()
# Real CS2 does not implement shell-style `|`: it is just another argument to
# echo.  Guard both halves so the simulator can never accidentally grow a pipe
# feature that the game does not have.
if(NOT out MATCHES "12345 \\| setinfo vcs_pipe_value")
    message(FATAL_ERROR "pipe token was not preserved literally:\n${out}")
endif()
if(NOT out MATCHES "Unknown command: vcs_pipe_value")
    message(FATAL_ERROR "setinfo appears to have executed through a fake pipe:\n${out}")
endif()
