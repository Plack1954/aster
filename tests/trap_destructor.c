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
    /* The POSIX descriptor capture below has no portable Windows equivalent. */
    return 0;
#else
    FILE *capture = tmpfile();
    if (capture == NULL) return 1;
    (void)fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0 || dup2(fileno(capture), STDOUT_FILENO) < 0) {
        (void)fclose(capture);
        return 2;
    }

    int status = lang_run_file("tests/vm/trap_destructor.as", false, NULL);
    (void)fflush(stdout);
    if (dup2(saved_stdout, STDOUT_FILENO) < 0) {
        (void)close(saved_stdout);
        (void)fclose(capture);
        return 3;
    }
    (void)close(saved_stdout);

    if (fseek(capture, 0L, SEEK_SET) != 0) {
        (void)fclose(capture);
        return 4;
    }
    char output[128];
    size_t length = fread(output, 1U, sizeof(output) - 1U, capture);
    output[length] = '\0';
    (void)fclose(capture);
    return status != 0 && strstr(output, "9\n") != NULL ? 0 : 5;
#endif
}
