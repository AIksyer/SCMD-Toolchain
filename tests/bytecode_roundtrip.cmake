if(NOT DEFINED SCMDC OR NOT DEFINED SCMDSIM OR NOT DEFINED ROOT OR NOT DEFINED OUT)
    message(FATAL_ERROR "bytecode_roundtrip.cmake missing required arguments")
endif()

execute_process(
    COMMAND "${SCMDC}" pack "${ROOT}" -o "${OUT}"
    RESULT_VARIABLE pack_rc
    OUTPUT_VARIABLE pack_out
    ERROR_VARIABLE pack_err
)
if(NOT pack_rc EQUAL 0)
    message(FATAL_ERROR "SCB pack failed (${pack_rc})\n${pack_out}\n${pack_err}")
endif()

execute_process(
    COMMAND "${SCMDSIM}" "${ROOT}" --exec alias_state --no-interactive --no-engine-messages --no-ansi
    RESULT_VARIABLE cfg_rc
    OUTPUT_VARIABLE cfg_out
    ERROR_VARIABLE cfg_err
)
if(NOT cfg_rc EQUAL 0)
    message(FATAL_ERROR "CFG simulation failed (${cfg_rc})\n${cfg_out}\n${cfg_err}")
endif()

execute_process(
    COMMAND "${SCMDSIM}" "${OUT}" --exec alias_state --no-interactive --no-engine-messages --no-ansi
    RESULT_VARIABLE scb_rc
    OUTPUT_VARIABLE scb_out
    ERROR_VARIABLE scb_err
)
if(NOT scb_rc EQUAL 0)
    message(FATAL_ERROR "SCB simulation failed (${scb_rc})\n${scb_out}\n${scb_err}")
endif()

if(NOT cfg_out STREQUAL scb_out)
    message(FATAL_ERROR "CFG/SCB output mismatch\n--- CFG ---\n${cfg_out}\n--- SCB ---\n${scb_out}")
endif()
if(NOT scb_out MATCHES "ON.*OFF.*ON")
    message(FATAL_ERROR "expected alias state output not found: ${scb_out}")
endif()
