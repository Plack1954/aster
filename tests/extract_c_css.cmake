if(NOT DEFINED LANG_EXECUTABLE OR NOT DEFINED C_COMPILER OR
   NOT DEFINED SOURCE_FILE OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR "missing extracted-CSS test arguments")
endif()
if(NOT DEFINED SOURCE_ROOT)
    get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(FIRST_DIRECTORY "${OUTPUT_DIRECTORY}/extracted_css_first")
set(SECOND_DIRECTORY "${OUTPUT_DIRECTORY}/extracted_css_second")
file(REMOVE_RECURSE "${FIRST_DIRECTORY}" "${SECOND_DIRECTORY}")
file(MAKE_DIRECTORY "${FIRST_DIRECTORY}" "${SECOND_DIRECTORY}")

foreach(DIRECTORY IN ITEMS "${FIRST_DIRECTORY}" "${SECOND_DIRECTORY}")
    execute_process(
        COMMAND "${LANG_EXECUTABLE}" emit-c-site
                "${SOURCE_FILE}" "${DIRECTORY}"
        WORKING_DIRECTORY "${SOURCE_ROOT}"
        OUTPUT_FILE "${DIRECTORY}/site.c"
        ERROR_VARIABLE EMIT_ERROR
        RESULT_VARIABLE EMIT_STATUS)
    if(NOT EMIT_STATUS EQUAL 0)
        message(FATAL_ERROR "site emission failed: ${EMIT_ERROR}")
    endif()
endforeach()

file(GLOB FIRST_ASSETS "${FIRST_DIRECTORY}/site-*.css")
file(GLOB SECOND_ASSETS "${SECOND_DIRECTORY}/site-*.css")
list(LENGTH FIRST_ASSETS FIRST_COUNT)
list(LENGTH SECOND_ASSETS SECOND_COUNT)
if(NOT FIRST_COUNT EQUAL 1 OR NOT SECOND_COUNT EQUAL 1)
    message(FATAL_ERROR "expected exactly one extracted stylesheet per build")
endif()
list(GET FIRST_ASSETS 0 FIRST_ASSET)
list(GET SECOND_ASSETS 0 SECOND_ASSET)
get_filename_component(FIRST_NAME "${FIRST_ASSET}" NAME)
get_filename_component(SECOND_NAME "${SECOND_ASSET}" NAME)
if(NOT FIRST_NAME STREQUAL SECOND_NAME)
    message(FATAL_ERROR "stylesheet hash is not deterministic")
endif()
file(READ "${FIRST_ASSET}" FIRST_CSS)
file(READ "${SECOND_ASSET}" SECOND_CSS)
if(NOT FIRST_CSS STREQUAL SECOND_CSS OR
   NOT FIRST_CSS MATCHES "data-aster-scope-[0-9a-f]+")
    message(FATAL_ERROR "extracted stylesheet content is invalid")
endif()

execute_process(
    COMMAND "${LANG_EXECUTABLE}" run "${SOURCE_FILE}"
    WORKING_DIRECTORY "${SOURCE_ROOT}"
    OUTPUT_VARIABLE VM_OUTPUT
    ERROR_VARIABLE VM_ERROR
    RESULT_VARIABLE VM_STATUS)
if(NOT VM_STATUS EQUAL 0)
    message(FATAL_ERROR "VM inline rendering failed: ${VM_ERROR}")
endif()
string(REGEX MATCHALL "<style>" VM_STYLES "${VM_OUTPUT}")
list(LENGTH VM_STYLES VM_STYLE_COUNT)
string(REGEX MATCHALL "<article class=\"card\"" VM_CARDS "${VM_OUTPUT}")
list(LENGTH VM_CARDS VM_CARD_COUNT)
if(NOT VM_STYLE_COUNT EQUAL 2 OR NOT VM_CARD_COUNT EQUAL 22)
    message(FATAL_ERROR
        "22 component renders did not produce one scoped and one global style")
endif()

set(EXECUTABLE "${FIRST_DIRECTORY}/site${EXECUTABLE_SUFFIX}")
if(C_COMPILER_ID STREQUAL "MSVC")
    set(C_FLAGS /std:c17 /W4 /WX)
else()
    set(C_FLAGS -std=c17 -Wall -Wextra -Wpedantic -Werror)
endif()
execute_process(
    COMMAND "${C_COMPILER}" ${C_FLAGS}
            "${FIRST_DIRECTORY}/site.c" -o "${EXECUTABLE}"
    OUTPUT_VARIABLE COMPILE_OUTPUT
    ERROR_VARIABLE COMPILE_ERROR
    RESULT_VARIABLE COMPILE_STATUS)
if(NOT COMPILE_STATUS EQUAL 0)
    message(FATAL_ERROR
        "generated site C did not compile: ${COMPILE_OUTPUT}${COMPILE_ERROR}")
endif()
execute_process(
    COMMAND "${EXECUTABLE}"
    OUTPUT_VARIABLE RUN_OUTPUT
    ERROR_VARIABLE RUN_ERROR
    RESULT_VARIABLE RUN_STATUS)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "generated site failed: ${RUN_ERROR}")
endif()
string(REGEX MATCHALL
    "<link rel=\"stylesheet\" href=\"/assets/site-[0-9a-f]+\\.css\">"
    LINKS "${RUN_OUTPUT}")
list(LENGTH LINKS LINK_COUNT)
if(NOT LINK_COUNT EQUAL 1)
    message(FATAL_ERROR "expected one external stylesheet link: ${RUN_OUTPUT}")
endif()
string(REGEX MATCHALL "<style>" INLINE_STYLES "${RUN_OUTPUT}")
list(LENGTH INLINE_STYLES INLINE_COUNT)
if(NOT INLINE_COUNT EQUAL 1)
    message(FATAL_ERROR "scoped style was not extracted exactly once")
endif()
