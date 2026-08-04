#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "lang/lang.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static size_t header_end(const char *data, size_t length) {
    for (size_t i = 3U; i < length; ++i)
        if (data[i - 3U] == '\r' && data[i - 2U] == '\n' &&
            data[i - 1U] == '\r' && data[i] == '\n')
            return i + 1U;
    return 0U;
}

static bool read_response(int fd, const char *body,
                          const char *content_type,
                          const char *connection) {
    char response[2048];
    size_t used = 0U;
    size_t headers = 0U;
    size_t expected = 0U;
    while (used < sizeof(response) - 1U) {
        ssize_t count = recv(
            fd, response + used, sizeof(response) - 1U - used, 0);
        if (count <= 0) return false;
        used += (size_t)count;
        response[used] = '\0';
        if (headers == 0U) {
            headers = header_end(response, used);
            if (headers != 0U) {
                const char *length = strstr(response, "Content-Length: ");
                if (length == NULL) return false;
                const char *length_value =
                    length + strlen("Content-Length: ");
                char *end = NULL;
                unsigned long parsed = strtoul(
                    length_value, &end, 10);
                if (end == length_value || parsed > SIZE_MAX - headers)
                    return false;
                expected = headers + (size_t)parsed;
            }
        }
        if (expected != 0U && used >= expected) break;
    }
    char connection_line[64];
    size_t connection_length = strlen(connection);
    static const char prefix[] = "Connection: ";
    if (sizeof(prefix) - 1U + connection_length + 2U >=
        sizeof(connection_line))
        return false;
    memcpy(connection_line, prefix, sizeof(prefix) - 1U);
    memcpy(connection_line + sizeof(prefix) - 1U,
           connection, connection_length);
    memcpy(connection_line + sizeof(prefix) - 1U + connection_length,
           "\r\n", 3U);
    return strstr(response, "HTTP/1.1 200 OK\r\n") != NULL &&
           strstr(response, content_type) != NULL &&
           strstr(response, connection_line) != NULL &&
           strstr(response, body) != NULL;
}

static int run_client(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    struct sockaddr_in endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (const struct sockaddr *)&endpoint,
                (socklen_t)sizeof(endpoint)) != 0)
        return 2;
    static const char first[] =
        "GET /first HTTP/1.1\r\nHost: keep.test\r\n\r\n";
    if (send(fd, first, sizeof(first) - 1U, 0) !=
        (ssize_t)(sizeof(first) - 1U))
        return 3;
    if (!read_response(
            fd, "body {}", "Content-Type: text/css; charset=utf-8",
            "keep-alive"))
        return 4;
    static const char second[] =
        "GET /second HTTP/1.1\r\nHost: keep.test\r\n\r\n";
    if (send(fd, second, sizeof(second) - 1U, 0) !=
        (ssize_t)(sizeof(second) - 1U))
        return 5;
    if (!read_response(
            fd, "second", "Content-Type: text/html; charset=utf-8",
            "close"))
        return 6;
    char byte;
    if (recv(fd, &byte, 1U, 0) != 0) return 7;
    (void)close(fd);
    return 0;
}

static bool view_is(LangNativeResult result, const char *expected) {
    LangStringView view;
    return result.ok && lang_value_string_view(&result.value, &view) &&
           view.length == strlen(expected) &&
           memcmp(view.data, expected, view.length) == 0;
}
#endif

int main(void) {
#if defined(_WIN32)
    return 0;
#else
    LangVM *vm = lang_vm_new();
    lang_register_http_natives(vm);
    static const char address[] = "127.0.0.1";
    LangValue open_args[6] = {
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={address, sizeof(address) - 1U}},
        {.tag=LANG_VALUE_I64, .as.i64=0},
        {.tag=LANG_VALUE_I64, .as.i64=4096},
        {.tag=LANG_VALUE_I64, .as.i64=1024},
        {.tag=LANG_VALUE_I64, .as.i64=1000},
        {.tag=LANG_VALUE_I64, .as.i64=2}
    };
    LangNativeResult server;
    if (!lang_vm_call_native(
            vm, "HttpServerOpenKeepAlive",
            open_args, 6U, &server) || !server.ok)
        return 10;
    LangNativeResult port;
    if (!lang_vm_call_native(
            vm, "HttpServerPort", &server.value, 1U, &port) ||
        !port.ok)
        return 11;
    pid_t child = fork();
    if (child < 0) return 12;
    if (child == 0)
        _exit(run_client((uint16_t)port.value.as.i64));

    LangNativeResult request;
    if (!lang_vm_call_native(
            vm, "HttpAccept", &server.value, 1U, &request) ||
        !request.ok)
        return 13;
    LangNativeResult path;
    if (!lang_vm_call_native(
            vm, "HttpRequestPath", &request.value, 1U, &path) ||
        !view_is(path, "/first"))
        return 14;
    static const char css_type[] = "text/css; charset=utf-8";
    static const char first_body[] = "body {}";
    LangValue response_args[4] = {
        request.value,
        {.tag=LANG_VALUE_I64, .as.i64=200},
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={css_type, sizeof(css_type) - 1U}},
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={first_body, sizeof(first_body) - 1U}}
    };
    LangNativeResult reuse;
    if (!lang_vm_call_native(
            vm, "HttpRespondReuse",
            response_args, 4U, &reuse) ||
        !reuse.ok || reuse.value.tag != LANG_VALUE_BOOL ||
        !reuse.value.as.boolean)
        return 15;
    LangNativeResult next;
    if (!lang_vm_call_native(
            vm, "HttpRequestNext", &request.value, 1U, &next) ||
        !next.ok || next.value.tag != LANG_VALUE_BOOL ||
        !next.value.as.boolean)
        return 16;
    if (!lang_vm_call_native(
            vm, "HttpRequestPath", &request.value, 1U, &path) ||
        !view_is(path, "/second"))
        return 17;
    static const char second_body[] = "second";
    response_args[2] = response_args[3];
    response_args[2].as.string.data = second_body;
    response_args[2].as.string.length = sizeof(second_body) - 1U;
    if (!lang_vm_call_native(
            vm, "HttpRespondHtmlReuse",
            response_args, 3U, &reuse) ||
        !reuse.ok || reuse.value.tag != LANG_VALUE_BOOL ||
        reuse.value.as.boolean)
        return 18;
    lang_value_drop(vm, &request.value);
    int child_status = 0;
    if (waitpid(child, &child_status, 0) != child ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
        return 19;
    lang_value_drop(vm, &server.value);
    lang_vm_free(vm);
    return 0;
#endif
}
