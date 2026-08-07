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

void vm_register_text_builtins(LangVM *vm) {
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
}
