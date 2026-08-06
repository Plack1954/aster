cmake_minimum_required(VERSION 3.16)
project(aster_lang VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

option(LANG_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LANG_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(ASTER_GENERATED_C_STRICT
    "Compile every generated-C test with the full warning suite" OFF)
option(ASTER_ENABLE_SQLITE_TESTS
    "Enable the slow SQLite and issue-tracker integration tests" OFF)
set(ASTER_GENERATED_C_TEST_LANES 2 CACHE STRING
    "Maximum number of generated-C test compilers run concurrently")
option(LANG_VM_COMPUTED_GOTO
    "Use the optional GCC/Clang computed-goto VM dispatch" OFF)
set(ASTER_H2O_SOURCE_ROOT "" CACHE PATH
    "Path to the pinned H2O source tree (enables Lime.H2O)")
set(ASTER_H2O_BUILD_ROOT "" CACHE PATH
    "Path containing the matching libh2o-evloop shared library")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

function(lang_enable_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes
        -Wmissing-prototypes -Werror=implicit-function-declaration
    )
    if(LANG_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

add_library(langlib
    src/common.c
    src/lexer.c
    src/css.c
    src/parser.c
    src/parser_elements.c
    src/checker.c
    src/checker_calls.c
    src/checker_types.c
    src/checker_elements.c
    src/checker_generics.c
    src/checker_debug.c
    src/ir.c
    src/ir_statements.c
    src/ir_html.c
    src/ir_verify.c
    src/ir_debug.c
    src/ir_bytecode.c
    src/c_backend.c
    src/c_backend_module.c
    src/c_backend_exports.c
    src/c_backend_types.c
    src/c_backend_html.c
    src/c_backend_css.c
    src/c_backend_runtime.c
    src/c_backend_async.c
    src/bytecode.c
    src/vm.c
    src/vm_async.c
    src/vm_values.c
    src/vm_builtin_exec.c
    src/vm_fast.c
    src/vm_html.c
    src/vm_builtins.c
    src/vm_verify.c
    src/http.c
    src/h2o_adapter.c
    src/sqlite.c
    src/project.c
    src/layout.c
)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(src/vm.c PROPERTIES
        COMPILE_OPTIONS "$<$<CONFIG:Release>:-O2>")
endif()
target_include_directories(langlib PUBLIC include)
target_compile_definitions(langlib PRIVATE
    ASTER_BROWSER_RUNTIME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/runtime/browser"
    ASTER_BUILD_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/std"
    ASTER_INSTALL_STDLIB_DIR="${CMAKE_INSTALL_FULL_DATADIR}/aster/std")
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    target_compile_definitions(langlib PRIVATE LANG_HAVE_SQLITE3=1)
    target_link_libraries(langlib PUBLIC SQLite::SQLite3)
endif()
set(LANG_HAVE_H2O OFF)
if(ASTER_H2O_SOURCE_ROOT OR ASTER_H2O_BUILD_ROOT)
    if(NOT ASTER_H2O_SOURCE_ROOT OR NOT ASTER_H2O_BUILD_ROOT)
        message(FATAL_ERROR
            "Lime.H2O requires both ASTER_H2O_SOURCE_ROOT and ASTER_H2O_BUILD_ROOT")
    endif()
    set(ASTER_H2O_LIBRARY
        "${ASTER_H2O_BUILD_ROOT}/libh2o-evloop.so")
    if(NOT EXISTS "${ASTER_H2O_SOURCE_ROOT}/include/h2o.h")
        message(FATAL_ERROR
            "ASTER_H2O_SOURCE_ROOT does not contain include/h2o.h")
    endif()
    if(NOT EXISTS "${ASTER_H2O_LIBRARY}")
        message(FATAL_ERROR
            "ASTER_H2O_BUILD_ROOT does not contain libh2o-evloop.so")
    endif()
    set(LANG_HAVE_H2O ON)
    target_compile_definitions(langlib PRIVATE
        LANG_HAVE_H2O=1 H2O_USE_LIBUV=0 _GNU_SOURCE=1)
    target_include_directories(langlib SYSTEM PRIVATE
        "${ASTER_H2O_SOURCE_ROOT}/include"
        "${ASTER_H2O_SOURCE_ROOT}/deps/picotls/include"
        "${ASTER_H2O_SOURCE_ROOT}/deps/quicly/include")
    target_link_libraries(langlib PUBLIC "${ASTER_H2O_LIBRARY}")
    message(STATUS "Lime.H2O enabled with ${ASTER_H2O_LIBRARY}")
endif()
if(LANG_VM_COMPUTED_GOTO)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR
            "LANG_VM_COMPUTED_GOTO requires GCC or Clang")
    endif()
    target_compile_definitions(langlib PRIVATE
        LANG_VM_COMPUTED_GOTO=1)
endif()
lang_enable_warnings(langlib)
if(LANG_VM_COMPUTED_GOTO)
    target_compile_options(langlib PRIVATE -Wno-pedantic)
endif()

if(LANG_SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(langlib PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(langlib PUBLIC -fsanitize=address,undefined)
endif()

add_executable(lang src/main.c)
target_link_libraries(lang PRIVATE langlib)
lang_enable_warnings(lang)

install(TARGETS lang RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY std/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/aster/std
    FILES_MATCHING PATTERN "*.as")

enable_testing()
set_property(GLOBAL PROPERTY ASTER_GENERATED_C_NEXT_LANE 0)

function(aster_limit_generated_c_test test_name)
    if(NOT ASTER_GENERATED_C_TEST_LANES MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR
            "ASTER_GENERATED_C_TEST_LANES must be a positive integer")
    endif()
    get_property(next_lane GLOBAL PROPERTY ASTER_GENERATED_C_NEXT_LANE)
    math(EXPR lane "${next_lane} % ${ASTER_GENERATED_C_TEST_LANES}")
    math(EXPR next_lane "${next_lane} + 1")
    set_property(GLOBAL PROPERTY ASTER_GENERATED_C_NEXT_LANE ${next_lane})
    set_tests_properties(${test_name} PROPERTIES
        RESOURCE_LOCK "aster_generated_c_compiler_${lane}"
        TIMEOUT 120)
endfunction()

# Keep generated-C test toolchain, runtime, and sanitizer wiring in one place.
# Additional -D arguments are forwarded to the runner and may intentionally
# override a default (for example, leak checks always enable sanitizers).
function(add_aster_generated_c_test name source_file output_basename)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DLANG_EXECUTABLE=$<TARGET_FILE:lang>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE_FILE=${source_file}
            -DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}
            -DOUTPUT_BASENAME=${output_basename}
            -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -DINCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
            -DRUNTIME_LIBRARY=$<TARGET_FILE:langlib>
            -DGENERATED_SANITIZE=${LANG_SANITIZE}
            -DGENERATED_STRICT=${ASTER_GENERATED_C_STRICT}
            "-DEXTRA_LIBRARIES=${SQLite3_LIBRARIES}"
            -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            ${ARGN}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compile_c_backend.cmake)
    aster_limit_generated_c_test(${name})
endfunction()

function(add_aster_generated_project_test name manifest_file target_name)
    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DLANG_EXECUTABLE=$<TARGET_FILE:lang>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DMANIFEST_FILE=${manifest_file}
            -DTARGET_NAME=${target_name}
            -DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}
            -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -DINCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
            -DRUNTIME_LIBRARY=$<TARGET_FILE:langlib>
            -DGENERATED_SANITIZE=${LANG_SANITIZE}
            -DGENERATED_STRICT=${ASTER_GENERATED_C_STRICT}
            "-DEXTRA_LIBRARIES=${SQLite3_LIBRARIES}"
            -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            ${ARGN}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compile_c_project.cmake)
    aster_limit_generated_c_test(${name})
endfunction()

add_test(NAME language_tests COMMAND lang test)
set_tests_properties(language_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME readonly_span_mutation_rejected
    COMMAND lang check tests/checker/readonly_span_mutation.as)
set_tests_properties(readonly_span_mutation_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME deleted_copy_constructor_rejected
    COMMAND lang check tests/checker/deleted_copy_constructor.as)
set_tests_properties(deleted_copy_constructor_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME custom_copy_source_is_immutable
    COMMAND lang check tests/checker/custom_copy_mutates_source.as)
set_tests_properties(custom_copy_source_is_immutable PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME nested_deleted_copy_rejected
    COMMAND lang check tests/checker/nested_deleted_copy.as)
set_tests_properties(nested_deleted_copy_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME tagged_union_custom_copy_vm
    COMMAND lang run tests/tagged_union_custom_copy.as)
set_tests_properties(tagged_union_custom_copy_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "11[\r\n]+12[\r\n]+0[\r\n]+22[\r\n]+32[\r\n]+43")
add_aster_generated_c_test(
    tagged_union_custom_copy_generated_c
    tests/tagged_union_custom_copy.as
    tagged_union_custom_copy
    "-DEXPECTED_OUTPUT=11\n12\n0\n22\n32\n43\n")
add_test(NAME tagged_union_custom_copy_cleanup_vm
    COMMAND lang run tests/tagged_union_custom_copy_cleanup.as)
set_tests_properties(tagged_union_custom_copy_cleanup_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "2[\r\n]+1")
add_aster_generated_c_test(
    tagged_union_custom_copy_cleanup_generated_c
    tests/tagged_union_custom_copy_cleanup.as
    tagged_union_custom_copy_cleanup
    "-DEXPECTED_OUTPUT=2\n1\n")
add_test(NAME option_deleted_copy_rejected
    COMMAND lang check tests/checker/option_custom_copy.as)
set_tests_properties(option_deleted_copy_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME list_custom_copy_vm
    COMMAND lang run tests/list_custom_copy.as)
set_tests_properties(list_custom_copy_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "3[\r\n]+4")
add_aster_generated_c_test(
    list_custom_copy_generated_c
    tests/list_custom_copy.as
    list_custom_copy
    "-DEXPECTED_OUTPUT=3\n4\n")
add_test(NAME list_custom_copy_cleanup_vm
    COMMAND lang run tests/list_custom_copy_cleanup.as)
set_tests_properties(list_custom_copy_cleanup_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "3[\r\n]+2[\r\n]+1")
add_aster_generated_c_test(
    list_custom_copy_cleanup_generated_c
    tests/list_custom_copy_cleanup.as
    list_custom_copy_cleanup
    "-DEXPECTED_OUTPUT=3\n2\n1\n")
add_test(NAME list_deleted_copy_rejected
    COMMAND lang check tests/checker/list_deleted_copy.as)
set_tests_properties(list_deleted_copy_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME projection_deleted_copy_rejected
    COMMAND lang check tests/checker/projection_deleted_copy.as)
set_tests_properties(projection_deleted_copy_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME sequential_collection_custom_copy_vm
    COMMAND lang run tests/sequential_collection_custom_copy.as)
set_tests_properties(sequential_collection_custom_copy_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "12[\r\n]+13[\r\n]+22[\r\n]+23")
add_aster_generated_c_test(
    sequential_collection_custom_copy_generated_c
    tests/sequential_collection_custom_copy.as
    sequential_collection_custom_copy
    "-DEXPECTED_OUTPUT=12\n13\n22\n23\n")
add_test(NAME associative_collection_custom_copy_vm
    COMMAND lang run tests/associative_collection_custom_copy.as)
set_tests_properties(associative_collection_custom_copy_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "12[\r\n]+13[\r\n]+1")
add_aster_generated_c_test(
    associative_collection_custom_copy_generated_c
    tests/associative_collection_custom_copy.as
    associative_collection_custom_copy
    "-DEXPECTED_OUTPUT=12\n13\n1\n")
add_test(NAME conditional_out_custom_copy_vm
    COMMAND lang run tests/conditional_out_custom_copy.as)
set_tests_properties(conditional_out_custom_copy_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "12[\r\n]+0[\r\n]+22[\r\n]+0[\r\n]+32[\r\n]+0")
add_aster_generated_c_test(
    conditional_out_custom_copy_generated_c
    tests/conditional_out_custom_copy.as
    conditional_out_custom_copy
    "-DEXPECTED_OUTPUT=12\n0\n22\n0\n32\n0\n")
add_test(NAME nested_custom_copy_vm
    COMMAND lang run tests/nested_custom_copy.as)
