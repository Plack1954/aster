if(NOT DEFINED ASTER_EXECUTABLE OR NOT DEFINED CASE)
    message(FATAL_ERROR "ASTER_EXECUTABLE and CASE are required")
endif()

if(CASE STREQUAL "missing-project")
    set(arguments run --project)
    set(expected
        "Required argument missing for option: '--project'.(.|[\r\n])*Aster Run Command")
elseif(CASE STREQUAL "unknown-command")
    set(arguments unknown-command)
    set(expected
        "specified command or file was not found(.|[\r\n])*aster-unknown-command does not exist")
else()
    message(FATAL_ERROR "unknown CLI failure case: ${CASE}")
endif()

execute_process(
    COMMAND "${ASTER_EXECUTABLE}" ${arguments}
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
set(combined "${output}${error}")
if(status EQUAL 0)
    message(FATAL_ERROR "command unexpectedly succeeded:\n${combined}")
endif()
if(NOT combined MATCHES "${expected}")
    message(FATAL_ERROR "command output did not match ${expected}:\n${combined}")
endif()
