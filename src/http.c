#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "lang/lang.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
typedef struct HttpServer {
    int unavailable;
} HttpServer;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

typedef struct HttpServer {
    int socket_fd;
    uint16_t port;
    uint32_t magic;
    size_t header_limit;
    size_t body_limit;
    int timeout_ms;
    size_t keep_alive_limit;
} HttpServer;

typedef struct HttpHeader {
    char *name;
    char *value;
} HttpHeader;

typedef struct HttpRequest {
    int client_fd;
    uint32_t magic;
    bool head;
    bool streaming;
    bool responded;
    bool client_keep_alive;
    bool has_surplus;
    char *buffer;
    size_t buffer_length;
    char *method;
    char *path;
    char *body;
    size_t body_length;
    HttpHeader headers[64];
    size_t header_count;
    char *header_data;
    size_t header_data_length;
    size_t header_limit;
    size_t body_limit;
    size_t request_limit;
    size_t request_count;
    char remote_ip[INET6_ADDRSTRLEN];
} HttpRequest;
#endif

#define HTTP_SERVER_MAGIC UINT32_C(0x4f485454)
#define HTTP_REQUEST_MAGIC UINT32_C(0x4f485251)

static LangNativeResult native_error(const char *message) {
    return lang_native_result_error(message);
}

static LangNativeResult native_i64(int64_t value) {
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_I64, .as.i64=value}, NULL
    };
}

static int form_hex_value(char byte) {
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    return -1;
}

static LangNativeResult http_form_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView body;
    LangStringView name;
    if (arg_count != 2U ||
        !lang_value_string_view(&args[0], &body) ||
        !lang_value_string_view(&args[1], &name))
        return native_error("http_form_value expects `(string, string)`");
    size_t cursor = 0U;
    while (cursor <= body.length) {
        size_t pair_end = cursor;
        while (pair_end < body.length &&
               body.data[pair_end] != '&')
            ++pair_end;
        size_t equals = cursor;
        while (equals < pair_end && body.data[equals] != '=')
            ++equals;
        if (equals - cursor == name.length &&
            (name.length == 0U ||
             memcmp(body.data + cursor, name.data, name.length) == 0)) {
            size_t value_start =
                equals < pair_end ? equals + 1U : pair_end;
            char *decoded = malloc(pair_end - value_start + 1U);
            if (decoded == NULL)
                return native_error("out of memory decoding form value");
            size_t output = 0U;
            for (size_t i = value_start; i < pair_end; ++i) {
                if (body.data[i] == '+') {
                    decoded[output++] = ' ';
                } else if (body.data[i] == '%') {
                    if (i + 2U >= pair_end) {
                        free(decoded);
                        LangValue error = {
                            .tag=LANG_VALUE_STRING_VIEW,
                            .as.string={
                                "invalid percent escape in form value",
                                strlen("invalid percent escape in form value")
                            }
                        };
                        LangValue tagged;
                        if (!lang_result_err_value(vm, error, &tagged))
                            return native_error(
                                "could not allocate form error Result");
                        return (LangNativeResult){true, tagged, NULL};
                    }
                    int high = form_hex_value(body.data[i + 1U]);
                    int low = form_hex_value(body.data[i + 2U]);
                    if (high < 0 || low < 0) {
                        free(decoded);
                        LangValue error = {
                            .tag=LANG_VALUE_STRING_VIEW,
                            .as.string={
                                "invalid percent escape in form value",
                                strlen("invalid percent escape in form value")
                            }
                        };
                        LangValue tagged;
                        if (!lang_result_err_value(vm, error, &tagged))
                            return native_error(
                                "could not allocate form error Result");
                        return (LangNativeResult){true, tagged, NULL};
                    }
                    decoded[output++] =
                        (char)((unsigned)high * 16U + (unsigned)low);
                    i += 2U;
                } else {
                    decoded[output++] = body.data[i];
                }
            }
            LangValue string;
            LangStringView decoded_view = {decoded, output};
            bool copied = lang_string_value(vm, decoded_view, &string);
            free(decoded);
            if (!copied)
                return native_error("could not allocate form value");
            LangValue tagged;
            if (!lang_result_ok_value(vm, string, &tagged)) {
                lang_value_drop(vm, &string);
                return native_error(
                    "could not allocate form success Result");
            }
            return (LangNativeResult){true, tagged, NULL};
        }
        if (pair_end == body.length) break;
        cursor = pair_end + 1U;
    }
    LangValue error = {
        .tag=LANG_VALUE_STRING_VIEW,
        .as.string={
            "required form field is missing",
            strlen("required form field is missing")
        }
    };
    LangValue tagged;
    if (!lang_result_err_value(vm, error, &tagged))
        return native_error("could not allocate form error Result");
    return (LangNativeResult){true, tagged, NULL};
}

#if defined(_WIN32)

static LangNativeResult http_server_open(LangVM *vm, const LangValue *args,
                                         size_t arg_count) {
    (void)vm;
    (void)args;
    (void)arg_count;
    return native_error("HTTP sockets are not implemented on Windows yet");
}

static LangNativeResult http_server_port(LangVM *vm, const LangValue *args,
                                         size_t arg_count) {
    (void)vm;
    (void)args;
    (void)arg_count;
    return native_error("HTTP sockets are not implemented on Windows yet");
}

static LangNativeResult http_serve_once(LangVM *vm, const LangValue *args,
                                        size_t arg_count) {
    (void)vm;
    (void)args;
    (void)arg_count;
    return native_error("HTTP sockets are not implemented on Windows yet");
}

#define WINDOWS_HTTP_STUB(name) \
    static LangNativeResult name(LangVM *vm, const LangValue *args, \
                                 size_t arg_count) { \
        (void)vm; (void)args; (void)arg_count; \
        return native_error("HTTP sockets are not implemented on Windows yet"); \
    }

WINDOWS_HTTP_STUB(http_accept_request)
WINDOWS_HTTP_STUB(http_request_method)
WINDOWS_HTTP_STUB(http_request_path)
WINDOWS_HTTP_STUB(http_request_header)
WINDOWS_HTTP_STUB(http_request_headers)
WINDOWS_HTTP_STUB(http_request_body)
WINDOWS_HTTP_STUB(http_request_remote_ip)
WINDOWS_HTTP_STUB(http_request_next)
WINDOWS_HTTP_STUB(http_path_matches)
WINDOWS_HTTP_STUB(http_path_param)
WINDOWS_HTTP_STUB(http_respond_html)
WINDOWS_HTTP_STUB(http_respond_html_reuse)
WINDOWS_HTTP_STUB(http_respond_redirect_reuse)
WINDOWS_HTTP_STUB(http_respond_reuse)
WINDOWS_HTTP_STUB(http_stream_begin)
WINDOWS_HTTP_STUB(http_stream_begin_headers)
WINDOWS_HTTP_STUB(http_stream_chunk)
WINDOWS_HTTP_STUB(http_stream_finish)

#undef WINDOWS_HTTP_STUB

#else

static void http_server_drop(void *pointer) {
    HttpServer *server = pointer;
    if (server->socket_fd >= 0) (void)close(server->socket_fd);
    free(server);
}

