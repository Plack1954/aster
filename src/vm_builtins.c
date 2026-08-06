#include "vm_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "unicode_data.inc"

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

static LangNativeRegistrar http_client_registrar = NULL;
static LangNativeRegistrar crypto_registrar = NULL;

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

typedef struct NativeFile {
    FILE *stream;
} NativeFile;

typedef struct NativeTemporaryFile {
    char *path;
} NativeTemporaryFile;

static void native_file_drop(void *handle) {
    NativeFile *file = handle;
    if (file->stream != NULL) (void)fclose(file->stream);
    free(file);
}

static void native_temporary_file_drop(void *handle) {
    NativeTemporaryFile *file = handle;
    if (file->path != NULL) {
        (void)remove(file->path);
        free(file->path);
    }
    free(file);
}

static LangNativeResult native_result_error(LangVM *vm,
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

static LangNativeResult native_result_i64(LangVM *vm, int64_t value) {
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

static bool native_path_string(
    const LangValue *value, char *output, size_t output_capacity) {
    LangStringView path;
    if (!lang_value_string_view(value, &path) ||
        path.length == 0U || path.length >= output_capacity)
        return false;
    for (size_t i = 0U; i < path.length; ++i)
        if (path.data[i] == '\0') return false;
    memcpy(output, path.data, path.length);
    output[path.length] = '\0';
    return true;
}

static bool native_path_separator(unsigned char value) {
#if defined(_WIN32)
    return value == (unsigned char)'/' || value == (unsigned char)'\\';
#else
    return value == (unsigned char)'/';
#endif
}

static bool native_path_rooted(LangStringView path) {
    if (path.length == 0U) return false;
    if (native_path_separator((unsigned char)path.data[0])) return true;
#if defined(_WIN32)
    return path.length >= 2U &&
           ((path.data[0] >= 'A' && path.data[0] <= 'Z') ||
            (path.data[0] >= 'a' && path.data[0] <= 'z')) &&
           path.data[1] == ':';
#else
    return false;
#endif
}

static LangNativeResult native_path_string_result(
    LangVM *vm, const char *data, size_t length) {
    LangValue result;
    if (!lang_string_value(
            vm, (LangStringView){data, length}, &result))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not allocate path string"
        };
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult native_path_combine_or_join_value(
    LangVM *vm, const LangValue *args, size_t arg_count,
    bool reset_on_rooted_second) {
    LangStringView first;
    LangStringView second;
    if (arg_count != 2U ||
        !lang_value_string_view(&args[0], &first) ||
        !lang_value_string_view(&args[1], &second))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "path combination expects two strings"
        };
    if (first.length == 0U ||
        (reset_on_rooted_second && native_path_rooted(second)))
        return native_path_string_result(vm, second.data, second.length);
    if (second.length == 0U)
        return native_path_string_result(vm, first.data, first.length);
    bool separator =
        native_path_separator((unsigned char)first.data[first.length - 1U]) ||
        native_path_separator((unsigned char)second.data[0]);
    if (first.length > SIZE_MAX - second.length - (separator ? 0U : 1U))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT}, "combined path is too long"
        };
    size_t length = first.length + second.length + (separator ? 0U : 1U);
    char *combined = vm_allocate(length == 0U ? 1U : length, 1U);
    memcpy(combined, first.data, first.length);
    size_t cursor = first.length;
    if (!separator) combined[cursor++] =
#if defined(_WIN32)
        '\\';
#else
        '/';
#endif
    memcpy(combined + cursor, second.data, second.length);
    LangNativeResult result =
        native_path_string_result(vm, combined, length);
    free(combined);
    return result;
}

static LangNativeResult native_path_combine_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_path_combine_or_join_value(
        vm, args, arg_count, true);
}

static LangNativeResult native_path_join_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_path_combine_or_join_value(
        vm, args, arg_count, false);
}

static bool native_path_view_argument(
    const LangValue *args, size_t arg_count, LangStringView *path) {
    return arg_count == 1U && lang_value_string_view(&args[0], path);
}

static size_t native_path_file_name_start(LangStringView path) {
    size_t start = 0U;
    for (size_t i = 0U; i < path.length; ++i)
        if (native_path_separator((unsigned char)path.data[i]))
            start = i + 1U;
    return start;
}

static bool native_path_is_root_view(LangStringView path) {
    if (!native_path_rooted(path)) return false;
#if defined(_WIN32)
    if (path.length == 1U && native_path_separator(
            (unsigned char)path.data[0]))
        return true;
    return path.length <= 3U && path.length >= 2U &&
           path.data[1] == ':' &&
           (path.length == 2U || native_path_separator(
                (unsigned char)path.data[2]));
#else
    for (size_t i = 0U; i < path.length; ++i)
        if (!native_path_separator((unsigned char)path.data[i]))
            return false;
    return true;
#endif
}

static size_t native_path_extension_start(
    LangStringView path, size_t file_name_start) {
    for (size_t i = path.length; i > file_name_start; --i) {
        unsigned char value = (unsigned char)path.data[i - 1U];
        if (value != (unsigned char)'.') continue;
        return i == path.length ? path.length : i - 1U;
    }
    return path.length;
}

static LangNativeResult native_path_get_file_name_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.GetFileName expects one string"
        };
    size_t start = native_path_file_name_start(path);
    return native_path_string_result(
        vm, path.length == start ? NULL : path.data + start,
        path.length - start);
}

static LangNativeResult native_path_get_extension_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.GetExtension expects one string"
        };
    size_t file_start = native_path_file_name_start(path);
    size_t extension = native_path_extension_start(path, file_start);
    return native_path_string_result(
        vm, path.length == extension ? NULL : path.data + extension,
        path.length - extension);
}

static LangNativeResult native_path_get_file_name_without_extension_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.GetFileNameWithoutExtension expects one string"
        };
    size_t file_start = native_path_file_name_start(path);
    size_t extension = native_path_extension_start(path, file_start);
    return native_path_string_result(
        vm, extension == file_start ? NULL : path.data + file_start,
        extension - file_start);
}

static LangNativeResult native_path_is_root_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "path root query expects one string"
        };
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_BOOL,
         .as.boolean=native_path_is_root_view(path)},
        NULL
    };
}

static LangNativeResult native_path_get_directory_name_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.GetDirectoryName expects one string"
        };
    size_t end = path.length;
    while (end > 0U &&
           native_path_separator((unsigned char)path.data[end - 1U]))
        --end;
    if (end < path.length)
        return native_path_string_result(vm, path.data, end);
    for (size_t i = end; i > 0U; --i) {
        if (!native_path_separator((unsigned char)path.data[i - 1U]))
            continue;
        size_t directory_length = i - 1U;
        if (directory_length == 0U) directory_length = 1U;
        return native_path_string_result(
            vm, path.data, directory_length);
    }
    return native_path_string_result(vm, "", 0U);
}

