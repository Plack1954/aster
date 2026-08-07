#include "vm_builtins_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static LangNativeResult native_add(LangVM *vm, const LangValue *args,
                                   size_t arg_count) {
    (void)vm;
    if (arg_count != 2U || args[0].tag != LANG_VALUE_I64 ||
        args[1].tag != LANG_VALUE_I64)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT}, "native_add expects two integers"
        };
    int64_t sum;
    if (!vm_checked_add(args[0].as.i64, args[1].as.i64, &sum))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT}, "native_add overflow"
        };
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_I64, .as.i64=sum}, NULL
    };
}

static LangNativeResult native_clock_value(LangVM *vm, const LangValue *args,
                                           size_t arg_count) {
    (void)vm;
    (void)args;
    if (arg_count != 0U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT}, "native_clock expects no arguments"
        };
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_I64, .as.i64=(int64_t)clock()}, NULL
    };
}

static LangNativeResult native_utc_now_unix_milliseconds(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    (void)args;
    if (arg_count != 0U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "NativeUtcNowUnixMilliseconds expects no arguments"
        };
    struct timespec value;
    if (timespec_get(&value, TIME_UTC) != TIME_UTC)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not read the UTC system clock"
        };
    int64_t milliseconds =
        (int64_t)value.tv_sec * INT64_C(1000) +
        (int64_t)value.tv_nsec / INT64_C(1000000);
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_I64, .as.i64=milliseconds}, NULL
    };
}

static void native_test_handle_drop(void *handle) {
    free(handle);
}

typedef struct TestHandle {
    int64_t id;
    int64_t *drop_log;
} TestHandle;

static void native_logged_handle_drop(void *handle) {
    TestHandle *test_handle = handle;
    *test_handle->drop_log = *test_handle->drop_log * 10 + test_handle->id;
    free(test_handle);
}

static LangNativeResult native_handle_open(LangVM *vm, const LangValue *args,
                                           size_t arg_count) {
    (void)args;
    if (arg_count != 0U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_handle_open expects no arguments"
        };
    void *handle = vm_allocate(1U, 1U);
    LangValue value;
    if (!lang_native_handle_value(vm, handle, native_test_handle_drop, &value)) {
        free(handle);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT}, "could not wrap native handle"
        };
    }
    return (LangNativeResult){true, value, NULL};
}

static LangNativeResult native_handle_open_id(LangVM *vm, const LangValue *args,
                                              size_t arg_count) {
    if (arg_count != 1U || args[0].tag != LANG_VALUE_I64)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_handle_open_id expects one integer"
        };
    TestHandle *handle = vm_allocate(1U, sizeof(*handle));
    handle->id = args[0].as.i64;
    handle->drop_log = vm_native_drop_log(vm);
    LangValue value;
    if (!lang_native_handle_value(vm, handle, native_logged_handle_drop, &value)) {
        free(handle);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT}, "could not wrap native handle"
        };
    }
    return (LangNativeResult){true, value, NULL};
}

static LangNativeResult native_handle_drop_log(LangVM *vm,
                                               const LangValue *args,
                                               size_t arg_count) {
    (void)vm;
    (void)args;
    if (arg_count != 0U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_handle_drop_log expects no arguments"
        };
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_I64, .as.i64=*vm_native_drop_log(vm)}, NULL
    };
}

static LangNativeResult native_handle_id(LangVM *vm, const LangValue *args,
                                         size_t arg_count) {
    (void)vm;
    if (arg_count != 1U) {
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_handle_id expects one native handle"
        };
    }
    TestHandle *handle = lang_native_handle_data(&args[0]);
    if (handle == NULL) {
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_handle_id received an invalid handle"
        };
    }
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_I64, .as.i64=handle->id}, NULL
    };
}

static LangNativeResult native_fail_handle(LangVM *vm, const LangValue *args,
                                           size_t arg_count) {
    (void)vm;
    (void)args;
    if (arg_count != 1U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_fail_handle expects one argument"
        };
    return (LangNativeResult){
        false, {.tag=LANG_VALUE_UNIT}, "intentional native failure"
    };
}

static LangNativeResult native_fill_bytes(LangVM *vm, const LangValue *args,
                                          size_t arg_count) {
    (void)vm;
    LangByteSlice bytes;
    if (arg_count != 2U ||
        !lang_value_byte_slice(&args[0], &bytes) ||
        args[1].tag != LANG_VALUE_U64 ||
        args[1].as.u64 > UINT8_MAX)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_fill_bytes expects `(Span<u8>, u8)`"
        };
    if (bytes.length != 0U)
        memset(bytes.data, (int)args[1].as.u64, bytes.length);
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_U64, .as.u64=(uint64_t)bytes.length},
        NULL
    };
}

