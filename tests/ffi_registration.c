#include "lang/lang.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static LangNativeResult native_stack_error(LangVM *vm,
                                            const LangValue *args,
                                            size_t arg_count) {
    (void)vm;
    (void)args;
    char message[64];
    const char prefix[] = "stack-backed callback error";
    memcpy(message, prefix, sizeof(prefix));
    if (arg_count != 0U) message[0] = 'S';
    return lang_native_result_error(message);
}

static LangNativeResult native_sum(LangVM *vm, const LangValue *args,
                                   size_t arg_count) {
    (void)vm;
    LangNativeResult result = {false, {.tag=LANG_VALUE_UNIT}, "bad arguments"};
    if (arg_count == 2U && args[0].tag == LANG_VALUE_I64 &&
        args[1].tag == LANG_VALUE_I64) {
        result.ok = true;
        result.value.tag = LANG_VALUE_I64;
        result.value.as.i64 = args[0].as.i64 + args[1].as.i64;
        result.error = NULL;
    }
    return result;
}

static LangNativeResult native_identity(LangVM *vm, const LangValue *args,
                                        size_t arg_count) {
    (void)vm;
    if (arg_count != 1U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT}, "expected one argument"
        };
    return (LangNativeResult){true, args[0], NULL};
}

static void drop_test_handle(void *handle) {
    free(handle);
}

int main(void) {
    LangVM *vm = lang_vm_new();
    if (vm == NULL ||
        !lang_register_native(vm, "test::sum", native_sum, 2U) ||
        !lang_register_native(
            vm, "test::identity", native_identity, 1U) ||
        !lang_register_native(
            vm, "test::stack_error", native_stack_error, 0U))
        return 1;
    if (lang_register_native(vm, "test::sum", native_sum, 2U)) return 2;
    LangValue args[2] = {
        {.tag=LANG_VALUE_I64, .as.i64=20},
        {.tag=LANG_VALUE_I64, .as.i64=22}
    };
    LangNativeResult result;
    if (!lang_vm_call_native(vm, "test::sum", args, 2U, &result) ||
        !result.ok || result.value.tag != LANG_VALUE_I64 ||
        result.value.as.i64 != 42)
        return 3;
    if (!lang_vm_call_native(vm, "test::identity", NULL, 0U, &result) ||
        result.ok)
        return 8;
    lang_native_result_drop(&result);
    LangNativeResult stack_error;
    if (!lang_vm_call_native(
            vm, "test::stack_error", NULL, 0U, &stack_error) ||
        stack_error.ok || stack_error.error != NULL ||
        strcmp(lang_native_result_error_message(&stack_error),
               "stack-backed callback error") != 0)
        return 12;
    lang_native_result_drop(&stack_error);
    static const char text[] = "view";
    int marker = 0;
    LangValue scalar_values[] = {
        {.tag=LANG_VALUE_BOOL, .as.boolean=true},
        {.tag=LANG_VALUE_U64, .as.u64=UINT64_C(99)},
        {.tag=LANG_VALUE_F64, .as.f64=1.5},
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={text, sizeof(text) - 1U}},
        {.tag=LANG_VALUE_RAW_POINTER, .as.pointer=&marker}
    };
    for (size_t i = 0U;
         i < sizeof(scalar_values) / sizeof(scalar_values[0]); ++i) {
        if (!lang_vm_call_native(
                vm, "test::identity", &scalar_values[i], 1U, &result) ||
            !result.ok || result.value.tag != scalar_values[i].tag)
            return 9;
    }
    void *raw_handle = malloc(1U);
    LangValue handle;
    if (raw_handle == NULL ||
        !lang_native_handle_value(vm, raw_handle, drop_test_handle, &handle) ||
        lang_native_handle_data(&handle) != raw_handle)
        return 4;
    lang_value_drop(vm, &handle);
    uint8_t storage[3] = {1U, 2U, 3U};
    LangValue slice = {
        .tag=LANG_VALUE_BYTE_SLICE,
        .as.bytes={storage, sizeof(storage)}
    };
    LangByteSlice extracted;
    if (!lang_value_byte_slice(&slice, &extracted) ||
        extracted.data != storage || extracted.length != sizeof(storage))
        return 5;
    LangValue tagged;
    LangValue payload = {.tag=LANG_VALUE_I64, .as.i64=7};
    if (!lang_result_ok_value(vm, payload, &tagged) ||
        tagged.tag != LANG_VALUE_OBJECT)
        return 6;
    lang_value_drop(vm, &tagged);
    payload = (LangValue){.tag=LANG_VALUE_BOOL, .as.boolean=false};
    if (!lang_result_err_value(vm, payload, &tagged) ||
        tagged.tag != LANG_VALUE_OBJECT)
        return 7;
    lang_value_drop(vm, &tagged);
    LangValue string;
    if (!lang_string_value(
            vm, (LangStringView){text, sizeof(text) - 1U}, &string))
        return 10;
    LangStringView view;
    if (!lang_value_string_view(&string, &view) ||
        view.length != sizeof(text) - 1U ||
        memcmp(view.data, text, view.length) != 0)
        return 11;
    lang_value_drop(vm, &string);
    lang_vm_free(vm);
    return 0;
}
