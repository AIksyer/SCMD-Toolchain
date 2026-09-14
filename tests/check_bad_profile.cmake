if(NOT DEFINED SCMDSIM OR NOT DEFINED ROOT)
    message(FATAL_ERROR "check_bad_profile.cmake missing required -D argument")
endif()
execute_process(
    COMMAND "${SCMDSIM}" "${ROOT}" --profile definitely-not-a-real-profile --no-interactive
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT rc EQUAL 2)
    message(FATAL_ERROR "expected exit code 2, got ${rc}\nstdout=${out}\nstderr=${err}")
endif()
if(NOT err MATCHES "unsupported compatibility profile")
    message(FATAL_ERROR "expected profile rejection diagnostic\nstderr=${err}")
endif()