static LangNativeResult native_checked_value(LangVM *vm,
                                             const LangValue *args,
                                             size_t arg_count) {
    if (arg_count != 1U || args[0].tag != LANG_VALUE_BOOL)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_checked_value expects one Boolean"
        };
    LangValue tagged;
    bool constructed;
    if (args[0].as.boolean) {
        LangValue payload = {
            .tag=LANG_VALUE_I64, .as.i64=42
        };
        constructed =
            lang_result_ok_value(vm, payload, &tagged);
    } else {
        static const char message[] = "native error";
        LangValue payload = {
            .tag=LANG_VALUE_STRING_VIEW,
            .as.string={message, sizeof(message) - 1U}
        };
        constructed =
            lang_result_err_value(vm, payload, &tagged);
    }
    if (!constructed)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct native Result"
        };
    return (LangNativeResult){true, tagged, NULL};
}

LangNativeResult native_result_error(LangVM *vm,
                                            const char *message) {
    LangValue error = {
        .tag=LANG_VALUE_STRING_VIEW,
        .as.string={message, strlen(message)}
    };
    LangValue tagged;
    if (!lang_result_err_value(vm, error, &tagged))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct native error Result"
        };
    return (LangNativeResult){true, tagged, NULL};
}

LangNativeResult native_result_i64(LangVM *vm, int64_t value) {
    LangValue tagged;
    LangValue payload = {.tag=LANG_VALUE_I64, .as.i64=value};
    if (!lang_result_ok_value(vm, payload, &tagged))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct native integer Result"
        };
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_process_argument_count_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)args;
    if (arg_count != 0U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_process_arg_count expects no arguments"
        };
    size_t process_argument_count = vm_process_argument_count(vm);
    if (process_argument_count > (size_t)UINT64_MAX)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "process argument count is not representable as nuint"
        };
    return (LangNativeResult){
        true,
        {
            .tag=LANG_VALUE_U64,
            .as.u64=(uint64_t)process_argument_count
        },
        NULL
    };
}

static LangNativeResult native_process_argument_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    if (arg_count != 1U || args[0].tag != LANG_VALUE_U64)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_process_arg expects one nuint index"
        };
    if (args[0].as.u64 >=
        (uint64_t)vm_process_argument_count(vm))
        return native_result_error(vm, "process argument index is out of range");
    const char *argument =
        vm_process_argument(vm, (size_t)args[0].as.u64);
    LangValue string;
    if (!lang_string_value(
            vm,
            (LangStringView){argument, strlen(argument)},
            &string))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not allocate process argument"
        };
    LangValue result;
    if (!lang_result_ok_value(vm, string, &result)) {
        lang_value_drop(vm, &string);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct process argument Result"
        };
    }
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult native_process_environment_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView name;
    if (arg_count != 1U ||
        !lang_value_string_view(&args[0], &name))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_process_environment expects one string name"
        };
    if (name.length == 0U || name.length >= 256U)
        return native_result_error(
            vm, "environment variable name has invalid length");
    for (size_t i = 0U; i < name.length; ++i)
        if (name.data[i] == '\0' || name.data[i] == '=')
            return native_result_error(
                vm, "environment variable name contains an invalid byte");
    char terminated_name[256];
    memcpy(terminated_name, name.data, name.length);
    terminated_name[name.length] = '\0';
    const char *environment_value = getenv(terminated_name);
    if (environment_value == NULL)
        return native_result_error(
            vm, "environment variable is not defined");
    LangValue string;
    if (!lang_string_value(
            vm,
            (LangStringView){
                environment_value, strlen(environment_value)
            },
            &string))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not allocate environment value"
        };
    LangValue result;
    if (!lang_result_ok_value(vm, string, &result)) {
        lang_value_drop(vm, &string);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct environment Result"
        };
    }
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult native_i64_to_string_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    if (arg_count != 1U || args[0].tag != LANG_VALUE_I64)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "i64_to_string expects one i64"
        };
    char buffer[32];
    int length = snprintf(
        buffer, sizeof(buffer), "%" PRId64, args[0].as.i64);
    LangValue string;
    if (length < 0 || (size_t)length >= sizeof(buffer) ||
        !lang_string_value(
            vm, (LangStringView){buffer, (size_t)length}, &string))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not format i64"
        };
    return (LangNativeResult){true, string, NULL};
}

static LangNativeResult native_u64_to_string_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    if (arg_count != 1U || args[0].tag != LANG_VALUE_U64)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "u64_to_string expects one u64"
        };
    char buffer[32];
    int length = snprintf(
        buffer, sizeof(buffer), "%" PRIu64, args[0].as.u64);
    LangValue string;
    if (length < 0 || (size_t)length >= sizeof(buffer) ||
        !lang_string_value(
            vm, (LangStringView){buffer, (size_t)length}, &string))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not format u64"
        };
    return (LangNativeResult){true, string, NULL};
}

