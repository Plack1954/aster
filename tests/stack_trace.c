#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "lang/lang.h"

#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

int main(void) {
#if defined(_WIN32)
    return 0;
#else
    FILE *capture = tmpfile();
    if (capture == NULL) return 1;
    (void)fflush(stderr);
    int saved_stderr = dup(STDERR_FILENO);
    if (saved_stderr < 0 ||
        dup2(fileno(capture), STDERR_FILENO) < 0) {
        (void)fclose(capture);
        return 2;
    }
    int status =
        lang_run_file("tests/vm/stack_trace.lang", false, NULL);
    (void)fflush(stderr);
    if (dup2(saved_stderr, STDERR_FILENO) < 0) {
        (void)close(saved_stderr);
        (void)fclose(capture);
        return 3;
    }
    (void)close(saved_stderr);
    if (fseek(capture, 0L, SEEK_SET) != 0) {
        (void)fclose(capture);
        return 4;
    }
    char output[2048];
    size_t length =
        fread(output, 1U, sizeof(output) - 1U, capture);
    output[length] = '\0';
    (void)fclose(capture);
    bool complete =
        strstr(output,
               "at inner (tests/vm/stack_trace.lang:2:12)") != NULL &&
        strstr(output,
               "at middle (tests/vm/stack_trace.lang:6:12)") != NULL &&
        strstr(output,
               "at main (tests/vm/stack_trace.lang:10:11)") != NULL;
    return status != 0 && complete ? 0 : 5;
#endif
}
