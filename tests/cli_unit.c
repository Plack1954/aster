#include "cli.h"

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

int main(void) {
    int failures = 0;
    failures += expect_driver_help();
    failures += expect_missing_project_error();
    failures += expect_help_alias();
    return failures == 0 ? 0 : 1;
}
