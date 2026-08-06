#include "lang/lang.h"

#include <stddef.h>

static LangNativeResult native_apply(LangVM *vm, const LangValue *args,
                                     size_t arg_count) {
    if (arg_count != 2U || args[0].tag != LANG_VALUE_I64)
        return lang_native_result_error("NativeApply expects value, callback");
    LangNativeResult result;
    if (!lang_vm_call_function(vm, &args[1], &args[0], 1U, &result))
        return lang_native_result_error("NativeApply received invalid callback");
    return result;
}

static void register_application_natives(LangVM *vm) {
    (void)lang_register_native(vm, "NativeApply", native_apply, 2U);
}

int main(void) {
    lang_configure_application_registrar(register_application_natives);
    int status = lang_run_file("tests/c_callback_surface.as", false, NULL);
    lang_configure_application_registrar(NULL);
    return status;
}