static bool send_all(int socket_fd, const char *data, size_t length) {
    while (length != 0U) {
#if defined(MSG_NOSIGNAL)
        ssize_t sent = send(socket_fd, data, length, MSG_NOSIGNAL);
#else
        ssize_t sent = send(socket_fd, data, length, 0);
#endif
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return false;
        data += (size_t)sent;
        length -= (size_t)sent;
    }
    return true;
}

static bool send_all_parts(int socket_fd,
                           const char *first, size_t first_length,
                           const char *second, size_t second_length) {
    struct iovec parts[2] = {
        {(void *)first, first_length},
        {(void *)second, second_length}
    };
    size_t part = 0U;
    while (part < 2U) {
        while (part < 2U && parts[part].iov_len == 0U) ++part;
        if (part == 2U) return true;
        struct msghdr message;
        memset(&message, 0, sizeof(message));
        message.msg_iov = &parts[part];
        message.msg_iovlen = 2U - part;
#if defined(MSG_NOSIGNAL)
        ssize_t sent = sendmsg(socket_fd, &message, MSG_NOSIGNAL);
#else
        ssize_t sent = sendmsg(socket_fd, &message, 0);
#endif
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return false;
        size_t consumed = (size_t)sent;
        while (part < 2U && consumed >= parts[part].iov_len) {
            consumed -= parts[part].iov_len;
            ++part;
        }
        if (part < 2U && consumed != 0U) {
            parts[part].iov_base =
                (char *)parts[part].iov_base + consumed;
            parts[part].iov_len -= consumed;
        }
    }
    return true;
}

static size_t request_header_end(const char *data, size_t length) {
    if (length < 4U) return 0U;
    for (size_t i = 3U; i < length; ++i)
        if (data[i - 3U] == '\r' && data[i - 2U] == '\n' &&
            data[i - 1U] == '\r' && data[i] == '\n')
            return i + 1U;
    return 0U;
}

static void http_request_drop(void *pointer) {
    HttpRequest *request = pointer;
    if (request->client_fd >= 0) (void)close(request->client_fd);
    request->magic = 0U;
    free(request->body);
    free(request->buffer);
    free(request);
}

static bool send_empty_status(int client_fd, int status, const char *reason,
                              const char *extra_header) {
    char response[512];
    int length = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n%sContent-Length: 0\r\nConnection: close\r\n\r\n",
        status, reason, extra_header != NULL ? extra_header : "");
    return length > 0 && (size_t)length < sizeof(response) &&
           send_all(client_fd, response, (size_t)length);
}

static void lowercase_ascii(char *text) {
    for (; *text != '\0'; ++text)
        if (*text >= 'A' && *text <= 'Z')
            *text = (char)(*text - 'A' + 'a');
}

