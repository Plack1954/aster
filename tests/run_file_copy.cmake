if(NOT DEFINED LANG OR NOT DEFINED SOURCE OR NOT DEFINED ROOT OR
   NOT DEFINED WORKING)
    message(FATAL_ERROR "LANG, SOURCE, ROOT, and WORKING are required")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(input "${ROOT}/input.bin")
set(output "${ROOT}/output.bin")

string(REPEAT "Aster-streaming-copy-0123456789\n" 4096 contents)
file(WRITE "${input}" "${contents}")
file(SIZE "${input}" expected_size)
file(SHA256 "${input}" expected_hash)

execute_process(
    COMMAND "${LANG}" run "${SOURCE}" -- "${input}" "${output}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE program_output
    ERROR_VARIABLE error
    WORKING_DIRECTORY "${WORKING}"
)

if(NOT status EQUAL 0)
    file(REMOVE_RECURSE "${ROOT}")
    message(FATAL_ERROR
        "buffered copy failed (${status})\n${program_output}\n${error}")
endif()
if(NOT program_output MATCHES "^${expected_size}[\r\n]+$")
    file(REMOVE_RECURSE "${ROOT}")
    message(FATAL_ERROR "unexpected copied-byte count: ${program_output}")
endif()
if(NOT EXISTS "${output}")
    file(REMOVE_RECURSE "${ROOT}")
    message(FATAL_ERROR "buffered copy did not create output")
endif()
file(SHA256 "${output}" actual_hash)
if(NOT actual_hash STREQUAL expected_hash)
    file(REMOVE_RECURSE "${ROOT}")
    message(FATAL_ERROR "buffered copy content mismatch")
endif()

file(REMOVE_RECURSE "${ROOT}")
