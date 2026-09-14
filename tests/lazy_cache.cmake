if(NOT DEFINED SCMDSIM OR NOT DEFINED WORKDIR)
    message(FATAL_ERROR "lazy_cache.cmake missing SCMDSIM/WORKDIR")
endif()
file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}/root")
file(WRITE "${WORKDIR}/root/a.cfg" "echoln A1\n")
file(WRITE "${WORKDIR}/run_a.script" "exec a\n:cache\n:stats\n")

execute_process(
    COMMAND "${SCMDSIM}" "${WORKDIR}/root" --cache --script "${WORKDIR}/run_a.script" --no-interactive --no-engine-messages --no-ansi
    RESULT_VARIABLE rc1 OUTPUT_VARIABLE out1 ERROR_VARIABLE err1
)
if(NOT rc1 EQUAL 0)
    message(FATAL_ERROR "first lazy/cache run failed: ${err1}\n${out1}")
endif()
if(NOT out1 MATCHES "A1" OR NOT out1 MATCHES "cache_hits=0" OR NOT out1 MATCHES "cache_misses=1")
    message(FATAL_ERROR "first lazy/cache run did not compile one fresh module:\n${out1}")
endif()

execute_process(
    COMMAND "${SCMDSIM}" "${WORKDIR}/root" --cache --script "${WORKDIR}/run_a.script" --no-interactive --no-engine-messages --no-ansi
    RESULT_VARIABLE rc2 OUTPUT_VARIABLE out2 ERROR_VARIABLE err2
)
if(NOT rc2 EQUAL 0)
    message(FATAL_ERROR "second lazy/cache run failed: ${err2}\n${out2}")
endif()
if(NOT out2 MATCHES "A1" OR NOT out2 MATCHES "cache_hits=1")
    message(FATAL_ERROR "second run did not use persistent module cache:\n${out2}")
endif()

# A changed source must invalidate the cache.
file(WRITE "${WORKDIR}/root/a.cfg" "echoln A2_CHANGED\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.02)
execute_process(
    COMMAND "${SCMDSIM}" "${WORKDIR}/root" --cache --script "${WORKDIR}/run_a.script" --no-interactive --no-engine-messages --no-ansi
    RESULT_VARIABLE rc3 OUTPUT_VARIABLE out3 ERROR_VARIABLE err3
)
if(NOT rc3 EQUAL 0 OR NOT out3 MATCHES "A2_CHANGED" OR NOT out3 MATCHES "cache_misses=1")
    message(FATAL_ERROR "changed module was not recompiled:\n${err3}\n${out3}")
endif()

# A module created after prior simulator runs must be discoverable without a package rebuild.
file(WRITE "${WORKDIR}/root/new_after_startup.cfg" "echoln NEW_MODULE_OK\n")
file(WRITE "${WORKDIR}/run_new.script" "exec new_after_startup\n:complete exec new_\n")
execute_process(
    COMMAND "${SCMDSIM}" "${WORKDIR}/root" --script "${WORKDIR}/run_new.script" --no-interactive --no-engine-messages --no-ansi
    RESULT_VARIABLE rc4 OUTPUT_VARIABLE out4 ERROR_VARIABLE err4
)
if(NOT rc4 EQUAL 0 OR NOT out4 MATCHES "NEW_MODULE_OK" OR NOT out4 MATCHES "new_after_startup")
    message(FATAL_ERROR "new module was not discoverable:\n${err4}\n${out4}")
endif()
