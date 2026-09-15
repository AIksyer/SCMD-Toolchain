if(NOT DEFINED SCMDC OR NOT DEFINED SCMDSIM OR NOT DEFINED SOURCE OR NOT DEFINED SCRIPT OR NOT DEFINED OUTDIR)
    message(FATAL_ERROR "SCMDC, SCMDSIM, SOURCE, SCRIPT and OUTDIR are required")
endif()
file(REMOVE_RECURSE "${OUTDIR}")
file(MAKE_DIRECTORY "${OUTDIR}")
set(OUTCFG "${OUTDIR}/export.cfg")
execute_process(
    COMMAND "${SCMDC}" "${SOURCE}" -o "${OUTCFG}" --console-mode sync
    RESULT_VARIABLE rc OUTPUT_VARIABLE cout ERROR_VARIABLE cerr
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "compile failed (${rc})\n${cout}\n${cerr}")
endif()
file(GLOB_RECURSE generated_cfg "${OUTDIR}/*.cfg")
set(all "")
foreach(f IN LISTS generated_cfg)
    file(READ "${f}" t)
    string(APPEND all "\n${t}")
endforeach()
if(NOT all MATCHES "alias ping ")
    message(FATAL_ERROR "exported function has no stable public alias")
endif()
if(all MATCHES "alias hidden ")
    message(FATAL_ERROR "non-exported function leaked a public alias")
endif()
execute_process(
    COMMAND "${SCMDSIM}" "${OUTDIR}" --exec export --script "${SCRIPT}"
            --no-interactive --no-engine-messages --no-ansi
    RESULT_VARIABLE src OUTPUT_VARIABLE sout ERROR_VARIABLE serr
)
if(NOT src EQUAL 0)
    message(FATAL_ERROR "simulation failed (${src})\n${sout}\n${serr}")
endif()
if(NOT sout MATCHES "EXPORT_OK")
    message(FATAL_ERROR "exported function was not callable:\n${sout}")
endif()
if(NOT sout MATCHES "Unknown command: hidden")
    message(FATAL_ERROR "hidden function unexpectedly callable:\n${sout}")
endif()