set_tests_properties(nested_custom_copy_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "41[\r\n]+42")
add_aster_generated_c_test(
    nested_custom_copy_generated_c
    tests/nested_custom_copy.as
    nested_custom_copy
    "-DEXPECTED_OUTPUT=41\n42\n")
add_test(NAME nested_custom_copy_cleanup_vm
    COMMAND lang run tests/nested_custom_copy_cleanup.as)
set_tests_properties(nested_custom_copy_cleanup_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "2[\r\n]+1")
add_aster_generated_c_test(
    nested_custom_copy_cleanup_generated_c
    tests/nested_custom_copy_cleanup.as
    nested_custom_copy_cleanup
    "-DEXPECTED_OUTPUT=2\n1\n")
add_test(NAME array_custom_copy_vm
    COMMAND lang run tests/array_custom_copy.as)
set_tests_properties(array_custom_copy_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "1[\r\n]+2[\r\n]+2[\r\n]+3")
add_aster_generated_c_test(
    array_custom_copy_generated_c
    tests/array_custom_copy.as
    array_custom_copy
    "-DEXPECTED_OUTPUT=1\n2\n2\n3\n")
add_test(NAME generic_wrapper_custom_copy_vm
    COMMAND lang run tests/generic_wrapper_custom_copy.as)
set_tests_properties(generic_wrapper_custom_copy_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "7[\r\n]+8")
add_aster_generated_c_test(
    generic_wrapper_custom_copy_generated_c
    tests/generic_wrapper_custom_copy.as
    generic_wrapper_custom_copy
    "-DEXPECTED_OUTPUT=7\n8\n")
add_test(NAME custom_copy_constructor_vm
    COMMAND lang run tests/custom_copy_constructor.as)
