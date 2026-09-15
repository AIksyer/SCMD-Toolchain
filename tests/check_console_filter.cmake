if(NOT DEFINED SCMDSIM OR NOT DEFINED ROOT)
  message(FATAL_ERROR "SCMDSIM and ROOT are required")
endif()
execute_process(
  COMMAND "${SCMDSIM}" "${ROOT}" --exec filter --no-interactive --no-ansi
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "scmdsim failed (${rc})\n${out}\n${err}")
endif()
if(out MATCHES "\\[InputService\\] execing aliasos/noise")
  message(FATAL_ERROR "con_filter_text_out did not hide internal exec message\n${out}")
endif()
if(NOT out MATCHES "FILTER_BODY" OR NOT out MATCHES "FILTER_VISIBLE")
  message(FATAL_ERROR "console filter incorrectly hid normal output\n${out}")
endif()
message(STATUS "SCMDSIM_CONSOLE_FILTER_PASS")
