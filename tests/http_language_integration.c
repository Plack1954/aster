#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "lang/lang.h"

#include <stddef.h>
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

static int request(uint16_t port, const char *path, int expected_status,
                   const char *expected_body) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    struct sockaddr_in endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (const struct sockaddr *)&endpoint,
                (socklen_t)sizeof(endpoint)) != 0) {
        (void)close(fd);
        return 2;
    }
    char message[512];
    int length = snprintf(
        message, sizeof(message),
        "GET %s HTTP/1.1\r\nHost: language.test\r\n\r\n", path);
    if (length < 0 || (size_t)length >= sizeof(message) ||
        send(fd, message, (size_t)length, 0) != length) {
        (void)close(fd);
        return 3;
    }
    char response[2048];
    size_t used = 0U;
    while (used < sizeof(response) - 1U) {
        ssize_t received = recv(
            fd, response + used, sizeof(response) - 1U - used, 0);
        if (received <= 0) break;
        used += (size_t)received;
    }
    (void)close(fd);
    response[used] = '\0';
    char status[64];
    (void)snprintf(
        status, sizeof(status), "HTTP/1.1 %d ", expected_status);
    return strstr(response, status) != NULL &&
        strstr(response, expected_body) != NULL ? 0 : 4;
}
#endif

int main(int argc, char **argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    return 0;
#else
    bool use_ir = argc == 2 && strcmp(argv[1], "ir") == 0;
    if (argc > 2 || (argc == 2 && !use_ir)) return 9;
    int descriptors[2];
    if (pipe(descriptors) != 0) return 10;
    pid_t child = fork();
    if (child < 0) return 11;
    if (child == 0) {
        (void)close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(12);
        (void)close(descriptors[1]);
        (void)setvbuf(stdout, NULL, _IONBF, 0);
        _exit(lang_run_file(
            "tests/http_language_server.lang", false,
            use_ir ? "run-ir" : NULL));
    }
    (void)close(descriptors[1]);
    FILE *ports = fdopen(descriptors[0], "r");
    if (ports == NULL) return 13;
    char line[64];
    if (fgets(line, sizeof(line), ports) == NULL) {
        (void)fclose(ports);
        return 14;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(line, &end, 10);
    if (end == line || parsed == 0UL || parsed > UINT16_MAX) {
        (void)fclose(ports);
        return 15;
    }
    uint16_t port = (uint16_t)parsed;
    int status = request(
        port, "/health", 200, "<strong>healthy</strong>");
    if (status == 0)
        status = request(
            port, "/missing", 404, "<h2>Not found</h2>");
    if (status == 0)
        status = request(
            port, "/redirect", 303, "Location: /health\r\n");
    (void)fclose(ports);
    if (status != 0)
        (void)kill(child, SIGTERM);
    int child_status = 0;
    if (waitpid(child, &child_status, 0) != child ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
        return 16;
    return status;
#endif
}