static LangNativeResult native_path_get_root_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.GetPathRoot expects one string"
        };
#if defined(_WIN32)
    if (path.length >= 2U && path.data[1] == ':') {
        size_t length = path.length >= 3U && native_path_separator(
            (unsigned char)path.data[2]) ? 3U : 2U;
        return native_path_string_result(vm, path.data, length);
    }
#endif
    if (path.length > 0U && native_path_separator(
            (unsigned char)path.data[0]))
        return native_path_string_result(vm, path.data, 1U);
    return native_path_string_result(vm, "", 0U);
}

static LangNativeResult native_path_change_extension_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    LangStringView extension;
    if (arg_count != 3U ||
        !lang_value_string_view(&args[0], &path) ||
        !lang_value_string_view(&args[1], &extension) ||
        args[2].tag != LANG_VALUE_BOOL)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.ChangeExtension expects path, extension, and removal flag"
        };
    size_t file_start = native_path_file_name_start(path);
    size_t base_length = native_path_extension_start(path, file_start);
    if (args[2].as.boolean)
        return native_path_string_result(vm, path.data, base_length);
    bool has_dot = extension.length > 0U && extension.data[0] == '.';
    if (base_length > SIZE_MAX - extension.length - (has_dot ? 0U : 1U))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT}, "changed path is too long"
        };
    size_t length = base_length + extension.length + (has_dot ? 0U : 1U);
    char *changed = vm_allocate(length == 0U ? 1U : length, 1U);
    if (base_length != 0U)
        memcpy(changed, path.data, base_length);
    size_t cursor = base_length;
    if (!has_dot) changed[cursor++] = '.';
    if (extension.length != 0U)
        memcpy(changed + cursor, extension.data, extension.length);
    LangNativeResult result = native_path_string_result(vm, changed, length);
    free(changed);
    return result;
}

static LangNativeResult native_path_is_fully_qualified_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.IsPathFullyQualified expects one string"
        };
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_BOOL, .as.boolean=native_path_rooted(path)},
        NULL
    };
}

static LangNativeResult native_directory_get_current_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)args;
    if (arg_count != 0U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Directory.GetCurrentDirectory expects no arguments"
        };
    char path[4096];
#if defined(_WIN32)
    if (_getcwd(path, (int)sizeof(path)) == NULL)
#else
    if (getcwd(path, sizeof(path)) == NULL)
#endif
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not get current directory"
        };
    return native_path_string_result(vm, path, strlen(path));
}

static LangNativeResult native_directory_set_current_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    char path[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], path, sizeof(path)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Directory.SetCurrentDirectory expects one valid path"
        };
#if defined(_WIN32)
    int status = _chdir(path);
#else
    int status = chdir(path);
#endif
    if (status != 0)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not set current directory"
        };
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_UNIT}, NULL
    };
}

static LangNativeResult native_environment_new_line_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)args;
    if (arg_count != 0U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Environment.NewLine expects no arguments"
        };
#if defined(_WIN32)
    return native_path_string_result(vm, "\r\n", 2U);
#else
    return native_path_string_result(vm, "\n", 1U);
#endif
}

static LangNativeResult native_result_unit(LangVM *vm) {
    LangValue result;
    if (!lang_result_ok_value(
            vm, (LangValue){.tag=LANG_VALUE_UNIT}, &result))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct filesystem Result"
        };
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult native_result_bool(LangVM *vm, bool value) {
    LangValue result;
    if (!lang_result_ok_value(
            vm,
            (LangValue){.tag=LANG_VALUE_BOOL, .as.boolean=value},
            &result))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct filesystem Result"
        };
    return (LangNativeResult){true, result, NULL};
}

typedef enum NativePathQuery {
    NATIVE_PATH_EXISTS,
    NATIVE_PATH_IS_FILE,
    NATIVE_PATH_IS_DIRECTORY
} NativePathQuery;

static LangNativeResult native_path_query(
    LangVM *vm, const LangValue *args, size_t arg_count,
    NativePathQuery query) {
    char path[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], path, sizeof(path)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "filesystem query expects one valid path"
        };
#if defined(_WIN32)
    struct _stat metadata;
    int status = _stat(path, &metadata);
#else
    struct stat metadata;
    int status = stat(path, &metadata);
#endif
    if (status != 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return native_result_bool(vm, false);
        return native_result_error(vm, "could not inspect path");
    }
    bool result = true;
    if (query == NATIVE_PATH_IS_FILE)
#if defined(_WIN32)
        result = (metadata.st_mode & _S_IFMT) == _S_IFREG;
#else
        result = S_ISREG(metadata.st_mode);
#endif
    else if (query == NATIVE_PATH_IS_DIRECTORY)
#if defined(_WIN32)
        result = (metadata.st_mode & _S_IFMT) == _S_IFDIR;
#else
        result = S_ISDIR(metadata.st_mode);
#endif
    return native_result_bool(vm, result);
}

static LangNativeResult native_path_exists_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_path_query(
        vm, args, arg_count, NATIVE_PATH_EXISTS);
}

static LangNativeResult native_path_is_file_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_path_query(
        vm, args, arg_count, NATIVE_PATH_IS_FILE);
}

static LangNativeResult native_path_is_directory_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_path_query(
        vm, args, arg_count, NATIVE_PATH_IS_DIRECTORY);
}

static LangNativeResult native_create_directory_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    char path[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], path, sizeof(path)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_create_directory expects one valid path"
        };
#if defined(_WIN32)
    int status = _mkdir(path);
#else
    int status = mkdir(path, (mode_t)0777);
#endif
    if (status != 0)
        return native_result_error(vm, "could not create directory");
    return native_result_unit(vm);
}

static LangNativeResult native_rename_path_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    char source[4096];
    char destination[4096];
    if (arg_count != 2U ||
        !native_path_string(&args[0], source, sizeof(source)) ||
        !native_path_string(&args[1], destination, sizeof(destination)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_rename_path expects two valid paths"
        };
    if (rename(source, destination) != 0)
        return native_result_error(vm, "could not rename path");
    return native_result_unit(vm);
}

static LangNativeResult native_remove_file_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    char path[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], path, sizeof(path)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_remove_file expects one valid path"
        };
#if defined(_WIN32)
    int status = remove(path);
#else
    int status = unlink(path);