set_tests_properties(custom_copy_constructor_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42[\r\n]+42[\r\n]+43[\r\n]+43")
add_aster_generated_c_test(
    custom_copy_constructor_generated_c
    tests/custom_copy_constructor.as
    custom_copy_constructor
    "-DEXPECTED_OUTPUT=42\n42\n43\n43\n")
add_test(NAME custom_copy_sites_vm
    COMMAND lang run-ir tests/custom_copy_sites.as)
set_tests_properties(custom_copy_sites_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "1[\r\n]+1[\r\n]+1[\r\n]+2[\r\n]+2[\r\n]+3[\r\n]+3[\r\n]+3[\r\n]+3[\r\n]+3")
add_aster_generated_c_test(
    custom_copy_sites_generated_c
    tests/custom_copy_sites.as
    custom_copy_sites
    "-DEXPECTED_OUTPUT=1\n1\n1\n2\n2\n3\n3\n3\n3\n3\n")
add_test(NAME custom_copy_raw_allocation_vm
    COMMAND lang run-ir tests/custom_copy_raw_allocation.as)
set_tests_properties(custom_copy_raw_allocation_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "10[\r\n]+20")
add_aster_generated_c_test(
    custom_copy_raw_allocation_generated_c
    tests/custom_copy_raw_allocation.as
    custom_copy_raw_allocation
    "-DEXPECTED_OUTPUT=10\n20\n")
add_test(NAME custom_copy_site_cleanup_vm
    COMMAND lang run-ir tests/custom_copy_site_cleanup.as)
set_tests_properties(custom_copy_site_cleanup_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "1[\r\n]+99[\r\n]+4[\r\n]+3[\r\n]+4[\r\n]+3[\r\n]+2[\r\n]+2")
add_aster_generated_c_test(
    custom_copy_site_cleanup_generated_c
    tests/custom_copy_site_cleanup.as
    custom_copy_site_cleanup
    "-DEXPECTED_OUTPUT=1\n99\n4\n3\n4\n3\n2\n2\n")
add_test(NAME custom_copy_cleanup_vm
    COMMAND lang run tests/custom_copy_cleanup.as)
set_tests_properties(custom_copy_cleanup_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "1[\r\n]+2[\r\n]+1")
add_aster_generated_c_test(
    custom_copy_cleanup_generated_c
    tests/custom_copy_cleanup.as
    custom_copy_cleanup
    "-DEXPECTED_OUTPUT=1\n2\n1\n")
add_test(NAME struct_members_vm COMMAND lang run tests/struct_members.as)
set_tests_properties(struct_members_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42")
add_test(NAME generic_delegate_vm COMMAND lang run tests/generic_delegate.as)
set_tests_properties(generic_delegate_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    generic_delegate_generated_c tests/generic_delegate.as generic_delegate)
add_aster_generated_c_test(
    struct_members_generated_c tests/struct_members.as struct_members)
add_test(NAME constructor_missing_field_rejected
    COMMAND lang check tests/checker/constructor_missing_field.as)
set_tests_properties(constructor_missing_field_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME readonly_member_mutation_rejected
    COMMAND lang check tests/checker/readonly_member_mutation.as)
set_tests_properties(readonly_member_mutation_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME hello_run COMMAND lang run examples/hello.as)
set_tests_properties(hello_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "Hello, world."
)
add_test(NAME hello_check COMMAND lang check examples/hello.as)
set_tests_properties(hello_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME function_visibility_vm
    COMMAND lang run-ir tests/function_visibility.as)
set_tests_properties(function_visibility_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42")
add_aster_generated_c_test(
    function_visibility_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/function_visibility.as
    aster_function_visibility
        "-DEXPECTED_OUTPUT=42\n")
foreach(visibility_failure
        function_visibility_required
        public_main_rejected
        private_main_rejected
        pub_function_rejected)
    add_test(NAME ${visibility_failure}
        COMMAND lang check tests/checker/${visibility_failure}.as)
    set_tests_properties(${visibility_failure} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        WILL_FAIL TRUE)
endforeach()
add_test(NAME function_visibility_ast_dump
    COMMAND lang dump-ast tests/function_visibility.as)
set_tests_properties(function_visibility_ast_dump PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "private long Internal")
add_test(NAME function_visibility_type_dump
    COMMAND lang dump-types tests/function_visibility.as)
set_tests_properties(function_visibility_type_dump PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "public long Exported")
add_test(NAME function_visibility_ir_dump
    COMMAND lang dump-ir tests/function_visibility.as)
set_tests_properties(function_visibility_ir_dump PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "private function.*Internal")
add_test(NAME function_visibility_bytecode_dump
    COMMAND lang dump-bytecode tests/function_visibility.as)
set_tests_properties(function_visibility_bytecode_dump PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "public function Exported")
add_test(NAME var_inference_vm COMMAND lang run-ir tests/var_inference.as)
set_tests_properties(var_inference_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "Ada[\r\n]+2[\r\n]+Grace"
)
add_aster_generated_c_test(
    var_inference_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/var_inference.as
    aster_var_inference
        "-DEXPECTED_OUTPUT=Ada\n2\nGrace\n")
add_test(NAME var_remains_statically_typed
    COMMAND lang check tests/checker/var_remains_statically_typed.as)
set_tests_properties(var_remains_statically_typed PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME console_surface_vm COMMAND lang run tests/console.as)
set_tests_properties(console_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "output"
)
add_aster_generated_c_test(
    console_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/console.as
    console_surface
        -DEXPECTED_OUTPUT=output\n42\ntrue\n1.5\n
        -DEXPECTED_ERROR=error\nwarning\n)
foreach(removed_console_global IN ITEMS print eprint)
    add_test(NAME global_${removed_console_global}_removed
        COMMAND lang check
            tests/checker/global_${removed_console_global}_removed.as)
    set_tests_properties(global_${removed_console_global}_removed PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        WILL_FAIL TRUE
    )
endforeach()
add_test(NAME stdlib_path_resolution
    COMMAND ${CMAKE_COMMAND}
        -DLANG_EXECUTABLE=$<TARGET_FILE:lang>
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/stdlib-path-resolution
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib_path_resolution.cmake)
add_executable(stdlib_config_test tests/stdlib_config.c)
target_link_libraries(stdlib_config_test PRIVATE langlib)
lang_enable_warnings(stdlib_config_test)
add_test(NAME stdlib_api_configuration
    COMMAND stdlib_config_test
        ${CMAKE_CURRENT_BINARY_DIR}/missing-stdlib
        ${CMAKE_CURRENT_SOURCE_DIR}/std
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/aster_libraries.as)
add_test(NAME async_surface_check COMMAND lang check tests/async_surface.as)
set_tests_properties(async_surface_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME async_ir_dump COMMAND lang dump-ir tests/async_surface.as)
set_tests_properties(async_ir_dump PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "async function.*completes"
)
add_test(NAME async_vm_surface_execution
    COMMAND lang run tests/async_surface.as)
set_tests_properties(async_vm_surface_execution PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME async_vm_execution
    COMMAND lang run tests/async_generated_c.as)
set_tests_properties(async_vm_execution PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME async_exception_vm_execution
    COMMAND lang run tests/async_exception_generated_c.as)
set_tests_properties(async_exception_vm_execution PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "cleaned"
)
add_test(NAME async_task_api_vm_execution
    COMMAND lang run tests/async_task_api.as)
set_tests_properties(async_task_api_vm_execution PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_aster_generated_c_test(
    async_task_api_generated_c_execution
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/async_task_api.as
    async_task_api)
add_aster_generated_c_test(
    task_type_runtime_generated_c_execution
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/task_type_runtime_generated_c.as
    task_type_runtime
        -DEXPECTED_OUTPUT=42\n)
add_test(NAME async_task_combinators_vm_execution
    COMMAND lang run tests/async_task_combinators.as)
set_tests_properties(async_task_combinators_vm_execution PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "10[
]40[
]10[
]20[
]99[
]0")
add_test(NAME async_task_combinator_errors_vm_execution
    COMMAND lang run tests/async_task_combinator_errors.as)
set_tests_properties(async_task_combinator_errors_vm_execution PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "settled[
]failed input[
]Task.WhenAny requires at least one Task")
add_aster_generated_c_test(
    async_task_combinators_generated_c_execution
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/async_task_combinators.as
    async_task_combinators
        -DEXPECTED_OUTPUT=10\n40\n10\n20\n99\n0\n)
add_aster_generated_c_test(
    async_task_combinator_errors_generated_c_execution
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/async_task_combinator_errors.as
    async_task_combinator_errors
        -DEXPECTED_OUTPUT=settled\nfailed\ input\nTask.WhenAny\ requires\ at\ least\ one\ Task\n)
add_test(NAME async_cancellation_vm_execution
    COMMAND lang run tests/async_cancellation.as)
set_tests_properties(async_cancellation_vm_execution PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "false[
]true[
]A task was canceled\.[
]The operation was canceled\.[
]false[
]completed[
]combined canceled")
add_aster_generated_c_test(
    async_cancellation_generated_c_execution
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/async_cancellation.as
    async_cancellation
        -DEXPECTED_OUTPUT=false\ntrue\nA\ task\ was\ canceled.\nThe\ operation\ was\ canceled.\nfalse\ncompleted\ncombined\ canceled\n)
add_aster_generated_c_test(
    async_generated_c_execution
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/async_generated_c.as
    async_generated_c
        -DEXPECTED_OUTPUT=42\n)
add_aster_generated_c_test(
    async_exception_generated_c_execution
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/async_exception_generated_c.as
    async_exception_generated_c
        -DEXPECTED_OUTPUT=cleaned\nboom\n)
add_aster_generated_c_test(
    async_object_results_generated_c_execution
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/async_object_results_generated_c.as
    async_object_results_generated_c)
add_test(NAME await_outside_async_rejected
    COMMAND lang check tests/checker/await_outside_async.as)
set_tests_properties(await_outside_async_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME async_requires_task_rejected
    COMMAND lang check tests/checker/async_requires_task.as)
set_tests_properties(async_requires_task_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME await_requires_task_rejected
    COMMAND lang check tests/checker/await_requires_task.as)
set_tests_properties(await_requires_task_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME dump_tokens_cli
    COMMAND lang dump-tokens examples/hello.as)
set_tests_properties(dump_tokens_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "identifier     int"
)
add_test(NAME dump_ast_cli COMMAND lang dump-ast examples/hello.as)
set_tests_properties(dump_ast_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "int main"
)
add_test(NAME dump_types_cli
    COMMAND lang dump-types examples/hello.as)
set_tests_properties(dump_types_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "int main"
)
add_test(NAME dump_layout_cli
    COMMAND lang dump-layout examples/structs.as)
set_tests_properties(dump_layout_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "Point size=16 align=8"
)
add_test(NAME extern_c_layout
    COMMAND lang dump-layout examples/ffi_layout.as)
set_tests_properties(extern_c_layout PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "CPoint size=8 align=4 abi=c"
)
add_test(NAME invalid_extern_c_layout
    COMMAND lang check tests/checker/invalid_extern_layout.as)
set_tests_properties(invalid_extern_c_layout PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME generic_types_run
    COMMAND lang run examples/generic_types.as)
set_tests_properties(generic_types_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42[\r\n]+7[\r\n]+owned box[\r\n]+owned box[\r\n]+9"
)
add_test(NAME generic_destructor_substitution_run
    COMMAND lang run examples/generic_destructors.as)
set_tests_properties(generic_destructor_substitution_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "2[\r\n]+1"
)
add_test(NAME generic_functions_run
    COMMAND lang run examples/generic_functions.as)
set_tests_properties(generic_functions_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42[\r\n]+generic[\r\n]+boxed[\r\n]+15[\r\n]+8"
)
add_test(NAME function_values_run
    COMMAND lang run examples/function_values.as)
set_tests_properties(function_values_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "5[\r\n]+10[\r\n]+21"
)
add_test(NAME aster_written_libraries_run
    COMMAND lang run examples/aster_libraries.as)
set_tests_properties(aster_written_libraries_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42[\r\n]+true[\r\n]+true[\r\n]+true[\r\n]+true[\r\n]+2[\r\n]+true[\r\n]+<strong>42</strong>[\r\n]+Aster libraries[\r\n]+<strong><section><p>content</p></section></strong>"
)
add_test(NAME http_result_api_run
    COMMAND lang run examples/http_results.as)
set_tests_properties(http_result_api_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "HTTP error returned as Result[\r\n]+HTTP server returned as Result"
)
add_test(NAME multi_route_http_application
    COMMAND lang run examples/http_router.as)
set_tests_properties(multi_route_http_application PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "<h2>home</h2>[\r\n]+<strong>42</strong>[\r\n]+<p>Aster</p>[\r\n]+<p>/users</p>"
)
if(ASTER_ENABLE_SQLITE_TESTS AND SQLite3_FOUND)
    add_test(NAME sqlite_application
        COMMAND lang run examples/sqlite.as)
    set_tests_properties(sqlite_application PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PASS_REGULAR_EXPRESSION "42[\r\n]+Ada"
    )
    add_test(NAME sqlite_application_surface_vm
        COMMAND lang run tests/sqlite_application_surface.as)
    set_tests_properties(sqlite_application_surface_vm PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_aster_generated_c_test(
    sqlite_application_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/sqlite_application_surface.as
    aster_sqlite_application_surface)
    add_test(NAME issue_tracker_render
        COMMAND lang project run
            examples/issue_tracker/aster.toml render)
    set_tests_properties(issue_tracker_render PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PASS_REGULAR_EXPRESSION
            "font-family: sans-serif.*color: #0645ad.*Add an export command.*Search results lose their sort order"
    )
    add_test(NAME issue_tracker_tests
        COMMAND lang project test
            examples/issue_tracker/aster.toml)
    set_tests_properties(issue_tracker_tests PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PASS_REGULAR_EXPRESSION
            "issue tracker model passed[\r\n]+issue tracker design passed"
    )
    add_test(NAME issue_tracker_application_proof
        COMMAND lang project run
            examples/issue_tracker/aster.toml proof)
    set_tests_properties(issue_tracker_application_proof PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PASS_REGULAR_EXPRESSION
            "configuration and file processing passed[\r\n]+validation and SQLite passed[\r\n]+routing and typed HTML passed"
    )
endif()
foreach(function_value_failure
        function_value_signature
        function_value_arity
        extern_function_value)
    add_test(NAME ${function_value_failure}_diagnostic
        COMMAND lang check tests/checker/${function_value_failure}.as)
    set_tests_properties(${function_value_failure}_diagnostic PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        WILL_FAIL TRUE
    )
endforeach()
foreach(generic_failure
        generic_arity
        generic_type_mismatch
        generic_extern_struct
        generic_function_conflict
        generic_function_uninferred
        recursive_generic_inline
        recursive_generic_expansion)
    add_test(NAME ${generic_failure}_diagnostic
        COMMAND lang check tests/checker/${generic_failure}.as)
    set_tests_properties(${generic_failure}_diagnostic PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        WILL_FAIL TRUE
    )
endforeach()
add_test(NAME dump_bytecode_cli
    COMMAND lang dump-bytecode examples/hello.as)
set_tests_properties(dump_bytecode_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "function main:"
)
add_test(NAME dump_ir_cli
    COMMAND lang dump-ir examples/arithmetic.as)
set_tests_properties(dump_ir_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "mul_checked"
)
add_test(NAME dump_ir_aggregates_cli
    COMMAND lang dump-ir examples/aggregate_mutation.as)
set_tests_properties(dump_ir_aggregates_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "local_field_set"
)
add_test(NAME dump_ir_result_try_cli
    COMMAND lang dump-ir examples/result.as)
set_tests_properties(dump_ir_result_try_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "local_enum_payload_move"
)
add_test(NAME dump_ir_iteration_cli
    COMMAND lang dump-ir examples/arrays.as)
set_tests_properties(dump_ir_iteration_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "local_iterator_next"
)
add_test(NAME dump_ir_raw_pointer_cli
    COMMAND lang dump-ir examples/raw_pointer.as)
set_tests_properties(dump_ir_raw_pointer_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "raw_store"
)
add_test(NAME dump_ir_elements_cli
    COMMAND lang dump-ir examples/component_children.as)
set_tests_properties(dump_ir_elements_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "local_element_append"
)
add_test(NAME dump_ir_element_control_flow_cli
    COMMAND lang dump-ir examples/html_match.as)
set_tests_properties(dump_ir_element_control_flow_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "local_enum_is"
)
foreach(ir_scalar_example
        hello
        arithmetic
        logical
        bitwise
        ranges
        method_calls
        function_values
        ir_native
        ir_aggregates
        aggregate_mutation
        structs
        match
        option
        result
        copyable_result
        raii
        arrays
        vector
        ir_iteration
        raw_pointer
        null_pointer
        ir_arena
        html
        html_control
        html_escaping
        html_coverage
        html_qualified
        html_raw
        html_collections
        custom_element
        components
        component_children
        html_match)
endforeach()
add_test(NAME dump_ir_bytecode_cli
    COMMAND lang dump-ir-bytecode examples/arithmetic.as)
set_tests_properties(dump_ir_bytecode_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "MUL_I64"
)
add_test(NAME dump_ir_bytecode_elements_cli
    COMMAND lang dump-ir-bytecode examples/component_children.as)
set_tests_properties(dump_ir_bytecode_elements_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "HTML_APPEND_LOCAL"
)
add_test(NAME emit_c_cli
    COMMAND lang emit-c examples/c_backend.as)
set_tests_properties(emit_c_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "int64_t aster_fn_0"
)

find_program(ASTER_WASM_CLANG NAMES clang)
find_program(ASTER_WASM_LINKER NAMES wasm-ld)
find_program(ASTER_WASM_OPTIMIZER NAMES wasm-opt)
find_program(ASTER_NODE_EXECUTABLE NAMES node)
find_program(ASTER_CURL_EXECUTABLE NAMES curl)
find_program(ASTER_PYTHON_EXECUTABLE NAMES python3)
if(ASTER_WASM_CLANG AND ASTER_WASM_LINKER AND
   ASTER_WASM_OPTIMIZER AND ASTER_NODE_EXECUTABLE)
    add_test(NAME wasm_counter_trial
        COMMAND bash examples/wasm_counter/build.sh $<TARGET_FILE:lang>)
    set_tests_properties(wasm_counter_trial PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PASS_REGULAR_EXPRESSION
            "wasm islands verified: typed exports, SSR counter, and two forms"
    )
endif()
if(ASTER_ENABLE_SQLITE_TESTS AND UNIX AND ASTER_WASM_CLANG AND
   ASTER_WASM_LINKER AND ASTER_WASM_OPTIMIZER AND
   ASTER_CURL_EXECUTABLE AND SQLite3_FOUND)
    add_test(NAME lime_browser_http_integration
        COMMAND bash tests/lime_browser_integration.sh
            $<TARGET_FILE:lang>
            ${CMAKE_C_COMPILER}
            $<TARGET_FILE:langlib>
            ${SQLite3_LIBRARY}
            ${CMAKE_CURRENT_BINARY_DIR}/lime-browser
            $<IF:$<BOOL:${LANG_SANITIZE}>,on,off>)
    set_tests_properties(lime_browser_http_integration PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        TIMEOUT 60
    )
endif()
add_aster_generated_c_test(
    c_backend_compile_and_run
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/c_backend.as
    aster_c_backend_success)
add_test(NAME csharp_surface_vm
    COMMAND lang run tests/csharp_surface.as)
set_tests_properties(csharp_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "7[\r\n]+7[\r\n]+Aster[\r\n]+Aster[\r\n]+0"
)
add_test(NAME c_pointer_surface_vm
    COMMAND lang run tests/c_pointer_surface.as)
set_tests_properties(c_pointer_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME text_surface_vm
    COMMAND lang run tests/text_builder_c.as)
set_tests_properties(text_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME text_search_fused_bytecode
    COMMAND lang dump-ir-bytecode tests/text_builder_c.as)
set_tests_properties(text_search_fused_bytecode PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "STRING_SEARCH_LOCAL_CONSTANT")
add_aster_generated_c_test(
    text_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/text_builder_c.as
    aster_text_surface)
add_test(NAME numeric_conversion_vm
    COMMAND lang run tests/numeric_conversion.as)
set_tests_properties(numeric_conversion_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    numeric_conversion_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/numeric_conversion.as
    aster_numeric_conversion)
add_test(NAME unicode_text_surface_vm
    COMMAND lang run tests/unicode_text_surface.as)
set_tests_properties(unicode_text_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    unicode_text_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/unicode_text_surface.as
    aster_unicode_text_surface)
add_test(NAME datetime_surface_vm
    COMMAND lang run tests/datetime_surface.as)
set_tests_properties(datetime_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    datetime_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/datetime_surface.as
    aster_datetime_surface)
add_test(NAME list_surface_vm
    COMMAND lang run tests/list_surface.as)
set_tests_properties(list_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    list_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/list_surface.as
    aster_list_surface)
add_test(NAME dictionary_surface_vm
    COMMAND lang run tests/dictionary_surface.as)
set_tests_properties(dictionary_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    dictionary_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/dictionary_surface.as
    aster_dictionary_surface)

add_test(NAME hash_set_surface_vm
    COMMAND lang run tests/hash_set_surface.as)
set_tests_properties(hash_set_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    hash_set_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/hash_set_surface.as
    aster_hash_set_surface)
add_test(NAME queue_surface_vm
    COMMAND lang run tests/queue_surface.as)
set_tests_properties(queue_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    queue_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/queue_surface.as
    aster_queue_surface)
add_test(NAME stack_surface_vm
    COMMAND lang run tests/stack_surface.as)
set_tests_properties(stack_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    stack_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/stack_surface.as
    aster_stack_surface)
add_test(NAME json_document_surface_vm
    COMMAND lang run tests/json_document_surface.as)
set_tests_properties(json_document_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    json_document_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/json_document_surface.as
    aster_json_document_surface)
add_test(NAME custom_indexer_surface_vm
    COMMAND lang run tests/custom_indexer_surface.as)
set_tests_properties(custom_indexer_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    custom_indexer_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/custom_indexer_surface.as
    aster_custom_indexer_surface)
add_test(NAME out_parameter_surface_vm
    COMMAND lang run tests/out_parameter_surface.as)
set_tests_properties(out_parameter_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    out_parameter_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/out_parameter_surface.as
    aster_out_parameter_surface)
add_test(NAME out_definite_assignment_vm
    COMMAND lang run-ir tests/out_definite_assignment.as)
set_tests_properties(out_definite_assignment_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "11[\r\n]+22[\r\n]+output failure[\r\n]+77")
add_aster_generated_c_test(
    out_definite_assignment_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/out_definite_assignment.as
    aster_out_definite_assignment
        "-DEXPECTED_OUTPUT=11\n22\noutput failure\n77\n")
foreach(out_flow_failure
        out_read_before_assignment
        out_missing_branch_assignment
        out_loop_only_assignment
        out_early_return_assignment)
    add_test(NAME ${out_flow_failure}
        COMMAND lang check tests/checker/${out_flow_failure}.as)
    set_tests_properties(${out_flow_failure} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} WILL_FAIL TRUE)
endforeach()
add_test(NAME empty_values_vm
    COMMAND lang run-ir tests/empty_values.as)
set_tests_properties(empty_values_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "0[\r\n]+0[\r\n]+0[\r\n]+0[\r\n]+0")
add_aster_generated_c_test(
    empty_values_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/empty_values.as
    aster_empty_values
        "-DEXPECTED_OUTPUT=0\n0\n0\n0\n0\n")
add_test(NAME out_parameter_requires_out
    COMMAND lang check tests/checker/out_parameter_requires_out.as)
set_tests_properties(out_parameter_requires_out PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} WILL_FAIL TRUE)
add_test(NAME out_parameter_requires_place
    COMMAND lang check tests/checker/out_parameter_requires_place.as)
set_tests_properties(out_parameter_requires_place PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} WILL_FAIL TRUE)
add_test(NAME system_io_surface_vm
    COMMAND lang run tests/system_io_surface.as)
set_tests_properties(system_io_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    system_io_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/system_io_surface.as
    aster_system_io_surface)
add_test(NAME streams_binary_surface_vm
    COMMAND lang run tests/streams_binary_surface.as)
set_tests_properties(streams_binary_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    streams_binary_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/streams_binary_surface.as
    aster_streams_binary_surface)
add_test(NAME byte_native_streams_vm
    COMMAND lang run tests/byte_native_streams.as)
set_tests_properties(byte_native_streams_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    byte_native_streams_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/byte_native_streams.as
    aster_byte_native_streams)
add_test(NAME http_binary_surface_vm
    COMMAND lang run tests/http_binary_surface.as)
set_tests_properties(http_binary_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    http_binary_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/http_binary_surface.as
    aster_http_binary_surface)
add_test(NAME overload_surface_vm
    COMMAND lang run tests/overloads.as)
set_tests_properties(overload_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "5[\r\n]+aster[\r\n]+5[\r\n]+9[\r\n]+15[\r\n]+21[\r\n]+5[\r\n]+115[\r\n]+2[\r\n]+9"
)
add_aster_generated_c_test(
    overload_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/overloads.as
    aster_overload_surface)
add_test(NAME duplicate_overload_signature_diagnostic
    COMMAND lang check tests/checker/duplicate_overload.as)
set_tests_properties(duplicate_overload_signature_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME nullable_surface_vm
    COMMAND lang run tests/nullable_surface.as)
set_tests_properties(nullable_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "nullable-null[\r\n]+nullable-present"
)
add_test(NAME exceptions_surface_vm
    COMMAND lang run tests/exceptions_surface.as)
set_tests_properties(exceptions_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "cleaned[\r\n]+database unavailable[\r\n]+recovered"
)
add_aster_generated_c_test(
    exceptions_surface_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/exceptions_surface.as
    exceptions_surface_generated)
add_test(NAME exceptions_finally_vm
    COMMAND lang run tests/exceptions_finally.as)
set_tests_properties(exceptions_finally_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "normal[\r\n]+finally-normal[\r\n]+caught[\r\n]+finally-caught[\r\n]+finally-propagate[\r\n]+propagated[\r\n]+replacement[\r\n]+rethrown[\r\n]+finally-return[\r\n]+7[\r\n]+finally-break"
)
add_aster_generated_c_test(
    exceptions_finally_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/exceptions_finally.as
    exceptions_finally_generated)
add_test(NAME exceptions_native_vm
    COMMAND lang run tests/exceptions_native.as)
set_tests_properties(exceptions_native_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "intentional native failure[\r\n]+native-recovered"
)
add_aster_generated_c_test(
    exceptions_native_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/exceptions_native.as
    exceptions_native_generated)
add_test(NAME exceptions_function_value_vm
    COMMAND lang run tests/exceptions_function_value.as)
set_tests_properties(exceptions_function_value_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    exceptions_function_value_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/exceptions_function_value.as
    exceptions_function_value_generated)
add_test(NAME list_destination_vm
    COMMAND lang run tests/list_destination.as)
set_tests_properties(list_destination_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "Reusable[\r\n]+1[\r\n]+2[\r\n]+Direct[\r\n]+0[\r\n]+7[\r\n]+Reusable[\r\n]+1[\r\n]+11"
)
add_test(NAME c_for_surface_vm
    COMMAND lang run tests/c_for_surface.as)
set_tests_properties(c_for_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "10[\r\n]+8[\r\n]+3[\r\n]+3"
)
add_test(NAME c_array_surface_vm
    COMMAND lang run tests/c_array_surface.as)
set_tests_properties(c_array_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "31[\r\n]+3[\r\n]+8"
)
add_test(NAME compound_place_surface_vm
    COMMAND lang run tests/compound_place_surface.as)
set_tests_properties(compound_place_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "15[\r\n]+99[\r\n]+7[\r\n]+5"
)
add_aster_generated_c_test(
    compound_place_surface_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/compound_place_surface.as
    aster_compound_place_surface
        "-DEXPECTED_OUTPUT=15\n99\n7\n5\n")
add_test(NAME pointer_dereference_surface_vm
    COMMAND lang run tests/pointer_dereference_surface.as)
set_tests_properties(pointer_dereference_surface_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "99[\r\n]+42[\r\n]+0"
)
add_aster_generated_c_test(
    pointer_dereference_surface_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/pointer_dereference_surface.as
    aster_pointer_dereference_surface
        "-DEXPECTED_OUTPUT=99\n42\n0\n")
add_aster_generated_c_test(
    c_array_surface_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_array_surface.as
    aster_c_array_surface
        "-DEXPECTED_OUTPUT=31\n3\n8\n")
add_aster_generated_c_test(
    c_for_surface_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_for_surface.as
    aster_c_for_surface
        "-DEXPECTED_OUTPUT=10\n8\n3\n3\n")
add_aster_generated_c_test(
    csharp_surface_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/csharp_surface.as
    aster_csharp_surface
        "-DEXPECTED_OUTPUT=7\n7\nAster\nAster\n0\n")
add_aster_generated_c_test(
    list_destination_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/list_destination.as
    aster_list_destination
        "-DEXPECTED_OUTPUT=Reusable\n1\n2\nDirect\n0\n7\nReusable\n1\n11\n")
add_aster_generated_c_test(
    c_backend_logical_short_circuit
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/logical.as
    aster_c_logical
        "-DEXPECTED_OUTPUT=3\n4\n5\n6\n7\n8\n")
add_aster_generated_c_test(
    c_backend_bitwise_operations
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_backend_bitwise.as
    aster_c_bitwise
        "-DEXPECTED_OUTPUT=1\n")
add_aster_generated_c_test(
    c_backend_static_method_call
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_backend_method.as
    aster_c_method
        "-DEXPECTED_OUTPUT=62\n")
add_aster_generated_c_test(
    c_backend_control_flow_expressions
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/control_flow_expressions.as
    aster_c_control_flow_expressions
        "-DEXPECTED_OUTPUT=10\n20\n40\n7\n0\n")
add_aster_generated_c_test(
    c_backend_owned_text
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/text.as
    aster_c_owned_text
        "-DEXPECTED_OUTPUT=Hello, Aster.\nowned text\n")
add_aster_generated_c_test(
    c_backend_function_values
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/function_values.as
    aster_c_function_values
        "-DEXPECTED_OUTPUT=5\n10\n21\n")
add_aster_generated_c_test(
    c_backend_vector_runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/vector.as
    aster_c_vector
        "-DEXPECTED_OUTPUT=2\nAda\nLin\n")
add_aster_generated_c_test(
    c_backend_borrowed_vector_iteration
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/borrowed_iteration.as
    aster_c_borrowed_vector
        "-DEXPECTED_OUTPUT=8\n2\n12\n4\n")
add_aster_generated_c_test(
    c_backend_html_runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_backend_html.as
    aster_c_html
        "-DEXPECTED_C_COMPACT_PATTERN=aster_url\\*result=aster_allocate\\(sizeof\\(\\*result\\)\\).*result->references=1U"
        "-DEXPECTED_OUTPUT=<section class=\"a&quot\;&amp\;&lt\;b&gt\;\"><h2>owned &amp\; safe</h2><strong>Administrator</strong><em>trusted</em><a href=\"#\">Open</a></section>\n<section class=\"a&quot\;&amp\;&lt\;b&gt\;\"><h2>owned &amp\; safe</h2><strong>Administrator</strong><em>trusted</em><a href=\"#\">Open</a></section>\n<section class=\"a&quot\;&amp\;&lt\;b&gt\;\"><h2>owned &amp\; safe</h2><em>trusted</em><a href=\"#\">Open</a></section>\n<p>early</p>\n<div><p>late</p></div>\n")
add_aster_generated_c_test(
    c_backend_html_collections
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/html_collections.as
    aster_c_html_collections
        "-DEXPECTED_OUTPUT=<section><strong>Optional</strong><h2>Array A</h2><h2>Array B</h2><ul><li>Ada</li><li>Lin</li></ul></section>\n")
add_aster_generated_c_test(
    c_backend_html_interpolation
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_html_attributes.as
    aster_c_html_interpolation)
add_aster_generated_c_test(
    c_backend_native_css
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_css.as
    aster_c_native_css)
add_aster_generated_c_test(
    c_backend_scoped_css
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/scoped_css.as
    aster_c_scoped_css)
add_test(NAME css_custom_properties_run
    COMMAND lang run tests/css_custom_properties.as)
set_tests_properties(css_custom_properties_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "style=\"--accent: #e45b20; --gap: 12\"")
add_test(NAME css_custom_property_unsafe_vm
    COMMAND lang run tests/css_custom_property_unsafe.as)
set_tests_properties(css_custom_property_unsafe_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} WILL_FAIL TRUE)
add_test(NAME css_custom_property_interpolation_rejected
    COMMAND lang check tests/checker/css_custom_property_interpolation.as)
set_tests_properties(css_custom_property_interpolation_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} WILL_FAIL TRUE)
add_test(NAME css_custom_property_static_rejections
    COMMAND lang check tests/checker/css_custom_property_static_rejections.as)
