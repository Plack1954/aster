if(NOT DEFINED LANG_EXECUTABLE OR NOT DEFINED C_COMPILER OR
   NOT DEFINED SOURCE_FILE OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR
        "LANG_EXECUTABLE, C_COMPILER, SOURCE_FILE, and OUTPUT_DIRECTORY are required")
endif()
if(NOT DEFINED EXPECTED_RUN_STATUS)
    set(EXPECTED_RUN_STATUS 0)
endif()
if(NOT DEFINED SOURCE_ROOT)
    get_filename_component(
        SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
if(NOT DEFINED OUTPUT_BASENAME)
    set(OUTPUT_BASENAME aster_generated_backend)
endif()

set(GENERATED_C "${OUTPUT_DIRECTORY}/${OUTPUT_BASENAME}.c")
set(GENERATED_EXE
    "${OUTPUT_DIRECTORY}/${OUTPUT_BASENAME}${EXECUTABLE_SUFFIX}")

execute_process(
    COMMAND "${LANG_EXECUTABLE}" emit-c "${SOURCE_FILE}"
    WORKING_DIRECTORY "${SOURCE_ROOT}"
    RESULT_VARIABLE EMIT_STATUS
    OUTPUT_FILE "${GENERATED_C}"
    ERROR_VARIABLE EMIT_ERROR
)
if(NOT EMIT_STATUS EQUAL 0)
    message(FATAL_ERROR "C emission failed: ${EMIT_ERROR}")
endif()
if(DEFINED EXPECTED_C_PATTERN)
    file(READ "${GENERATED_C}" GENERATED_C_TEXT)
    if(NOT GENERATED_C_TEXT MATCHES "${EXPECTED_C_PATTERN}")
        message(FATAL_ERROR
            "generated C did not contain required pattern: ${EXPECTED_C_PATTERN}")
    endif()
endif()
if(DEFINED EXPECTED_C_COMPACT_PATTERN)
    file(READ "${GENERATED_C}" GENERATED_C_TEXT)
    string(REGEX REPLACE "[ \t\r\n]" "" GENERATED_C_COMPACT
        "${GENERATED_C_TEXT}")
    if(NOT GENERATED_C_COMPACT MATCHES "${EXPECTED_C_COMPACT_PATTERN}")
        message(FATAL_ERROR
            "generated C did not contain required compact pattern: ${EXPECTED_C_COMPACT_PATTERN}")
    endif()
endif()

if(C_COMPILER_ID STREQUAL "MSVC")
    set(C_FLAGS /std:c17 /W4 /WX)
    if(DEFINED INCLUDE_DIRECTORY)
        list(APPEND C_FLAGS "/I${INCLUDE_DIRECTORY}")
    endif()
else()
    set(C_FLAGS
        -std=c17
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Werror)
    if(GENERATED_SANITIZE)
        list(APPEND C_FLAGS
            -fsanitize=address,undefined
            -fno-omit-frame-pointer)
    endif()
    if(DEFINED INCLUDE_DIRECTORY)
        list(APPEND C_FLAGS "-I${INCLUDE_DIRECTORY}")
    endif()
endif()

set(LINK_INPUTS)
if(DEFINED RUNTIME_LIBRARY)
    list(APPEND LINK_INPUTS "${RUNTIME_LIBRARY}")
endif()
if(DEFINED EXTRA_LIBRARIES)
    list(APPEND LINK_INPUTS ${EXTRA_LIBRARIES})
endif()

execute_process(
    COMMAND "${C_COMPILER}" ${C_FLAGS} "${GENERATED_C}" ${LINK_INPUTS}
            -o "${GENERATED_EXE}"
    RESULT_VARIABLE COMPILE_STATUS
    OUTPUT_VARIABLE COMPILE_OUTPUT
    ERROR_VARIABLE COMPILE_ERROR
)
if(NOT COMPILE_STATUS EQUAL 0)
    message(FATAL_ERROR
        "generated C did not compile:\n${COMPILE_OUTPUT}${COMPILE_ERROR}")
endif()

if(GENERATED_LEAK_CHECK)
    if(NOT GENERATED_SANITIZE)
        message(FATAL_ERROR
            "GENERATED_LEAK_CHECK requires GENERATED_SANITIZE")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1"
            "${GENERATED_EXE}"
        WORKING_DIRECTORY "${SOURCE_ROOT}"
        RESULT_VARIABLE RUN_STATUS
        OUTPUT_VARIABLE RUN_OUTPUT
        ERROR_VARIABLE RUN_ERROR
    )
else()
    execute_process(
        COMMAND "${GENERATED_EXE}"
        WORKING_DIRECTORY "${SOURCE_ROOT}"
        RESULT_VARIABLE RUN_STATUS
        OUTPUT_VARIABLE RUN_OUTPUT
        ERROR_VARIABLE RUN_ERROR
    )
endif()
if(NOT RUN_STATUS EQUAL EXPECTED_RUN_STATUS)
    message(FATAL_ERROR
        "generated executable returned ${RUN_STATUS}, expected ${EXPECTED_RUN_STATUS}:\n${RUN_OUTPUT}${RUN_ERROR}")
endif()
if(DEFINED EXPECTED_ERROR AND NOT RUN_ERROR MATCHES "${EXPECTED_ERROR}")
    message(FATAL_ERROR
        "generated executable stderr did not match `${EXPECTED_ERROR}`:\n${RUN_ERROR}")
endif()
if(DEFINED EXPECTED_OUTPUT AND NOT RUN_OUTPUT STREQUAL EXPECTED_OUTPUT)
    message(FATAL_ERROR
        "generated executable stdout did not match:\nexpected:\n${EXPECTED_OUTPUT}\nactual:\n${RUN_OUTPUT}")
endif()
if(DEFINED EXPECTED_ERROR_OUTPUT AND
   NOT RUN_ERROR STREQUAL EXPECTED_ERROR_OUTPUT)
    message(FATAL_ERROR
        "generated executable stderr did not match:\nexpected:\n${EXPECTED_ERROR_OUTPUT}\nactual:\n${RUN_ERROR}")
endif()
if(DEFINED EXPECTED_FILE)
    file(READ "${EXPECTED_FILE}" EXPECTED_FILE_OUTPUT)
    if(NOT RUN_OUTPUT STREQUAL EXPECTED_FILE_OUTPUT)
        message(FATAL_ERROR
            "generated executable stdout did not match ${EXPECTED_FILE}:\n${RUN_OUTPUT}")
    endif()
endif()
