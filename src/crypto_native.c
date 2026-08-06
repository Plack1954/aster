#include "lang/lang.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__linux__)
#include <sys/random.h>
#else
#include <unistd.h>
#endif

#if defined(LANG_HAVE_OPENSSL)
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif

static LangNativeResult crypto_error(LangVM *vm, const char *message) {
    LangValue text;
    LangValue result;
    if (!lang_string_value(vm, (LangStringView){message, strlen(message)},
                           &text))
        return lang_native_result_error("could not allocate crypto error");
    if (!lang_result_err_value(vm, text, &result)) {
        lang_value_drop(vm, &text);
        return lang_native_result_error("could not allocate crypto Result");
    }
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult crypto_success(LangVM *vm, LangValue payload) {
    LangValue result;
    if (!lang_result_ok_value(vm, payload, &result)) {
        lang_value_drop(vm, &payload);
        return lang_native_result_error("could not allocate crypto Result");
    }
    return (LangNativeResult){true, result, NULL};
}

static bool os_random_fill(unsigned char *data, size_t length) {
#if defined(_WIN32)
    if (length > (size_t)ULONG_MAX) return false;
    return BCryptGenRandom(NULL, data, (ULONG)length,
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__linux__)
    size_t offset = 0U;
    while (offset < length) {
        ssize_t count = getrandom(data + offset, length - offset, 0U);
        if (count > 0) offset += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
#else
    arc4random_buf(data, length);
    return true;
#endif
}

static LangNativeResult native_crypto_random_fill(
    LangVM *vm, const LangValue *args, size_t count
) {
    LangByteSlice destination;
    if (count != 1U || !lang_value_byte_slice(&args[0], &destination))
        return lang_native_result_error(
            "NativeCryptoRandomFill expects one Span<byte>");
    if (!os_random_fill(destination.data, destination.length))
        return crypto_error(vm, "operating-system random generation failed");
    return crypto_success(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult native_crypto_random_hex(
    LangVM *vm, const LangValue *args, size_t count
) {
    if (count != 1U || args[0].tag != LANG_VALUE_I64 ||
        args[0].as.i64 < 0 || args[0].as.i64 > 1024)
        return lang_native_result_error(
            "NativeCryptoRandomHex expects a byte count from 0 through 1024");
    size_t bytes = (size_t)args[0].as.i64;
    unsigned char random[1024];
    char encoded[2048];
    if (!os_random_fill(random, bytes))
        return crypto_error(vm, "operating-system random generation failed");
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0U; i < bytes; ++i) {
        encoded[i * 2U] = digits[random[i] >> 4U];
        encoded[i * 2U + 1U] = digits[random[i] & 15U];
    }
    LangValue text;
    if (!lang_string_value(vm, (LangStringView){encoded, bytes * 2U}, &text))
        return crypto_error(vm, "could not allocate random hexadecimal text");
    return crypto_success(vm, text);
}

static LangNativeResult native_crypto_uuid_v4(
    LangVM *vm, const LangValue *args, size_t count
) {
    (void)args;
    if (count != 0U)
        return lang_native_result_error("NativeCryptoUuidV4 takes no arguments");
    unsigned char bytes[16];
    if (!os_random_fill(bytes, sizeof(bytes)))
        return crypto_error(vm, "operating-system random generation failed");
    bytes[6] = (unsigned char)((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = (unsigned char)((bytes[8] & 0x3fU) | 0x80U);
    char uuid[36];
    static const char digits[] = "0123456789abcdef";
    size_t output = 0U;
    for (size_t i = 0U; i < sizeof(bytes); ++i) {
        if (i == 4U || i == 6U || i == 8U || i == 10U) uuid[output++] = '-';
        uuid[output++] = digits[bytes[i] >> 4U];
        uuid[output++] = digits[bytes[i] & 15U];
    }
    LangValue text;
    if (!lang_string_value(vm, (LangStringView){uuid, sizeof(uuid)}, &text))
        return crypto_error(vm, "could not allocate UUID text");
    return crypto_success(vm, text);
}

static LangNativeResult native_crypto_fixed_time_equals(
    LangVM *vm, const LangValue *args, size_t count
) {
    (void)vm;
    LangByteSlice left;
    LangByteSlice right;
    if (count != 2U || !lang_value_byte_slice(&args[0], &left) ||
        !lang_value_byte_slice(&args[1], &right))
        return lang_native_result_error(
            "NativeCryptoFixedTimeEquals expects two byte spans");
    bool equal = false;
    if (left.length == right.length) {
        if (left.length == 0U)
            return (LangNativeResult){true,
                {.tag=LANG_VALUE_BOOL, .as.boolean=true}, NULL};
#if defined(LANG_HAVE_OPENSSL)
        equal = CRYPTO_memcmp(left.data, right.data, left.length) == 0;
#else
        unsigned int difference = 0U;
        for (size_t i = 0U; i < left.length; ++i)
            difference |= (unsigned int)(left.data[i] ^ right.data[i]);
        equal = difference == 0U;
#endif
    }
    return (LangNativeResult){true,
        {.tag=LANG_VALUE_BOOL, .as.boolean=equal}, NULL};
}

#if defined(LANG_HAVE_OPENSSL)
static LangNativeResult crypto_digest(
    LangVM *vm, const LangValue *args, size_t count, bool hmac
) {
    LangByteSlice key = {0};
    LangByteSlice data;
    LangByteSlice destination;
    size_t data_index = hmac ? 1U : 0U;
    size_t destination_index = hmac ? 2U : 1U;
    if (count != (hmac ? 3U : 2U) ||
        (hmac && !lang_value_byte_slice(&args[0], &key)) ||
        !lang_value_byte_slice(&args[data_index], &data) ||
        !lang_value_byte_slice(&args[destination_index], &destination) ||
        destination.length < 32U)
        return crypto_error(vm, "SHA-256 destination must contain 32 bytes");
    unsigned int written = 0U;
    bool ok;
    if (hmac) {
        ok = key.length <= (size_t)INT_MAX &&
             HMAC(EVP_sha256(), key.data, (int)key.length, data.data,
                  data.length, destination.data, &written) != NULL;
    } else {
        ok = EVP_Digest(data.data, data.length, destination.data, &written,
                        EVP_sha256(), NULL) == 1;
    }
    if (!ok || written != 32U)
        return crypto_error(vm, "OpenSSL SHA-256 operation failed");
    return crypto_success(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult native_crypto_sha256(
    LangVM *vm, const LangValue *args, size_t count
) { return crypto_digest(vm, args, count, false); }

static LangNativeResult native_crypto_hmac_sha256(
    LangVM *vm, const LangValue *args, size_t count
) { return crypto_digest(vm, args, count, true); }
#else
static LangNativeResult crypto_unavailable(
    LangVM *vm, const LangValue *args, size_t count
) {
    (void)args;
    (void)count;
    return crypto_error(vm,
        "SHA-256 is unavailable; rebuild with ASTER_ENABLE_CRYPTO=ON");
}
#endif

void lang_register_crypto_natives(LangVM *vm) {
    (void)lang_register_native(vm, "NativeCryptoRandomFill",
                               native_crypto_random_fill, 1U);
    (void)lang_register_native(vm, "NativeCryptoRandomHex",
                               native_crypto_random_hex, 1U);
    (void)lang_register_native(vm, "NativeCryptoUuidV4",
                               native_crypto_uuid_v4, 0U);
    (void)lang_register_native(vm, "NativeCryptoFixedTimeEquals",
                               native_crypto_fixed_time_equals, 2U);
#if defined(LANG_HAVE_OPENSSL)
    (void)lang_register_native(vm, "NativeCryptoSha256",
                               native_crypto_sha256, 2U);
    (void)lang_register_native(vm, "NativeCryptoHmacSha256",
                               native_crypto_hmac_sha256, 3U);
#else
    (void)lang_register_native(vm, "NativeCryptoSha256",
                               crypto_unavailable, 2U);
    (void)lang_register_native(vm, "NativeCryptoHmacSha256",
                               crypto_unavailable, 3U);
#endif
}
