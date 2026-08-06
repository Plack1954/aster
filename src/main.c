#include "lang/lang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(FILE *stream) {
    fputs(
        "usage: lang <command> [file]\n"
        "commands:\n"
        "  run file.as [-- args...] execute verified typed IR\n"
        "  run-ir file.as [-- args...] explicit typed-IR alias\n"
        "  check file.as        parse and type-check\n"
        "  dump-tokens file.as  print lexer output\n"
        "  dump-ast file.as     print parsed syntax\n"
        "  dump-types file.as   print inferred types\n"
        "  dump-layout file.as  print target type layouts\n"
        "  dump-ir file.as      print typed control-flow IR\n"
        "  dump-ir-bytecode file.as disassemble IR-lowered bytecode\n"
        "  emit-c file.as       emit portable C17 from typed IR\n"
        "  emit-c-site file.as ASSET_DIR\n"
        "                         emit C17 plus one hashed external stylesheet\n"
        "  emit-c-runtime         emit the reusable C17 runtime\n"
        "  dump-bytecode file.as disassemble IR-lowered bytecode\n"
        "  repl                   interactive expression runner\n"
        "  test                   run the integration suite\n"
        "  bench                  run a small front-end benchmark\n"
        "  project run MANIFEST [TARGET]   run a project target\n"
        "  project run-ir MANIFEST [TARGET] explicit typed-IR alias\n"
        "  project check MANIFEST [TARGET] check a project target\n"
        "  project emit-c MANIFEST [TARGET] emit a target as C17\n"
        "  project emit-c-site MANIFEST ASSET_DIR [TARGET]\n"
        "                                  emit C17 plus external CSS\n"
        "  project build-web MANIFEST OUTPUT_DIR [TARGET]\n"
        "                                  build server C and browser Wasm\n"
        "  project build-site MANIFEST OUTPUT_DIR [TARGET]\n"
        "                                  materialize a static site\n"
        "  project test MANIFEST           run project test targets\n",
        stream);
}

