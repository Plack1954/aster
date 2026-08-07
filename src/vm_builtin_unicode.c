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

#include "unicode_data.inc"

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


void vm_register_unicode_builtins(LangVM *vm) {
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
}
