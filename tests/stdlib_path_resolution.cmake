if(NOT DEFINED LANG_EXECUTABLE OR NOT DEFINED SOURCE_DIR OR
   NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR
        "LANG_EXECUTABLE, SOURCE_DIR, and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/work")

function(require_success label)
    if(NOT RESULT_CODE EQUAL 0)
        message(FATAL_ERROR
            "${label} failed (${RESULT_CODE}):\n${RESULT_OUTPUT}${RESULT_ERROR}")
    endif()
endfunction()

# Development/build configuration must not depend on the launch directory.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=ASTER_STDLIB_PATH
            "${LANG_EXECUTABLE}" check
            "${SOURCE_DIR}/examples/aster_libraries.lang"
    WORKING_DIRECTORY "${TEST_ROOT}/work"
    RESULT_VARIABLE RESULT_CODE
    OUTPUT_VARIABLE RESULT_OUTPUT
    ERROR_VARIABLE RESULT_ERROR)
require_success("build-configured standard library")

# A relocatable bundle may put std beside the executable directory.
file(MAKE_DIRECTORY "${TEST_ROOT}/bundle/bin")
file(COPY "${LANG_EXECUTABLE}" DESTINATION "${TEST_ROOT}/bundle/bin")
file(COPY "${SOURCE_DIR}/std" DESTINATION "${TEST_ROOT}/bundle")
get_filename_component(LANG_NAME "${LANG_EXECUTABLE}" NAME)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=ASTER_STDLIB_PATH
            "${TEST_ROOT}/bundle/bin/${LANG_NAME}" check
            "${SOURCE_DIR}/examples/aster_libraries.lang"
    WORKING_DIRECTORY "${TEST_ROOT}/work"
    RESULT_VARIABLE RESULT_CODE
    OUTPUT_VARIABLE RESULT_OUTPUT
    ERROR_VARIABLE RESULT_ERROR)
require_success("executable-relative standard library")

# Installed layouts use <prefix>/bin and <prefix>/share/aster/std.
file(MAKE_DIRECTORY
    "${TEST_ROOT}/installed/bin" "${TEST_ROOT}/installed/share/aster")
file(COPY "${LANG_EXECUTABLE}" DESTINATION "${TEST_ROOT}/installed/bin")
file(COPY "${SOURCE_DIR}/std"
    DESTINATION "${TEST_ROOT}/installed/share/aster")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=ASTER_STDLIB_PATH
            "${TEST_ROOT}/installed/bin/${LANG_NAME}" check
            "${SOURCE_DIR}/examples/aster_libraries.lang"
    WORKING_DIRECTORY "${TEST_ROOT}/work"
    RESULT_VARIABLE RESULT_CODE
    OUTPUT_VARIABLE RESULT_OUTPUT
    ERROR_VARIABLE RESULT_ERROR)
require_success("installation-prefix standard library")

# Explicit environment configuration is authoritative.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "ASTER_STDLIB_PATH=${SOURCE_DIR}/std"
            "${LANG_EXECUTABLE}" check
            "${SOURCE_DIR}/examples/aster_libraries.lang"
    WORKING_DIRECTORY "${TEST_ROOT}/work"
    RESULT_VARIABLE RESULT_CODE
    OUTPUT_VARIABLE RESULT_OUTPUT
    ERROR_VARIABLE RESULT_ERROR)
require_success("environment-configured standard library")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "ASTER_STDLIB_PATH=${TEST_ROOT}/missing-std"
            "${LANG_EXECUTABLE}" check
            "${SOURCE_DIR}/examples/aster_libraries.lang"
    WORKING_DIRECTORY "${TEST_ROOT}/work"
    RESULT_VARIABLE RESULT_CODE
    OUTPUT_VARIABLE RESULT_OUTPUT
    ERROR_VARIABLE RESULT_ERROR)
if(RESULT_CODE EQUAL 0 OR
   NOT RESULT_ERROR MATCHES "${TEST_ROOT}/missing-std")
    message(FATAL_ERROR
        "invalid ASTER_STDLIB_PATH was not authoritative:\n${RESULT_OUTPUT}${RESULT_ERROR}")
endif()

# A manifest may pin its own stdlib root relative to the manifest directory.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=ASTER_STDLIB_PATH
            "${LANG_EXECUTABLE}" project check
            "${SOURCE_DIR}/tests/stdlib_manifest/aster.toml"
    WORKING_DIRECTORY "${TEST_ROOT}/work"
    RESULT_VARIABLE RESULT_CODE
    OUTPUT_VARIABLE RESULT_OUTPUT
    ERROR_VARIABLE RESULT_ERROR)
require_success("manifest-configured standard library")
