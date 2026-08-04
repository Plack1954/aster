if(NOT DEFINED LANG_EXECUTABLE OR
   NOT DEFINED SOURCE_FILE OR
   NOT DEFINED EXPECTED_FILE OR
   NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "golden test arguments are incomplete")
endif()

execute_process(
    COMMAND "${LANG_EXECUTABLE}" run "${SOURCE_FILE}"
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE errors
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "program failed with status ${status}:\n${errors}")
endif()
file(READ "${EXPECTED_FILE}" expected)
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
        "golden output mismatch\n--- expected ---\n${expected}"
        "--- actual ---\n${actual}")
endif()
