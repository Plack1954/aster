#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "lang/lang.h"

#include <stdbool.h>
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

static int client_request(uint16_t port, bool head) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) return 1;
    struct sockaddr_in endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket_fd, (const struct sockaddr *)&endpoint,
                (socklen_t)sizeof(endpoint)) != 0)
        return 2;
    const char *request = head
        ? "HEAD / HTTP/1.1\r\nHost: localhost\r\n\r\n"
        : "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    if (send(socket_fd, request, strlen(request), 0) < 0) return 3;
    char response[4096];
    size_t used = 0U;
    for (;;) {
        ssize_t received = recv(socket_fd, response + used,
                                sizeof(response) - 1U - used, 0);
        if (received <= 0) break;
        used += (size_t)received;
        if (used == sizeof(response) - 1U) break;
    }
    (void)close(socket_fd);
    response[used] = '\0';
    if (strstr(response, "HTTP/1.1 200 OK") == NULL ||
        strstr(response, "Content-Length: 17") == NULL)
        return 4;
    bool has_body = strstr(response, "<h1>loopback</h1>") != NULL;
    return has_body == !head ? 0 : 5;
}

static int serve_request(LangVM *vm, LangValue server, uint16_t port,
                         bool head) {
    pid_t child = fork();
    if (child < 0) return 10;
    if (child == 0) _exit(client_request(port, head));
    const char body_text[] = "<h1>loopback</h1>";
    LangValue args[2] = {
        server,
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={body_text, sizeof(body_text) - 1U}}
    };
    LangNativeResult result;
    if (!lang_vm_call_native(vm, "HttpServeOnce", args, 2U, &result) ||
        !result.ok || result.value.tag != LANG_VALUE_I64 ||
        result.value.as.i64 != 200)
        return 11;
    int child_status = 0;
    if (waitpid(child, &child_status, 0) != child ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
        return 12;
    return 0;
}
#endif

int main(void) {
#if defined(_WIN32)
    return 0;
#else
    LangVM *vm = lang_vm_new();
    if (vm == NULL) return 20;
    lang_register_http_natives(vm);
    const char address_text[] = "127.0.0.1";
    LangValue open_args[2] = {
        {.tag=LANG_VALUE_STRING_VIEW,
         .as.string={address_text, sizeof(address_text) - 1U}},
        {.tag=LANG_VALUE_I64, .as.i64=0}
    };
    LangNativeResult opened;
    if (!lang_vm_call_native(vm, "HttpServerOpen", open_args, 2U, &opened) ||
        !opened.ok)
        return 21;
    LangNativeResult port_result;
    if (!lang_vm_call_native(vm, "HttpServerPort", &opened.value, 1U,
                             &port_result) ||
        !port_result.ok || port_result.value.tag != LANG_VALUE_I64)
        return 22;
    uint16_t port = (uint16_t)port_result.value.as.i64;
    int status = serve_request(vm, opened.value, port, false);
    if (status == 0) status = serve_request(vm, opened.value, port, true);
    lang_value_drop(vm, &opened.value);
    lang_vm_free(vm);
    return status;
#endif
}
