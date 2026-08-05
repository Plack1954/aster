#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "lang/lang.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool send_all(int socket_fd, const char *data, size_t length) {
    while (length != 0U) {
        ssize_t sent = send(socket_fd, data, length, 0);
        if (sent <= 0) return false;
        data += (size_t)sent;
        length -= (size_t)sent;
    }
    return true;
}

static int verify_response(uint16_t port) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) return 1;
    struct sockaddr_in endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket_fd, (const struct sockaddr *)&endpoint,
                (socklen_t)sizeof(endpoint)) != 0) {
        (void)close(socket_fd);
        return 2;
    }
    static const char request[] =
        "GET /async/42 HTTP/1.1\r\n"
        "Host: lime.test\r\n"
        "Connection: close\r\n"
        "Content-Length: 0\r\n\r\n";
    if (!send_all(socket_fd, request, sizeof(request) - 1U)) {
        (void)close(socket_fd);
        return 3;
    }
    char response[2048];
    size_t used = 0U;
    while (used < sizeof(response) - 1U) {
        ssize_t received = recv(
            socket_fd, response + used, sizeof(response) - 1U - used, 0);
        if (received < 0) {
            (void)close(socket_fd);
            return 4;
        }
        if (received == 0) break;
        used += (size_t)received;
    }
    (void)close(socket_fd);
    response[used] = '\0';
    return strstr(response, "HTTP/1.1 200 OK\r\n") != NULL &&
           strstr(response,
                  "Content-Type: text/plain; charset=utf-8\r\n") != NULL &&
           strstr(response, "\r\n\r\nasync:42") != NULL ? 0 : 5;
}
#endif

int main(int argc, char **argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    return 0;
#else
    if (argc != 2) return 6;
    int descriptors[2];
    if (pipe(descriptors) != 0) return 7;
    pid_t child = fork();
    if (child < 0) return 8;
    if (child == 0) {
        (void)close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(9);
        (void)close(descriptors[1]);
        if (strcmp(argv[1], "vm") == 0) {
            (void)setvbuf(stdout, NULL, _IONBF, 0);
            _exit(lang_project_run_ir(
                "packages/lime/aster.toml", "async_http_server"));
        }
        execl(argv[1], argv[1], (char *)NULL);
        _exit(10);
    }
    (void)close(descriptors[1]);
    FILE *ports = fdopen(descriptors[0], "r");
    if (ports == NULL) return 11;
    char line[64];
    if (fgets(line, sizeof(line), ports) == NULL) {
        (void)fclose(ports);
        (void)kill(child, SIGTERM);
        (void)waitpid(child, NULL, 0);
        return 12;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(line, &end, 10);
    int result = end == line || parsed == 0UL || parsed > UINT16_MAX
        ? 13 : verify_response((uint16_t)parsed);
    (void)fclose(ports);
    if (result != 0) (void)kill(child, SIGTERM);
    int status = 0;
    bool child_ok = waitpid(child, &status, 0) == child &&
        WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return child_ok ? result : 14;
#endif
}