static LangNativeResult native_float_to_string_value(
    LangVM *vm, const LangValue *args, size_t arg_count,
    int precision) {
    if (arg_count != 1U || args[0].tag != LANG_VALUE_F64)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "floating ToString expects one floating value"
        };
    char buffer[64];
    int length = snprintf(
        buffer, sizeof(buffer), precision == 9 ? "%.9g" : "%.17g",
        args[0].as.f64);
    LangValue string;
    if (length < 0 || (size_t)length >= sizeof(buffer) ||
        !lang_string_value(
            vm, (LangStringView){buffer, (size_t)length}, &string))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not format floating value"
        };
    return (LangNativeResult){true, string, NULL};
}

static LangNativeResult native_f32_to_string_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_float_to_string_value(vm, args, arg_count, 9);
}

static LangNativeResult native_f64_to_string_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_float_to_string_value(vm, args, arg_count, 17);
}

static LangNativeResult
native_interpolation_builder_append_formatted_value(
    LangVM *vm, const LangValue *args,
    size_t arg_count) {
    (void)vm;
    if (arg_count != 2U ||
        args[0].tag != LANG_VALUE_OBJECT ||
        !vm_value_is_string_builder(&args[0]))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "formatted interpolation append expects a StringBuilder and scalar"
        };

    const char *text = NULL;
    size_t length = 0U;
    char buffer[64];
    int formatted = 0;
    if (args[1].tag == LANG_VALUE_BOOL) {
        text = args[1].as.boolean ? "true" : "false";
        length = strlen(text);
    } else if (args[1].tag == LANG_VALUE_I64) {
        formatted = snprintf(
            buffer, sizeof(buffer), "%" PRId64,
            args[1].as.i64);
    } else if (args[1].tag == LANG_VALUE_U64) {
        formatted = snprintf(
            buffer, sizeof(buffer), "%" PRIu64,
            args[1].as.u64);
    } else if (args[1].tag == LANG_VALUE_F64) {
        formatted = snprintf(
            buffer, sizeof(buffer), "%g",
            args[1].as.f64);
    } else {
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "formatted interpolation append requires bool, char, or numeric value"
        };
    }
    if (text == NULL) {
        if (formatted < 0 ||
            (size_t)formatted >= sizeof(buffer))
            return (LangNativeResult){
                false, {.tag=LANG_VALUE_UNIT},
                "could not format interpolation value"
            };
        text = buffer;
        length = (size_t)formatted;
    }
    if (!vm_string_builder_append_bytes(
            args[0].as.object, text, length))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not grow interpolation StringBuilder"
        };
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_UNIT}, NULL
    };
}


void vm_register_core_builtins(LangVM *vm) {
    (void)lang_register_native(vm, "NativeAdd", native_add, 2U);
    (void)lang_register_native(vm, "NativeClock", native_clock_value, 0U);
    (void)lang_register_native(
        vm, "NativeUtcNowUnixMilliseconds",
        native_utc_now_unix_milliseconds, 0U);
    (void)lang_register_native(vm, "NativeHandleOpen", native_handle_open, 0U);
    (void)lang_register_native(vm, "NativeHandleOpenId",
                               native_handle_open_id, 1U);
    (void)lang_register_native(vm, "NativeHandleDropLog",
                               native_handle_drop_log, 0U);
    (void)lang_register_native(vm, "NativeHandleId",
                               native_handle_id, 1U);
    (void)lang_register_native(vm, "NativeFailHandle",
                               native_fail_handle, 1U);
    (void)lang_register_native(vm, "NativeFillBytes",
                               native_fill_bytes, 2U);
    (void)lang_register_native(vm, "NativeCheckedValue",
                               native_checked_value, 1U);
}

void vm_register_process_builtins(LangVM *vm) {
    (void)lang_register_native(vm, "NativeProcessArgCount",
                               native_process_argument_count_value, 0U);
    (void)lang_register_native(vm, "NativeProcessArg",
                               native_process_argument_value, 1U);
    (void)lang_register_native(vm, "NativeProcessEnvironment",
                               native_process_environment_value, 1U);
    lang_register_process_spawn_natives(vm);
    (void)lang_register_native(vm, "I64ToString",
                               native_i64_to_string_value, 1U);
    (void)lang_register_native(vm, "U64ToString",
                               native_u64_to_string_value, 1U);
    (void)lang_register_native(vm, "F32ToString",
                               native_f32_to_string_value, 1U);
    (void)lang_register_native(vm, "F64ToString",
                               native_f64_to_string_value, 1U);
    (void)lang_register_native(
        vm, "__interpolation_builder_append_formatted",
        native_interpolation_builder_append_formatted_value,
        2U);
}
