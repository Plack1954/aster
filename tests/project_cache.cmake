if(NOT DEFINED LANG_EXECUTABLE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "LANG_EXECUTABLE and TEST_ROOT are required")
endif()

set(project_root "${TEST_ROOT}/project")
set(cache_root "${TEST_ROOT}/cache")
set(manifest "${project_root}/Cache.asproj")
set(source "${project_root}/src/cache/lib.as")
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${project_root}/src/cache" "${cache_root}")
file(WRITE "${manifest}"
    "name = \"Cache.Fixture\"\n"
    "output_type = \"exe\"\n"
    "source_root = \"src\"\n"
    "entry = \"Cache.Lib\"\n")
file(WRITE "${source}"
    "namespace Cache.Lib;\n"
    "int main() { return 1; }\n")

function(run_cached operation expected_trace expected_status output_file)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "ASTER_CACHE_DIR=${cache_root}"
            "ASTER_CACHE_TRACE=1"
            "${LANG_EXECUTABLE}" project "${operation}" "${manifest}"
        RESULT_VARIABLE status
        OUTPUT_FILE "${output_file}"
        ERROR_VARIABLE error)
    if(NOT status EQUAL expected_status)
        message(FATAL_ERROR
            "${operation} returned ${status}, expected ${expected_status}: ${error}")
    endif()
    if(NOT error MATCHES "aster cache: ${expected_trace}")
        message(FATAL_ERROR
            "${operation} did not report ${expected_trace}: ${error}")
    endif()
endfunction()

set(empty "${TEST_ROOT}/empty.txt")
run_cached("check" "miss project check" 0 "${empty}")
run_cached("check" "hit project check" 0 "${empty}")

set(first_c "${TEST_ROOT}/first.c")
set(cached_c "${TEST_ROOT}/cached.c")
run_cached("emit-c" "miss project C" 0 "${first_c}")
run_cached("emit-c" "hit project C" 0 "${cached_c}")
file(SHA256 "${first_c}" first_hash)
file(SHA256 "${cached_c}" cached_hash)
if(NOT first_hash STREQUAL cached_hash)
    message(FATAL_ERROR "cached generated C differs from cold output")
endif()

# Corrupt artifacts are rejected and replaced rather than replayed.
file(GLOB c_cache "${cache_root}/*-c.cache")
list(LENGTH c_cache c_cache_count)
if(NOT c_cache_count EQUAL 1)
    message(FATAL_ERROR "expected one generated-C cache artifact")
endif()
list(GET c_cache 0 c_cache_file)
file(APPEND "${c_cache_file}" "corrupt")
set(recovered_c "${TEST_ROOT}/recovered.c")
run_cached("emit-c" "miss project C" 0 "${recovered_c}")
file(SHA256 "${recovered_c}" recovered_hash)
if(NOT first_hash STREQUAL recovered_hash)
    message(FATAL_ERROR "recovered generated C differs from cold output")
endif()

# A semantic source edit invalidates both operation slots.
file(WRITE "${source}"
    "namespace Cache.Lib;\n"
    "int main() { return 2; }\n")
run_cached("check" "miss project check" 0 "${empty}")
set(second_c "${TEST_ROOT}/second.c")
run_cached("emit-c" "miss project C" 0 "${second_c}")
file(SHA256 "${second_c}" second_hash)
if(first_hash STREQUAL second_hash)
    message(FATAL_ERROR "source edit did not change generated C")
endif()

# Failed checks are deliberately never cached.
file(APPEND "${source}" "public int broken( {\n")
run_cached("check" "miss project check" 1 "${empty}")
run_cached("check" "miss project check" 1 "${empty}")

file(REMOVE_RECURSE "${TEST_ROOT}")