static bool parse_content_length(const char *text, size_t *out_length) {
    if (*text == '\0') return false;
    size_t value = 0U;
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9') return false;
        unsigned digit = (unsigned)(*text - '0');
        if (value > (SIZE_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    *out_length = value;
    return true;
}

static bool ascii_token_equal(const char *start, size_t length,
                              const char *expected) {
    if (length != strlen(expected)) return false;
    for (size_t i = 0U; i < length; ++i) {
        char value = start[i];
        if (value >= 'A' && value <= 'Z')
            value = (char)(value - 'A' + 'a');
        if (value != expected[i]) return false;
    }
    return true;
}

static bool header_has_token(const char *value, const char *token) {
    while (*value != '\0') {
        while (*value == ' ' || *value == '\t' || *value == ',') ++value;
        const char *start = value;
        while (*value != '\0' && *value != ',') ++value;
        const char *end = value;
        while (end > start &&
               (end[-1] == ' ' || end[-1] == '\t'))
            --end;
        if (ascii_token_equal(start, (size_t)(end - start), token))
            return true;
    }
    return false;
}

static void reset_request_data(HttpRequest *request) {
    free(request->body);
    request->body = NULL;
    request->body_length = 0U;
    request->buffer_length = 0U;
    request->method = NULL;
    request->path = NULL;
    request->head = false;
    request->streaming = false;
    request->responded = false;
    request->client_keep_alive = false;
    request->has_surplus = false;
    request->header_count = 0U;
    request->header_data = NULL;
    request->header_data_length = 0U;
    request->buffer[0] = '\0';
}

static bool read_request(HttpRequest *request, int *error_status) {
    reset_request_data(request);
    size_t used = 0U;
    size_t header_end = 0U;
    while (header_end == 0U && used < request->header_limit) {
        ssize_t received = recv(
            request->client_fd, request->buffer + used,
            request->header_limit - used, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) {
            *error_status = 400;
            return false;
        }
        used += (size_t)received;
        header_end = request_header_end(request->buffer, used);
    }
    request->buffer[used] = '\0';
    request->buffer_length = used;
    if (header_end == 0U) {
        (void)send_empty_status(
            request->client_fd, 431,
            "Request Header Fields Too Large", NULL);
        *error_status = 431;
        return false;
    }

    char *line_end = strstr(request->buffer, "\r\n");
    char *first_space = strchr(request->buffer, ' ');
    char *second_space =
        first_space != NULL ? strchr(first_space + 1, ' ') : NULL;
    size_t version_length =
        line_end != NULL && second_space != NULL && second_space < line_end
        ? (size_t)(line_end - (second_space + 1))
        : 0U;
    bool http_10 =
        version_length == 8U &&
        memcmp(second_space + 1, "HTTP/1.0", 8U) == 0;
    bool http_11 =
        version_length == 8U &&
        memcmp(second_space + 1, "HTTP/1.1", 8U) == 0;
    if (line_end == NULL || first_space == NULL || second_space == NULL ||
        second_space >= line_end || (!http_10 && !http_11)) {
        (void)send_empty_status(
            request->client_fd, 400, "Bad Request", NULL);
        *error_status = 400;
        return false;
    }
    *first_space = '\0';
    *second_space = '\0';
    *line_end = '\0';
    request->method = request->buffer;
    request->path = first_space + 1;
    request->head = strcmp(request->method, "HEAD") == 0;

    char *cursor = line_end + 2;
    size_t content_length = 0U;
    bool have_content_length = false;
    bool connection_close = false;
    bool connection_keep_alive = false;
    while (*cursor != '\0' && !(cursor[0] == '\r' && cursor[1] == '\n')) {
        char *end = strstr(cursor, "\r\n");
        char *colon = strchr(cursor, ':');
        if (end == NULL || colon == NULL || colon >= end ||
            request->header_count >=
                sizeof(request->headers) / sizeof(request->headers[0])) {
            (void)send_empty_status(
                request->client_fd, 400, "Bad Request", NULL);
            *error_status = 400;
            return false;
        }
        *colon = '\0';
        *end = '\0';
        char *value = colon + 1;
        while (*value == ' ' || *value == '\t') ++value;
        char *value_end = end;
        while (value_end > value &&
               (value_end[-1] == ' ' || value_end[-1] == '\t'))
            *--value_end = '\0';
        lowercase_ascii(cursor);
        if (strcmp(cursor, "transfer-encoding") == 0 &&
            *value != '\0') {
            (void)send_empty_status(
                request->client_fd, 400, "Bad Request", NULL);
            *error_status = 400;
            return false;
        }
        if (strcmp(cursor, "content-length") == 0) {
            size_t parsed = 0U;
            if (!parse_content_length(value, &parsed) ||
                (have_content_length && parsed != content_length)) {
                (void)send_empty_status(
                    request->client_fd, 400, "Bad Request", NULL);
                *error_status = 400;
                return false;
            }
            content_length = parsed;
            have_content_length = true;
        }
        if (strcmp(cursor, "connection") == 0) {
            connection_close =
                connection_close || header_has_token(value, "close");
            connection_keep_alive =
                connection_keep_alive ||
                header_has_token(value, "keep-alive");
        }
        request->headers[request->header_count++] =
            (HttpHeader){cursor, value};
        cursor = end + 2;
    }
    if (request->header_count != 0U) {
        char *output = request->headers[0].name;
        request->header_data = output;
        for (size_t i = 0U; i < request->header_count; ++i) {
            size_t name_length = strlen(request->headers[i].name);
            size_t value_length = strlen(request->headers[i].value);
            memmove(output, request->headers[i].name, name_length + 1U);
            request->headers[i].name = output;
            output += name_length + 1U;
            memmove(output, request->headers[i].value, value_length + 1U);
            request->headers[i].value = output;
            output += value_length + 1U;
        }
        request->header_data_length =
            (size_t)(output - request->header_data);
    }
    if (content_length > request->body_limit) {
        (void)send_empty_status(
            request->client_fd, 413, "Payload Too Large", NULL);
        *error_status = 413;
        return false;
    }
    request->body = calloc(content_length + 1U, 1U);
    if (request->body == NULL) {
        *error_status = 500;
        return false;
    }
    size_t available = used > header_end ? used - header_end : 0U;
    size_t already = available;
    if (already > content_length) already = content_length;
    request->has_surplus = available > content_length;
    if (already != 0U)
        memcpy(request->body, request->buffer + header_end, already);
    size_t body_used = already;
    while (body_used < content_length) {
        ssize_t received = recv(
            request->client_fd, request->body + body_used,
            content_length - body_used, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) {
            (void)send_empty_status(
                request->client_fd, 400, "Bad Request", NULL);
            *error_status = 400;
            return false;
        }
        body_used += (size_t)received;
    }
    request->body_length = content_length;
    request->client_keep_alive =
        !connection_close && (http_11 || connection_keep_alive);
    ++request->request_count;
    *error_status = 0;
    return true;
}

static HttpRequest *accept_request(HttpServer *server, int *error_status) {
    int client_fd;
    struct sockaddr_storage peer_address;
    socklen_t peer_address_length = (socklen_t)sizeof(peer_address);
    memset(&peer_address, 0, sizeof(peer_address));
    do {
        client_fd = accept(server->socket_fd,
                           (struct sockaddr *)&peer_address,
                           &peer_address_length);
    } while (client_fd < 0 && errno == EINTR);
    if (client_fd < 0) {
        *error_status = 500;
        return NULL;
    }
    HttpRequest *request = calloc(1U, sizeof(*request));
    if (request == NULL) {
        (void)close(client_fd);
        *error_status = 500;
        return NULL;
    }
    request->client_fd = client_fd;
    request->magic = HTTP_REQUEST_MAGIC;
    request->header_limit = server->header_limit;
    request->body_limit = server->body_limit;
    request->request_limit = server->keep_alive_limit;
    const void *peer_source = NULL;
    if (peer_address.ss_family == AF_INET)
        peer_source = &((struct sockaddr_in *)&peer_address)->sin_addr;
    else if (peer_address.ss_family == AF_INET6)
        peer_source = &((struct sockaddr_in6 *)&peer_address)->sin6_addr;
    if (peer_source != NULL)
        (void)inet_ntop(peer_address.ss_family, peer_source,
                        request->remote_ip, sizeof(request->remote_ip));
    request->buffer = calloc(request->header_limit + 1U, 1U);
    if (request->buffer == NULL) {
        http_request_drop(request);
        *error_status = 500;
        return NULL;
    }
    struct timeval timeout;
    timeout.tv_sec = server->timeout_ms / 1000;
    timeout.tv_usec = (server->timeout_ms % 1000) * 1000;
    (void)setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                     &timeout, (socklen_t)sizeof(timeout));
    (void)setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO,
                     &timeout, (socklen_t)sizeof(timeout));
    if (!read_request(request, error_status)) {
        http_request_drop(request);
        return NULL;
    }
    return request;
}

static HttpRequest *get_request(const LangValue *value) {
    HttpRequest *request = lang_native_handle_data(value);
    return request != NULL && request->magic == HTTP_REQUEST_MAGIC
         ? request : NULL;
}

static const char *status_reason(int status) {
    switch (status) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Range Not Satisfiable";
        case 418: return "I'm a Teapot";
        case 422: return "Unprocessable Content";
        case 425: return "Too Early";
        case 426: return "Upgrade Required";
        case 428: return "Precondition Required";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default: return NULL;
    }
}

static bool respond_redirect_mode(HttpRequest *request,
                                  LangStringView location,
                                  bool keep_alive) {
    if (location.length == 0U || location.data[0] != '/')
        return false;
    for (size_t i = 0U; i < location.length; ++i)
        if (location.data[i] == '\r' || location.data[i] == '\n')
            return false;
    char header[1024];
    int header_length = snprintf(
        header, sizeof(header),
        "HTTP/1.1 303 See Other\r\n"
        "Location: %.*s\r\n"
        "Content-Length: 0\r\n"
        "Connection: %s\r\n"
        "X-Content-Type-Options: nosniff\r\n\r\n",
        (int)location.length, location.data,
        keep_alive ? "keep-alive" : "close");
    bool ok =
        location.length <= (size_t)INT_MAX &&
        header_length > 0 &&
        (size_t)header_length < sizeof(header) &&
        send_all(request->client_fd, header, (size_t)header_length);
    if (!ok || !keep_alive) {
        (void)close(request->client_fd);
        request->client_fd = -1;
    }
    request->responded = true;
    return ok;
}

static bool respond_body_mode(HttpRequest *request, int status,
                              LangStringView body,
                              const char *content_type,
                              bool keep_alive) {
    const char *reason = status_reason(status);
    if (reason == NULL) return false;
    size_t response_length = status == 204 ? 0U : body.length;
    char header[512];
    int header_length = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "X-Content-Type-Options: nosniff\r\n\r\n",
        status, reason, content_type, response_length,
        keep_alive ? "keep-alive" : "close");
    bool ok = header_length > 0 && (size_t)header_length < sizeof(header);
    if (ok && !request->head && response_length != 0U)
        ok = send_all_parts(
            request->client_fd, header, (size_t)header_length,
            body.data, response_length);
    else if (ok)
        ok = send_all(request->client_fd, header, (size_t)header_length);
    if (!ok || !keep_alive) {
        (void)close(request->client_fd);
        request->client_fd = -1;
    }
    request->responded = true;
    return ok;
}

