if(NOT DEFINED SCMDSIM OR NOT DEFINED ROOT)
    message(FATAL_ERROR "SCMDSIM and ROOT are required")
endif()
execute_process(
    COMMAND "${SCMDSIM}" "${ROOT}" --script "${ROOT}/../echo_ui.script"
            --no-interactive --no-engine-messages --no-ansi
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "scmdsim failed (${rc})\n${out}\n${err}")
endif()
if(NOT out MATCHES "\\[Console\\] ECHO_PREFIX_TEST")
    message(FATAL_ERROR "echo did not reproduce real-CS2 [Console] prefix:\n${out}")
endif()
if(NOT out MATCHES "ECHOLN_RAW_TEST")
    message(FATAL_ERROR "echoln output missing:\n${out}")
endif()
if(out MATCHES "\\[Console\\] ECHOLN_RAW_TEST")
    message(FATAL_ERROR "echoln incorrectly gained [Console] prefix:\n${out}")
endif()
