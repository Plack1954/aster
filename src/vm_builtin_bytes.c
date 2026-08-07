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


void vm_register_byte_builtins(LangVM *vm) {
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
}
