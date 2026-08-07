#ifndef ASTER_CLI_PARSE_H
#define ASTER_CLI_PARSE_H

#include <stddef.h>

typedef enum {
    ASTER_CLI_DRIVER_HELP,
    ASTER_CLI_VERSION,
    ASTER_CLI_INFO,
    ASTER_CLI_RUN,
    ASTER_CLI_TEST,
    ASTER_CLI_RESTORE,
    ASTER_CLI_RUN_HELP,
    ASTER_CLI_TEST_HELP,
    ASTER_CLI_RESTORE_HELP,
    ASTER_CLI_UNKNOWN_COMMAND,
    ASTER_CLI_MISSING_PROJECT_ARGUMENT,
    ASTER_CLI_UNRECOGNIZED_TEST_ARGUMENT,
    ASTER_CLI_UNRECOGNIZED_RESTORE_ARGUMENT,
    ASTER_CLI_OUT_OF_MEMORY,
} AsterCliAction;

typedef struct {
    AsterCliAction action;
    const char *project;
    const char *error_value;
    size_t application_argument_count;
    const char **application_arguments;
} AsterCliInvocation;

/* Parses without retaining or modifying argument strings. */
AsterCliInvocation aster_cli_parse(int argc, char **argv);
void aster_cli_invocation_dispose(AsterCliInvocation *invocation);

#endif