set_tests_properties(css_custom_property_static_rejections PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} WILL_FAIL TRUE)
add_aster_generated_c_test(
    c_backend_css_custom_properties
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/css_custom_properties.as
    aster_c_css_custom_properties)
add_aster_generated_c_test(
    c_backend_css_custom_property_unsafe
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/css_custom_property_unsafe.as
    aster_c_css_custom_property_unsafe
        -DEXPECTED_RUN_STATUS=1
        -DEXPECTED_ERROR=unsafe.CSS.custom.property.value)
add_aster_generated_c_test(
    c_backend_native_html_text
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_html_text.as
    aster_c_native_html_text)
add_test(NAME c_backend_extracted_css
    COMMAND ${CMAKE_COMMAND}
        -DLANG_EXECUTABLE=$<TARGET_FILE:lang>
        -DC_COMPILER=${CMAKE_C_COMPILER}
        -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/scoped_css.as
        -DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}
        -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/extract_c_css.cmake)
add_aster_generated_c_test(
    c_backend_buffer_and_slice
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_backend_buffer_slice.as
    aster_c_buffer_slice
        "-DEXPECTED_OUTPUT=17\n0\n")
add_aster_generated_c_test(
    c_backend_integer_ranges
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_backend_range.as
    aster_c_range
        "-DEXPECTED_OUTPUT=24\n")
add_aster_generated_c_test(
    c_backend_checked_overflow
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_backend_overflow.as
    aster_c_backend_overflow
        -DEXPECTED_RUN_STATUS=1
        -DEXPECTED_ERROR=integer.overflow)
add_aster_generated_c_test(
    c_backend_aggregates_compile_and_run
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_backend_aggregates.as
    aster_c_backend_aggregates)
add_test(NAME struct_deconstruction_vm
    COMMAND lang run-ir tests/struct_deconstruction.as)
set_tests_properties(struct_deconstruction_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "201[\r\n]+created[\r\n]+1[\r\n]+201")
add_aster_generated_c_test(
    struct_deconstruction_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/struct_deconstruction.as
    aster_struct_deconstruction
        "-DEXPECTED_OUTPUT=201\ncreated\n1\n201\n")