static bool extra_response_headers_valid(LangStringView headers) {
    if (headers.length > 16384U) return false;
    size_t line_start = 0U;
    while (line_start < headers.length) {
        size_t line_end = line_start;
        while (line_end + 1U < headers.length &&
               !(headers.data[line_end] == '\r' &&
                 headers.data[line_end + 1U] == '\n'))
            ++line_end;
        if (line_end == line_start || line_end + 1U >= headers.length)
            return false;
        size_t colon = line_start;
        while (colon < line_end && headers.data[colon] != ':') {
            unsigned char byte = (unsigned char)headers.data[colon];
            bool token =
                (byte >= 'a' && byte <= 'z') ||
                (byte >= 'A' && byte <= 'Z') ||
                (byte >= '0' && byte <= '9') ||
                byte == '!' || byte == '#' || byte == '$' ||
                byte == '%' || byte == '&' || byte == '\'' ||
                byte == '*' || byte == '+' || byte == '-' ||
                byte == '.' || byte == '^' || byte == '_' ||
                byte == '`' || byte == '|' || byte == '~';
            if (!token) return false;
            ++colon;
        }
        if (colon == line_start || colon == line_end) return false;
        static const char *const reserved[] = {
            "content-length", "content-type", "connection",
            "transfer-encoding", "x-content-type-options"
        };
        for (size_t reserved_index = 0U;
             reserved_index < sizeof(reserved) / sizeof(reserved[0]);
             ++reserved_index) {
            const char *name = reserved[reserved_index];
            size_t name_length = strlen(name);
            if (colon - line_start != name_length) continue;
            bool equal = true;
            for (size_t i = 0U; i < name_length; ++i) {
                unsigned char byte =
                    (unsigned char)headers.data[line_start + i];
                if (byte >= 'A' && byte <= 'Z')
                    byte = (unsigned char)(byte + ('a' - 'A'));
                if (byte != (unsigned char)name[i]) {
                    equal = false;
                    break;
                }
            }
            if (equal) return false;
        }
        for (size_t i = colon + 1U; i < line_end; ++i) {
            unsigned char byte = (unsigned char)headers.data[i];
            if (byte < 32U && byte != '\t') return false;
            if (byte == 127U) return false;
        }
        line_start = line_end + 2U;
    }
    return true;
}

static bool respond_body_headers_mode(
    HttpRequest *request, int status, LangStringView body,
    const char *content_type, LangStringView headers,
    bool keep_alive
) {
    const char *reason = status_reason(status);
    if (reason == NULL || !extra_response_headers_valid(headers))
        return false;
    size_t response_length = status == 204 ? 0U : body.length;
    char prefix[512];
    int prefix_length = snprintf(
        prefix, sizeof(prefix),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n",
        status, reason, content_type, response_length);
    char suffix[160];
    int suffix_length = snprintf(
        suffix, sizeof(suffix),
        "Connection: %s\r\n"
        "X-Content-Type-Options: nosniff\r\n\r\n",
        keep_alive ? "keep-alive" : "close");
    bool ok =
        prefix_length > 0 && (size_t)prefix_length < sizeof(prefix) &&
        suffix_length > 0 && (size_t)suffix_length < sizeof(suffix) &&
        send_all(request->client_fd, prefix, (size_t)prefix_length) &&
        (headers.length == 0U ||
         send_all(request->client_fd, headers.data, headers.length)) &&
        send_all(request->client_fd, suffix, (size_t)suffix_length);
    if (ok && !request->head && response_length != 0U)
        ok = send_all(request->client_fd, body.data, response_length);
    if (!ok || !keep_alive) {
        (void)close(request->client_fd);
        request->client_fd = -1;
    }
    request->responded = true;
    return ok;
}

static bool respond_html(HttpRequest *request, int status,
                         LangStringView body) {
    return respond_body_mode(
        request, status, body,
        "text/html; charset=utf-8", false);
}

static LangNativeResult http_server_open(LangVM *vm, const LangValue *args,
                                         size_t arg_count) {
    if ((arg_count != 2U && arg_count != 5U && arg_count != 6U) ||
        args[1].tag != LANG_VALUE_I64)
        return native_error(
            "http_server_open expects an address, port, and optional limits");
    LangStringView address;
    if (!lang_value_string_view(&args[0], &address) ||
        address.length == 0U || address.length >= 64U)
        return native_error("http_server_open requires a short IPv4 address");
    if (args[1].as.i64 < 0 || args[1].as.i64 > 65535)
        return native_error("HTTP port must be between 0 and 65535");
    int64_t header_limit = 16384;
    int64_t body_limit = 1048576;
    int64_t timeout_ms = 5000;
    int64_t keep_alive_limit = 1;
    if (arg_count >= 5U) {
        if (args[2].tag != LANG_VALUE_I64 ||
            args[3].tag != LANG_VALUE_I64 ||
            args[4].tag != LANG_VALUE_I64)
            return native_error(
                "configured HTTP limits must be i64 values");
        header_limit = args[2].as.i64;
        body_limit = args[3].as.i64;
        timeout_ms = args[4].as.i64;
    }
    if (arg_count == 6U) {
        if (args[5].tag != LANG_VALUE_I64)
            return native_error(
                "HTTP keep-alive request limit must be an i64");
        keep_alive_limit = args[5].as.i64;
    }
    if (header_limit < 1024 || header_limit > 65536)
        return native_error(
            "HTTP header limit must be between 1024 and 65536");
    if (body_limit < 0 || body_limit > 16777216)
        return native_error(
            "HTTP body limit must be between 0 and 16777216");
    if (timeout_ms < 1 || timeout_ms > 300000)
        return native_error(
            "HTTP timeout must be between 1 and 300000 milliseconds");
    if (keep_alive_limit < 1 || keep_alive_limit > 1000)
        return native_error(
            "HTTP keep-alive limit must be between 1 and 1000 requests");

    char address_text[64];
    memcpy(address_text, address.data, address.length);
    address_text[address.length] = '\0';
    struct in_addr ipv4;
    if (inet_pton(AF_INET, address_text, &ipv4) != 1)
        return native_error("invalid IPv4 listen address");

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) return native_error("could not create HTTP socket");
    int enabled = 1;
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
                     &enabled, (socklen_t)sizeof(enabled));
    struct sockaddr_in endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr = ipv4;
    endpoint.sin_port = htons((uint16_t)args[1].as.i64);
    if (bind(socket_fd, (const struct sockaddr *)&endpoint,
             (socklen_t)sizeof(endpoint)) != 0 ||
        listen(socket_fd, 16) != 0) {
        (void)close(socket_fd);
        return native_error("could not bind or listen on HTTP socket");
    }
    socklen_t endpoint_length = (socklen_t)sizeof(endpoint);
    if (getsockname(socket_fd, (struct sockaddr *)&endpoint,
                    &endpoint_length) != 0) {
        (void)close(socket_fd);
        return native_error("could not determine HTTP listen port");
    }
    HttpServer *server = calloc(1U, sizeof(*server));
    if (server == NULL) {
        (void)close(socket_fd);
        return native_error("out of memory creating HTTP server");
    }
    server->socket_fd = socket_fd;
    server->port = ntohs(endpoint.sin_port);
    server->magic = HTTP_SERVER_MAGIC;
    server->header_limit = (size_t)header_limit;
    server->body_limit = (size_t)body_limit;
    server->timeout_ms = (int)timeout_ms;
    server->keep_alive_limit = (size_t)keep_alive_limit;
    LangValue handle;
    if (!lang_native_handle_value(vm, server, http_server_drop, &handle)) {
        http_server_drop(server);
        return native_error("could not wrap HTTP server handle");
    }
    return (LangNativeResult){true, handle, NULL};
}

