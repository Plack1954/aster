if(NOT DEFINED LANG_EXECUTABLE OR NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "LANG_EXECUTABLE and SOURCE_FILE are required")
endif()

execute_process(
    COMMAND "${LANG_EXECUTABLE}" run-direct "${SOURCE_FILE}"
    RESULT_VARIABLE DIRECT_STATUS
    OUTPUT_VARIABLE DIRECT_OUTPUT
    ERROR_VARIABLE DIRECT_ERROR
)
execute_process(
    COMMAND "${LANG_EXECUTABLE}" run-ir "${SOURCE_FILE}"
    RESULT_VARIABLE IR_STATUS
    OUTPUT_VARIABLE IR_OUTPUT
    ERROR_VARIABLE IR_ERROR
)

if(NOT DIRECT_STATUS EQUAL IR_STATUS)
    message(FATAL_ERROR
        "backend exit status differs: direct=${DIRECT_STATUS}, ir=${IR_STATUS}")
endif()
if(NOT DIRECT_OUTPUT STREQUAL IR_OUTPUT)
    message(FATAL_ERROR
        "backend stdout differs:\ndirect=[${DIRECT_OUTPUT}]\nir=[${IR_OUTPUT}]")
endif()
if(NOT DIRECT_ERROR STREQUAL IR_ERROR)
    message(FATAL_ERROR
        "backend stderr differs:\ndirect=[${DIRECT_ERROR}]\nir=[${IR_ERROR}]")
endif()