#endif
    if (status != 0)
        return native_result_error(vm, "could not remove file");
    return native_result_unit(vm);
}

static LangNativeResult native_remove_directory_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    char path[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], path, sizeof(path)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_remove_directory expects one valid path"
        };
#if defined(_WIN32)
    int status = _rmdir(path);
#else
    int status = rmdir(path);
#endif
    if (status != 0)
        return native_result_error(vm, "could not remove directory");
    return native_result_unit(vm);
}

static LangNativeResult native_file_open_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    LangStringView mode;
    if (arg_count != 2U ||
        !lang_value_string_view(&args[0], &path) ||
        !lang_value_string_view(&args[1], &mode))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_open expects `(string, string)`"
        };
    bool valid_mode =
        (mode.length == 2U &&
         (memcmp(mode.data, "rb", 2U) == 0 ||
          memcmp(mode.data, "wb", 2U) == 0 ||
          memcmp(mode.data, "ab", 2U) == 0)) ||
        (mode.length == 3U &&
         memcmp(mode.data, "w+b", 3U) == 0);
    if (!valid_mode)
        return native_result_error(vm, "invalid file mode");
    if (path.length == SIZE_MAX)
        return native_result_error(vm, "file path is too long");
    char *path_c = vm_allocate(path.length + 1U, 1U);
    if (path.length != 0U)
        memcpy(path_c, path.data, path.length);
    path_c[path.length] = '\0';
    char mode_c[4] = {0};
    memcpy(mode_c, mode.data, mode.length);
    FILE *stream = fopen(path_c, mode_c);
    free(path_c);
    if (stream == NULL)
        return native_result_error(vm, "could not open file");
    NativeFile *file = vm_allocate(1U, sizeof(*file));
    file->stream = stream;
    LangValue handle;
    if (!lang_native_handle_value(
            vm, file, native_file_drop, &handle)) {
        native_file_drop(file);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not wrap file handle"
        };
    }
    LangValue tagged;
    if (!lang_result_ok_value(vm, handle, &tagged)) {
        lang_value_drop(vm, &handle);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct file Result"
        };
    }
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_file_create_temporary_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    char directory[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], directory, sizeof(directory)))
        return native_result_error(
            vm, "temporary-file directory is invalid");

    size_t directory_length = strlen(directory);
    bool has_separator = native_path_separator(
        (unsigned char)directory[directory_length - 1U]);
    char path[4096];
    for (unsigned attempt = 0U; attempt < 128U; ++attempt) {
        uint64_t token = (uint64_t)time(NULL) ^
#if defined(_WIN32)
            ((uint64_t)(unsigned)_getpid() << 32U) ^
#else
            ((uint64_t)(unsigned)getpid() << 32U) ^
#endif
            ((uint64_t)attempt * UINT64_C(0x9e3779b97f4a7c15));
        int length = snprintf(
            path, sizeof(path), "%s%s.aster-upload-%016" PRIx64 ".tmp",
#if defined(_WIN32)
            directory, has_separator ? "" : "\\", token);
#else
            directory, has_separator ? "" : "/", token);
#endif
        if (length <= 0 || (size_t)length >= sizeof(path))
            return native_result_error(
                vm, "temporary-file path is too long");
#if defined(_WIN32)
        int descriptor = _open(
            path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
            _S_IREAD | _S_IWRITE);
        if (descriptor >= 0) (void)_close(descriptor);
#else
        int descriptor = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (descriptor >= 0) (void)close(descriptor);
#endif
        if (descriptor < 0) {
            if (errno == EEXIST) continue;
            return native_result_error(
                vm, "could not create temporary file");
        }

        NativeTemporaryFile *file = vm_allocate(1U, sizeof(*file));
        file->path = vm_allocate((size_t)length + 1U, 1U);
        memcpy(file->path, path, (size_t)length + 1U);
        LangValue handle;
        if (!lang_native_handle_value(
                vm, file, native_temporary_file_drop, &handle)) {
            native_temporary_file_drop(file);
            return (LangNativeResult){
                false, {.tag=LANG_VALUE_UNIT},
                "could not wrap temporary-file handle"
            };
        }
        LangValue tagged;
        if (!lang_result_ok_value(vm, handle, &tagged)) {
            lang_value_drop(vm, &handle);
            return (LangNativeResult){
                false, {.tag=LANG_VALUE_UNIT},
                "could not construct temporary-file Result"
            };
        }
        return (LangNativeResult){true, tagged, NULL};
    }
    return native_result_error(
        vm, "could not allocate a unique temporary file");
}

static LangNativeResult native_file_temporary_path_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    NativeTemporaryFile *file = arg_count == 1U
        ? lang_native_handle_data(&args[0]) : NULL;
    if (file == NULL || file->path == NULL)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "temporary-file path expects one live handle"
        };
    return native_path_string_result(vm, file->path, strlen(file->path));
}

