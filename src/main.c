#include "lang/lang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(ASTER_PUBLIC_CLI)
#include <sys/stat.h>
#endif

#if !defined(ASTER_VERSION)
#define ASTER_VERSION "0.1.0"
#endif

#if defined(ASTER_PUBLIC_CLI)
static void aster_driver_usage(FILE *stream) {
    fputs(
        "Usage:\n"
        "  aster [options]\n"
        "  aster [command] [options]\n\n"
        "Options:\n"
        "  --info          Display Aster information.\n"
        "  --version       Display the Aster version.\n"
        "  -h, --help      Show command line help.\n\n"
        "Commands:\n"
        "  run             Run source code without explicit compile commands.\n"
        "  test            Run project tests.\n"
        "  help            Show command line help.\n",
        stream);
}

static void aster_run_usage(FILE *stream) {
    fputs(
        "Description:\n"
        "  Runs source code without explicit compile commands.\n\n"
        "Usage:\n"
        "  aster run [options] [[--] <applicationArguments>...]\n\n"
        "Options:\n"
        "  --project <PATH>  The path to the project file or project directory.\n"
        "  -h, --help        Show command line help.\n",
        stream);
}

static void aster_test_usage(FILE *stream) {
    fputs(
        "Description:\n"
        "  Runs project tests.\n\n"
        "Usage:\n"
        "  aster test [<PROJECT>] [options]\n\n"
        "Arguments:\n"
        "  <PROJECT>       The project file or project directory to test.\n\n"
        "Options:\n"
        "  -h, --help      Show command line help.\n",
        stream);
}

static char *aster_manifest_path(const char *project_path) {
    const char *path = project_path != NULL ? project_path : ".";
    struct stat metadata;
    bool directory = stat(path, &metadata) == 0 && S_ISDIR(metadata.st_mode);
    if (!directory) {
        size_t length = strlen(path) + 1U;
        char *copy = malloc(length);
        if (copy != NULL) memcpy(copy, path, length);
        return copy;
    }
    size_t length = strlen(path);
    bool separator = length != 0U &&
        (path[length - 1U] == '/' || path[length - 1U] == '\\');
    const char manifest[] = "aster.toml";
    char *result = malloc(length + (separator ? 0U : 1U) + sizeof(manifest));
    if (result == NULL) return NULL;
    memcpy(result, path, length);
    size_t output = length;
    if (!separator) result[output++] = '/';
    memcpy(result + output, manifest, sizeof(manifest));
    return result;
}

static int aster_run_command(int argc, char **argv) {
    const char *project = NULL;
    int argument_start = argc;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            aster_run_usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--") == 0) {
            argument_start = i + 1;
            break;
        }
        if (strcmp(argv[i], "--project") == 0) {
            if (++i == argc) {
                fputs("Option '--project' expects a single argument.\n", stderr);
                return 1;
            }
            project = argv[i];
            continue;
        }
        const char prefix[] = "--project=";
        if (strncmp(argv[i], prefix, sizeof(prefix) - 1U) == 0) {
            project = argv[i] + sizeof(prefix) - 1U;
            continue;
        }
        argument_start = i;
        break;
    }
    char *manifest = aster_manifest_path(project);
    if (manifest == NULL) {
        fputs("Could not allocate the project path.\n", stderr);
        return 1;
    }
    int status = lang_project_run_args(
        manifest, NULL, (size_t)(argc - argument_start),
        (const char *const *)&argv[argument_start]);
    free(manifest);
    return status;
}

static int aster_test_command(int argc, char **argv) {
    const char *project = NULL;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            aster_test_usage(stdout);
            return 0;
        }
        if (argv[i][0] == '-' || project != NULL) {
            fprintf(stderr, "Unrecognized command or argument '%s'.\n", argv[i]);
            return 1;
        }
        project = argv[i];
    }
    char *manifest = aster_manifest_path(project);
    if (manifest == NULL) {
        fputs("Could not allocate the project path.\n", stderr);
        return 1;
    }
    int status = lang_project_test(manifest);
    free(manifest);
    return status;
}

static int aster_main(int argc, char **argv) {
    if (argc == 1) {
        aster_driver_usage(stdout);
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0 && argc == 2) {
        puts(ASTER_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "--info") == 0 && argc == 2) {
        printf("Aster:\n Version: %s\n\nRuntime Environment:\n OS: %s\n",
               ASTER_VERSION,
#if defined(_WIN32)
               "Windows"
#elif defined(__APPLE__)
               "macOS"
#elif defined(__linux__)
               "Linux"
#else
               "Unknown"
#endif
        );
        return 0;
    }
    if ((strcmp(argv[1], "-h") == 0 ||
         strcmp(argv[1], "--help") == 0) && argc == 2) {
        aster_driver_usage(stdout);
        return 0;
    }
    if (strcmp(argv[1], "help") == 0) {
        if (argc == 2) {
            aster_driver_usage(stdout);
            return 0;
        }
        if (argc == 3 && strcmp(argv[2], "run") == 0) {
            aster_run_usage(stdout);
            return 0;
        }
        if (argc == 3 && strcmp(argv[2], "test") == 0) {
            aster_test_usage(stdout);
            return 0;
        }
    }
    if (strcmp(argv[1], "run") == 0) return aster_run_command(argc, argv);
    if (strcmp(argv[1], "test") == 0) return aster_test_command(argc, argv);
    fprintf(stderr, "The command could not be loaded, possibly because:\n"
                    "  * You intended to execute an Aster program, but "
                    "aster-%s does not exist.\n",
                    argv[1]);
    return 1;
}
#endif

#if !defined(ASTER_PUBLIC_CLI)
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
#endif

int main(int argc, char **argv) {
    lang_configure_http_client_registrar(lang_register_http_client_natives);
    lang_configure_crypto_registrar(lang_register_crypto_natives);
    if (argc != 0) lang_set_executable_path(argv[0]);
#if defined(ASTER_PUBLIC_CLI)
    return aster_main(argc, argv);
#else
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
#endif
}