add_aster_generated_c_test(
    c_backend_enums_result_try_compile_and_run
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_backend_enums.as
    aster_c_backend_enums)
add_aster_generated_c_test(
    c_backend_deterministic_cleanup
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_backend_cleanup.as
    aster_c_backend_cleanup
        "-DEXPECTED_OUTPUT=2\n1\n3\n4\n5\n6\n6\n7\n70\n8\n80\n9\n90\n91\n100\n10\n10\n")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    function(add_c_backend_cleanup_leak_test edge)
        add_aster_generated_c_test(
    c_backend_cleanup_${edge}_leak_checked
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_backend_cleanup/${edge}.as
    aster_c_backend_cleanup_${edge}
                -DGENERATED_SANITIZE=ON
                -DGENERATED_LEAK_CHECK=ON)
        set_tests_properties(c_backend_cleanup_${edge}_leak_checked
            PROPERTIES LABELS "cleanup;generated-c;leak")
    endfunction()

    add_c_backend_cleanup_leak_test(normal_scope)
    add_c_backend_cleanup_leak_test(return)
    add_c_backend_cleanup_leak_test(break)
    add_c_backend_cleanup_leak_test(exception_transfer)
    add_c_backend_cleanup_leak_test(catch_entry)
endif()
add_aster_generated_c_test(
    c_backend_owning_field_replacement
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/aggregate_mutation.as
    aster_c_field_replacement
        "-DEXPECTED_OUTPUT=10\n9\n1\n2\n")
add_test(NAME c_backend_owned_html_emits
    COMMAND lang emit-c examples/html.as)
