#include "lang/lang.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(LANG_HAVE_CURL)
#include <curl/curl.h>

typedef struct CurlHttpResponse {
    long status_code;
    unsigned char *body;
    size_t body_length;
    size_t body_capacity;
    char *headers;
    size_t headers_length;
    size_t headers_capacity;
    char *effective_url;
    size_t maximum_body_length;
    bool body_limit_exceeded;
    bool header_limit_exceeded;
} CurlHttpResponse;

static bool curl_initialized = false;

static void curl_http_response_drop(void *data) {
    CurlHttpResponse *response = data;
    if (response == NULL) return;
    free(response->body);
    free(response->headers);
    free(response->effective_url);
    free(response);
}

static bool curl_append(
    unsigned char **data, size_t *length, size_t *capacity,
    const unsigned char *source, size_t count, size_t maximum
) {
    if (count > maximum - *length) return false;
    size_t required = *length + count;
    if (required > *capacity) {
        size_t next = *capacity == 0U ? 4096U : *capacity;
        while (next < required) {
            if (next > maximum / 2U) {
                next = maximum;
                break;
            }
            next *= 2U;
        }
        unsigned char *grown = realloc(*data, next == 0U ? 1U : next);
        if (grown == NULL) return false;
        *data = grown;
        *capacity = next;
    }
    if (count != 0U) memcpy(*data + *length, source, count);
    *length = required;
    return true;
}

static size_t curl_body_write(
    char *data, size_t size, size_t members, void *context
) {
    CurlHttpResponse *response = context;
    if (members != 0U && size > SIZE_MAX / members) return 0U;
    size_t count = size * members;
    if (!curl_append(
            &response->body, &response->body_length,
            &response->body_capacity, (const unsigned char *)data,
            count, response->maximum_body_length)) {
        response->body_limit_exceeded =
            count > response->maximum_body_length - response->body_length;
        return 0U;
    }
    return count;
}

static size_t curl_header_write(
    char *data, size_t size, size_t members, void *context
) {
    CurlHttpResponse *response = context;
    if (members != 0U && size > SIZE_MAX / members) return 0U;
    size_t count = size * members;
    const size_t maximum = 1024U * 1024U;
    unsigned char *headers = (unsigned char *)response->headers;
    if (!curl_append(
            &headers, &response->headers_length,
            &response->headers_capacity, (const unsigned char *)data,
            count, maximum)) {
        response->header_limit_exceeded = true;
        return 0U;
    }
    response->headers = (char *)headers;
    return count;
}

static char *curl_copy_text(LangStringView value) {
    if (memchr(value.data, '\0', value.length) != NULL) return NULL;
    char *copy = malloc(value.length + 1U);
    if (copy == NULL) return NULL;
    if (value.length != 0U) memcpy(copy, value.data, value.length);
    copy[value.length] = '\0';
    return copy;
}

static bool curl_add_headers(
    LangStringView text, struct curl_slist **headers
) {
    size_t start = 0U;
    while (start < text.length) {
        size_t end = start;
        while (end < text.length && text.data[end] != '\n') ++end;
        size_t length = end - start;
        if (length != 0U && text.data[start + length - 1U] == '\r')
            --length;
        if (length != 0U) {
            LangStringView line = {text.data + start, length};
            char *copy = curl_copy_text(line);
            if (copy == NULL) return false;
            struct curl_slist *next = curl_slist_append(*headers, copy);
            free(copy);
            if (next == NULL) return false;
            *headers = next;
        }
        start = end < text.length ? end + 1U : end;
    }
    return true;
}