static int run_tests(void) {
    static const char *positive[] = {
        "examples/hello.as", "examples/arithmetic.as", "examples/structs.as",
        "examples/arrays.as", "examples/html.as"
        , "examples/html_control.as", "examples/recursion.as",
        "examples/result.as", "examples/arena.as", "examples/ffi.as",
        "examples/raii.as", "examples/components.as"
        , "examples/loop_cleanup.as", "examples/match.as",
        "examples/html_match.as", "examples/raw_pointer.as"
        , "examples/component_children.as", "examples/borrowed_native.as",
        "examples/borrowed_component_html.as",
        "examples/foreach.as",
        "tests/foreach_resource_copy.as",
        "examples/aggregate_cleanup.as",
        "examples/destructors.as",
        "examples/destructor_mutation.as",
        "examples/nested_destructors.as",
        "examples/numeric_types.as",
        "examples/option.as",
        "examples/type_alias.as"
        , "examples/aggregate_mutation.as"
        , "examples/native_handle_field.as"
        , "examples/null_pointer.as"
        , "examples/nested_arrays.as"
        , "examples/string_escapes.as"
        , "examples/shifts.as"
        , "examples/html_escaping.as"
        , "examples/copyable_result.as"
        , "examples/temporary_values.as"
        , "examples/generic_types.as"
        , "tests/reference_counted_string.as"
        , "tests/checker/list_get_value_copy.as"
        , "tests/custom_copy_sites.as"
        , "tests/custom_copy_raw_allocation.as"
        , "tests/custom_copy_site_cleanup.as"
        , "tests/checker/copy_owning_result.as"
        , "tests/checker/generic_value_copy.as"
        , "tests/checker/buffer_value_copy.as"
        , "tests/checker/native_handle_value_copy.as"
        , "tests/checker/conditional_value_copy.as"
        , "tests/checker/loop_value_copy.as"
        , "tests/checker/native_handle_struct_copy.as"
        , "tests/checker/immutable_string_builder.as"
        , "tests/checker/string_builder_finish_without_move.as"
        , "tests/checker/immutable_vec.as"
        , "tests/checker/immutable_field_assignment.as"
        , "tests/checker/immutable_index_assignment.as"
        , "tests/html_child_value_copy.as"
        , "tests/custom_copy_constructor.as"
        , "tests/custom_copy_cleanup.as"
        , "tests/nested_custom_copy.as"
        , "tests/nested_custom_copy_cleanup.as"
        , "tests/array_custom_copy.as"
        , "tests/generic_wrapper_custom_copy.as"
        , "tests/tagged_union_custom_copy.as"
        , "tests/tagged_union_custom_copy_cleanup.as"
        , "tests/list_custom_copy.as"
        , "tests/list_custom_copy_cleanup.as"
        , "tests/sequential_collection_custom_copy.as"
        , "tests/associative_collection_custom_copy.as"
    };
    static const char *negative[] = {
        "tests/parser/mismatched_element.as",
        "tests/parser/recovery.as",
        "tests/checker/missing_property.as"
        , "tests/checker/noncopyable_arena.as"
        , "tests/checker/noncopyable_struct.as"
        , "tests/checker/generic_noncopyable.as"
        , "tests/checker/deleted_copy_constructor.as"
        , "tests/checker/custom_copy_mutates_source.as"
        , "tests/checker/nested_deleted_copy.as"
        , "tests/checker/option_custom_copy.as"
        , "tests/checker/list_deleted_copy.as"
        , "tests/checker/projection_deleted_copy.as"
        , "tests/checker/component_missing_property.as"
        , "tests/checker/non_exhaustive_match.as"
        , "tests/checker/payloadless_enum_call.as"
        , "tests/checker/switch_binding_type.as"
        , "tests/checker/raw_without_unsafe.as"
        , "tests/checker/invalid_component_children.as"
        , "tests/checker/missing_return_path.as"
        , "tests/checker/invalid_struct_construction.as"
        , "tests/checker/invalid_enum_payload.as"
        , "tests/checker/explicit_destructor_call.as"
        , "tests/checker/numeric_mismatch.as"
        , "tests/checker/integer_literal_out_of_range.as"
        , "tests/checker/invalid_cast.as"
        , "tests/checker/pointer_element_mismatch.as"
        , "tests/checker/cyclic_type_alias.as"
        , "tests/checker/vec_wrong_element.as"
        , "tests/modules/visibility_main.as"
        , "tests/modules/ambiguous_main.as"
        , "tests/modules/type_mismatch_main.as"
        , "tests/checker/custom_element_property.as"
        , "tests/checker/immutable_arena.as"
        , "tests/checker/store_through_const_pointer.as"
        , "tests/checker/untyped_null.as"
        , "tests/checker/ordered_pointer_comparison.as"
        , "tests/lexer/invalid_escape.as"
        , "tests/lexer/unterminated_string.as"
        , "tests/checker/unknown_element_property.as"
        , "tests/checker/duplicate_element_property.as"
        , "tests/checker/constant_index_out_of_bounds.as"
        , "tests/checker/element_disallows_children.as"
        , "tests/checker/unknown_element.as"
        , "tests/modules/nested_type_isolation_main.as"
        , "tests/modules/imported_destructor_copy.as"
        , "tests/checker/invalid_extern_layout.as"
        , "tests/checker/generic_arity.as"
        , "tests/checker/generic_type_mismatch.as"
        , "tests/checker/recursive_generic_inline.as"
        , "tests/checker/recursive_generic_expansion.as"
    };
    int failures = 0;
    for (size_t i = 0U; i < sizeof(positive) / sizeof(positive[0]); ++i) {
        int status = lang_run_file(positive[i], true, NULL);
        printf("[%s] %s\n", status == 0 ? "pass" : "FAIL", positive[i]);
        if (status != 0) ++failures;
    }
    for (size_t i = 0U; i < sizeof(negative) / sizeof(negative[0]); ++i) {
        int status = lang_run_file(negative[i], true, NULL);
        printf("[%s] %s (expected diagnostic)\n", status != 0 ? "pass" : "FAIL", negative[i]);
        if (status == 0) ++failures;
    }
    printf("%zu tests, %d failures\n",
           sizeof(positive) / sizeof(positive[0]) +
           sizeof(negative) / sizeof(negative[0]), failures);
    return failures == 0 ? 0 : 1;
}

static int repl(void) {
    fputs("Aster 0.1 REPL (expressions; enter `quit` to exit)\n", stdout);
    char line[1024];
    while (fputs("> ", stdout), fflush(stdout), fgets(line, sizeof(line), stdin) != NULL) {
        if (strcmp(line, "quit\n") == 0 || strcmp(line, "quit") == 0) break;
        size_t length = strlen(line);
        while (length != 0U && (line[length - 1U] == '\n' ||
               line[length - 1U] == '\r' || line[length - 1U] == ';'))
            line[--length] = '\0';
        const char prefix[] = "int main() { Console.WriteLine(";
        const char suffix[] = "); return 0; }";
        size_t source_length = sizeof(prefix) - 1U + length + sizeof(suffix);
        char *source = malloc(source_length);
        if (source == NULL) { fputs("fatal: out of memory\n", stderr); return 2; }
        (void)snprintf(source, source_length, "%s%s%s", prefix, line, suffix);
        (void)lang_run_text("<repl>", source, false, NULL);
        free(source);
    }
    return 0;
}

