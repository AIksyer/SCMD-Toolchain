if(NOT DEFINED SCMDSIM OR NOT DEFINED ROOT OR NOT DEFINED SCRIPT)
    message(FATAL_ERROR "SCMDSIM, ROOT and SCRIPT are required")
endif()

execute_process(
    COMMAND "${SCMDSIM}" "${ROOT}" --script "${SCRIPT}" --no-interactive --no-engine-messages --no-ansi --no-cache
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
set(all "${out}\n${err}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "scmdsim SOS compatibility run failed (${rc}):\n${all}")
endif()

set(required
    "math_test.output:  28.000000"
    "update_test_local_opvar_get.output:  999.000000"
    "get_opvar.output:  120.000000"
    "remap_opvar.output:  240.000000"
    "set_output_float.output_opvar_exists:  0.000000"
    "update_test_opvar_set.output_opvar_exists:  1.000000"
    "test_opvars.test_float:  240.000000"
    "update_test_import_op2.import_stack:  \"update_test_opvar\""
    "Name: update_test_import_op2::update_test_import_op1"
    "update_test_convar.output:  321.000000"
    "snd_musicvolume = 0.420000"
    "update_test_simple_nest_import_op2_OPER.output:  16777216.000000"
    "333.000000"
)
foreach(needle IN LISTS required)
    string(FIND "${all}" "${needle}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "missing SOS compatibility evidence: ${needle}\n--- output ---\n${all}")
    endif()
endforeach()
message(STATUS "SOS compatibility model PASS")
