#include "cli.h"
#include "lang/lang.h"

#include <stdio.h>

int main(int argc, char **argv) {
    lang_configure_http_client_registrar(lang_register_http_client_natives);
    lang_configure_crypto_registrar(lang_register_crypto_natives);
    if (argc != 0) lang_set_executable_path(argv[0]);
    return aster_cli_main(argc, argv, stdout, stderr);
}