static int benchmark(void) {
    int status = lang_benchmark_file("examples/benchmark.as", 100U);
    status |= lang_benchmark_file("examples/benchmark_html.as", 100U);
    return status;
}

int main(int argc, char **argv) {
    if (argc != 0) lang_set_executable_path(argv[0]);
    if (argc < 2) { usage(stderr); return 2; }
    if (strcmp(argv[1], "test") == 0) return run_tests();
    if (strcmp(argv[1], "repl") == 0) return repl();
    if (strcmp(argv[1], "bench") == 0) return benchmark();
    if (argc == 2 && strcmp(argv[1], "emit-c-runtime") == 0)
        return lang_c_emit_runtime(stdout) ? 0 : 1;
    if (strcmp(argv[1], "project") == 0) {
        if (argc >= 5 && argc <= 6 &&
            strcmp(argv[2], "build-web") == 0)
            return lang_project_build_web(
                argv[3], argv[4], argc == 6 ? argv[5] : NULL);
        if (argc >= 5 && argc <= 6 &&
            strcmp(argv[2], "build-site") == 0)
            return lang_project_build_site(
                argv[3], argv[4], argc == 6 ? argv[5] : NULL);
        if (argc >= 5 && argc <= 6 &&
            strcmp(argv[2], "emit-c-site") == 0)
            return lang_project_emit_c_site(
                argv[3], argc == 6 ? argv[5] : NULL, argv[4]);
        if (argc >= 4 && argc <= 5 &&
            (strcmp(argv[2], "run") == 0 ||
             strcmp(argv[2], "run-ir") == 0 ||
             strcmp(argv[2], "emit-c") == 0 ||
             strcmp(argv[2], "check") == 0)) {
            if (strcmp(argv[2], "run-ir") == 0)
                return lang_project_run_ir(
                    argv[3], argc == 5 ? argv[4] : NULL);
            if (strcmp(argv[2], "emit-c") == 0)
                return lang_project_emit_c(
                    argv[3], argc == 5 ? argv[4] : NULL);
            return lang_project_run(
                argv[3], argc == 5 ? argv[4] : NULL,
                strcmp(argv[2], "check") == 0);
        }
        if (argc == 4 && strcmp(argv[2], "test") == 0)
            return lang_project_test(argv[3]);
        usage(stderr);
        return 2;
    }
    if (argc >= 3 &&
        (strcmp(argv[1], "run") == 0 ||
         strcmp(argv[1], "run-ir") == 0)) {
        int argument_start = 3;
        if (argument_start < argc &&
            strcmp(argv[argument_start], "--") == 0)
            ++argument_start;
        const char *backend =
            strcmp(argv[1], "run-ir") == 0 ? "run-ir" : NULL;
        return lang_run_file_args(
            argv[2], false, backend,
            (size_t)(argc - argument_start),
            (const char *const *)&argv[argument_start]);
    }
    if (argc == 4 && strcmp(argv[1], "emit-c-site") == 0)
        return lang_emit_c_site_file(argv[2], argv[3]);
    if (argc != 3) { usage(stderr); return 2; }
    if (strcmp(argv[1], "check") == 0) return lang_run_file(argv[2], true, NULL);
    if (strcmp(argv[1], "dump-tokens") == 0) return lang_run_file(argv[2], true, "tokens");
    if (strcmp(argv[1], "dump-ast") == 0) return lang_run_file(argv[2], true, "ast");
    if (strcmp(argv[1], "dump-types") == 0) return lang_run_file(argv[2], true, "types");
    if (strcmp(argv[1], "dump-layout") == 0) return lang_run_file(argv[2], true, "layout");
    if (strcmp(argv[1], "dump-ir") == 0) return lang_run_file(argv[2], true, "ir");
    if (strcmp(argv[1], "dump-ir-bytecode") == 0)
        return lang_run_file(argv[2], true, "ir-bytecode");
    if (strcmp(argv[1], "emit-c") == 0)
        return lang_run_file(argv[2], true, "c");
    if (strcmp(argv[1], "dump-bytecode") == 0) return lang_run_file(argv[2], true, "bytecode");
    usage(stderr);
    return 2;
}