static HttpServer *get_server(const LangValue *value) {
    HttpServer *server = lang_native_handle_data(value);
    return server != NULL && server->magic == HTTP_SERVER_MAGIC ? server : NULL;
}

static LangNativeResult http_server_port(LangVM *vm, const LangValue *args,
                                         size_t arg_count) {
    (void)vm;
    if (arg_count != 1U) return native_error("http_server_port expects a server");
    HttpServer *server = get_server(&args[0]);
    if (server == NULL) return native_error("invalid HTTP server handle");
    return native_i64((int64_t)server->port);
}

static LangNativeResult http_accept_request(LangVM *vm,
                                            const LangValue *args,
                                            size_t arg_count) {
    if (arg_count != 1U) return native_error("http_accept expects a server");
    HttpServer *server = get_server(&args[0]);
    if (server == NULL) return native_error("invalid HTTP server handle");
    int error_status = 0;
    HttpRequest *request = accept_request(server, &error_status);
    if (request == NULL)
        return native_error(error_status == 431
            ? "HTTP request headers exceeded configured limit"
            : error_status == 413
              ? "HTTP request body exceeded configured limit"
              : "could not accept or parse HTTP request");
    LangValue handle;
    if (!lang_native_handle_value(vm, request, http_request_drop, &handle)) {
        http_request_drop(request);
        return native_error("could not wrap HTTP request handle");
    }
    return (LangNativeResult){true, handle, NULL};
}

static LangNativeResult borrowed_request_text(const LangValue *args,
                                              size_t arg_count,
                                              bool path) {
    if (arg_count != 1U)
        return native_error("HTTP request accessor expects one request");
    HttpRequest *request = get_request(&args[0]);
    if (request == NULL) return native_error("invalid HTTP request handle");
    const char *text = path ? request->path : request->method;
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={text, strlen(text)}},
        NULL
    };
}

static LangNativeResult http_request_method(LangVM *vm,
                                            const LangValue *args,
                                            size_t arg_count) {
    (void)vm;
    return borrowed_request_text(args, arg_count, false);
}

static LangNativeResult http_request_path(LangVM *vm, const LangValue *args,
                                          size_t arg_count) {
    (void)vm;
    return borrowed_request_text(args, arg_count, true);
}

static bool header_name_equal(const char *stored, LangStringView requested) {
    size_t length = strlen(stored);
    if (length != requested.length) return false;
    for (size_t i = 0U; i < length; ++i) {
        char value = requested.data[i];
        if (value >= 'A' && value <= 'Z')
            value = (char)(value - 'A' + 'a');
        if (stored[i] != value) return false;
    }
    return true;
}

static LangNativeResult http_request_header(LangVM *vm,
                                            const LangValue *args,
                                            size_t arg_count) {
    (void)vm;
    if (arg_count != 2U)
        return native_error("http_request_header expects `(request, name)`");
    HttpRequest *request = get_request(&args[0]);
    LangStringView name;
    if (request == NULL || !lang_value_string_view(&args[1], &name))
        return native_error("invalid HTTP request or header name");
    const char *value = "";
    for (size_t i = 0U; i < request->header_count; ++i)
        if (header_name_equal(request->headers[i].name, name))
            value = request->headers[i].value;
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={value, strlen(value)}},
        NULL
    };
}

static LangNativeResult http_request_headers(LangVM *vm,
                                             const LangValue *args,
                                             size_t arg_count) {
    (void)vm;
    if (arg_count != 1U)
        return native_error("http_request_headers expects one request");
    HttpRequest *request = get_request(&args[0]);
    if (request == NULL)
        return native_error("invalid HTTP request handle");
    const char *data = request->header_data != NULL
                     ? request->header_data : "";
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={data, request->header_data_length}},
        NULL
    };
}

static LangNativeResult http_request_body(LangVM *vm,
                                          const LangValue *args,
                                          size_t arg_count) {
    (void)vm;
    if (arg_count != 1U)
        return native_error("http_request_body expects one request");
    HttpRequest *request = get_request(&args[0]);
    if (request == NULL)
        return native_error("invalid HTTP request handle");
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={
             request->body != NULL ? request->body : "",
             request->body_length
         }},
        NULL
    };
}

static LangNativeResult http_request_remote_ip(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)vm;
    if (arg_count != 1U)
        return native_error("HttpRequestRemoteIpAddress expects one request");
    HttpRequest *request = get_request(&args[0]);
    if (request == NULL)
        return native_error("invalid HTTP request handle");
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={request->remote_ip, strlen(request->remote_ip)}},
        NULL
    };
}

static LangNativeResult http_request_next(LangVM *vm,
                                          const LangValue *args,
                                          size_t arg_count) {
    (void)vm;
    if (arg_count != 1U)
        return native_error("http_request_next expects one request");
    HttpRequest *request = get_request(&args[0]);
    if (request == NULL)
        return native_error("invalid HTTP request handle");
    if (request->client_fd < 0 || !request->responded ||
        request->streaming || request->has_surplus ||
        request->request_count >= request->request_limit)
        return (LangNativeResult){
            true,
            {.tag=LANG_VALUE_BOOL, .as.boolean=false},
            NULL
        };
    int error_status = 0;
    bool ok = read_request(request, &error_status);
    if (!ok) {
        (void)close(request->client_fd);
        request->client_fd = -1;
    }
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_BOOL, .as.boolean=ok},
        NULL
    };
}

static bool next_path_segment(LangStringView path, size_t *offset,
                              LangStringView *segment) {
    while (*offset < path.length && path.data[*offset] == '/')
        ++*offset;
    if (*offset >= path.length || path.data[*offset] == '?')
        return false;
    size_t start = *offset;
    while (*offset < path.length &&
           path.data[*offset] != '/' &&
           path.data[*offset] != '?')
        ++*offset;
    size_t length = *offset - start;
    *segment = (LangStringView){
        length == 0U ? NULL : path.data + start, length
    };
    return segment->length != 0U;
}

static bool path_segment_equal(LangStringView left,
                               LangStringView right) {
    return left.length == right.length &&
           (left.length == 0U ||
            memcmp(left.data, right.data, left.length) == 0);
}

