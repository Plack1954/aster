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

static int request_client(uint16_t port) {
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
    static const char request[] =
        "POST /users?id=7 HTTP/1.1\r\n"
        "Host: example.test\r\n"
        "X-Trace: aster\r\n"
        "Content-Length: 11\r\n\r\n"
        "hello world";
    if (send(fd, request, sizeof(request) - 1U, 0) < 0) return 3;
    char response[2048];
    size_t used = 0U;
    for (;;) {
        ssize_t count = recv(fd, response + used,
                             sizeof(response) - 1U - used, 0);
        if (count <= 0) break;
        used += (size_t)count;
        if (used == sizeof(response) - 1U) break;
    }
    (void)close(fd);
    response[used] = '\0';
    if (strstr(response, "HTTP/1.1 201 Created") == NULL ||
        strstr(response, "Transfer-Encoding: chunked") == NULL ||
        strstr(response, "5\r\nhello\r\n") == NULL ||
        strstr(response, "6\r\n world\r\n") == NULL ||
        strstr(response, "0\r\n\r\n") == NULL)
        return 4;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 5;
    if (connect(fd, (const struct sockaddr *)&endpoint,
                (socklen_t)sizeof(endpoint)) != 0)
        return 6;
    static const char oversized[] =
        "POST /large HTTP/1.1\r\n"
        "Host: example.test\r\n"
        "Content-Length: 65\r\n\r\n";
    if (send(fd, oversized, sizeof(oversized) - 1U, 0) < 0)
        return 7;
    used = 0U;
    for (;;) {
        ssize_t count = recv(
            fd, response + used,
            sizeof(response) - 1U - used, 0);
        if (count <= 0) break;
        used += (size_t)count;
        if (used == sizeof(response) - 1U) break;
    }
    (void)close(fd);
    response[used] = '\0';
    return strstr(
        response, "HTTP/1.1 413 Payload Too Large") != NULL
        ? 0 : 8;
}

static bool view_equals(LangNativeResult result, const char *expected) {
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
    LangValue open_args[5] = {
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={address, sizeof(address) - 1U}},
        {.tag=LANG_VALUE_I64, .as.i64=0},
        {.tag=LANG_VALUE_I64, .as.i64=4096},
        {.tag=LANG_VALUE_I64, .as.i64=64},
        {.tag=LANG_VALUE_I64, .as.i64=1000}
    };
    LangNativeResult server;
    if (!lang_vm_call_native(
            vm, "HttpServerOpenConfig",
            open_args, 5U, &server) ||
        !server.ok)
        return 10;
    LangNativeResult port;
    if (!lang_vm_call_native(vm, "HttpServerPort", &server.value, 1U, &port) ||
        !port.ok)
        return 11;
    pid_t child = fork();
    if (child < 0) return 12;
    if (child == 0) _exit(request_client((uint16_t)port.value.as.i64));

    LangNativeResult request;
    if (!lang_vm_call_native(vm, "HttpAccept", &server.value, 1U, &request) ||
        !request.ok)
        return 13;
    LangNativeResult method;
    LangNativeResult path;
    if (!lang_vm_call_native(vm, "HttpRequestMethod", &request.value, 1U,
                             &method) ||
        !lang_vm_call_native(vm, "HttpRequestPath", &request.value, 1U,
                             &path) ||
        !view_equals(method, "POST") ||
        !view_equals(path, "/users?id=7"))
        return 14;
    static const char host_name[] = "HOST";
    LangValue header_args[2] = {
        request.value,
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={host_name, sizeof(host_name) - 1U}}
    };
    LangNativeResult host;
    if (!lang_vm_call_native(vm, "HttpRequestHeader", header_args, 2U,
                             &host) ||
        !view_equals(host, "example.test"))
        return 15;
    LangNativeResult request_body;
    if (!lang_vm_call_native(
            vm, "HttpRequestBody", &request.value, 1U,
            &request_body) ||
        !view_equals(request_body, "hello world"))
        return 16;
    static const char content_type[] =
        "text/plain; charset=utf-8";
    LangValue begin_args[3] = {
        request.value,
        {.tag=LANG_VALUE_I64, .as.i64=201},
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={
             content_type, sizeof(content_type) - 1U
         }}
    };
    LangNativeResult streamed;
    if (!lang_vm_call_native(
            vm, "HttpStreamBegin", begin_args, 3U,
            &streamed) || !streamed.ok)
        return 17;
    static const char first_chunk[] = "hello";
    static const char second_chunk[] = " world";
    LangValue chunk_args[2] = {
        request.value,
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={
             first_chunk, sizeof(first_chunk) - 1U
         }}
    };
    if (!lang_vm_call_native(
            vm, "HttpStreamChunk", chunk_args, 2U,
            &streamed) || !streamed.ok)
        return 18;
    chunk_args[1].as.string.data = second_chunk;
    chunk_args[1].as.string.length =
        sizeof(second_chunk) - 1U;
    if (!lang_vm_call_native(
            vm, "HttpStreamChunk", chunk_args, 2U,
            &streamed) || !streamed.ok ||
        !lang_vm_call_native(
            vm, "HttpStreamFinish",
            &request.value, 1U, &streamed) ||
        !streamed.ok)
        return 19;
    lang_value_drop(vm, &request.value);
    LangNativeResult rejected_request;
    if (!lang_vm_call_native(
            vm, "HttpAccept", &server.value, 1U,
            &rejected_request) ||
        rejected_request.ok)
        return 20;
    int child_status = 0;
    if (waitpid(child, &child_status, 0) != child ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
        return 21;
    lang_value_drop(vm, &server.value);
    lang_vm_free(vm);
    return 0;
#endif
}
