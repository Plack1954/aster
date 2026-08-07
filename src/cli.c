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
#include <windows.h>
#define ASTER_MODE_IS_DIRECTORY(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#else
#include <dirent.h>
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
        "  restore         Restore project dependencies.\n"
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

static void write_restore_help(FILE *stream) {
    fputs(
        "Description:\n"
        "  Aster Restore Command\n\n"
        "Usage:\n"
        "  aster restore [<ROOT>] [options]\n\n"
        "Arguments:\n"
        "  <ROOT>          The project file or project directory to restore.\n\n"
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

static bool has_suffix(const char *value, const char *suffix) {
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    return value_length >= suffix_length &&
           strcmp(value + value_length - suffix_length, suffix) == 0;
}

static char *join_path(const char *directory, const char *name) {
    size_t length = strlen(directory);
    bool separator = length != 0U &&
        directory[length - 1U] != '/' && directory[length - 1U] != '\\';
    size_t size = length + (separator ? 1U : 0U) + strlen(name) + 1U;
    char *path = malloc(size);
    if (path == NULL) return NULL;
    (void)snprintf(path, size, "%s%s%s", directory,
                   separator ? "/" : "", name);
    return path;
}

static char *discover_directory_project(
    const char *directory, FILE *error) {
    char *match = NULL;
    size_t matches = 0U;
#if defined(_WIN32)
    char *pattern = join_path(directory, "*.asproj");
    if (pattern == NULL) return NULL;
    WIN32_FIND_DATAA data;
    HANDLE search = FindFirstFileA(pattern, &data);
    free(pattern);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U)
                continue;
            ++matches;
            if (matches == 1U) match = join_path(directory, data.cFileName);
        } while (FindNextFileA(search, &data) != 0);
        (void)FindClose(search);
    }
#else
    DIR *entries = opendir(directory);
    if (entries == NULL) {
        fprintf(error, "Could not open project directory '%s'.\n", directory);
        return NULL;
    }
    struct dirent *entry = NULL;
    while ((entry = readdir(entries)) != NULL) {
        if (!has_suffix(entry->d_name, ".asproj")) continue;
        ++matches;
        if (matches == 1U) match = join_path(directory, entry->d_name);
    }
    (void)closedir(entries);
#endif
    if (matches == 1U && match != NULL) return match;
    free(match);
    if (matches == 0U)
        fprintf(error, "Could not find a project in '%s'.\n", directory);
    else
        fprintf(error,
                "More than one project was found in '%s'. Specify which "
                "project file to use.\n", directory);
    return NULL;
}

static char *project_file_path(const char *project_path, FILE *error) {
    const char *path = project_path != NULL ? project_path : ".";
    struct stat metadata;
    if (stat(path, &metadata) != 0) {
        fprintf(error, "The project path does not exist: %s.\n", path);
        return NULL;
    }
    if (!ASTER_MODE_IS_DIRECTORY(metadata.st_mode))
        return copy_string(path);
    return discover_directory_project(path, error);
}

static int run_project(const char *project, size_t argument_count,
                       const char *const *arguments, FILE *error) {
    char *project_file = project_file_path(project, error);
    if (project_file == NULL) return 1;
    int status = lang_project_run_args(
        project_file, argument_count, arguments);
    free(project_file);
    return status;
}

static int test_project(const char *project, FILE *error) {
    char *project_file = project_file_path(project, error);
    if (project_file == NULL) return 1;
    int status = lang_project_test(project_file);
    free(project_file);
    return status;
}

static int restore_project(const char *project, FILE *error) {
    char *project_file = project_file_path(project, error);
    if (project_file == NULL) return 1;
    int status = lang_project_restore(project_file);
    free(project_file);
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
        case ASTER_CLI_RESTORE:
            status = restore_project(parsed.project, error);
            break;
        case ASTER_CLI_RUN_HELP:
            write_run_help(output);
            break;
        case ASTER_CLI_TEST_HELP:
            write_test_help(output);
            break;
        case ASTER_CLI_RESTORE_HELP:
            write_restore_help(output);
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
        case ASTER_CLI_UNRECOGNIZED_RESTORE_ARGUMENT:
            fprintf(error, "Unrecognized command or argument '%s'.\n\n",
                    parsed.error_value);
            write_restore_help(error);
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
