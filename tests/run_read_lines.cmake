if(NOT DEFINED LANG OR NOT DEFINED SOURCE OR NOT DEFINED ROOT OR
   NOT DEFINED WORKING)
    message(FATAL_ERROR "LANG, SOURCE, ROOT, and WORKING are required")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(input "${ROOT}/lines.txt")
string(REPEAT "long-line-segment-" 40 long_line)
set(contents "alpha\n\n${long_line}\nomega")
file(WRITE "${input}" "${contents}")

execute_process(
    COMMAND "${LANG}" run "${SOURCE}" -- "${input}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    WORKING_DIRECTORY "${WORKING}"
)
set(expected "${contents}\n")
if(NOT status EQUAL 0)
    file(REMOVE_RECURSE "${ROOT}")
    message(FATAL_ERROR
        "streaming line reader failed (${status})\n${output}\n${error}")
endif()
if(NOT output STREQUAL expected)
    file(REMOVE_RECURSE "${ROOT}")
    message(FATAL_ERROR
        "line output mismatch\nexpected:\n${expected}\nactual:\n${output}")
endif()

file(REMOVE_RECURSE "${ROOT}")