static LangNativeResult http_path_matches(LangVM *vm,
                                          const LangValue *args,
                                          size_t arg_count) {
    (void)vm;
    LangStringView pattern;
    LangStringView path;
    if (arg_count != 2U ||
        !lang_value_string_view(&args[0], &pattern) ||
        !lang_value_string_view(&args[1], &path))
        return native_error(
            "http_path_matches expects `(pattern, path)`");
    size_t pattern_offset = 0U;
    size_t path_offset = 0U;
    bool matches = true;
    for (;;) {
        LangStringView pattern_segment;
        LangStringView path_segment;
        bool have_pattern = next_path_segment(
            pattern, &pattern_offset, &pattern_segment);
        bool have_path = next_path_segment(
            path, &path_offset, &path_segment);
        if (have_pattern != have_path) {
            matches = false;
            break;
        }
        if (!have_pattern) break;
        if (pattern_segment.data[0] != ':' &&
            !path_segment_equal(pattern_segment, path_segment)) {
            matches = false;
            break;
        }
    }
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_BOOL, .as.boolean=matches},
        NULL
    };
}

static LangNativeResult http_path_param(LangVM *vm,
                                        const LangValue *args,
                                        size_t arg_count) {
    (void)vm;
    LangStringView pattern;
    LangStringView path;
    LangStringView name;
    if (arg_count != 3U ||
        !lang_value_string_view(&args[0], &pattern) ||
        !lang_value_string_view(&args[1], &path) ||
        !lang_value_string_view(&args[2], &name))
        return native_error(
            "http_path_param expects `(pattern, path, name)`");
    size_t pattern_offset = 0U;
    size_t path_offset = 0U;
    LangStringView result = {"", 0U};
    for (;;) {
        LangStringView pattern_segment;
        LangStringView path_segment;
        bool have_pattern = next_path_segment(
            pattern, &pattern_offset, &pattern_segment);
        bool have_path = next_path_segment(
            path, &path_offset, &path_segment);
        if (have_pattern != have_path)
            return (LangNativeResult){
                true,
                {.tag=LANG_VALUE_STRING_VIEW,
                 .as.string={"", 0U}},
                NULL
            };
        if (!have_pattern) break;
        if (pattern_segment.data[0] != ':' &&
            !path_segment_equal(pattern_segment, path_segment))
            return (LangNativeResult){
                true,
                {.tag=LANG_VALUE_STRING_VIEW,
                 .as.string={"", 0U}},
                NULL
            };
        if (pattern_segment.length == name.length + 1U &&
            pattern_segment.data[0] == ':' &&
            (name.length == 0U ||
             memcmp(pattern_segment.data + 1U,
                    name.data, name.length) == 0))
            result = path_segment;
    }
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string=result},
        NULL
    };
}

static LangNativeResult http_respond_html(LangVM *vm, const LangValue *args,
                                          size_t arg_count) {
    (void)vm;
    if (arg_count != 3U || args[1].tag != LANG_VALUE_I64)
        return native_error(
            "http_respond_html expects `(request, status, body)`");
    HttpRequest *request = get_request(&args[0]);
    LangStringView body;
    if (request == NULL || request->client_fd < 0 ||
        request->responded ||
        !lang_value_string_view(&args[2], &body))
        return native_error("invalid HTTP request or response body");
    int64_t status = args[1].as.i64;
    if (status < 100 || status > 599 || status_reason((int)status) == NULL)
        return native_error("unsupported HTTP response status");
    return respond_html(request, (int)status, body)
         ? native_i64(status)
         : native_error("HTTP response write failed");
}

static LangNativeResult http_respond_html_reuse(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    if (arg_count != 3U || args[1].tag != LANG_VALUE_I64)
        return native_error(
            "http_respond_html_reuse expects `(request, status, body)`");
    HttpRequest *request = get_request(&args[0]);
    LangStringView body;
    if (request == NULL || request->client_fd < 0 ||
        request->responded ||
        !lang_value_string_view(&args[2], &body))
        return native_error("invalid HTTP request or response body");
    int64_t status = args[1].as.i64;
    if (status < 100 || status > 599 ||
        status_reason((int)status) == NULL)
        return native_error("unsupported HTTP response status");
    bool keep_alive =
        request->client_keep_alive &&
        !request->has_surplus &&
        request->request_count < request->request_limit;
    if (!respond_body_mode(
            request, (int)status, body,
            "text/html; charset=utf-8", keep_alive))
        return native_error("HTTP response write failed");
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_BOOL, .as.boolean=keep_alive},
        NULL
    };
}

static LangNativeResult http_respond_html_value_reuse(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    if (arg_count != 3U || args[1].tag != LANG_VALUE_I64)
        return native_error(
            "http_try_respond_html expects `(request, status, Html)`");
    HttpRequest *request = get_request(&args[0]);
    LangStringView body;
    if (request == NULL || request->client_fd < 0 ||
        request->responded ||
        !lang_value_html_view(&args[2], &body))
        return native_error("invalid HTTP request or Html response body");
    int64_t status = args[1].as.i64;
    if (status < 100 || status > 599 ||
        status_reason((int)status) == NULL)
        return native_error("unsupported HTTP response status");
    bool keep_alive =
        request->client_keep_alive &&
        !request->has_surplus &&
        request->request_count < request->request_limit;
    if (!respond_body_mode(
            request, (int)status, body,
            "text/html; charset=utf-8", keep_alive))
        return native_error("HTTP Html response write failed");
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_BOOL, .as.boolean=keep_alive},
        NULL
    };
}

static LangNativeResult http_respond_redirect_reuse(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    if (arg_count != 2U)
        return native_error(
            "http_respond_redirect_reuse expects `(request, location)`");
    HttpRequest *request = get_request(&args[0]);
    LangStringView location;
    if (request == NULL || request->client_fd < 0 ||
        request->responded ||
        !lang_value_string_view(&args[1], &location))
        return native_error("invalid HTTP redirect");
    bool keep_alive =
        request->client_keep_alive &&
        !request->has_surplus &&
        request->request_count < request->request_limit;
    if (!respond_redirect_mode(request, location, keep_alive))
        return native_error(
            "redirect location must be a short local path without newlines");
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_BOOL, .as.boolean=keep_alive},
        NULL
    };
}

static bool allowed_content_type(LangStringView content_type,
                                 const char **out_value) {
    static const char *const allowed[] = {
        "text/html; charset=utf-8",
        "text/plain; charset=utf-8",
        "text/css; charset=utf-8",
        "text/javascript; charset=utf-8",
        "application/json; charset=utf-8",
        "application/xml; charset=utf-8",
        "image/svg+xml",
        "image/png",
        "image/jpeg",
        "image/gif",
        "image/webp",
        "image/x-icon",
        "font/woff",
        "font/woff2",
        "font/ttf",
        "application/wasm",
        "application/octet-stream"
    };
    for (size_t i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        size_t length = strlen(allowed[i]);
        if (content_type.length == length &&
            memcmp(content_type.data, allowed[i], length) == 0) {
            *out_value = allowed[i];
            return true;
        }
    }
    return false;
}

