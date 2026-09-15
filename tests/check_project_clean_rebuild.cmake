if(NOT DEFINED SCMDC OR NOT DEFINED WORKDIR)
    message(FATAL_ERROR "check_project_clean_rebuild.cmake missing required -D argument")
endif()
file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}/src")
file(WRITE "${WORKDIR}/src/main.scmd" "function main()\n{\n    console.print(\"PROJECT_CLEAN_PASS\");\n}\n")
file(WRITE "${WORKDIR}/clean.scmdproj" [=[project "clean"
{
    entry = "src/main.scmd";
    output = "build";
    package = "cleanpkg";
    bootstrap = true;
    target cs2
    {
        console { mode = sync; settle = 16ms; tick = 16ms; }
        paging { max_bytes = 4096; max_commands = 40; }
    }
}
]=])
execute_process(COMMAND "${SCMDC}" build "${WORKDIR}/clean.scmdproj"
    RESULT_VARIABLE first_rc OUTPUT_VARIABLE first_out ERROR_VARIABLE first_err)
if(NOT first_rc EQUAL 0)
    message(FATAL_ERROR "first build failed (${first_rc})\n${first_out}\n${first_err}")
endif()
file(WRITE "${WORKDIR}/build/cleanpkg/stale.cfg" "echoln STALE_BAD\n")
file(MAKE_DIRECTORY "${WORKDIR}/build/cleanpkg/lazy/dead")
file(WRITE "${WORKDIR}/build/cleanpkg/lazy/dead/000.cfg" "echoln STALE_LAZY_BAD\n")
execute_process(COMMAND "${SCMDC}" build "${WORKDIR}/clean.scmdproj"
    RESULT_VARIABLE second_rc OUTPUT_VARIABLE second_out ERROR_VARIABLE second_err)
if(NOT second_rc EQUAL 0)
    message(FATAL_ERROR "second build failed (${second_rc})\n${second_out}\n${second_err}")
endif()
if(EXISTS "${WORKDIR}/build/cleanpkg/stale.cfg" OR EXISTS "${WORKDIR}/build/cleanpkg/lazy/dead/000.cfg")
    message(FATAL_ERROR "project rebuild left stale generated CFG files behind")
endif()
if(NOT EXISTS "${WORKDIR}/build/cleanpkg/entry.cfg")
    message(FATAL_ERROR "clean rebuild lost generated package entry")
endif()
