#include "lang/lang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(FILE *stream) {
    fputs(
        "usage: lang <command> [file]\n"
        "commands:\n"
        "  run file.lang [-- args...] check, compile, and execute\n"
        "  run-ir file.lang [-- args...] execute through typed IR\n"
        "  run-direct file.lang [-- args...] use legacy bytecode compiler\n"
        "  check file.lang        parse and type-check\n"
        "  dump-tokens file.lang  print lexer output\n"
        "  dump-ast file.lang     print parsed syntax\n"
        "  dump-types file.lang   print inferred types\n"
        "  dump-layout file.lang  print target type layouts\n"
        "  dump-ir file.lang      print typed control-flow IR\n"
        "  dump-ir-bytecode file.lang disassemble IR-lowered bytecode\n"
        "  emit-c file.lang       emit portable C17 from typed IR\n"
        "  emit-c-site file.lang ASSET_DIR\n"
        "                         emit C17 plus one hashed external stylesheet\n"
        "  emit-c-runtime         emit the reusable C17 runtime\n"
        "  dump-bytecode file.lang disassemble bytecode\n"
        "  repl                   interactive expression runner\n"
        "  test                   run the integration suite\n"
        "  bench                  run a small front-end benchmark\n"
        "  project run MANIFEST [TARGET]   run a project target\n"
        "  project run-ir MANIFEST [TARGET] run via typed IR\n"
        "  project run-direct MANIFEST [TARGET] use legacy compiler\n"
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
        "examples/hello.lang", "examples/arithmetic.lang", "examples/structs.lang",
        "examples/arrays.lang", "examples/html.lang"
        , "examples/html_control.lang", "examples/recursion.lang",
        "examples/result.lang", "examples/arena.lang", "examples/ffi.lang",
        "examples/raii.lang", "examples/components.lang"
        , "examples/loop_cleanup.lang", "examples/match.lang",
        "examples/html_match.lang", "examples/raw_pointer.lang"
        , "examples/component_children.lang", "examples/borrowed_native.lang",
        "examples/borrowed_component_html.lang",
        "examples/foreach.lang",
        "tests/foreach_resource_copy.lang",
        "examples/aggregate_cleanup.lang",
        "examples/destructors.lang",
        "examples/destructor_mutation.lang",
        "examples/nested_destructors.lang",
        "examples/numeric_types.lang",
        "examples/option.lang",
        "examples/type_alias.lang"
        , "examples/aggregate_mutation.lang"
        , "examples/native_handle_field.lang"
        , "examples/null_pointer.lang"
        , "examples/nested_arrays.lang"
        , "examples/string_escapes.lang"
        , "examples/shifts.lang"
        , "examples/html_escaping.lang"
        , "examples/copyable_result.lang"
        , "examples/temporary_values.lang"
        , "examples/generic_types.lang"
        , "tests/reference_counted_string.lang"
        , "tests/checker/list_get_value_copy.lang"
        , "tests/checker/copy_owning_result.lang"
        , "tests/checker/generic_value_copy.lang"
        , "tests/checker/buffer_value_copy.lang"
        , "tests/checker/native_handle_value_copy.lang"
        , "tests/checker/conditional_value_copy.lang"
        , "tests/checker/loop_value_copy.lang"
        , "tests/checker/native_handle_struct_copy.lang"
        , "tests/checker/immutable_string_builder.lang"
        , "tests/checker/string_builder_finish_without_move.lang"
        , "tests/checker/immutable_vec.lang"
        , "tests/checker/immutable_field_assignment.lang"
        , "tests/checker/immutable_index_assignment.lang"
        , "tests/html_child_value_copy.lang"
    };
    static const char *negative[] = {
        "tests/parser/mismatched_element.lang",
        "tests/parser/recovery.lang",
        "tests/checker/missing_property.lang"
        , "tests/checker/noncopyable_arena.lang"
        , "tests/checker/noncopyable_struct.lang"
        , "tests/checker/generic_noncopyable.lang"
        , "tests/checker/component_missing_property.lang"
        , "tests/checker/non_exhaustive_match.lang"
        , "tests/checker/payloadless_enum_call.lang"
        , "tests/checker/switch_binding_type.lang"
        , "tests/checker/raw_without_unsafe.lang"
        , "tests/checker/invalid_component_children.lang"
        , "tests/checker/missing_return_path.lang"
        , "tests/checker/invalid_struct_construction.lang"
        , "tests/checker/invalid_enum_payload.lang"
        , "tests/checker/explicit_destructor_call.lang"
        , "tests/checker/numeric_mismatch.lang"
        , "tests/checker/integer_literal_out_of_range.lang"
        , "tests/checker/invalid_cast.lang"
        , "tests/checker/pointer_element_mismatch.lang"
        , "tests/checker/cyclic_type_alias.lang"
        , "tests/checker/vec_wrong_element.lang"
        , "tests/modules/visibility_main.lang"
        , "tests/modules/ambiguous_main.lang"
        , "tests/modules/type_mismatch_main.lang"
        , "tests/checker/custom_element_property.lang"
        , "tests/checker/immutable_arena.lang"
        , "tests/checker/store_through_const_pointer.lang"
        , "tests/checker/untyped_null.lang"
        , "tests/checker/ordered_pointer_comparison.lang"
        , "tests/lexer/invalid_escape.lang"
        , "tests/lexer/unterminated_string.lang"
        , "tests/checker/unknown_element_property.lang"
        , "tests/checker/duplicate_element_property.lang"
        , "tests/checker/constant_index_out_of_bounds.lang"
        , "tests/checker/element_disallows_children.lang"
        , "tests/checker/unknown_element.lang"
        , "tests/modules/nested_type_isolation_main.lang"
        , "tests/modules/imported_destructor_copy.lang"
        , "tests/checker/invalid_extern_layout.lang"
        , "tests/checker/generic_arity.lang"
        , "tests/checker/generic_type_mismatch.lang"
        , "tests/checker/recursive_generic_inline.lang"
        , "tests/checker/recursive_generic_expansion.lang"
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
        const char prefix[] = "int main() { print(";
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
    int status = lang_benchmark_file("examples/benchmark.lang", 100U);
    status |= lang_benchmark_file("examples/benchmark_html.lang", 100U);
    return status;
}

int main(int argc, char **argv) {
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
             strcmp(argv[2], "run-direct") == 0 ||
             strcmp(argv[2], "emit-c") == 0 ||
             strcmp(argv[2], "check") == 0)) {
            if (strcmp(argv[2], "run-ir") == 0)
                return lang_project_run_ir(
                    argv[3], argc == 5 ? argv[4] : NULL);
            if (strcmp(argv[2], "run-direct") == 0)
                return lang_project_run_direct(
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
         strcmp(argv[1], "run-ir") == 0 ||
         strcmp(argv[1], "run-direct") == 0)) {
        int argument_start = 3;
        if (argument_start < argc &&
            strcmp(argv[argument_start], "--") == 0)
            ++argument_start;
        const char *backend =
            strcmp(argv[1], "run-ir") == 0 ? "run-ir" :
            strcmp(argv[1], "run-direct") == 0 ? "run-direct" : NULL;
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