static LangNativeResult native_file_read_all_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    if (arg_count != 1U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_read_all expects one file handle"
        };
    NativeFile *file = lang_native_handle_data(&args[0]);
    if (file == NULL || file->stream == NULL)
        return native_result_error(vm, "invalid file handle");
    if (fseek(file->stream, 0L, SEEK_END) != 0)
        return native_result_error(vm, "could not seek file");
    long end = ftell(file->stream);
    if (end < 0L || fseek(file->stream, 0L, SEEK_SET) != 0)
        return native_result_error(vm, "could not measure file");
    size_t length = (size_t)end;
    char *bytes = vm_allocate(length + 1U, 1U);
    size_t read = length == 0U
                ? 0U : fread(bytes, 1U, length, file->stream);
    if (read != length) {
        free(bytes);
        return native_result_error(vm, "could not read complete file");
    }
    bytes[length] = '\0';
    LangValue string;
    bool made_string = lang_string_value(
        vm, (LangStringView){bytes, length}, &string);
    free(bytes);
    if (!made_string)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not allocate file contents"
        };
    LangValue tagged;
    if (!lang_result_ok_value(vm, string, &tagged)) {
        lang_value_drop(vm, &string);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct read Result"
        };
    }
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_file_write_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView data;
    if (arg_count != 2U ||
        !lang_value_string_view(&args[1], &data))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_write expects `(file, string)`"
        };
    NativeFile *file = lang_native_handle_data(&args[0]);
    if (file == NULL || file->stream == NULL)
        return native_result_error(vm, "invalid file handle");
    size_t written = data.length == 0U
                   ? 0U : fwrite(data.data, 1U, data.length, file->stream);
    if (written != data.length || fflush(file->stream) != 0)
        return native_result_error(vm, "could not write complete file");
    LangValue count = {
        .tag=LANG_VALUE_U64, .as.u64=(uint64_t)written
    };
    LangValue tagged;
    if (!lang_result_ok_value(vm, count, &tagged))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct write Result"
        };
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_file_read_bytes_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangByteSlice bytes;
    if (arg_count != 2U ||
        !lang_value_byte_slice(&args[1], &bytes))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_read_into expects `(file, Span<u8>)`"
        };
    NativeFile *file = lang_native_handle_data(&args[0]);
    if (file == NULL || file->stream == NULL)
        return native_result_error(vm, "invalid file handle");
    size_t read = bytes.length == 0U
                ? 0U : fread(bytes.data, 1U, bytes.length, file->stream);
    if (read == 0U && ferror(file->stream) != 0)
        return native_result_error(vm, "could not read file bytes");
    LangValue count = {
        .tag=LANG_VALUE_U64, .as.u64=(uint64_t)read
    };
    LangValue tagged;
    if (!lang_result_ok_value(vm, count, &tagged))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct byte-read Result"
        };
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_file_write_bytes_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangByteSlice bytes;
    if (arg_count != 3U ||
        !lang_value_byte_slice(&args[1], &bytes) ||
        args[2].tag != LANG_VALUE_U64 ||
        args[2].as.u64 > (uint64_t)bytes.length)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_write_bytes expects `(file, Span<u8>, nuint)`"
        };
    NativeFile *file = lang_native_handle_data(&args[0]);
    if (file == NULL || file->stream == NULL)
        return native_result_error(vm, "invalid file handle");
    size_t requested = (size_t)args[2].as.u64;
    size_t written = 0U;
    while (written < requested) {
        size_t amount = fwrite(
            bytes.data + written, 1U, requested - written, file->stream);
        if (amount == 0U)
            return native_result_error(
                vm, "could not write complete file bytes");
        written += amount;
    }
    if (fflush(file->stream) != 0)
        return native_result_error(vm, "could not flush file bytes");
    LangValue count = {
        .tag=LANG_VALUE_U64, .as.u64=(uint64_t)written
    };
    LangValue tagged;
    if (!lang_result_ok_value(vm, count, &tagged))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct byte-write Result"
        };
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_byte_slice_length_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangByteSlice bytes;
    if (arg_count != 1U ||
        !lang_value_byte_slice(&args[0], &bytes))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte_slice_len expects one Span<u8>"
        };
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_U64, .as.u64=(uint64_t)bytes.length},
        NULL
    };
}

static LangNativeResult native_byte_slice_at_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangByteSlice bytes;
    if (arg_count != 2U ||
        !lang_value_byte_slice(&args[0], &bytes) ||
        args[1].tag != LANG_VALUE_U64 ||
        args[1].as.u64 >= (uint64_t)bytes.length)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte_slice_at index is out of bounds"
        };
    return (LangNativeResult){
        true,
        {
            .tag=LANG_VALUE_U64,
            .as.u64=(uint64_t)bytes.data[(size_t)args[1].as.u64]
        },
        NULL
    };
}

static LangNativeResult native_byte_slice_set_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangByteSlice bytes;
    if (arg_count != 3U ||
        !lang_value_byte_slice(&args[0], &bytes) ||
        args[1].tag != LANG_VALUE_U64 ||
        args[1].as.u64 >= (uint64_t)bytes.length ||
        args[2].tag != LANG_VALUE_U64 || args[2].as.u64 > UINT8_MAX)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte_slice_set expects a valid slice index and byte"
        };
    bytes.data[(size_t)args[1].as.u64] = (unsigned char)args[2].as.u64;
    return (LangNativeResult){true, {.tag=LANG_VALUE_UNIT}, NULL};
}

static LangNativeResult native_byte_slice_range_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangByteSlice bytes;
    if (arg_count != 3U ||
        !lang_value_byte_slice(&args[0], &bytes) ||
        args[1].tag != LANG_VALUE_U64 ||
        args[2].tag != LANG_VALUE_U64 ||
        args[1].as.u64 > (uint64_t)bytes.length ||
        args[2].as.u64 >
            (uint64_t)bytes.length - args[1].as.u64)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte slice range is out of bounds"
        };
    size_t start = (size_t)args[1].as.u64;
    size_t length = (size_t)args[2].as.u64;
    return (LangNativeResult){
        true,
        {
            .tag=LANG_VALUE_BYTE_SLICE,
            .as.bytes={
                length == 0U ? bytes.data : bytes.data + start,
                length
            }
        },
        NULL
    };
}

static LangNativeResult native_byte_slice_copy_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangByteSlice source;
    LangByteSlice destination;
    if (arg_count != 2U ||
        !lang_value_byte_slice(&args[0], &source) ||
        !lang_value_byte_slice(&args[1], &destination))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte slice copy expects source and destination spans"
        };
    if (destination.length < source.length)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte slice destination is too short"
        };
    if (source.length != 0U)
        memmove(destination.data, source.data, source.length);
    return (LangNativeResult){true, {.tag=LANG_VALUE_UNIT}, NULL};
}

static LangNativeResult native_byte_slice_try_copy_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangByteSlice source;
    LangByteSlice destination;
    if (arg_count != 2U ||
        !lang_value_byte_slice(&args[0], &source) ||
        !lang_value_byte_slice(&args[1], &destination))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte slice try-copy expects source and destination spans"
        };
    bool fits = destination.length >= source.length;
    if (fits && source.length != 0U)
        memmove(destination.data, source.data, source.length);
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_BOOL, .as.boolean=fits}, NULL
    };
}

static LangNativeResult native_byte_slice_fill_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangByteSlice bytes;
    if (arg_count != 2U ||
        !lang_value_byte_slice(&args[0], &bytes) ||
        args[1].tag != LANG_VALUE_U64 || args[1].as.u64 > UINT8_MAX)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte slice fill expects a span and byte"
        };
    if (bytes.length != 0U)
        memset(bytes.data, (int)args[1].as.u64, bytes.length);
    return (LangNativeResult){true, {.tag=LANG_VALUE_UNIT}, NULL};
}

static LangNativeResult native_byte_slice_clear_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangByteSlice bytes;
    if (arg_count != 1U ||
        !lang_value_byte_slice(&args[0], &bytes))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte slice clear expects one span"
        };
    if (bytes.length != 0U)
        memset(bytes.data, 0, bytes.length);
    return (LangNativeResult){true, {.tag=LANG_VALUE_UNIT}, NULL};
}

