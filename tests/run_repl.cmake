if(NOT DEFINED LANG_EXECUTABLE OR
   NOT DEFINED INPUT_FILE OR
   NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "REPL test arguments are incomplete")
endif()
execute_process(
    COMMAND "${LANG_EXECUTABLE}" repl
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    INPUT_FILE "${INPUT_FILE}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    RESULT_VARIABLE status
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "REPL failed with status ${status}:\n${errors}")
endif()
if(NOT output MATCHES "3")
    message(FATAL_ERROR "REPL did not evaluate input:\n${output}")
endif()
