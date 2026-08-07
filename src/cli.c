#include "cli.h"
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

typedef int (*AsterCommandHandler)(
    int argc, char **argv, FILE *output, FILE *error);

typedef struct {
    const char *name;
    AsterCommandHandler handler;
    void (*write_help)(FILE *stream);
} AsterCommand;

static bool is_help_option(const char *argument) {
    return strcmp(argument, "-?") == 0 ||
           strcmp(argument, "-h") == 0 ||
           strcmp(argument, "--help") == 0;
}

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

static int run_command(int argc, char **argv,
                       FILE *output, FILE *error) {
    const char *project = NULL;
    int argument_start = argc;
    for (int i = 2; i < argc; ++i) {
        if (is_help_option(argv[i])) {
            write_run_help(output);
            return 0;
        }
        if (strcmp(argv[i], "--") == 0) {
            argument_start = i + 1;
            break;
        }
        if (strcmp(argv[i], "--project") == 0) {
            if (++i == argc) {
                fputs("Required argument missing for option: '--project'.\n\n",
                      error);
                write_run_help(error);
                return 1;
            }
            project = argv[i];
            continue;
        }
        const char prefix[] = "--project=";
        if (strncmp(argv[i], prefix, sizeof(prefix) - 1U) == 0) {
            project = argv[i] + sizeof(prefix) - 1U;
            if (project[0] == '\0') {
                fputs("Required argument missing for option: '--project'.\n\n",
                      error);
                write_run_help(error);
                return 1;
            }
            continue;
        }
        argument_start = i;
        break;
    }
    return run_project(
        project, (size_t)(argc - argument_start),
        (const char *const *)&argv[argument_start], error);
}

static int test_command(int argc, char **argv,
                        FILE *output, FILE *error) {
    const char *project = NULL;
    for (int i = 2; i < argc; ++i) {
        if (is_help_option(argv[i])) {
            write_test_help(output);
            return 0;
        }
        if (argv[i][0] == '-' || project != NULL) {
            fprintf(error, "Unrecognized command or argument '%s'.\n\n",
                    argv[i]);
            write_test_help(error);
            return 1;
        }
        project = argv[i];
    }
    char *manifest = manifest_path(project);
    if (manifest == NULL) {
        fputs("Could not allocate the project path.\n", error);
        return 1;
    }
    int status = lang_project_test(manifest);
    free(manifest);
    return status;
}

static const AsterCommand commands[] = {
    {"run", run_command, write_run_help},
    {"test", test_command, write_test_help},
};

static const AsterCommand *find_command(const char *name) {
    for (size_t i = 0U; i < sizeof(commands) / sizeof(commands[0]); ++i)
        if (strcmp(name, commands[i].name) == 0) return &commands[i];
    return NULL;
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
    if (argc <= 1) {
        write_driver_help(output);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        fprintf(output, "%s\n", ASTER_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--info") == 0) {
        fprintf(output,
                "Aster:\n Version: %s\n\nRuntime Environment:\n OS: %s\n",
                ASTER_VERSION, operating_system_name());
        return 0;
    }
    if (argc == 2 && is_help_option(argv[1])) {
        write_driver_help(output);
        return 0;
    }
    if (strcmp(argv[1], "help") == 0) {
        if (argc == 2) {
            write_driver_help(output);
            return 0;
        }
        if (argc == 3) {
            const AsterCommand *help_command = find_command(argv[2]);
            if (help_command != NULL) {
                help_command->write_help(output);
                return 0;
            }
        }
        return write_unknown_command("help", error);
    }
    const AsterCommand *command = find_command(argv[1]);
    if (command != NULL)
        return command->handler(argc, argv, output, error);
    return write_unknown_command(argv[1], error);
}