static LangNativeResult native_byte_slice_equal_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangByteSlice left;
    LangByteSlice right;
    if (arg_count != 2U ||
        !lang_value_byte_slice(&args[0], &left) ||
        !lang_value_byte_slice(&args[1], &right))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte slice equality expects two spans"
        };
    bool equal = left.length == right.length &&
        (left.length == 0U ||
         memcmp(left.data, right.data, left.length) == 0);
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_BOOL, .as.boolean=equal}, NULL
    };
}

static LangNativeResult native_byte_slice_index_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangByteSlice bytes;
    if (arg_count != 2U ||
        !lang_value_byte_slice(&args[0], &bytes) ||
        args[1].tag != LANG_VALUE_U64 || args[1].as.u64 > UINT8_MAX)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte slice search expects a span and byte"
        };
    const unsigned char *found = bytes.length == 0U ? NULL : memchr(
        bytes.data, (int)args[1].as.u64, bytes.length);
    int64_t index = found == NULL
        ? INT64_C(-1) : (int64_t)(found - bytes.data);
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_I64, .as.i64=index}, NULL
    };
}

static LangNativeResult native_byte_slice_edge_value(
    LangVM *vm, const LangValue *args, size_t arg_count,
    bool suffix) {
    (void)vm;
    LangByteSlice bytes;
    LangByteSlice edge;
    if (arg_count != 2U ||
        !lang_value_byte_slice(&args[0], &bytes) ||
        !lang_value_byte_slice(&args[1], &edge))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "byte slice edge test expects two spans"
        };
    bool matches = edge.length <= bytes.length;
    if (matches && edge.length != 0U) {
        size_t start = suffix ? bytes.length - edge.length : 0U;
        matches = memcmp(bytes.data + start, edge.data, edge.length) == 0;
    }
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_BOOL, .as.boolean=matches}, NULL
    };
}

static LangNativeResult native_byte_slice_starts_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_byte_slice_edge_value(vm, args, arg_count, false);
}

static LangNativeResult native_byte_slice_ends_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_byte_slice_edge_value(vm, args, arg_count, true);
}

static LangNativeResult native_string_byte_slice_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangStringView bytes;
    if (arg_count != 1U ||
        !lang_value_string_view(&args[0], &bytes))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "StringAsByteSlice expects one string"
        };
    return (LangNativeResult){
        true,
        {
            .tag=LANG_VALUE_BYTE_SLICE,
            .as.bytes={
                (unsigned char *)(uintptr_t)bytes.data,
                bytes.length
            }
        },
        NULL
    };
}

static LangNativeResult native_byte_slice_string_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangByteSlice bytes;
    if (arg_count != 3U ||
        !lang_value_byte_slice(&args[0], &bytes) ||
        args[1].tag != LANG_VALUE_U64 ||
        args[2].tag != LANG_VALUE_U64 ||
        args[1].as.u64 > args[2].as.u64 ||
        args[2].as.u64 > (uint64_t)bytes.length)
        return native_result_error(
            vm, "byte slice string range is out of bounds");
    size_t start = (size_t)args[1].as.u64;
    size_t end = (size_t)args[2].as.u64;
    LangValue string;
    if (!lang_string_value(
            vm,
            (LangStringView){
                end == start ? NULL : (const char *)bytes.data + start,
                end - start
            },
            &string))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not allocate byte slice string"
        };
    LangValue tagged;
    if (!lang_result_ok_value(vm, string, &tagged)) {
        lang_value_drop(vm, &string);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct byte slice string Result"
        };
    }
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_file_close_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    NativeFile *file = arg_count == 1U
        ? lang_native_handle_data(&args[0]) : NULL;
    if (file == NULL)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_close expects one file handle"
        };
    if (file->stream != NULL) {
        (void)fclose(file->stream);
        file->stream = NULL;
    }
    return (LangNativeResult){true, {.tag=LANG_VALUE_UNIT}, NULL};
}

static LangNativeResult native_file_seek_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    NativeFile *file = arg_count == 3U
        ? lang_native_handle_data(&args[0]) : NULL;
    if (file == NULL || args[1].tag != LANG_VALUE_I64 ||
        args[2].tag != LANG_VALUE_I64 || args[2].as.i64 < 0 ||
        args[2].as.i64 > 2)
        return native_result_error(vm, "invalid file seek");
    int origin = args[2].as.i64 == 0 ? SEEK_SET
               : args[2].as.i64 == 1 ? SEEK_CUR : SEEK_END;
    if (fseek(file->stream, (long)args[1].as.i64, origin) != 0)
        return native_result_error(vm, "could not seek file");
    long position = ftell(file->stream);
    if (position < 0)
        return native_result_error(vm, "could not read file position");
    return native_result_i64(vm, (int64_t)position);
}

static LangNativeResult native_file_length_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    NativeFile *file = arg_count == 1U
        ? lang_native_handle_data(&args[0]) : NULL;
    if (file == NULL)
        return native_result_error(vm, "invalid file handle");
    long position = ftell(file->stream);
    if (position < 0 || fseek(file->stream, 0L, SEEK_END) != 0)
        return native_result_error(vm, "could not measure file");
    long length = ftell(file->stream);
    if (length < 0 || fseek(file->stream, position, SEEK_SET) != 0)
        return native_result_error(vm, "could not restore file position");
    return native_result_i64(vm, (int64_t)length);
}

static LangNativeResult native_file_flush_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    NativeFile *file = arg_count == 1U
        ? lang_native_handle_data(&args[0]) : NULL;
    if (file == NULL)
        return native_result_error(vm, "invalid file handle");
    if (fflush(file->stream) != 0)
        return native_result_error(vm, "could not flush file");
    return native_result_unit(vm);
}

static LangNativeResult native_string_as_str_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangStringView view;
    if (arg_count != 1U ||
        !lang_value_string_view(&args[0], &view))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "internal string view conversion expects one string"
        };
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_STRING_VIEW, .as.string=view}, NULL
    };
}

static LangNativeResult native_str_len_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangStringView view;
    if (arg_count != 1U ||
        !lang_value_string_view(&args[0], &view))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "string_len expects one string view"
        };
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_U64, .as.u64=(uint64_t)view.length},
        NULL
    };
}

static LangNativeResult native_str_byte_at_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangStringView view;
    if (arg_count != 2U ||
        !lang_value_string_view(&args[0], &view) ||
        args[1].tag != LANG_VALUE_U64 ||
        args[1].as.u64 >= (uint64_t)view.length)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "string_byte_at index is outside the string view"
        };
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_U64,
         .as.u64=(uint64_t)(unsigned char)
             view.data[(size_t)args[1].as.u64]},
        NULL
    };
}