static LangNativeResult http_respond_reuse(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    if (arg_count != 4U || args[1].tag != LANG_VALUE_I64)
        return native_error(
            "http_respond_reuse expects request, status, content type, body");
    HttpRequest *request = get_request(&args[0]);
    LangStringView content_type;
    LangStringView body;
    const char *safe_content_type = NULL;
    int64_t status = args[1].as.i64;
    if (request == NULL || request->client_fd < 0 ||
        request->responded ||
        status < 100 || status > 599 ||
        status_reason((int)status) == NULL ||
        !lang_value_string_view(&args[2], &content_type) ||
        !allowed_content_type(content_type, &safe_content_type) ||
        !lang_value_string_view(&args[3], &body))
        return native_error(
            "invalid HTTP response state, status, content type, or body");
    bool keep_alive =
        request->client_keep_alive &&
        !request->has_surplus &&
        request->request_count < request->request_limit;
    if (!respond_body_mode(
            request, (int)status, body,
            safe_content_type, keep_alive))
        return native_error("HTTP response write failed");
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_BOOL, .as.boolean=keep_alive},
        NULL
    };
}

static LangNativeResult http_respond_headers_reuse(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)vm;
    if (arg_count != 5U || args[1].tag != LANG_VALUE_I64)
        return native_error(
            "http response with headers expects request, status, content type, headers, body");
    HttpRequest *request = get_request(&args[0]);
    LangStringView content_type;
    LangStringView headers;
    LangStringView body;
    const char *safe_content_type = NULL;
    int64_t status = args[1].as.i64;
    if (request == NULL || request->client_fd < 0 || request->responded ||
        status < 100 || status > 599 ||
        status_reason((int)status) == NULL ||
        !lang_value_string_view(&args[2], &content_type) ||
        !allowed_content_type(content_type, &safe_content_type) ||
        !lang_value_string_view(&args[3], &headers) ||
        !lang_value_string_view(&args[4], &body) ||
        !extra_response_headers_valid(headers))
        return native_error("invalid HTTP response headers or body");
    bool keep_alive = request->client_keep_alive &&
        !request->has_surplus &&
        request->request_count < request->request_limit;
    if (!respond_body_headers_mode(
            request, (int)status, body, safe_content_type,
            headers, keep_alive))
        return native_error("HTTP response write failed");
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_BOOL, .as.boolean=keep_alive}, NULL
    };
}

static LangNativeResult http_respond_html_headers_reuse(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)vm;
    if (arg_count != 4U || args[1].tag != LANG_VALUE_I64)
        return native_error(
            "HTML response with headers expects request, status, headers, body");
    HttpRequest *request = get_request(&args[0]);
    LangStringView headers;
    LangStringView body;
    int64_t status = args[1].as.i64;
    if (request == NULL || request->client_fd < 0 || request->responded ||
        status < 100 || status > 599 ||
        status_reason((int)status) == NULL ||
        !lang_value_string_view(&args[2], &headers) ||
        !lang_value_html_view(&args[3], &body) ||
        !extra_response_headers_valid(headers))
        return native_error("invalid HTTP HTML response headers or body");
    bool keep_alive = request->client_keep_alive &&
        !request->has_surplus &&
        request->request_count < request->request_limit;
    if (!respond_body_headers_mode(
            request, (int)status, body, "text/html; charset=utf-8",
            headers, keep_alive))
        return native_error("HTTP HTML response write failed");
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_BOOL, .as.boolean=keep_alive}, NULL
    };
}

static LangNativeResult http_stream_begin(LangVM *vm,
                                          const LangValue *args,
                                          size_t arg_count) {
    (void)vm;
    if (arg_count != 3U || args[1].tag != LANG_VALUE_I64)
        return native_error(
            "http_stream_begin expects `(request, status, content_type)`");
    HttpRequest *request = get_request(&args[0]);
    LangStringView content_type;
    const char *safe_content_type = NULL;
    const char *reason = status_reason((int)args[1].as.i64);
    if (request == NULL || request->client_fd < 0 ||
        request->responded || reason == NULL ||
        !lang_value_string_view(&args[2], &content_type) ||
        !allowed_content_type(content_type, &safe_content_type))
        return native_error(
            "invalid HTTP stream state, status, or content type");
    char header[512];
    int length = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n\r\n",
        (int)args[1].as.i64, reason, safe_content_type);
    if (length <= 0 || (size_t)length >= sizeof(header) ||
        !send_all(request->client_fd, header, (size_t)length))
        return native_error("HTTP stream header write failed");
    request->streaming = true;
    request->responded = true;
    return native_i64(args[1].as.i64);
}

static LangNativeResult http_stream_begin_headers(LangVM *vm,
                                                  const LangValue *args,
                                                  size_t arg_count) {
    (void)vm;
    if (arg_count != 4U || args[1].tag != LANG_VALUE_I64)
        return native_error(
            "http_stream_begin_headers expects "
            "`(request, status, content_type, headers)`");
    HttpRequest *request = get_request(&args[0]);
    LangStringView content_type;
    LangStringView headers;
    const char *safe_content_type = NULL;
    const char *reason = status_reason((int)args[1].as.i64);
    if (request == NULL || request->client_fd < 0 ||
        request->responded || reason == NULL ||
        !lang_value_string_view(&args[2], &content_type) ||
        !lang_value_string_view(&args[3], &headers) ||
        !allowed_content_type(content_type, &safe_content_type) ||
        !extra_response_headers_valid(headers))
        return native_error(
            "invalid HTTP stream state, status, content type, or headers");
    char prefix[512];
    int prefix_length = snprintf(
        prefix, sizeof(prefix),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Transfer-Encoding: chunked\r\n",
        (int)args[1].as.i64, reason, safe_content_type);
    static const char suffix[] =
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n\r\n";
    bool ok =
        prefix_length > 0 &&
        (size_t)prefix_length < sizeof(prefix) &&
        send_all(request->client_fd, prefix, (size_t)prefix_length) &&
        (headers.length == 0U ||
         send_all(request->client_fd, headers.data, headers.length)) &&
        send_all(request->client_fd, suffix, sizeof(suffix) - 1U);
    if (!ok)
        return native_error("HTTP stream header write failed");
    request->streaming = true;
    request->responded = true;
    return native_i64(args[1].as.i64);
}

static LangNativeResult http_stream_chunk(LangVM *vm,
                                          const LangValue *args,
                                          size_t arg_count) {
    (void)vm;
    if (arg_count != 2U)
        return native_error(
            "http_stream_chunk expects `(request, data)`");
    HttpRequest *request = get_request(&args[0]);
    LangStringView data;
    if (request == NULL || request->client_fd < 0 ||
        !request->streaming ||
        !lang_value_string_view(&args[1], &data))
        return native_error("invalid HTTP stream or chunk");
    if (request->head || data.length == 0U)
        return native_i64((int64_t)data.length);
    char prefix[32];
    int prefix_length = snprintf(
        prefix, sizeof(prefix), "%zx\r\n", data.length);
    bool ok =
        prefix_length > 0 &&
        (size_t)prefix_length < sizeof(prefix) &&
        send_all(request->client_fd, prefix,
                 (size_t)prefix_length) &&
        send_all(request->client_fd, data.data, data.length) &&
        send_all(request->client_fd, "\r\n", 2U);
    return ok
         ? native_i64((int64_t)data.length)
         : native_error("HTTP stream chunk write failed");
}