set_tests_properties(c_backend_owned_html_emits PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_aster_generated_c_test(
    c_backend_html_coverage
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/html_coverage.as
    aster_c_html_coverage
        -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/html_coverage.out)
add_test(NAME ref_parameter_value_copy_vm
    COMMAND lang run-ir tests/ref_value_copy.as)
set_tests_properties(ref_parameter_value_copy_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "0[\r\n]+1"
)
add_aster_generated_c_test(
    ref_parameter_value_copy_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/ref_value_copy.as
    aster_ref_parameter_value_copy
        "-DEXPECTED_OUTPUT=0\n1\n")
add_test(NAME ref_aliasing_vm
    COMMAND lang run-ir tests/ref_aliasing.as)
set_tests_properties(ref_aliasing_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "7[\r\n]+8[\r\n]+19[\r\n]+alias failure[\r\n]+changed[\r\n]+7[\r\n]+8")
add_aster_generated_c_test(
    ref_aliasing_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/ref_aliasing.as
    aster_ref_aliasing
        "-DEXPECTED_OUTPUT=7\n8\n19\nalias failure\nchanged\n7\n8\n")
add_test(NAME parameter_mode_boundaries_vm
    COMMAND lang run-ir tests/parameter_mode_boundaries.as)
set_tests_properties(parameter_mode_boundaries_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "41[\r\n]+52[\r\n]+63")
add_aster_generated_c_test(
    parameter_mode_boundaries_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/parameter_mode_boundaries.as
    aster_parameter_mode_boundaries
        "-DEXPECTED_OUTPUT=41\n52\n63\n")
add_test(NAME foreach_value_copy_vm
    COMMAND lang run-ir tests/foreach_value_copy.as)
set_tests_properties(foreach_value_copy_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "99[\r\n]+1[\r\n]+88[\r\n]+2[\r\n]+1"
)
add_aster_generated_c_test(
    foreach_value_copy_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/foreach_value_copy.as
    aster_foreach_value_copy
        "-DEXPECTED_OUTPUT=99\n1\n88\n2\n1\n")
add_test(NAME value_copy_isolation_vm
    COMMAND lang run-ir tests/value_copy_isolation.as)
set_tests_properties(value_copy_isolation_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "1[\r\n]+2[\r\n]+1[\r\n]+2"
)
add_aster_generated_c_test(
    value_copy_isolation_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/value_copy_isolation.as
    aster_value_copy_isolation
        "-DEXPECTED_OUTPUT=1\n2\n1\n2\n")
add_test(NAME return_value_elision_vm
    COMMAND lang run-ir tests/return_value_elision.as)
set_tests_properties(return_value_elision_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "1[\r\n]+<strong>Aster</strong>[\r\n]+1"
)
add_aster_generated_c_test(
    return_value_elision_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/return_value_elision.as
    aster_return_value_elision
        "-DEXPECTED_OUTPUT=1\n<strong>Aster</strong>\n1\n")
add_test(NAME return_value_elision_ir
    COMMAND ${CMAKE_COMMAND}
        -DLANG_EXECUTABLE=$<TARGET_FILE:lang>
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/return_value_elision.as
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_return_elision.cmake)
set_tests_properties(return_value_elision_ir PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_aster_generated_c_test(
    c_backend_borrowed_component_html
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/borrowed_component_html.as
    aster_c_borrowed_component_html
        "-DEXPECTED_OUTPUT=<article><h2>Aster &amp\; HTML</h2></article>\nAster & HTML\n")
add_aster_generated_c_test(
    c_backend_process_environment
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_backend_environment.as
    aster_c_process_environment
        "-DEXPECTED_OUTPUT=configured\n")
set_tests_properties(c_backend_process_environment PROPERTIES
    ENVIRONMENT "ASTER_C_ENV_TEST=configured")
add_aster_generated_c_test(
    c_backend_foreach
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/foreach.as
    aster_c_foreach
        "-DEXPECTED_OUTPUT=First\nSecond\n2\n")
add_test(NAME repl_cli
    COMMAND ${CMAKE_COMMAND}
        -DLANG_EXECUTABLE=$<TARGET_FILE:lang>
        -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/repl_input.txt
        -DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_repl.cmake
)
add_test(NAME benchmark_cli COMMAND lang bench)
set_tests_properties(benchmark_cli PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "compiler(.|[\r\n])*VM(.|[\r\n])*instructions/run"
)
add_test(NAME project_default_target
    COMMAND lang project run tests/project/aster.toml)
set_tests_properties(project_default_target PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "84"
)
add_test(NAME project_library_target_check
    COMMAND lang project check tests/project/aster.toml math)
set_tests_properties(project_library_target_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME project_local_dependency_run
    COMMAND lang project run tests/local_dependency/aster.toml app)
set_tests_properties(project_local_dependency_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "local dependency"
)
add_test(NAME project_local_dependency_emit_c
    COMMAND lang project emit-c tests/local_dependency/aster.toml app)
set_tests_properties(project_local_dependency_emit_c PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME lime_library_check
    COMMAND lang project check packages/lime/aster.toml library)
set_tests_properties(lime_library_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
if(ASTER_PYTHON_EXECUTABLE)
    add_test(NAME lime_typed_routes_generated
        COMMAND ${ASTER_PYTHON_EXECUTABLE}
            tools/generate_lime_typed_routes.py --check)
    set_tests_properties(lime_typed_routes_generated PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
add_test(NAME lime_current_http_check
    COMMAND lang project check packages/lime/aster.toml current_http)
set_tests_properties(lime_current_http_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME lime_h2o_check
    COMMAND lang project check packages/lime/aster.toml h2o)
set_tests_properties(lime_h2o_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME lime_forms_check
    COMMAND lang project check packages/lime/aster.toml forms)
set_tests_properties(lime_forms_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME lime_sessions_check
    COMMAND lang project check packages/lime/aster.toml sessions)
set_tests_properties(lime_sessions_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME lime_static_check
    COMMAND lang project check packages/lime/aster.toml static)
set_tests_properties(lime_static_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME lime_content_check
    COMMAND lang project check packages/lime/aster.toml content)
set_tests_properties(lime_content_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME lime_markdown_check
    COMMAND lang project check packages/lime/aster.toml markdown)
set_tests_properties(lime_markdown_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME lime_smoke
    COMMAND lang project test packages/lime/aster.toml)
set_tests_properties(lime_smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "Lime"
)
if(UNIX)
    add_test(NAME lime_ssg_integration
        COMMAND bash tests/lime_ssg_integration.sh
            $<TARGET_FILE:lang>
            ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(lime_ssg_integration PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
add_test(NAME lime_blog_fixture_c_backend
    COMMAND lang project emit-c packages/lime/aster.toml blog_fixture_server)
set_tests_properties(lime_blog_fixture_c_backend PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_aster_generated_project_test(
    lime_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/lime/aster.toml
    smoke
        "-DEXPECTED_OUTPUT=<h1>Stateful Lime</h1>\n<p>Stateful Lime:missing</p>\n<section><h1>Lime</h1><p>Small, explicit, and written in Aster.</p></section>\n<article>first-post</article>\n<p>two words!:new</p>\n<h1>Method not allowed</h1>\n/\n")
add_aster_generated_project_test(
    lime_async_response_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/lime/aster.toml
    async_response
        "-DEXPECTED_OUTPUT=200\n200\n<article><h1>Async Lime response</h1></article>\n<article><h1>Async Lime response</h1></article>\n")
add_aster_generated_project_test(
    project_c_backend_compile_and_run
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/project/aster.toml
    app
        "-DEXPECTED_OUTPUT=84\n")
add_aster_generated_project_test(
    docs_server_c_backend_compile_and_run
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/docs_server/aster.toml
    render)
if(ASTER_ENABLE_SQLITE_TESTS AND SQLite3_FOUND)
    add_aster_generated_project_test(
    issue_tracker_c_backend_compile_and_run
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/issue_tracker/aster.toml
    render)
    add_aster_generated_project_test(
    issue_tracker_proof_c_backend_compile_and_run
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/issue_tracker/aster.toml
    proof
            "-DEXPECTED_OUTPUT=configuration and file processing passed\nvalidation and SQLite passed\nrouting and typed HTML passed\n")
    add_aster_generated_project_test(
    issue_tracker_generated_c_http_integration
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/issue_tracker/aster.toml
    integration_server
            -DDRIVER_EXECUTABLE=$<TARGET_FILE:generated_http_integration_test>)
    set_tests_properties(
        issue_tracker_generated_c_http_integration PROPERTIES
        RESOURCE_LOCK issue_tracker_integration_database)
endif()
add_test(NAME project_test_targets
    COMMAND lang project test tests/project/aster.toml)
set_tests_properties(project_test_targets PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "\\[pass\\] smoke"
)
add_aster_generated_project_test(
    lime_sessions_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/lime/aster.toml
    sessions_smoke)
set_tests_properties(lime_sessions_c_backend PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME project_whole_namespace_using
    COMMAND lang project check tests/project/aster.toml bad_selective)
set_tests_properties(project_whole_namespace_using PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME project_module_path_identity
    COMMAND lang project check tests/project/aster.toml bad_module)
set_tests_properties(project_module_path_identity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME project_generic_canonical_identity
    COMMAND lang project run tests/project/aster.toml generic_app)
set_tests_properties(project_generic_canonical_identity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "22[\r\n]+14[\r\n]+3[\r\n]+5"
)
add_test(NAME docs_server_render
    COMMAND lang project run examples/docs_server/aster.toml render)
set_tests_properties(docs_server_render PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "configuration parsed(.|[\r\n])*documentation directory traversed[\r\n]+static asset loaded"
)
add_test(NAME docs_server_check
    COMMAND lang project check examples/docs_server/aster.toml server)
set_tests_properties(docs_server_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME docs_server_aster_tests
    COMMAND lang project test examples/docs_server/aster.toml)
set_tests_properties(docs_server_aster_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "\\[pass\\] smoke"
)
add_test(NAME html_run COMMAND lang run examples/html.as)
set_tests_properties(html_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "&amp;.*&lt;simple&gt;"
)
add_test(NAME html_control_run COMMAND lang run examples/html_control.as)
set_tests_properties(html_control_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "<strong>Visible</strong>.*<li>Ada</li><li>Lin</li>"
)
add_test(NAME html_render_golden
    COMMAND ${CMAKE_COMMAND}
        -DLANG_EXECUTABLE=$<TARGET_FILE:lang>
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/html.as
        -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/html.out
        -DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_golden.cmake
)
add_test(NAME html_control_golden
    COMMAND ${CMAKE_COMMAND}
        -DLANG_EXECUTABLE=$<TARGET_FILE:lang>
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/html_control.as
        -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/html_control.out
        -DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_golden.cmake
)
add_test(NAME html_all_escaping_golden
    COMMAND ${CMAKE_COMMAND}
        -DLANG_EXECUTABLE=$<TARGET_FILE:lang>
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/html_escaping.as
        -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/html_escaping.out
        -DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_golden.cmake
)
add_test(NAME html_coverage_golden
    COMMAND ${CMAKE_COMMAND}
        -DLANG_EXECUTABLE=$<TARGET_FILE:lang>
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/html_coverage.as
        -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/html_coverage.out
        -DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_golden.cmake
)
add_test(NAME html_qualified_names_golden
    COMMAND ${CMAKE_COMMAND}
        -DLANG_EXECUTABLE=$<TARGET_FILE:lang>
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/html_qualified.as
        -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/html_qualified.out
        -DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_golden.cmake
)
add_test(NAME unknown_element_property_diagnostic
    COMMAND lang check tests/checker/unknown_element_property.as)
set_tests_properties(unknown_element_property_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME duplicate_element_property_diagnostic
    COMMAND lang check tests/checker/duplicate_element_property.as)
set_tests_properties(duplicate_element_property_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME element_disallows_children_diagnostic
    COMMAND lang check tests/checker/element_disallows_children.as)
set_tests_properties(element_disallows_children_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME unknown_element_diagnostic
    COMMAND lang check tests/checker/unknown_element.as)
set_tests_properties(unknown_element_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME mismatched_element_closing_diagnostic
    COMMAND lang check tests/parser/mismatched_element.as)
set_tests_properties(mismatched_element_closing_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME malformed_element_brace_diagnostic
    COMMAND lang check tests/parser/malformed_element_brace.as)
set_tests_properties(malformed_element_brace_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME recursion_run COMMAND lang run examples/recursion.as)
set_tests_properties(recursion_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "55"
)
add_test(NAME expression_return_run
    COMMAND lang run examples/expression_return.as)
set_tests_properties(expression_return_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME result_try_run COMMAND lang run examples/result.as)
set_tests_properties(result_try_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME structurally_copyable_result_run
    COMMAND lang run examples/copyable_result.as)
set_tests_properties(structurally_copyable_result_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME module_import_run COMMAND lang run examples/modules/main.as)
set_tests_properties(module_import_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME qualified_module_function_run
    COMMAND lang run tests/modules/qualified_main.as)
set_tests_properties(qualified_module_function_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "true[\r\n]+false"
)
add_test(NAME module_namespace_isolation
    COMMAND lang run tests/modules/isolation_main.as)
set_tests_properties(module_namespace_isolation PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "^42[\r\n]+$"
)
add_test(NAME ambiguous_public_import_diagnostic
    COMMAND lang check tests/modules/ambiguous_main.as)
set_tests_properties(ambiguous_public_import_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME module_type_and_destructor_isolation
    COMMAND lang run tests/modules/type_isolation_main.as)
set_tests_properties(module_type_and_destructor_isolation PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "102[\r\n]+1"
)
add_test(NAME same_named_module_types_are_distinct
    COMMAND lang check tests/modules/type_mismatch_main.as)
set_tests_properties(same_named_module_types_are_distinct PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME module_copy_metadata_isolation
    COMMAND lang run tests/modules/type_copy_isolation_main.as)
set_tests_properties(module_copy_metadata_isolation PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME temporary_value_cleanup
    COMMAND lang run examples/temporary_values.as)
set_tests_properties(temporary_value_cleanup PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "temporary clone[\r\n]+20[\r\n]+3"
)
add_test(NAME imported_nested_type_isolation_identity
    COMMAND lang check tests/modules/nested_type_isolation_main.as)
set_tests_properties(imported_nested_type_isolation_identity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME qualified_struct_runtime_identity
    COMMAND lang run tests/modules/qualified_struct_main.as)
set_tests_properties(qualified_struct_runtime_identity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "7"
)
add_test(NAME imported_destructor_copy_check
    COMMAND lang check tests/modules/imported_destructor_copy.as)
set_tests_properties(imported_destructor_copy_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME qualified_enum_runtime_identity
    COMMAND lang run tests/modules/qualified_enum_main.as)
set_tests_properties(qualified_enum_runtime_identity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42[\r\n]+0"
)
add_test(NAME arena_run COMMAND lang run examples/arena.as)
set_tests_properties(arena_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME source_ffi_run COMMAND lang run examples/ffi.as)
set_tests_properties(source_ffi_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME mutable_slice_ffi_run COMMAND lang run examples/ffi_slice.as)
set_tests_properties(mutable_slice_ffi_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "16"
)
add_test(NAME native_result_ffi_run COMMAND lang run examples/ffi_result.as)
set_tests_properties(native_result_ffi_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME file_raii_run COMMAND lang run examples/file.as)
set_tests_properties(file_raii_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "Hello, world\\."
)
add_test(
    NAME process_arguments_and_environment
    COMMAND lang run examples/process.as -- aster-argument)
set_tests_properties(process_arguments_and_environment PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    ENVIRONMENT "ASTER_PROCESS_TEST=aster-environment"
    PASS_REGULAR_EXPRESSION
        "1[\r\n]+aster-argument[\r\n]+aster-environment"
)
add_test(
    NAME filesystem_application_operations
    COMMAND ${CMAKE_COMMAND}
        -DLANG=$<TARGET_FILE:lang>
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/filesystem.as
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/filesystem-test
        -DWORKING=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_filesystem.cmake
)
add_test(
    NAME directory_discovery_vm
    COMMAND lang run tests/directory_discovery.as
)
set_tests_properties(directory_discovery_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "packages/lime/test_content/a.md[\r\n]+packages/lime/test_content/b.md"
)
add_aster_generated_c_test(
    directory_discovery_c_backend
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/directory_discovery.as
    aster_directory_discovery
        "-DEXPECTED_OUTPUT=packages/lime/test_content/a.md\npackages/lime/test_content/b.md\n")
set_tests_properties(directory_discovery_c_backend PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(
    NAME cli_parser_application
    COMMAND lang run examples/cli.as --
        --verbose --output=report.txt input.as --
        --literal -x
)
set_tests_properties(cli_parser_application PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "flag[\r\n]+verbose[\r\n]+named[\r\n]+output[\r\n]+report.txt[\r\n]+positional[\r\n]+input.as[\r\n]+positional[\r\n]+--literal[\r\n]+positional[\r\n]+-x"
)
add_test(
    NAME text_utility_application
    COMMAND lang run examples/text_utilities.as
)
set_tests_properties(text_utility_application PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "true[\r\n]+true[\r\n]+true[\r\n]+-9223372036854775808[\r\n]+18446744073709551615"
)
add_test(
    NAME aster_testing_project
    COMMAND lang project test
        examples/testing_project/aster.toml
)
set_tests_properties(aster_testing_project PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "\\[pass\\][\r\n]+addition[\r\n]+\\[pass\\][\r\n]+greeting.*\\[pass\\] suite.*1 project tests, 0 failures"
)
add_test(
    NAME aster_testing_failure_output
    COMMAND lang run examples/testing_failure.as
)
set_tests_properties(aster_testing_failure_output PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(
    NAME buffered_file_copy
    COMMAND ${CMAKE_COMMAND}
        -DLANG=$<TARGET_FILE:lang>
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/file_copy.as
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/file-copy-test
        -DWORKING=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_file_copy.cmake
)
add_test(
    NAME buffered_line_reader
    COMMAND ${CMAKE_COMMAND}
        -DLANG=$<TARGET_FILE:lang>
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/read_lines.as
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/read-lines-test
        -DWORKING=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_read_lines.cmake
)

add_test(
    NAME callback_line_reader
    COMMAND ${CMAKE_COMMAND}
        -DLANG=$<TARGET_FILE:lang>
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/for_each_line.as
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/callback-line-reader
        -DWORKING=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_for_each_line.cmake
)
add_test(
    NAME callback_line_reader_error
    COMMAND ${CMAKE_COMMAND}
        -DLANG=$<TARGET_FILE:lang>
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/for_each_line_error.as
        -DROOT=${CMAKE_CURRENT_BINARY_DIR}/callback-line-reader-error
        -DWORKING=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_for_each_line_error.cmake
)
add_test(NAME raii_cleanup_run COMMAND lang run examples/raii.as)
set_tests_properties(raii_cleanup_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "21[\r\n]+2134"
)
add_test(NAME component_element_run COMMAND lang run examples/components.as)
set_tests_properties(component_element_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "<h2>Ada</h2><strong>Administrator</strong>.*<h2>Lin</h2>"
)
add_test(NAME loop_cleanup_run COMMAND lang run examples/loop_cleanup.as)
set_tests_properties(loop_cleanup_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "12[\r\n]+123"
)
add_test(NAME enum_match_run COMMAND lang run examples/match.as)
set_tests_properties(enum_match_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42[\r\n]+0"
)
add_test(NAME html_match_run COMMAND lang run examples/html_match.as)
set_tests_properties(html_match_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "<section><strong>Loaded</strong></section>"
)
add_test(NAME raw_pointer_run COMMAND lang run examples/raw_pointer.as)
set_tests_properties(raw_pointer_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME null_pointer_run COMMAND lang run examples/null_pointer.as)
set_tests_properties(null_pointer_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "null"
)
add_test(NAME nested_array_types_run
    COMMAND lang run examples/nested_arrays.as)
set_tests_properties(nested_array_types_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "3[\r\n]+null element"
)
add_test(NAME component_children_run
    COMMAND lang run examples/component_children.as)
set_tests_properties(component_children_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "<section class=\"panel\"><h2>People</h2><strong>Featured</strong><ul><li>Ada</li><li>Lin</li></ul></section>"
)
add_test(NAME borrowed_native_run COMMAND lang run examples/borrowed_native.as)
set_tests_properties(borrowed_native_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "7[\r\n]+7[\r\n]+0[\r\n]+7"
)
add_test(NAME field_value_copy_run
    COMMAND lang run tests/checker/field_value_copy.as)
set_tests_properties(field_value_copy_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "borrowed field"
)
add_test(NAME mutable_borrowed_parameter_run
    COMMAND lang run tests/checker/mutable_borrowed_parameter.as)
set_tests_properties(mutable_borrowed_parameter_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME immutable_mutable_borrow_diagnostic
    COMMAND lang check tests/checker/immutable_mutable_borrow.as)
set_tests_properties(immutable_mutable_borrow_diagnostic PROPERTIES
    WILL_FAIL TRUE
    PASS_REGULAR_EXPRESSION
        "mutable borrowed argument requires a `mut` local"
)
add_test(NAME aggregate_cleanup_run
    COMMAND lang run examples/aggregate_cleanup.as)
set_tests_properties(aggregate_cleanup_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "0[\r\n]+0[\r\n]+5"
)
add_test(NAME aggregate_mutation_run
    COMMAND lang run examples/aggregate_mutation.as)
set_tests_properties(aggregate_mutation_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "10[\r\n]+9[\r\n]+1[\r\n]+2"
)
add_test(NAME native_handle_field_borrow_run
    COMMAND lang run examples/native_handle_field.as)
set_tests_properties(native_handle_field_borrow_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42[\r\n]+0[\r\n]+7"
)
add_test(NAME language_destructors_run
    COMMAND lang run examples/destructors.as)
set_tests_properties(language_destructors_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "2[\r\n]+1[\r\n]+21[\r\n]+3[\r\n]+213[\r\n]+4[\r\n]+2134"
)
add_test(NAME mutable_destructor_receiver_run
    COMMAND lang run examples/destructor_mutation.as)
set_tests_properties(mutable_destructor_receiver_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME nested_destructors_run
    COMMAND lang run examples/nested_destructors.as)
set_tests_properties(nested_destructors_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "7[\r\n]+7[\r\n]+8[\r\n]+8[\r\n]+9[\r\n]+9[\r\n]+10"
)
add_test(NAME numeric_types_run
    COMMAND lang run examples/numeric_types.as)
set_tests_properties(numeric_types_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42[\r\n]+65535[\r\n]+2.5[\r\n]+20[\r\n]+18446744073709551615[\r\n]+-9223372036854775808[\r\n]+42[\r\n]+42[\r\n]+65"
)
add_test(NAME integer_shifts_run COMMAND lang run examples/shifts.as)
set_tests_properties(integer_shifts_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "12[\r\n]+-2[\r\n]+1[\r\n]+12[\r\n]+42"
)
add_test(NAME option_run COMMAND lang run examples/option.as)
set_tests_properties(option_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42[\r\n]+0"
)
add_test(NAME type_alias_run COMMAND lang run examples/type_alias.as)
set_tests_properties(type_alias_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME text_runtime_run COMMAND lang run examples/text.as)
set_tests_properties(text_runtime_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "Hello, Aster\\.[\r\n]+owned text"
)
add_test(NAME decoded_string_escapes_run
    COMMAND lang run examples/string_escapes.as)
set_tests_properties(decoded_string_escapes_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "3[\r\n]+line[\r\n]+\"quote\"\\\\[\r\n]+3[\r\n]+3"
)
add_test(NAME vector_runtime_run COMMAND lang run examples/vector.as)
set_tests_properties(vector_runtime_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "2[\r\n]+Ada[\r\n]+Lin"
)
add_test(NAME custom_element_run COMMAND lang run examples/custom_element.as)
set_tests_properties(custom_element_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "<notice tone=\"warning\"><strong>Careful</strong></notice>"
)
add_test(NAME explicit_raw_html_run COMMAND lang run examples/html_raw.as)
set_tests_properties(explicit_raw_html_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "<section><em>trusted</em></section>"
)
add_test(NAME native_css_run COMMAND lang run tests/native_css.as)
set_tests_properties(native_css_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "@future-layout experimental\\(value\\).*future-property: unknown-function\\(1 / 2\\)"
)
add_test(NAME scoped_css_run COMMAND lang run tests/scoped_css.as)
set_tests_properties(scoped_css_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "\\.card\\[data-aster-scope-[0-9a-f]+\\].*@keyframes reveal.*from \\{ opacity: 0; \\}.*<aside class=\"card\"><style>\\.card \\{ color: aster; \\}</style>Plain</aside>"
)
add_test(NAME native_html_text_run
    COMMAND lang run tests/native_html_text.as)
set_tests_properties(native_html_text_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "<h1>A&amp;B</h1><p>Benchmark content &amp; escaping.</p><p>Hello <strong>Aster</strong> world.</p><span>Shown</span><code>if \\(condition\\) \\{ \\.\\.\\. \\}</code>")
add_test(NAME invalid_native_css_diagnostic
    COMMAND lang check tests/checker/invalid_native_css.as)
set_tests_properties(invalid_native_css_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME html_child_collections_run
    COMMAND lang run examples/html_collections.as)
set_tests_properties(html_child_collections_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "<section><strong>Optional</strong><h2>Array A</h2><h2>Array B</h2><ul><li>Ada</li><li>Lin</li></ul></section>"
)
add_test(NAME expired_arena_pointer_trap
    COMMAND lang run tests/vm/expired_arena_pointer.as)
set_tests_properties(expired_arena_pointer_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME unregistered_native_trap
    COMMAND lang run tests/vm/unregistered_native.as)
set_tests_properties(unregistered_native_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME native_failure_cleanup
    COMMAND lang run tests/vm/native_failure_cleanup.as)
set_tests_properties(native_failure_cleanup PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME module_cycle_diagnostic
    COMMAND lang check tests/modules/cycle_a.as)
set_tests_properties(module_cycle_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)

add_executable(ffi_registration_test tests/ffi_registration.c)
target_link_libraries(ffi_registration_test PRIVATE langlib)
lang_enable_warnings(ffi_registration_test)
add_test(NAME ffi_registration COMMAND ffi_registration_test)

add_executable(ast_shape_test tests/ast_shape.c)
target_include_directories(ast_shape_test PRIVATE src)
target_link_libraries(ast_shape_test PRIVATE langlib)
lang_enable_warnings(ast_shape_test)
add_test(NAME element_control_ast_shape COMMAND ast_shape_test)
set_tests_properties(element_control_ast_shape PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(css_ast_test tests/css_ast.c)
target_include_directories(css_ast_test PRIVATE src)
target_link_libraries(css_ast_test PRIVATE langlib)
lang_enable_warnings(css_ast_test)
add_test(NAME native_css_ast_shape COMMAND css_ast_test)
set_tests_properties(native_css_ast_shape PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(lexer_unit_test tests/lexer_test.c)
target_include_directories(lexer_unit_test PRIVATE src)
target_link_libraries(lexer_unit_test PRIVATE langlib)
lang_enable_warnings(lexer_unit_test)
add_test(NAME lexer_tokens_comments_and_spans COMMAND lexer_unit_test)

add_executable(class_reference_test tests/class_reference_test.c)
target_include_directories(class_reference_test PRIVATE src)
target_link_libraries(class_reference_test PRIVATE langlib)
lang_enable_warnings(class_reference_test)
add_test(NAME class_declaration_reference_layout COMMAND class_reference_test)
add_test(NAME class_lifetime_vm
    COMMAND lang run-ir tests/class_lifetime.as)
set_tests_properties(class_lifetime_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "41[\r\n]+destroyed")
add_aster_generated_c_test(
    class_lifetime_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/class_lifetime.as
    aster_class_lifetime
        "-DEXPECTED_OUTPUT=41\ndestroyed\n")
add_test(NAME class_properties_vm
    COMMAND lang run-ir tests/class_properties.as)
set_tests_properties(class_properties_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "5[\r\n]+8[\r\n]+16[\r\n]+2[\r\n]+Ada[\r\n]+42[\r\n]+9[\r\n]+4")
add_aster_generated_c_test(
    class_properties_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/class_properties.as
    aster_class_properties
        "-DEXPECTED_OUTPUT=5\n8\n16\n2\nAda\n42\n9\n4\n")
add_test(NAME static_class_members_vm
    COMMAND lang run-ir tests/static_class_members.as)
set_tests_properties(static_class_members_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "7[\r\n]+9[\r\n]+3[\r\n]+14[\r\n]+2")
add_aster_generated_c_test(
    static_class_members_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/static_class_members.as
    aster_static_class_members
        "-DEXPECTED_OUTPUT=7\n9\n3\n14\n2\n")
add_test(NAME class_inheritance_vm
    COMMAND lang run-ir tests/class_inheritance.as)
set_tests_properties(class_inheritance_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "7[\r\n]+4[\r\n]+5[\r\n]+7[\r\n]+200[\r\n]+100[\r\n]+2[\r\n]+2[\r\n]+1[\r\n]+100")
add_aster_generated_c_test(
    class_inheritance_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/class_inheritance.as
    aster_class_inheritance
        "-DEXPECTED_OUTPUT=7\n4\n5\n7\n200\n100\n2\n2\n1\n100\n")
foreach(inheritance_rejection
        missing_abstract_override
        override_nonvirtual
        override_sealed_method
        derive_sealed_class
        instantiate_abstract_class)
    add_test(NAME ${inheritance_rejection}_rejected
        COMMAND lang check tests/checker/${inheritance_rejection}.as)
    set_tests_properties(${inheritance_rejection}_rejected PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        WILL_FAIL TRUE)
endforeach()
add_test(NAME interfaces_vm
    COMMAND lang run-ir tests/interfaces.as)
set_tests_properties(interfaces_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "7[\r\n]+8[\r\n]+8[\r\n]+6[\r\n]+7[\r\n]+7[\r\n]+9[\r\n]+200[\r\n]+100")
add_aster_generated_c_test(
    interfaces_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/interfaces.as
    aster_interfaces
        "-DEXPECTED_OUTPUT=7\n8\n8\n6\n7\n7\n9\n200\n100\n")
foreach(interface_rejection
        interface_missing_member
        interface_private_implementation
        interface_inherits_class
        interface_cycle
        interface_multiple_base_classes
        instantiate_interface
        interface_field)
    add_test(NAME ${interface_rejection}_rejected
        COMMAND lang check tests/checker/${interface_rejection}.as)
    set_tests_properties(${interface_rejection}_rejected PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        WILL_FAIL TRUE)
endforeach()
add_test(NAME private_static_field_rejected
    COMMAND lang check tests/checker/private_static_field_rejected.as)
set_tests_properties(private_static_field_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME private_static_property_setter_rejected
    COMMAND lang check tests/checker/private_static_property_setter.as)
set_tests_properties(private_static_property_setter_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME static_owned_field_rejected
    COMMAND lang check tests/checker/static_owned_field_rejected.as)
set_tests_properties(static_owned_field_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME private_property_setter_rejected
    COMMAND lang check tests/checker/private_property_setter.as)
set_tests_properties(private_property_setter_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME read_only_property_assignment_rejected
    COMMAND lang check tests/checker/read_only_property_assignment.as)
set_tests_properties(read_only_property_assignment_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME bound_method_delegates_vm
    COMMAND lang run-ir tests/bound_method_delegates.as)
set_tests_properties(bound_method_delegates_vm PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "12[\r\n]+15[\r\n]+23[\r\n]+6")
add_aster_generated_c_test(
    bound_method_delegates_generated_c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/bound_method_delegates.as
    aster_bound_method_delegates
        "-DEXPECTED_OUTPUT=12\n15\n23\n6\n")
add_test(NAME bound_struct_method_rejected
    COMMAND lang check tests/checker/bound_struct_method_rejected.as)
set_tests_properties(bound_struct_method_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME private_bound_method_rejected
    COMMAND lang check tests/checker/private_bound_method_rejected.as)
set_tests_properties(private_bound_method_rejected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME bound_method_requires_delegate
    COMMAND lang check tests/checker/bound_method_requires_delegate.as)
set_tests_properties(bound_method_requires_delegate PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_test(NAME bound_null_method_vm_trap
    COMMAND lang run-ir tests/bound_null_method.as)
set_tests_properties(bound_null_method_vm_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE)
add_aster_generated_c_test(
    bound_null_method_generated_c_trap
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/bound_null_method.as
    aster_bound_null_method
        -DEXPECTED_RUN_STATUS=1
        "-DEXPECTED_ERROR=cannot.bind.an.instance.method.to.a.null.class.reference")

add_executable(parser_recovery_test tests/parser_recovery.c)
target_include_directories(parser_recovery_test PRIVATE src)
target_link_libraries(parser_recovery_test PRIVATE langlib)
lang_enable_warnings(parser_recovery_test)
add_test(NAME parser_reports_multiple_errors COMMAND parser_recovery_test)
set_tests_properties(parser_reports_multiple_errors PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_executable(parser_collections_test tests/parser_collections.c)
target_include_directories(parser_collections_test PRIVATE src)
target_link_libraries(parser_collections_test PRIVATE langlib)
lang_enable_warnings(parser_collections_test)
add_test(NAME parser_geometric_collection_stress
    COMMAND parser_collections_test)
add_executable(parser_type_syntax_test tests/parser_type_syntax.c)
target_include_directories(parser_type_syntax_test PRIVATE src)
target_link_libraries(parser_type_syntax_test PRIVATE langlib)
lang_enable_warnings(parser_type_syntax_test)
add_test(NAME parser_structural_type_syntax
    COMMAND parser_type_syntax_test)
add_test(NAME enum_multiple_payloads_diagnostic
    COMMAND lang check tests/parser/enum_multiple_payloads.as)
set_tests_properties(enum_multiple_payloads_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME explicit_function_return_and_struct_shorthand
    COMMAND lang run tests/parser/function_tail_expression.as)
set_tests_properties(explicit_function_return_and_struct_shorthand PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "42"
)
add_test(NAME control_flow_expressions_run
    COMMAND lang run tests/control_flow_expressions.as)
set_tests_properties(control_flow_expressions_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "10[\r\n]+20[\r\n]+40[\r\n]+7[\r\n]+0"
)
add_test(NAME borrowed_iteration_run
    COMMAND lang run tests/borrowed_iteration.as)
set_tests_properties(borrowed_iteration_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "8[\r\n]+2"
)
add_test(NAME borrowed_function_values_run
    COMMAND lang run tests/borrowed_function_values.as)
set_tests_properties(borrowed_function_values_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "3[\r\n]+6[\r\n]+lime"
)
add_test(NAME buffer_slice_runtime_run
    COMMAND lang run tests/c_backend_buffer_slice.as)
set_tests_properties(buffer_slice_runtime_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "17[\r\n]+0")

add_test(NAME ignored_binding_drops_immediately
    COMMAND lang run tests/immediate_discard_cleanup.as)
set_tests_properties(ignored_binding_drops_immediately PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "7[\r\n]+8"
)
add_test(NAME compound_assignment_and_based_integers
    COMMAND lang run tests/parser/compound_assignment_and_bases.as)
set_tests_properties(compound_assignment_and_based_integers PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "4"
)
add_test(NAME invalid_based_integer_diagnostic
    COMMAND lang check tests/lexer/invalid_based_integer.as)
set_tests_properties(invalid_based_integer_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME compound_assignment_rejects_complex_place
    COMMAND lang check tests/parser/compound_assignment_place.as)
set_tests_properties(compound_assignment_rejects_complex_place PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME logical_short_circuit_run
    COMMAND lang run examples/logical.as)
set_tests_properties(logical_short_circuit_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "3[\r\n]+4[\r\n]+5[\r\n]+6[\r\n]+7[\r\n]+8"
)
add_test(NAME invalid_logical_operand_diagnostic
    COMMAND lang check tests/checker/invalid_logical_operand.as)
set_tests_properties(invalid_logical_operand_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME bitwise_operations_and_compound_assignment
    COMMAND lang run examples/bitwise.as)
set_tests_properties(bitwise_operations_and_compound_assignment PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "8[\r\n]+15[\r\n]+6[\r\n]+243[\r\n]+5[\r\n]+15[\r\n]+7"
)
add_test(NAME invalid_bitwise_operand_diagnostic
    COMMAND lang check tests/checker/invalid_bitwise_operand.as)
set_tests_properties(invalid_bitwise_operand_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME static_method_call_sugar
    COMMAND lang run examples/method_calls.as)
set_tests_properties(static_method_call_sugar PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "42[\r\n]+2[\r\n]+9[\r\n]+method calls[\r\n]+41[\r\n]+42[\r\n]+42"
)
add_test(NAME inferred_self_requires_qualified_owner
    COMMAND lang check tests/parser/inferred_self_requires_owner.as)
set_tests_properties(inferred_self_requires_qualified_owner PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME inferred_self_must_be_first
    COMMAND lang check tests/parser/inferred_self_must_be_first.as)
set_tests_properties(inferred_self_must_be_first PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME integer_range_loop
    COMMAND lang run examples/ranges.as)
set_tests_properties(integer_range_loop PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION
        "100[\r\n]+200[\r\n]+11[\r\n]+6[\r\n]+254"
)
add_test(NAME range_rejects_float_bounds
    COMMAND lang check tests/checker/invalid_float_range.as)
set_tests_properties(range_rejects_float_bounds PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME range_rejects_inclusive_syntax
    COMMAND lang check tests/parser/inclusive_range.as)
set_tests_properties(range_rejects_inclusive_syntax PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)

add_executable(typed_ast_resolution_test tests/typed_ast_test.c)
target_include_directories(typed_ast_resolution_test PRIVATE src)
target_link_libraries(typed_ast_resolution_test PRIVATE langlib)
lang_enable_warnings(typed_ast_resolution_test)
add_test(NAME typed_ast_retains_resolved_function
    COMMAND typed_ast_resolution_test)
set_tests_properties(typed_ast_retains_resolved_function PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_executable(generic_identity_test tests/generic_identity_test.c)
target_include_directories(generic_identity_test PRIVATE src)
target_link_libraries(generic_identity_test PRIVATE langlib)
lang_enable_warnings(generic_identity_test)
add_test(NAME generic_instantiation_pointer_identity
    COMMAND generic_identity_test)
set_tests_properties(generic_instantiation_pointer_identity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_executable(ir_structure_test tests/ir_test.c)
target_include_directories(ir_structure_test PRIVATE src)
target_link_libraries(ir_structure_test PRIVATE langlib)
lang_enable_warnings(ir_structure_test)
add_test(NAME typed_ir_structure_and_verifier COMMAND ir_structure_test)
set_tests_properties(typed_ir_structure_and_verifier PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME typed_ast_local_shadowing_run
    COMMAND lang run tests/typed_ast_test.as)
set_tests_properties(typed_ast_local_shadowing_run PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "2[\r\n]+42"
)

add_executable(http_integration_test tests/http_integration.c)
target_link_libraries(http_integration_test PRIVATE langlib)
lang_enable_warnings(http_integration_test)
add_test(NAME http_fixed_response COMMAND http_integration_test)

add_executable(http_request_integration_test tests/http_request_integration.c)
target_link_libraries(http_request_integration_test PRIVATE langlib)
lang_enable_warnings(http_request_integration_test)
add_test(NAME http_request_exposure COMMAND http_request_integration_test)

add_executable(http_keep_alive_integration_test
    tests/http_keep_alive_integration.c)
target_link_libraries(http_keep_alive_integration_test PRIVATE langlib)
lang_enable_warnings(http_keep_alive_integration_test)
add_test(NAME http_keep_alive_reuse
    COMMAND http_keep_alive_integration_test)

add_executable(docs_server_integration_test
    tests/docs_server_integration.c)
target_link_libraries(docs_server_integration_test PRIVATE langlib)
lang_enable_warnings(docs_server_integration_test)
add_test(NAME docs_server_http_application
    COMMAND docs_server_integration_test)
set_tests_properties(docs_server_http_application PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(http_language_integration_test
    tests/http_language_integration.c)
target_link_libraries(http_language_integration_test PRIVATE langlib)
lang_enable_warnings(http_language_integration_test)
add_test(NAME http_language_handler_loop
    COMMAND http_language_integration_test)
set_tests_properties(http_language_handler_loop PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    TIMEOUT 10
)
add_test(NAME ir_http_language_handler_loop
    COMMAND http_language_integration_test ir)
set_tests_properties(ir_http_language_handler_loop PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    TIMEOUT 10
)

add_executable(generated_http_integration_test
    tests/generated_http_integration.c)
target_link_libraries(generated_http_integration_test PRIVATE langlib)
lang_enable_warnings(generated_http_integration_test)

add_executable(lime_http_integration_test
    tests/lime_http_integration.c)
target_link_libraries(lime_http_integration_test PRIVATE langlib)
lang_enable_warnings(lime_http_integration_test)
add_test(NAME lime_current_http_vm_integration
    COMMAND lime_http_integration_test vm)
set_tests_properties(lime_current_http_vm_integration PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    TIMEOUT 10
)
add_aster_generated_project_test(
    lime_current_http_c_integration
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/lime/aster.toml
    current_http_server
        -DDRIVER_EXECUTABLE=$<TARGET_FILE:lime_http_integration_test>)
set_tests_properties(lime_current_http_c_integration PROPERTIES
    TIMEOUT 120
)
add_executable(lime_async_http_integration_test
    tests/lime_async_http_integration.c)
target_link_libraries(lime_async_http_integration_test PRIVATE langlib)
lang_enable_warnings(lime_async_http_integration_test)
add_test(NAME lime_async_current_http_vm_integration
    COMMAND lime_async_http_integration_test vm)
set_tests_properties(lime_async_current_http_vm_integration PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    TIMEOUT 10
)
add_aster_generated_project_test(
    lime_async_current_http_c_integration
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/lime/aster.toml
    async_http_server
        -DDRIVER_EXECUTABLE=$<TARGET_FILE:lime_async_http_integration_test>)
set_tests_properties(lime_async_current_http_c_integration PROPERTIES
    TIMEOUT 120
)
if(LANG_HAVE_H2O AND UNIX)
    add_executable(lime_h2o_integration_test
        tests/lime_h2o_integration.c)
    target_link_libraries(lime_h2o_integration_test PRIVATE langlib)
    lang_enable_warnings(lime_h2o_integration_test)
    add_test(NAME lime_h2o_vm_integration
        COMMAND lime_h2o_integration_test vm)
    set_tests_properties(lime_h2o_vm_integration PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        TIMEOUT 15
        ENVIRONMENT "LD_LIBRARY_PATH=${ASTER_H2O_BUILD_ROOT}"
    )
    add_aster_generated_project_test(
    lime_h2o_c_integration
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/lime/aster.toml
    h2o_http_server
            -DDRIVER_EXECUTABLE=$<TARGET_FILE:lime_h2o_integration_test>
            "-DEXTRA_LIBRARIES=${SQLite3_LIBRARIES}\;${ASTER_H2O_LIBRARY}")
    set_tests_properties(lime_h2o_c_integration PROPERTIES
        TIMEOUT 15
        ENVIRONMENT "LD_LIBRARY_PATH=${ASTER_H2O_BUILD_ROOT}"
    )
endif()
if(ASTER_ENABLE_SQLITE_TESTS AND SQLite3_FOUND)
    add_test(NAME issue_tracker_vm_http_integration
        COMMAND generated_http_integration_test vm)
    set_tests_properties(issue_tracker_vm_http_integration PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        TIMEOUT 10
        RESOURCE_LOCK issue_tracker_integration_database
    )
endif()

add_executable(trap_destructor_test tests/trap_destructor.c)
target_link_libraries(trap_destructor_test PRIVATE langlib)
lang_enable_warnings(trap_destructor_test)
add_test(NAME trap_language_destructor COMMAND trap_destructor_test)
set_tests_properties(trap_language_destructor PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(stack_trace_test tests/stack_trace.c)
target_link_libraries(stack_trace_test PRIVATE langlib)
lang_enable_warnings(stack_trace_test)
add_test(NAME runtime_stack_trace_locations COMMAND stack_trace_test)
set_tests_properties(runtime_stack_trace_locations PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(malformed_bytecode_test tests/malformed_bytecode.c)
target_include_directories(malformed_bytecode_test PRIVATE src)
target_link_libraries(malformed_bytecode_test PRIVATE langlib)
lang_enable_warnings(malformed_bytecode_test)
add_test(NAME malformed_bytecode_rejected COMMAND malformed_bytecode_test)
if(UNIX AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_executable(vm_allocation_failure_test
        tests/vm_allocation_failure.c)
    target_link_libraries(vm_allocation_failure_test PRIVATE langlib)
    target_link_options(vm_allocation_failure_test PRIVATE
        -Wl,--wrap=calloc)
    lang_enable_warnings(vm_allocation_failure_test)
    add_test(NAME vm_allocation_failure_returns_null
        COMMAND vm_allocation_failure_test)
endif()

add_test(NAME http_language_router_check
    COMMAND lang check examples/http_server.as)
set_tests_properties(http_language_router_check PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME division_by_zero_trap COMMAND lang run tests/vm/division_by_zero.as)
set_tests_properties(division_by_zero_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME index_out_of_bounds_trap
    COMMAND lang run tests/vm/index_out_of_bounds.as)
set_tests_properties(index_out_of_bounds_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME constant_index_out_of_bounds_diagnostic
    COMMAND lang check tests/checker/constant_index_out_of_bounds.as)
set_tests_properties(constant_index_out_of_bounds_diagnostic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME index_assignment_out_of_bounds_trap
    COMMAND lang run tests/vm/index_assignment_out_of_bounds.as)
set_tests_properties(index_assignment_out_of_bounds_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME null_pointer_load_trap
    COMMAND lang run tests/vm/null_pointer_load.as)
set_tests_properties(null_pointer_load_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME i8_overflow_trap
    COMMAND lang run tests/vm/i8_overflow.as)
set_tests_properties(i8_overflow_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME u8_underflow_trap
    COMMAND lang run tests/vm/u8_underflow.as)
set_tests_properties(u8_underflow_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME invalid_shift_count_trap
    COMMAND lang run tests/vm/shift_count.as)
set_tests_properties(invalid_shift_count_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME negative_shift_count_trap
    COMMAND lang run tests/vm/negative_shift_count.as)
set_tests_properties(negative_shift_count_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME shift_overflow_trap
    COMMAND lang run tests/vm/shift_overflow.as)
set_tests_properties(shift_overflow_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME cast_out_of_range_trap
    COMMAND lang run tests/vm/cast_out_of_range.as)
set_tests_properties(cast_out_of_range_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME explicit_panic_trap COMMAND lang run tests/vm/panic.as)
set_tests_properties(explicit_panic_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)
add_test(NAME recursion_depth_trap
    COMMAND lang run tests/vm/recursion_depth.as)
set_tests_properties(recursion_depth_trap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    WILL_FAIL TRUE
)

add_custom_target(bench
    COMMAND lang bench
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS lang
)
