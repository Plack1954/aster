if(NOT DEFINED LANG_EXECUTABLE OR NOT DEFINED SOURCE_FILE)
    message(FATAL_ERROR "LANG_EXECUTABLE and SOURCE_FILE are required")
endif()

execute_process(
    COMMAND "${LANG_EXECUTABLE}" dump-ir "${SOURCE_FILE}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE ir
    ERROR_VARIABLE error
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "dump-ir failed: ${error}")
endif()

foreach(function_name MakeBuffer MakeList MakeHtml MakeStruct)
    string(FIND "${ir}" "function Loaded0::${function_name}" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "missing IR for ${function_name}")
    endif()
    string(SUBSTRING "${ir}" ${start} -1 remainder)
    string(FIND "${remainder}" "\nfunction " next)
    if(next EQUAL -1)
        set(section "${remainder}")
    else()
        string(SUBSTRING "${remainder}" 0 ${next} section)
    endif()
    string(FIND "${section}" "local_move" move_position)
    if(move_position EQUAL -1)
        message(FATAL_ERROR "${function_name} does not return by local move")
    endif()
    string(FIND "${section}" "value_clone" clone_position)
    if(NOT clone_position EQUAL -1)
        message(FATAL_ERROR "${function_name} deep-copies its return value")
    endif()
endforeach()
