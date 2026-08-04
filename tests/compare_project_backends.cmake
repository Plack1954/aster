if(NOT DEFINED LANG_EXECUTABLE OR NOT DEFINED MANIFEST_FILE OR
   NOT DEFINED TARGET_NAME)
    message(FATAL_ERROR
        "LANG_EXECUTABLE, MANIFEST_FILE, and TARGET_NAME are required")
endif()

execute_process(
    COMMAND "${LANG_EXECUTABLE}" project run-direct
            "${MANIFEST_FILE}" "${TARGET_NAME}"
    RESULT_VARIABLE DIRECT_STATUS
    OUTPUT_VARIABLE DIRECT_OUTPUT
    ERROR_VARIABLE DIRECT_ERROR
)
execute_process(
    COMMAND "${LANG_EXECUTABLE}" project run-ir
            "${MANIFEST_FILE}" "${TARGET_NAME}"
    RESULT_VARIABLE IR_STATUS
    OUTPUT_VARIABLE IR_OUTPUT
    ERROR_VARIABLE IR_ERROR
)

if(NOT DIRECT_STATUS EQUAL IR_STATUS)
    message(FATAL_ERROR
        "project backend exit status differs: direct=${DIRECT_STATUS}, ir=${IR_STATUS}")
endif()
if(NOT DIRECT_OUTPUT STREQUAL IR_OUTPUT)
    message(FATAL_ERROR
        "project backend stdout differs:\ndirect=[${DIRECT_OUTPUT}]\nir=[${IR_OUTPUT}]")
endif()
if(NOT DIRECT_ERROR STREQUAL IR_ERROR)
    message(FATAL_ERROR
        "project backend stderr differs:\ndirect=[${DIRECT_ERROR}]\nir=[${IR_ERROR}]")
endif()