static LangNativeResult native_str_index_of_ordinal(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangStringView value;
    LangStringView needle;
    if (arg_count != 3U ||
        !lang_value_string_view(&args[0], &value) ||
        !lang_value_string_view(&args[1], &needle) ||
        args[2].tag != LANG_VALUE_U64)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "StringIndexOfOrdinal expects two strings and a start index"
        };
    uint64_t raw_start = args[2].as.u64;
    if (raw_start > (uint64_t)value.length)
        return (LangNativeResult){
            true, {.tag=LANG_VALUE_I64, .as.i64=-1}, NULL};
    size_t start = (size_t)raw_start;
    if (needle.length == 0U)
        return (LangNativeResult){
            true, {.tag=LANG_VALUE_I64, .as.i64=(int64_t)start}, NULL};
    if (needle.length > value.length - start)
        return (LangNativeResult){
            true, {.tag=LANG_VALUE_I64, .as.i64=-1}, NULL};
    const char *cursor = value.data + start;
    const char *end = value.data + (value.length - needle.length + 1U);
    while (cursor < end) {
        const char *candidate = memchr(
            cursor, (unsigned char)needle.data[0], (size_t)(end - cursor));
        if (candidate == NULL) break;
        if (needle.length == 1U ||
            memcmp(candidate + 1U, needle.data + 1U,
                   needle.length - 1U) == 0)
            return (LangNativeResult){
                true,
                {.tag=LANG_VALUE_I64,
                 .as.i64=(int64_t)(candidate - value.data)},
                NULL};
        cursor = candidate + 1U;
    }
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_I64, .as.i64=-1}, NULL};
}

static bool unicode_in_ranges(
    uint32_t scalar, const AsterUnicodeRange *ranges, size_t count) {
    size_t low = 0U;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (scalar < ranges[middle].first) high = middle;
        else if (scalar > ranges[middle].last) low = middle + 1U;
        else return true;
    }
    return false;
}

static uint32_t unicode_map(
    uint32_t scalar, const AsterUnicodeMapping *mappings, size_t count) {
    size_t low = 0U;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (scalar < mappings[middle].from) high = middle;
        else if (scalar > mappings[middle].from) low = middle + 1U;
        else return mappings[middle].to;
    }
    return scalar;
}

static uint64_t unicode_special_map(
    uint32_t scalar, const AsterUnicodeSpecial *mappings, size_t count) {
    size_t low = 0U;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (scalar < mappings[middle].from) high = middle;
        else if (scalar > mappings[middle].from) low = middle + 1U;
        else return mappings[middle].packed;
    }
    return 0U;
}

static bool unicode_scalar_argument(
    const LangValue *args, size_t arg_count, uint32_t *out_scalar) {
    if (arg_count != 1U || args[0].tag != LANG_VALUE_U64 ||
        args[0].as.u64 > UINT32_C(0x10ffff) ||
        (args[0].as.u64 >= UINT32_C(0xd800) &&
         args[0].as.u64 <= UINT32_C(0xdfff)))
        return false;
    *out_scalar = (uint32_t)args[0].as.u64;
    return true;
}

static LangNativeResult native_unicode_to_upper(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    uint32_t scalar;
    if (!unicode_scalar_argument(args, arg_count, &scalar))
        return (LangNativeResult){false, {.tag=LANG_VALUE_UNIT},
                                  "invalid Unicode scalar"};
    scalar = unicode_map(
        scalar, aster_unicode_upper_map,
        sizeof(aster_unicode_upper_map) / sizeof(aster_unicode_upper_map[0]));
    return (LangNativeResult){true, {.tag=LANG_VALUE_U64, .as.u64=scalar}, NULL};
}

static LangNativeResult native_unicode_to_lower(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    uint32_t scalar;
    if (!unicode_scalar_argument(args, arg_count, &scalar))
        return (LangNativeResult){false, {.tag=LANG_VALUE_UNIT},
                                  "invalid Unicode scalar"};
    scalar = unicode_map(
        scalar, aster_unicode_lower_map,
        sizeof(aster_unicode_lower_map) / sizeof(aster_unicode_lower_map[0]));
    return (LangNativeResult){true, {.tag=LANG_VALUE_U64, .as.u64=scalar}, NULL};
}

static LangNativeResult native_unicode_special_upper(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    uint32_t scalar;
    if (!unicode_scalar_argument(args, arg_count, &scalar))
        return (LangNativeResult){false, {.tag=LANG_VALUE_UNIT},
                                  "invalid Unicode scalar"};
    uint64_t packed = unicode_special_map(
        scalar, aster_unicode_special_upper,
        sizeof(aster_unicode_special_upper) /
            sizeof(aster_unicode_special_upper[0]));
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_U64, .as.u64=packed}, NULL};
}

static LangNativeResult native_unicode_special_lower(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    uint32_t scalar;
    if (!unicode_scalar_argument(args, arg_count, &scalar))
        return (LangNativeResult){false, {.tag=LANG_VALUE_UNIT},
                                  "invalid Unicode scalar"};
    uint64_t packed = unicode_special_map(
        scalar, aster_unicode_special_lower,
        sizeof(aster_unicode_special_lower) /
            sizeof(aster_unicode_special_lower[0]));
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_U64, .as.u64=packed}, NULL};
}

#define DEFINE_UNICODE_CLASS_NATIVE(function_name, table_name)               \
    static LangNativeResult function_name(                                   \
        LangVM *vm, const LangValue *args, size_t arg_count) {                \
        (void)vm;                                                              \
        uint32_t scalar;                                                       \
        if (!unicode_scalar_argument(args, arg_count, &scalar))               \
            return (LangNativeResult){false, {.tag=LANG_VALUE_UNIT},           \
                                      "invalid Unicode scalar"};             \
        bool result = unicode_in_ranges(                                      \
            scalar, table_name, sizeof(table_name) / sizeof(table_name[0]));  \
        return (LangNativeResult){                                            \
            true, {.tag=LANG_VALUE_BOOL, .as.boolean=result}, NULL};          \
    }

DEFINE_UNICODE_CLASS_NATIVE(native_unicode_is_letter, aster_unicode_letter)
DEFINE_UNICODE_CLASS_NATIVE(native_unicode_is_digit, aster_unicode_digit)
DEFINE_UNICODE_CLASS_NATIVE(native_unicode_is_upper, aster_unicode_upper)
DEFINE_UNICODE_CLASS_NATIVE(native_unicode_is_lower, aster_unicode_lower)
DEFINE_UNICODE_CLASS_NATIVE(
    native_unicode_is_white_space, aster_unicode_white_space)
