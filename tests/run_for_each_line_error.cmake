if(NOT DEFINED LANG OR NOT DEFINED SOURCE OR NOT DEFINED ROOT OR
   NOT DEFINED WORKING)
    message(FATAL_ERROR "LANG, SOURCE, ROOT, and WORKING are required")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(input "${ROOT}/lines.txt")
file(WRITE "${input}" "ordinary-line-crossing-chunks\nbad\nnot-visited\n")

execute_process(
    COMMAND "${LANG}" run "${SOURCE}" -- "${input}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    WORKING_DIRECTORY "${WORKING}"
)
set(expected "line callback rejected input\n")
if(NOT status EQUAL 0)
    file(REMOVE_RECURSE "${ROOT}")
    message(FATAL_ERROR
        "callback error propagation failed (${status})\n${output}\n${error}")
endif()
if(NOT output STREQUAL expected)
    file(REMOVE_RECURSE "${ROOT}")
    message(FATAL_ERROR
        "callback error mismatch\nexpected:\n${expected}\nactual:\n${output}")
endif()

file(REMOVE_RECURSE "${ROOT}")
