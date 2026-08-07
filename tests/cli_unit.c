#include "cli.h"
#include "cli_parse.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool stream_contains(FILE *stream, const char *expected) {
    char contents[4096];
    rewind(stream);
    size_t length = fread(contents, 1U, sizeof(contents) - 1U, stream);
    contents[length] = '\0';
    return strstr(contents, expected) != NULL;
}

static bool open_capture(FILE **output, FILE **error) {
    *output = tmpfile();
    *error = tmpfile();
    if (*output != NULL && *error != NULL) return true;
    if (*output != NULL) (void)fclose(*output);
    if (*error != NULL) (void)fclose(*error);
    return false;
}

static int expect_driver_help(void) {
    FILE *output = NULL;
    FILE *error = NULL;
    if (!open_capture(&output, &error)) return 1;
    char *arguments[] = {"aster"};
    int status = aster_cli_main(1, arguments, output, error);
    bool valid = status == 0 && stream_contains(output, "Usage:") &&
                 !stream_contains(error, "Usage:");
    (void)fclose(output);
    (void)fclose(error);
    return valid ? 0 : 1;
}

static int expect_missing_project_error(void) {
    FILE *output = NULL;
    FILE *error = NULL;
    if (!open_capture(&output, &error)) return 1;
    char *arguments[] = {"aster", "run", "--project"};
    int status = aster_cli_main(3, arguments, output, error);
    bool valid = status != 0 && stream_contains(
        error, "Required argument missing for option: '--project'.");
    (void)fclose(output);
    (void)fclose(error);
    return valid ? 0 : 1;
}

static int expect_help_alias(void) {
    FILE *output = NULL;
    FILE *error = NULL;
    if (!open_capture(&output, &error)) return 1;
    char *arguments[] = {"aster", "test", "-?"};
    int status = aster_cli_main(3, arguments, output, error);
    bool valid = status == 0 &&
                 stream_contains(output, "Aster Test Command");
    (void)fclose(output);
    (void)fclose(error);
    return valid ? 0 : 1;
}

static int expect_interleaved_run_options(void) {
    char *arguments[] = {
        "aster", "run", "first", "--project", "sample", "second"
    };
    AsterCliInvocation parsed = aster_cli_parse(6, arguments);
    bool valid = parsed.action == ASTER_CLI_RUN &&
                 strcmp(parsed.project, "sample") == 0 &&
                 parsed.application_argument_count == 2U &&
                 strcmp(parsed.application_arguments[0], "first") == 0 &&
                 strcmp(parsed.application_arguments[1], "second") == 0;
    aster_cli_invocation_dispose(&parsed);
    return valid ? 0 : 1;
}

static int expect_option_separator(void) {
    char *arguments[] = {
        "aster", "run", "--", "--project", "literal"
    };
    AsterCliInvocation parsed = aster_cli_parse(5, arguments);
    bool valid = parsed.action == ASTER_CLI_RUN &&
                 parsed.project == NULL &&
                 parsed.application_argument_count == 2U &&
                 strcmp(parsed.application_arguments[0], "--project") == 0 &&
                 strcmp(parsed.application_arguments[1], "literal") == 0;
    aster_cli_invocation_dispose(&parsed);
    return valid ? 0 : 1;
}

static int expect_late_help_option(void) {
    char *arguments[] = {"aster", "run", "argument", "--help"};
    AsterCliInvocation parsed = aster_cli_parse(4, arguments);
    bool valid = parsed.action == ASTER_CLI_RUN_HELP;
    aster_cli_invocation_dispose(&parsed);
    return valid ? 0 : 1;
}

static int expect_test_argument_error(void) {
    char *arguments[] = {"aster", "test", "one", "two"};
    AsterCliInvocation parsed = aster_cli_parse(4, arguments);
    bool valid = parsed.action == ASTER_CLI_UNRECOGNIZED_TEST_ARGUMENT &&
                 strcmp(parsed.error_value, "two") == 0;
    aster_cli_invocation_dispose(&parsed);
    return valid ? 0 : 1;
}

static int expect_restore_project(void) {
    char *arguments[] = {"aster", "restore", "App.asproj"};
    AsterCliInvocation parsed = aster_cli_parse(3, arguments);
    bool valid = parsed.action == ASTER_CLI_RESTORE &&
                 strcmp(parsed.project, "App.asproj") == 0;
    aster_cli_invocation_dispose(&parsed);
    return valid ? 0 : 1;
}

int main(void) {
    int failures = 0;
    failures += expect_driver_help();
    failures += expect_missing_project_error();
    failures += expect_help_alias();
    failures += expect_interleaved_run_options();
    failures += expect_option_separator();
    failures += expect_late_help_option();
    failures += expect_test_argument_error();
    failures += expect_restore_project();
    return failures == 0 ? 0 : 1;
}
