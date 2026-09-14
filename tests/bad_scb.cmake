if(NOT DEFINED SCMDSIM OR NOT DEFINED BAD)
    message(FATAL_ERROR "bad_scb.cmake missing required arguments")
endif()
file(WRITE "${BAD}" "SCB1broken")
execute_process(
    COMMAND "${SCMDSIM}" "${BAD}" --no-interactive --no-engine-messages --no-ansi
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(rc EQUAL 0)
    message(FATAL_ERROR "invalid SCB unexpectedly accepted\n${out}\n${err}")
endif()
if(NOT err MATCHES "SCB")
    message(FATAL_ERROR "invalid SCB did not report SCB diagnostic\n${out}\n${err}")
endif()