#undef DEFINE_UNICODE_CLASS_NATIVE

static LangNativeResult native_unicode_decode_scalar(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangStringView view;
    if (arg_count != 2U || !lang_value_string_view(&args[0], &view) ||
        args[1].tag != LANG_VALUE_U64 || args[1].as.u64 >= view.length)
        return (LangNativeResult){
            true, {.tag=LANG_VALUE_U64, .as.u64=UINT64_MAX}, NULL};
    size_t index = (size_t)args[1].as.u64;
    const unsigned char *bytes = (const unsigned char *)view.data;
    uint32_t scalar;
    size_t length;
    unsigned char first = bytes[index];
    if (first < 0x80U) { scalar = first; length = 1U; }
    else if (first >= 0xc2U && first <= 0xdfU) {
        scalar = (uint32_t)(first & 0x1fU); length = 2U;
    } else if (first >= 0xe0U && first <= 0xefU) {
        scalar = (uint32_t)(first & 0x0fU); length = 3U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        scalar = (uint32_t)(first & 0x07U); length = 4U;
    } else {
        return (LangNativeResult){
            true, {.tag=LANG_VALUE_U64, .as.u64=UINT64_MAX}, NULL};
    }
    if (index + length > view.length)
        return (LangNativeResult){
            true, {.tag=LANG_VALUE_U64, .as.u64=UINT64_MAX}, NULL};
    for (size_t offset = 1U; offset < length; ++offset) {
        unsigned char continuation = bytes[index + offset];
        if ((continuation & 0xc0U) != 0x80U)
            return (LangNativeResult){
                true, {.tag=LANG_VALUE_U64, .as.u64=UINT64_MAX}, NULL};
        scalar = (scalar << 6U) | (uint32_t)(continuation & 0x3fU);
    }
    if ((length == 3U && scalar < 0x800U) ||
        (length == 4U && scalar < 0x10000U) || scalar > 0x10ffffU ||
        (scalar >= 0xd800U && scalar <= 0xdfffU))
        return (LangNativeResult){
            true, {.tag=LANG_VALUE_U64, .as.u64=UINT64_MAX}, NULL};
    uint64_t packed = ((uint64_t)length << 32U) | scalar;
    return (LangNativeResult){true, {.tag=LANG_VALUE_U64, .as.u64=packed}, NULL};
}

static LangNativeResult native_str_slice_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView view;
    if (arg_count != 3U ||
        !lang_value_string_view(&args[0], &view) ||
        args[1].tag != LANG_VALUE_U64 ||
        args[2].tag != LANG_VALUE_U64 ||
        args[1].as.u64 > args[2].as.u64 ||
        args[2].as.u64 > (uint64_t)view.length)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "string_slice requires valid start and end byte offsets"
        };
    size_t start = (size_t)args[1].as.u64;
    size_t end = (size_t)args[2].as.u64;
    LangValue result;
    if (!lang_string_value(
            vm, (LangStringView){
                end == start ? NULL : view.data + start, end - start
            },
            &result))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not allocate string slice"
        };
    return (LangNativeResult){true, result, NULL};
}

typedef struct NativeDirectory {
#if defined(_WIN32)
    int unavailable;
#else
    DIR *stream;
#endif
} NativeDirectory;

static void native_directory_drop(void *handle) {
    NativeDirectory *directory = handle;
#if !defined(_WIN32)
    if (directory->stream != NULL)
        (void)closedir(directory->stream);
#endif
    free(directory);
}

static LangNativeResult native_directory_open_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (arg_count != 1U ||
        !lang_value_string_view(&args[0], &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_directory_open expects one path"
        };
#if defined(_WIN32)
    (void)path;
    return native_result_error(
        vm, "directory traversal is not implemented on Windows yet");
#else
    if (path.length == SIZE_MAX)
        return native_result_error(vm, "directory path is too long");
    char *path_c = vm_allocate(path.length + 1U, 1U);
    if (path.length != 0U)
        memcpy(path_c, path.data, path.length);
    path_c[path.length] = '\0';
    DIR *stream = opendir(path_c);
    free(path_c);
    if (stream == NULL)
        return native_result_error(vm, "could not open directory");
    NativeDirectory *directory = vm_allocate(1U, sizeof(*directory));
    directory->stream = stream;
    LangValue handle;
    if (!lang_native_handle_value(
            vm, directory, native_directory_drop, &handle)) {
        native_directory_drop(directory);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not wrap directory handle"
        };
    }
    LangValue result;
    if (!lang_result_ok_value(vm, handle, &result)) {
        lang_value_drop(vm, &handle);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct directory Result"
        };
    }
    return (LangNativeResult){true, result, NULL};
#endif
}

static LangNativeResult native_directory_next_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    if (arg_count != 1U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_directory_next expects one directory"
        };
    NativeDirectory *directory = lang_native_handle_data(&args[0]);
    if (directory == NULL)
        return native_result_error(vm, "invalid directory handle");
#if defined(_WIN32)
    return native_result_error(
        vm, "directory traversal is not implemented on Windows yet");
#else
    struct dirent *entry = NULL;
    errno = 0;
    do {
        entry = readdir(directory->stream);
    } while (entry != NULL &&
             (strcmp(entry->d_name, ".") == 0 ||
              strcmp(entry->d_name, "..") == 0));
    if (entry == NULL)
        return native_result_error(
            vm, errno == 0
                ? "end of directory"
                : "could not read directory");
    LangValue name;
    if (!lang_string_value(
            vm,
            (LangStringView){
                entry->d_name, strlen(entry->d_name)
            },
            &name))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not allocate directory entry"
        };
    LangValue result;
    if (!lang_result_ok_value(vm, name, &result)) {
        lang_value_drop(vm, &name);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct directory entry Result"
        };
    }
    return (LangNativeResult){true, result, NULL};
#endif
}

