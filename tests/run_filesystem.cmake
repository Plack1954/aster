if(NOT DEFINED LANG OR NOT DEFINED SOURCE OR NOT DEFINED ROOT OR
   NOT DEFINED WORKING)
    message(FATAL_ERROR "LANG, SOURCE, ROOT, and WORKING are required")
endif()

set(test_directory "${ROOT}/tree")
set(first_file "${test_directory}/first.txt")
set(second_file "${test_directory}/second.txt")

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")

execute_process(
    COMMAND "${LANG}" run "${SOURCE}" --
        "${test_directory}" "${first_file}" "${second_file}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    WORKING_DIRECTORY "${WORKING}"
)

if(NOT status EQUAL 0)
    file(REMOVE_RECURSE "${ROOT}")
    message(FATAL_ERROR
        "filesystem example failed (${status})\n${output}\n${error}")
endif()

if(NOT output MATCHES
   "true[\r\n]+true[\r\n]+16[\r\n]+false[\r\n]+true")
    file(REMOVE_RECURSE "${ROOT}")
    message(FATAL_ERROR "unexpected filesystem output:\n${output}")
endif()

if(EXISTS "${test_directory}")
    file(REMOVE_RECURSE "${ROOT}")
    message(FATAL_ERROR "filesystem example left its test directory behind")
endif()

file(REMOVE_RECURSE "${ROOT}")