static LangNativeResult http_stream_finish(LangVM *vm,
                                           const LangValue *args,
                                           size_t arg_count) {
    (void)vm;
    if (arg_count != 1U)
        return native_error(
            "http_stream_finish expects one request");
    HttpRequest *request = get_request(&args[0]);
    if (request == NULL || request->client_fd < 0 ||
        !request->streaming)
        return native_error("invalid HTTP stream");
    bool ok = request->head ||
              send_all(request->client_fd, "0\r\n\r\n", 5U);
    (void)close(request->client_fd);
    request->client_fd = -1;
    request->streaming = false;
    return ok ? native_i64(0)
              : native_error("HTTP stream finish failed");
}

static LangNativeResult http_serve_once(LangVM *vm, const LangValue *args,
                                        size_t arg_count) {
    (void)vm;
    if (arg_count != 2U)
        return native_error("http_serve_once expects `(server, body)`");
    HttpServer *server = get_server(&args[0]);
    LangStringView body;
    if (server == NULL || !lang_value_string_view(&args[1], &body))
        return native_error("invalid HTTP server or response body");

    int error_status = 0;
    HttpRequest *request = accept_request(server, &error_status);
    if (request == NULL)
        return error_status == 431 ? native_i64(431)
                                  : native_error("HTTP accept or parse failed");
    bool head = strcmp(request->method, "HEAD") == 0;
    bool get = strcmp(request->method, "GET") == 0;
    if (!get && !head) {
        (void)send_empty_status(request->client_fd, 405,
                                "Method Not Allowed", "Allow: GET, HEAD\r\n");
        http_request_drop(request);
        return native_i64(405);
    }
    bool ok = respond_html(request, 200, body);
    http_request_drop(request);
    return ok ? native_i64(200) : native_error("HTTP response write failed");
}

#endif

static LangNativeResult wrap_http_result(
    LangVM *vm, LangNativeResult operation) {
    LangValue tagged;
    if (operation.ok) {
        LangValue value = operation.value;
        if (lang_result_ok_value(vm, value, &tagged))
            return (LangNativeResult){true, tagged, NULL};
        lang_value_drop(vm, &value);
        return native_error("could not allocate HTTP success Result");
    }
    const char *message = lang_native_result_error_message(&operation);
    if (message == NULL) message = "HTTP operation failed";
    LangValue error;
    if (!lang_string_value(
            vm, (LangStringView){message, strlen(message)}, &error)) {
        lang_native_result_drop(&operation);
        return native_error("could not copy HTTP error diagnostic");
    }
    lang_native_result_drop(&operation);
    if (!lang_result_err_value(vm, error, &tagged)) {
        lang_value_drop(vm, &error);
        return native_error("could not allocate HTTP error Result");
    }
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult http_try_server_open(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return wrap_http_result(
        vm, http_server_open(vm, args, arg_count));
}

static LangNativeResult http_try_accept(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return wrap_http_result(
        vm, http_accept_request(vm, args, arg_count));
}

static LangNativeResult http_try_request_next(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return wrap_http_result(
        vm, http_request_next(vm, args, arg_count));
}

static LangNativeResult http_try_respond_html_reuse(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return wrap_http_result(
        vm, http_respond_html_reuse(vm, args, arg_count));
}

static LangNativeResult http_try_respond_html(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return wrap_http_result(
        vm, http_respond_html_value_reuse(vm, args, arg_count));
}

static LangNativeResult http_try_respond_redirect_reuse(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return wrap_http_result(
        vm, http_respond_redirect_reuse(vm, args, arg_count));
}

static LangNativeResult http_try_respond_reuse(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return wrap_http_result(
        vm, http_respond_reuse(vm, args, arg_count));
}

static LangNativeResult http_try_respond_headers_reuse(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    return wrap_http_result(
        vm, http_respond_headers_reuse(vm, args, arg_count));
}

static LangNativeResult http_try_respond_html_headers_reuse(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    return wrap_http_result(
        vm, http_respond_html_headers_reuse(vm, args, arg_count));
}

void lang_register_http_natives(LangVM *vm) {
    (void)lang_register_native(vm, "HttpServerOpen", http_server_open, 2U);
    (void)lang_register_native(
        vm, "HttpServerOpenConfig", http_server_open, 5U);
    (void)lang_register_native(
        vm, "HttpServerOpenKeepAlive", http_server_open, 6U);
    (void)lang_register_native(
        vm, "HttpTryServerOpen", http_try_server_open, 6U);
    (void)lang_register_native(vm, "HttpServerPort", http_server_port, 1U);
    (void)lang_register_native(vm, "HttpServeOnce", http_serve_once, 2U);
    (void)lang_register_native(vm, "HttpAccept", http_accept_request, 1U);
    (void)lang_register_native(vm, "HttpTryAccept",
                               http_try_accept, 1U);
    (void)lang_register_native(vm, "HttpRequestMethod",
                               http_request_method, 1U);
    (void)lang_register_native(vm, "HttpRequestPath",
                               http_request_path, 1U);
    (void)lang_register_native(vm, "HttpRequestHeader",
                               http_request_header, 2U);
    (void)lang_register_native(vm, "HttpRequestHeaders",
                               http_request_headers, 1U);
    (void)lang_register_native(vm, "HttpRequestBody",
                               http_request_body, 1U);
    (void)lang_register_native(vm, "HttpRequestRemoteIpAddress",
                               http_request_remote_ip, 1U);
    (void)lang_register_native(vm, "HttpRequestNext",
                               http_request_next, 1U);
    (void)lang_register_native(vm, "HttpTryRequestNext",
                               http_try_request_next, 1U);
    (void)lang_register_native(vm, "HttpPathMatches",
                               http_path_matches, 2U);
    (void)lang_register_native(vm, "HttpPathParam",
                               http_path_param, 3U);
    (void)lang_register_native(vm, "HttpFormValue",
                               http_form_value, 2U);
    (void)lang_register_native(vm, "HttpRespondHtml",
                               http_respond_html, 3U);
    (void)lang_register_native(vm, "HttpRespondHtmlReuse",
                               http_respond_html_reuse, 3U);
    (void)lang_register_native(vm, "HttpTryRespondHtmlReuse",
                               http_try_respond_html_reuse, 3U);
    (void)lang_register_native(vm, "HttpTryRespondHtml",
                               http_try_respond_html, 3U);
    (void)lang_register_native(vm, "HttpTryRespondRedirectReuse",
                               http_try_respond_redirect_reuse, 2U);
    (void)lang_register_native(vm, "HttpRespondReuse",
                               http_respond_reuse, 4U);
    (void)lang_register_native(vm, "HttpTryRespondReuse",
                               http_try_respond_reuse, 4U);
    (void)lang_register_native(vm, "HttpTryRespondHeadersReuse",
                               http_try_respond_headers_reuse, 5U);
    (void)lang_register_native(vm, "HttpTryRespondHtmlHeadersReuse",
                               http_try_respond_html_headers_reuse, 4U);
    (void)lang_register_native(vm, "HttpStreamBegin",
                               http_stream_begin, 3U);
    (void)lang_register_native(vm, "HttpStreamBeginHeaders",
                               http_stream_begin_headers, 4U);
    (void)lang_register_native(vm, "HttpStreamChunk",
                               http_stream_chunk, 2U);
    (void)lang_register_native(vm, "HttpStreamFinish",
                               http_stream_finish, 1U);
}
