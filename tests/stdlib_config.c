#include "lang/lang.h"

int main(int argc, char **argv) {
    if (argc != 4) return 2;

    lang_set_stdlib_path(argv[1]);
    if (lang_run_file(argv[3], true, NULL) == 0) return 3;

    lang_set_stdlib_path(argv[2]);
    if (lang_run_file(argv[3], true, NULL) != 0) return 4;

    lang_set_stdlib_path(NULL);
    return 0;
}