static LangNativeResult curl_typed_error(LangVM *vm, const char *message) {
    LangValue text;
    LangValue result;
    LangStringView view = {message, strlen(message)};
    if (!lang_string_value(vm, view, &text))
        return lang_native_result_error("could not allocate HTTP error");
    if (!lang_result_err_value(vm, text, &result)) {
        lang_value_drop(vm, &text);
        return lang_native_result_error("could not allocate HTTP Result");
    }
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult curl_typed_success(
    LangVM *vm, LangValue payload
) {
    LangValue result;
    if (!lang_result_ok_value(vm, payload, &result)) {
        lang_value_drop(vm, &payload);
        return lang_native_result_error("could not allocate HTTP Result");
    }
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult native_http_client_send(
    LangVM *vm, const LangValue *args, size_t count
) {
    LangStringView method;
    LangStringView url;
    LangStringView request_headers;
    LangByteSlice body;
    if (count != 7U ||
        !lang_value_string_view(&args[0], &method) ||
        !lang_value_string_view(&args[1], &url) ||
        !lang_value_string_view(&args[2], &request_headers) ||
        !lang_value_byte_slice(&args[3], &body) ||
        args[4].tag != LANG_VALUE_I64 || args[4].as.i64 < 0 ||
        args[5].tag != LANG_VALUE_I64 || args[5].as.i64 < 0 ||
        args[6].tag != LANG_VALUE_BOOL)
        return lang_native_result_error(
            "NativeHttpClientSend received invalid arguments");
    if ((uint64_t)args[5].as.i64 > (uint64_t)SIZE_MAX)
        return curl_typed_error(vm, "HTTP response limit is too large");
    if (!curl_initialized)
        return curl_typed_error(vm, "libcurl initialization failed");
    if (body.length > (size_t)INT64_MAX)
        return curl_typed_error(vm, "HTTP request body is too large");

    char *method_text = curl_copy_text(method);
    char *url_text = curl_copy_text(url);
    if (method_text == NULL || url_text == NULL) {
        free(method_text);
        free(url_text);
        return curl_typed_error(vm, "HTTP method or URL is invalid");
    }
    CURL *easy = curl_easy_init();
    CurlHttpResponse *response = calloc(1U, sizeof(*response));
    struct curl_slist *headers = NULL;
    if (easy == NULL || response == NULL ||
        !curl_add_headers(request_headers, &headers)) {
        curl_easy_cleanup(easy);
        curl_slist_free_all(headers);
        curl_http_response_drop(response);
        free(method_text);
        free(url_text);
        return curl_typed_error(vm, "could not allocate HTTP request");
    }
    response->maximum_body_length = (size_t)args[5].as.i64;
    char error[CURL_ERROR_SIZE] = {0};
    (void)curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, error);
    (void)curl_easy_setopt(easy, CURLOPT_URL, url_text);
    (void)curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, method_text);
    (void)curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    (void)curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, args[4].as.i64);
    (void)curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION,
                           args[6].as.boolean ? 1L : 0L);
    (void)curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 10L);
#if LIBCURL_VERSION_NUM >= 0x075500
    (void)curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    (void)curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    (void)curl_easy_setopt(easy, CURLOPT_PROTOCOLS,
                           CURLPROTO_HTTP | CURLPROTO_HTTPS);
    (void)curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS,
                           CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    (void)curl_easy_setopt(easy, CURLOPT_USERAGENT, "Aster/0.1");
    (void)curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, curl_body_write);
    (void)curl_easy_setopt(easy, CURLOPT_WRITEDATA, response);
    (void)curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, curl_header_write);
    (void)curl_easy_setopt(easy, CURLOPT_HEADERDATA, response);
    if (headers != NULL)
        (void)curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    if (body.length != 0U) {
        (void)curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body.data);
        (void)curl_easy_setopt(
            easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.length);
    }

    CURLcode performed = curl_easy_perform(easy);
    if (performed == CURLE_OK)
        performed = curl_easy_getinfo(
            easy, CURLINFO_RESPONSE_CODE, &response->status_code);
    char *effective = NULL;
    if (performed == CURLE_OK)
        performed = curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective);
    if (performed == CURLE_OK && effective != NULL) {
        size_t length = strlen(effective);
        response->effective_url = malloc(length + 1U);
        if (response->effective_url == NULL)
            performed = CURLE_OUT_OF_MEMORY;
        else
            memcpy(response->effective_url, effective, length + 1U);
    }
    curl_easy_cleanup(easy);
    curl_slist_free_all(headers);
    free(method_text);
    free(url_text);
    if (performed != CURLE_OK) {
        const char *message = response->body_limit_exceeded
            ? "HTTP response exceeded its configured body limit"
            : response->header_limit_exceeded
                ? "HTTP response headers exceeded 1 MiB"
                : error[0] != '\0' ? error : curl_easy_strerror(performed);
        LangNativeResult failure = curl_typed_error(vm, message);
        curl_http_response_drop(response);
        return failure;
    }
    LangValue handle;
    if (!lang_native_handle_value(
            vm, response, curl_http_response_drop, &handle)) {
        curl_http_response_drop(response);
        return curl_typed_error(vm, "could not allocate HTTP response handle");
    }
    return curl_typed_success(vm, handle);
}

static CurlHttpResponse *curl_response(
    const LangValue *args, size_t count
) {
    return count == 1U ? lang_native_handle_data(&args[0]) : NULL;
}

static LangNativeResult native_http_response_status(
    LangVM *vm, const LangValue *args, size_t count
) {
    (void)vm;
    CurlHttpResponse *response = curl_response(args, count);
    if (response == NULL)
        return lang_native_result_error("invalid HTTP response handle");
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_I64, .as.i64=response->status_code}, NULL};
}