void lang_vm_register_builtins(LangVM *vm) {
    *vm_native_drop_log(vm) = 0;
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
    (void)lang_register_native(vm, "NativeFileOpen",
                               native_file_open_value, 2U);
    (void)lang_register_native(vm, "NativeFileCreateTemporary",
                               native_file_create_temporary_value, 1U);
    (void)lang_register_native(vm, "NativeFileTemporaryPath",
                               native_file_temporary_path_value, 1U);
    (void)lang_register_native(vm, "NativeFileReadAll",
                               native_file_read_all_value, 1U);
    (void)lang_register_native(vm, "NativeFileWrite",
                               native_file_write_value, 2U);
    (void)lang_register_native(vm, "NativeFileReadInto",
                               native_file_read_bytes_value, 2U);
    (void)lang_register_native(vm, "NativeFileWriteBytes",
                               native_file_write_bytes_value, 3U);
    (void)lang_register_native(vm, "NativeFileSeek",
                               native_file_seek_value, 3U);
    (void)lang_register_native(vm, "NativeFileLength",
                               native_file_length_value, 1U);
    (void)lang_register_native(vm, "NativeFileFlush",
                               native_file_flush_value, 1U);
    (void)lang_register_native(vm, "ByteSliceLen",
                               native_byte_slice_length_value, 1U);
    (void)lang_register_native(vm, "ByteSliceAt",
                               native_byte_slice_at_value, 2U);
    (void)lang_register_native(vm, "ByteSliceSet",
                               native_byte_slice_set_value, 3U);
    (void)lang_register_native(vm, "ByteSliceRange",
                               native_byte_slice_range_value, 3U);
    (void)lang_register_native(vm, "ByteSliceRangeMut",
                               native_byte_slice_range_value, 3U);
    (void)lang_register_native(vm, "ByteSliceCopyTo",
                               native_byte_slice_copy_value, 2U);
    (void)lang_register_native(vm, "ByteSliceTryCopyTo",
                               native_byte_slice_try_copy_value, 2U);
    (void)lang_register_native(vm, "ByteSliceFill",
                               native_byte_slice_fill_value, 2U);
    (void)lang_register_native(vm, "ByteSliceClear",
                               native_byte_slice_clear_value, 1U);
    (void)lang_register_native(vm, "ByteSliceSequenceEqual",
                               native_byte_slice_equal_value, 2U);
    (void)lang_register_native(vm, "ByteSliceIndexOf",
                               native_byte_slice_index_value, 2U);
    (void)lang_register_native(vm, "ByteSliceStartsWith",
                               native_byte_slice_starts_value, 2U);
    (void)lang_register_native(vm, "ByteSliceEndsWith",
                               native_byte_slice_ends_value, 2U);
    (void)lang_register_native(vm, "StringAsByteSlice",
                               native_string_byte_slice_value, 1U);
    (void)lang_register_native(vm, "ByteSliceToString",
                               native_byte_slice_string_value, 3U);
    (void)lang_register_native(vm, "NativeFileClose",
                               native_file_close_value, 1U);
    (void)lang_register_native(vm, "StringView",
                               native_string_as_str_value, 1U);
    (void)lang_register_native(vm, "StringLen",
                               native_str_len_value, 1U);
    (void)lang_register_native(vm, "StringByteAt",
                               native_str_byte_at_value, 2U);
    (void)lang_register_native(vm, "StringSlice",
                               native_str_slice_value, 3U);
    (void)lang_register_native(vm, "StringIndexOfOrdinal",
                               native_str_index_of_ordinal, 3U);
    (void)lang_register_native(vm, "UnicodeToUpper",
                               native_unicode_to_upper, 1U);
    (void)lang_register_native(vm, "UnicodeToLower",
                               native_unicode_to_lower, 1U);
    (void)lang_register_native(vm, "UnicodeSpecialUpper",
                               native_unicode_special_upper, 1U);
    (void)lang_register_native(vm, "UnicodeSpecialLower",
                               native_unicode_special_lower, 1U);
    (void)lang_register_native(vm, "UnicodeIsLetter",
                               native_unicode_is_letter, 1U);
    (void)lang_register_native(vm, "UnicodeIsDigit",
                               native_unicode_is_digit, 1U);
    (void)lang_register_native(vm, "UnicodeIsUpper",
                               native_unicode_is_upper, 1U);
    (void)lang_register_native(vm, "UnicodeIsLower",
                               native_unicode_is_lower, 1U);
    (void)lang_register_native(vm, "UnicodeIsWhiteSpace",
                               native_unicode_is_white_space, 1U);
    (void)lang_register_native(vm, "UnicodeDecodeScalar",
                               native_unicode_decode_scalar, 2U);
    (void)lang_register_native(vm, "NativeDirectoryOpen",
                               native_directory_open_value, 1U);
    (void)lang_register_native(vm, "NativeDirectoryNext",
                               native_directory_next_value, 1U);
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
    (void)lang_register_native(vm, "NativePathExists",
                               native_path_exists_value, 1U);
    (void)lang_register_native(vm, "NativePathIsFile",
                               native_path_is_file_value, 1U);
    (void)lang_register_native(vm, "NativePathIsDirectory",
                               native_path_is_directory_value, 1U);
    (void)lang_register_native(vm, "NativePathCombine",
                               native_path_combine_value, 2U);
    (void)lang_register_native(vm, "NativePathJoin",
                               native_path_join_value, 2U);
    (void)lang_register_native(vm, "NativePathGetFileName",
                               native_path_get_file_name_value, 1U);
    (void)lang_register_native(
        vm, "NativePathGetFileNameWithoutExtension",
        native_path_get_file_name_without_extension_value, 1U);
    (void)lang_register_native(vm, "NativePathGetExtension",
                               native_path_get_extension_value, 1U);
    (void)lang_register_native(vm, "NativePathIsRoot",
                               native_path_is_root_value, 1U);
    (void)lang_register_native(vm, "NativePathGetDirectoryName",
                               native_path_get_directory_name_value, 1U);
    (void)lang_register_native(vm, "NativePathGetPathRoot",
                               native_path_get_root_value, 1U);
    (void)lang_register_native(vm, "NativePathChangeExtension",
                               native_path_change_extension_value, 3U);
    (void)lang_register_native(vm, "NativePathIsPathFullyQualified",
                               native_path_is_fully_qualified_value, 1U);
    (void)lang_register_native(vm, "NativeDirectoryGetCurrentDirectory",
                               native_directory_get_current_value, 0U);
    (void)lang_register_native(vm, "NativeDirectorySetCurrentDirectory",
                               native_directory_set_current_value, 1U);
    (void)lang_register_native(vm, "NativeEnvironmentNewLine",
                               native_environment_new_line_value, 0U);
    (void)lang_register_native(vm, "NativeCreateDirectory",
                               native_create_directory_value, 1U);
    (void)lang_register_native(vm, "NativeRenamePath",
                               native_rename_path_value, 2U);
    (void)lang_register_native(vm, "NativeRemoveFile",
                               native_remove_file_value, 1U);
    (void)lang_register_native(vm, "NativeRemoveDirectory",
                               native_remove_directory_value, 1U);
    lang_register_http_natives(vm);
    lang_register_configured_http_client_natives(vm);
    lang_register_configured_crypto_natives(vm);
    lang_register_h2o_natives(vm);
    lang_register_sqlite_natives(vm);
}
