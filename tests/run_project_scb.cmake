if(NOT DEFINED SCMD OR NOT DEFINED SCMDSIM OR NOT DEFINED PROJECT OR NOT DEFINED WORKDIR OR NOT DEFINED EXEC_NAME OR NOT DEFINED EXPECT)
    message(FATAL_ERROR "run_project_scb.cmake missing required -D argument")
endif()
if(NOT DEFINED MAX_COMMANDS)
    set(MAX_COMMANDS 5000000)
endif()

get_filename_component(project_dir "${PROJECT}" DIRECTORY)
get_filename_component(project_name "${PROJECT}" NAME)
file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}")
file(COPY "${project_dir}/" DESTINATION "${WORKDIR}")
set(copied_project "${WORKDIR}/${project_name}")
set(cfgroot "${WORKDIR}/build")
set(scb "${WORKDIR}/program.scb")

execute_process(
    COMMAND "${SCMD}" build "${copied_project}"
    RESULT_VARIABLE build_rc
    OUTPUT_VARIABLE build_out
    ERROR_VARIABLE build_err
)
if(NOT build_rc EQUAL 0)
    message(FATAL_ERROR "build failed (${build_rc})\n${build_out}\n${build_err}")
endif()

execute_process(
    COMMAND "${SCMD}" pack "${cfgroot}" -o "${scb}"
    RESULT_VARIABLE pack_rc
    OUTPUT_VARIABLE pack_out
    ERROR_VARIABLE pack_err
)
if(NOT pack_rc EQUAL 0)
    message(FATAL_ERROR "SCB pack failed (${pack_rc})\n${pack_out}\n${pack_err}")
endif()

execute_process(
    COMMAND "${SCMDSIM}" "${scb}" --exec "${EXEC_NAME}" --no-interactive --no-engine-messages --no-ansi --max-commands "${MAX_COMMANDS}"
    RESULT_VARIABLE sim_rc
    OUTPUT_VARIABLE sim_out
    ERROR_VARIABLE sim_err
)
if(NOT sim_rc EQUAL 0)
    message(FATAL_ERROR "SCB simulation failed (${sim_rc})\n${sim_out}\n${sim_err}")
endif()
if(NOT sim_out MATCHES "${EXPECT}")
    message(FATAL_ERROR "expected '${EXPECT}' not found\n--- stdout ---\n${sim_out}\n--- stderr ---\n${sim_err}")
endif()
if(sim_out MATCHES "_FAIL")
    message(FATAL_ERROR "runtime reported failure\n${sim_out}")
endif()
