#include "vm_builtins_internal.h"

static LangNativeRegistrar http_client_registrar = NULL;
static LangNativeRegistrar crypto_registrar = NULL;
static LangNativeRegistrar application_registrar = NULL;

void lang_configure_application_registrar(LangNativeRegistrar registrar) {
    application_registrar = registrar;
}

void lang_configure_http_client_registrar(LangNativeRegistrar registrar) {
    http_client_registrar = registrar;
}

void lang_register_configured_http_client_natives(LangVM *vm) {
    if (http_client_registrar != NULL) http_client_registrar(vm);
}

void lang_configure_crypto_registrar(LangNativeRegistrar registrar) {
    crypto_registrar = registrar;
}

void lang_register_configured_crypto_natives(LangVM *vm) {
    if (crypto_registrar != NULL) crypto_registrar(vm);
}


void lang_vm_register_builtins(LangVM *vm) {
    *vm_native_drop_log(vm) = 0;
    vm_register_core_builtins(vm);
    vm_register_file_builtins(vm);
    vm_register_byte_builtins(vm);
    vm_register_file_close_builtin(vm);
    vm_register_text_builtins(vm);
    vm_register_unicode_builtins(vm);
    vm_register_directory_builtins(vm);
    vm_register_process_builtins(vm);
    vm_register_path_builtins(vm);
    lang_register_http_natives(vm);
    lang_register_configured_http_client_natives(vm);
    lang_register_configured_crypto_natives(vm);
    lang_register_h2o_natives(vm);
    lang_register_sqlite_natives(vm);
    if (application_registrar != NULL) application_registrar(vm);
}
