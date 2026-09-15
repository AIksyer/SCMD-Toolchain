if(NOT DEFINED VCS16SCMD OR NOT DEFINED SCMDC OR NOT DEFINED SCMDSIM OR NOT DEFINED SOURCE OR NOT DEFINED OUTDIR)
    message(FATAL_ERROR "VCS16SCMD, SCMDC, SCMDSIM, SOURCE and OUTDIR are required")
endif()
file(REMOVE_RECURSE "${OUTDIR}")
file(MAKE_DIRECTORY "${OUTDIR}")
set(GEN "${OUTDIR}/generated.scmd")
execute_process(
    COMMAND "${VCS16SCMD}" "${SOURCE}" -o "${GEN}" --prefix aot
    RESULT_VARIABLE arc OUTPUT_VARIABLE aout ERROR_VARIABLE aerr
)
if(NOT arc EQUAL 0)
    message(FATAL_ERROR "vCS -> SCMD AOT failed (${arc})\n${aout}\n${aerr}")
endif()
file(WRITE "${OUTDIR}/main.scmd" [=[
get "generated.scmd";
function aot_host_syscall()
{
    if(aot_sys_hi == 0 && aot_sys_lo == 7)
    {
        if(aot_r0_hi == 0 && aot_r0_lo == 42)
        {
            console.print("VCS_AOT_42");
        }
        else
        {
            console.print("VCS_AOT_BAD");
        }
        aot_sysret_lo = 0;
        aot_sysret_hi = 0;
        return;
    }
    aot_trapped = true;
}
function main() { aot_run(); }
]=])
execute_process(
    COMMAND "${SCMDC}" "${OUTDIR}/main.scmd" -o "${OUTDIR}/aot.cfg" --console-mode sync
    RESULT_VARIABLE crc OUTPUT_VARIABLE cout ERROR_VARIABLE cerr
)
if(NOT crc EQUAL 0)
    message(FATAL_ERROR "AOT SCMD compile failed (${crc})\n${cout}\n${cerr}")
endif()
execute_process(
    COMMAND "${SCMDSIM}" "${OUTDIR}" --exec aot --no-interactive --no-engine-messages --no-ansi
    RESULT_VARIABLE src OUTPUT_VARIABLE sout ERROR_VARIABLE serr
)
if(NOT src EQUAL 0)
    message(FATAL_ERROR "AOT simulation failed (${src})\n${sout}\n${serr}")
endif()
if(NOT sout MATCHES "VCS_AOT_42")
    message(FATAL_ERROR "AOT semantics mismatch:\n${sout}")
endif()
if(sout MATCHES "VCS_AOT_BAD")
    message(FATAL_ERROR "AOT produced wrong register value:\n${sout}")
endif()