static LangNativeResult curl_response_text(
    LangVM *vm, const char *data, size_t length
) {
    LangValue value;
    if (!lang_string_value(vm, (LangStringView){data, length}, &value))
        return lang_native_result_error("could not allocate HTTP response text");
    return (LangNativeResult){true, value, NULL};
}

static LangNativeResult native_http_response_headers(
    LangVM *vm, const LangValue *args, size_t count
) {
    CurlHttpResponse *response = curl_response(args, count);
    if (response == NULL)
        return lang_native_result_error("invalid HTTP response handle");
    return curl_response_text(
        vm, response->headers, response->headers_length);
}

static LangNativeResult native_http_response_url(
    LangVM *vm, const LangValue *args, size_t count
) {
    CurlHttpResponse *response = curl_response(args, count);
    if (response == NULL)
        return lang_native_result_error("invalid HTTP response handle");
    const char *url = response->effective_url != NULL
        ? response->effective_url : "";
    return curl_response_text(vm, url, strlen(url));
}

static LangNativeResult native_http_response_body_length(
    LangVM *vm, const LangValue *args, size_t count
) {
    (void)vm;
    CurlHttpResponse *response = curl_response(args, count);
    if (response == NULL || response->body_length > (size_t)INT64_MAX)
        return lang_native_result_error("invalid HTTP response body length");
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_I64, .as.i64=(int64_t)response->body_length},
        NULL};
}

static LangNativeResult native_http_response_copy_body(
    LangVM *vm, const LangValue *args, size_t count
) {
    CurlHttpResponse *response = count == 2U
        ? lang_native_handle_data(&args[0]) : NULL;
    LangByteSlice destination;
    if (response == NULL ||
        !lang_value_byte_slice(&args[1], &destination) ||
        destination.length < response->body_length)
        return curl_typed_error(vm, "HTTP response destination is too small");
    if (response->body_length != 0U)
        memcpy(destination.data, response->body, response->body_length);
    return curl_typed_success(vm, (LangValue){
        .tag=LANG_VALUE_U64, .as.u64=(uint64_t)response->body_length});
}

void lang_register_http_client_natives(LangVM *vm) {
    if (!curl_initialized)
        curl_initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    (void)lang_register_native(
        vm, "NativeHttpClientSend", native_http_client_send, 7U);
    (void)lang_register_native(
        vm, "NativeHttpClientResponseStatus", native_http_response_status, 1U);
    (void)lang_register_native(
        vm, "NativeHttpClientResponseHeaders", native_http_response_headers, 1U);
    (void)lang_register_native(
        vm, "NativeHttpClientResponseUrl", native_http_response_url, 1U);
    (void)lang_register_native(
        vm, "NativeHttpClientResponseBodyLength",
        native_http_response_body_length, 1U);
    (void)lang_register_native(
        vm, "NativeHttpClientResponseCopyBody",
        native_http_response_copy_body, 2U);
}

#else

static LangNativeResult curl_unavailable(
    LangVM *vm, const LangValue *args, size_t count
) {
    (void)args;
    (void)count;
    LangValue text;
    LangValue result;
    const char *message =
        "System.Net.Http is unavailable; rebuild with ASTER_ENABLE_CURL=ON";
    if (!lang_string_value(
            vm, (LangStringView){message, strlen(message)}, &text))
        return lang_native_result_error(message);
    if (!lang_result_err_value(vm, text, &result)) {
        lang_value_drop(vm, &text);
        return lang_native_result_error(message);
    }
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult curl_unavailable_direct(
    LangVM *vm, const LangValue *args, size_t count
) {
    (void)vm;
    (void)args;
    (void)count;
    return lang_native_result_error(
        "System.Net.Http is unavailable; rebuild with ASTER_ENABLE_CURL=ON");
}

void lang_register_http_client_natives(LangVM *vm) {
    (void)lang_register_native(
        vm, "NativeHttpClientSend", curl_unavailable, 7U);
    (void)lang_register_native(
        vm, "NativeHttpClientResponseStatus", curl_unavailable_direct, 1U);
    (void)lang_register_native(
        vm, "NativeHttpClientResponseHeaders", curl_unavailable_direct, 1U);
    (void)lang_register_native(
        vm, "NativeHttpClientResponseUrl", curl_unavailable_direct, 1U);
    (void)lang_register_native(
        vm, "NativeHttpClientResponseBodyLength", curl_unavailable_direct, 1U);
    (void)lang_register_native(
        vm, "NativeHttpClientResponseCopyBody", curl_unavailable, 2U);
}

#endif
