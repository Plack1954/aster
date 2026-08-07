#include "cli_parse.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool is_help_option(const char *argument) {
    return strcmp(argument, "-?") == 0 ||
           strcmp(argument, "-h") == 0 ||
           strcmp(argument, "--help") == 0;
}

static AsterCliInvocation invocation(AsterCliAction action) {
    return (AsterCliInvocation){.action=action};
}

static AsterCliInvocation parse_run(int argc, char **argv) {
    AsterCliInvocation result = invocation(ASTER_CLI_RUN);
    if (argc > 2) {
        result.application_arguments = calloc(
            (size_t)(argc - 2), sizeof(*result.application_arguments));
        if (result.application_arguments == NULL)
            return invocation(ASTER_CLI_OUT_OF_MEMORY);
    }

    bool options_enabled = true;
    for (int i = 2; i < argc; ++i) {
        if (options_enabled && strcmp(argv[i], "--") == 0) {
            options_enabled = false;
            continue;
        }
        if (options_enabled && is_help_option(argv[i])) {
            result.action = ASTER_CLI_RUN_HELP;
            return result;
        }
        if (options_enabled && strcmp(argv[i], "--project") == 0) {
            if (++i == argc) {
                result.action = ASTER_CLI_MISSING_PROJECT_ARGUMENT;
                return result;
            }
            result.project = argv[i];
            continue;
        }
        const char project_prefix[] = "--project=";
        if (options_enabled && strncmp(
                argv[i], project_prefix,
                sizeof(project_prefix) - 1U) == 0) {
            result.project = argv[i] + sizeof(project_prefix) - 1U;
            if (result.project[0] == '\0')
                result.action = ASTER_CLI_MISSING_PROJECT_ARGUMENT;
            if (result.action != ASTER_CLI_RUN) return result;
            continue;
        }
        result.application_arguments[result.application_argument_count++] =
            argv[i];
    }
    return result;
}

static AsterCliInvocation parse_test(int argc, char **argv) {
    AsterCliInvocation result = invocation(ASTER_CLI_TEST);
    for (int i = 2; i < argc; ++i) {
        if (is_help_option(argv[i])) {
            result.action = ASTER_CLI_TEST_HELP;
            return result;
        }
        if (argv[i][0] == '-' || result.project != NULL) {
            result.action = ASTER_CLI_UNRECOGNIZED_TEST_ARGUMENT;
            result.error_value = argv[i];
            return result;
        }
        result.project = argv[i];
    }
    return result;
}

static AsterCliInvocation parse_restore(int argc, char **argv) {
    AsterCliInvocation result = invocation(ASTER_CLI_RESTORE);
    for (int i = 2; i < argc; ++i) {
        if (is_help_option(argv[i])) {
            result.action = ASTER_CLI_RESTORE_HELP;
            return result;
        }
        if (argv[i][0] == '-' || result.project != NULL) {
            result.action = ASTER_CLI_UNRECOGNIZED_RESTORE_ARGUMENT;
            result.error_value = argv[i];
            return result;
        }
        result.project = argv[i];
    }
    return result;
}

AsterCliInvocation aster_cli_parse(int argc, char **argv) {
    if (argc <= 1) return invocation(ASTER_CLI_DRIVER_HELP);
    if (argc == 2 && strcmp(argv[1], "--version") == 0)
        return invocation(ASTER_CLI_VERSION);
    if (argc == 2 && strcmp(argv[1], "--info") == 0)
        return invocation(ASTER_CLI_INFO);
    if (argc == 2 && is_help_option(argv[1]))
        return invocation(ASTER_CLI_DRIVER_HELP);
    if (strcmp(argv[1], "help") == 0) {
        if (argc == 2) return invocation(ASTER_CLI_DRIVER_HELP);
        if (argc == 3 && strcmp(argv[2], "run") == 0)
            return invocation(ASTER_CLI_RUN_HELP);
        if (argc == 3 && strcmp(argv[2], "test") == 0)
            return invocation(ASTER_CLI_TEST_HELP);
        if (argc == 3 && strcmp(argv[2], "restore") == 0)
            return invocation(ASTER_CLI_RESTORE_HELP);
        AsterCliInvocation result = invocation(ASTER_CLI_UNKNOWN_COMMAND);
        result.error_value = "help";
        return result;
    }
    if (strcmp(argv[1], "run") == 0) return parse_run(argc, argv);
    if (strcmp(argv[1], "test") == 0) return parse_test(argc, argv);
    if (strcmp(argv[1], "restore") == 0)
        return parse_restore(argc, argv);
    AsterCliInvocation result = invocation(ASTER_CLI_UNKNOWN_COMMAND);
    result.error_value = argv[1];
    return result;
}

void aster_cli_invocation_dispose(AsterCliInvocation *invocation_value) {
    if (invocation_value == NULL) return;
    free(invocation_value->application_arguments);
    *invocation_value = (AsterCliInvocation){0};
}
