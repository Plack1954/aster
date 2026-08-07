#include "cli.h"
#include "cli_parse.h"
#include "lang/lang.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if !defined(ASTER_VERSION)
#define ASTER_VERSION "0.1.0"
#endif

#if defined(_WIN32)
#define ASTER_MODE_IS_DIRECTORY(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#else
#define ASTER_MODE_IS_DIRECTORY(mode) S_ISDIR(mode)
#endif

static void write_driver_help(FILE *stream) {
    fputs(
        "Usage:\n"
        "  aster [options]\n"
        "  aster [command] [options]\n\n"
        "Options:\n"
        "  --info          Display Aster information.\n"
        "  --version       Display the Aster version.\n"
        "  -?, -h, --help  Show command line help.\n\n"
        "Commands:\n"
        "  run             Run source code without explicit compile commands.\n"
        "  test            Run project tests.\n"
        "  help            Show command line help.\n",
        stream);
}

static void write_run_help(FILE *stream) {
    fputs(
        "Description:\n"
        "  Aster Run Command\n\n"
        "Usage:\n"
        "  aster run [<applicationArguments>...] [options]\n\n"
        "Arguments:\n"
        "  <applicationArguments>  Arguments passed to the application. []\n\n"
        "Options:\n"
        "  --project <PROJECT_PATH>  The path to the project file to run\n"
        "                            (defaults to the current directory).\n"
        "  -?, -h, --help            Show command line help.\n",
        stream);
}

static void write_test_help(FILE *stream) {
    fputs(
        "Description:\n"
        "  Aster Test Command\n\n"
        "Usage:\n"
        "  aster test [<PROJECT>] [options]\n\n"
        "Arguments:\n"
        "  <PROJECT>       The project file or project directory to test.\n\n"
        "Options:\n"
        "  -?, -h, --help  Show command line help.\n",
        stream);
}

static char *copy_string(const char *value) {
    size_t length = strlen(value) + 1U;
    char *copy = malloc(length);
    if (copy != NULL) memcpy(copy, value, length);
    return copy;
}

static char *manifest_path(const char *project_path) {
    const char *path = project_path != NULL ? project_path : ".";
    struct stat metadata;
    if (stat(path, &metadata) != 0 ||
        !ASTER_MODE_IS_DIRECTORY(metadata.st_mode))
        return copy_string(path);

    size_t length = strlen(path);
    bool has_separator = length != 0U &&
        (path[length - 1U] == '/' || path[length - 1U] == '\\');
    const char manifest[] = "aster.toml";
    char *result = malloc(
        length + (has_separator ? 0U : 1U) + sizeof(manifest));
    if (result == NULL) return NULL;
    memcpy(result, path, length);
    size_t output = length;
    if (!has_separator) result[output++] = '/';
    memcpy(result + output, manifest, sizeof(manifest));
    return result;
}

static int run_project(const char *project, size_t argument_count,
                       const char *const *arguments, FILE *error) {
    char *manifest = manifest_path(project);
    if (manifest == NULL) {
        fputs("Could not allocate the project path.\n", error);
        return 1;
    }
    int status = lang_project_run_args(
        manifest, NULL, argument_count, arguments);
    free(manifest);
    return status;
}

static int test_project(const char *project, FILE *error) {
    char *manifest = manifest_path(project);
    if (manifest == NULL) {
        fputs("Could not allocate the project path.\n", error);
        return 1;
    }
    int status = lang_project_test(manifest);
    free(manifest);
    return status;
}

static const char *operating_system_name(void) {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

static int write_unknown_command(const char *name, FILE *error) {
    fputs("Could not execute because the specified command or file was not "
          "found.\nPossible reasons for this include:\n"
          "  * You misspelled a built-in aster command.\n"
          "  * You intended to execute an Aster program, but aster-",
          error);
    fputs(name, error);
    fputs(" does not exist.\n"
          "  * You intended to run a global tool, but an aster-prefixed "
          "executable with this name could not be found on the PATH.\n",
          error);
    return 1;
}

int aster_cli_main(int argc, char **argv, FILE *output, FILE *error) {
    AsterCliInvocation parsed = aster_cli_parse(argc, argv);
    int status = 0;
    switch (parsed.action) {
        case ASTER_CLI_DRIVER_HELP:
            write_driver_help(output);
            break;
        case ASTER_CLI_VERSION:
            fprintf(output, "%s\n", ASTER_VERSION);
            break;
        case ASTER_CLI_INFO:
            fprintf(output,
                    "Aster:\n Version: %s\n\n"
                    "Runtime Environment:\n OS: %s\n",
                    ASTER_VERSION, operating_system_name());
            break;
        case ASTER_CLI_RUN:
            status = run_project(
                parsed.project, parsed.application_argument_count,
                parsed.application_arguments, error);
            break;
        case ASTER_CLI_TEST:
            status = test_project(parsed.project, error);
            break;
        case ASTER_CLI_RUN_HELP:
            write_run_help(output);
            break;
        case ASTER_CLI_TEST_HELP:
            write_test_help(output);
            break;
        case ASTER_CLI_UNKNOWN_COMMAND:
            status = write_unknown_command(parsed.error_value, error);
            break;
        case ASTER_CLI_MISSING_PROJECT_ARGUMENT:
            fputs("Required argument missing for option: '--project'.\n\n",
                  error);
            write_run_help(error);
            status = 1;
            break;
        case ASTER_CLI_UNRECOGNIZED_TEST_ARGUMENT:
            fprintf(error, "Unrecognized command or argument '%s'.\n\n",
                    parsed.error_value);
            write_test_help(error);
            status = 1;
            break;
        case ASTER_CLI_OUT_OF_MEMORY:
            fputs("Could not allocate command-line arguments.\n", error);
            status = 1;
            break;
    }
    aster_cli_invocation_dispose(&parsed);
    return status;
}
