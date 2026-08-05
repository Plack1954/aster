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
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool send_all_client(int fd, const char *data, size_t length) {
    while (length != 0U) {
        ssize_t sent = send(fd, data, length, 0);
        if (sent <= 0) return false;
        data += (size_t)sent;
        length -= (size_t)sent;
    }
    return true;
}

static size_t response_header_end(const char *data, size_t length) {
    for (size_t i = 3U; i < length; ++i)
        if (data[i - 3U] == '\r' && data[i - 2U] == '\n' &&
            data[i - 1U] == '\r' && data[i] == '\n')
            return i + 1U;
    return 0U;
}

static bool read_response(int fd, const char *content_type,
                          const char *body_fragment,
                          const char *connection) {
    char response[8192];
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
            headers = response_header_end(response, used);
            if (headers != 0U) {
                const char *field =
                    strstr(response, "Content-Length: ");
                if (field == NULL) return false;
                const char *number =
                    field + strlen("Content-Length: ");
                char *end = NULL;
                unsigned long length = strtoul(number, &end, 10);
                if (end == number || length > SIZE_MAX - headers)
                    return false;
                expected = headers + (size_t)length;
            }
        }
        if (expected != 0U && used >= expected) break;
    }
    char connection_field[64];
    int written = snprintf(
        connection_field, sizeof(connection_field),
        "Connection: %s\r\n", connection);
    return written > 0 &&
           (size_t)written < sizeof(connection_field) &&
           strstr(response, "HTTP/1.1 200 OK\r\n") != NULL &&
           strstr(response, content_type) != NULL &&
           strstr(response, connection_field) != NULL &&
           strstr(response, body_fragment) != NULL;
}

static int exercise_server(uint16_t port) {
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
    static const char asset_request[] =
        "GET /assets/site.css HTTP/1.1\r\n"
        "Host: docs.test\r\n\r\n";
    if (!send_all_client(
            fd, asset_request, sizeof(asset_request) - 1U))
        return 3;
    if (!read_response(
            fd, "Content-Type: text/css; charset=utf-8",
            "font-family: sans-serif", "keep-alive"))
        return 4;
    static const char guide_request[] =
        "GET /guide HTTP/1.1\r\n"
        "Host: docs.test\r\n"
        "Connection: close\r\n\r\n";
    if (!send_all_client(
            fd, guide_request, sizeof(guide_request) - 1U))
        return 5;
    if (!read_response(
            fd, "Content-Type: text/html; charset=utf-8",
            "<h2>Language guide</h2>", "close"))
        return 6;
    char byte;
    if (recv(fd, &byte, 1U, 0) != 0) return 7;
    (void)close(fd);
    return 0;
}
#endif

int main(int argc, char **argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    return 0;
#else
    (void)argv;
    if (argc != 1) return 9;
    int descriptors[2];
    if (pipe(descriptors) != 0) return 10;
    pid_t child = fork();
    if (child < 0) return 11;
    if (child == 0) {
        (void)close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(12);
        (void)close(descriptors[1]);
        (void)setvbuf(stdout, NULL, _IONBF, 0);
        _exit(lang_project_run(
            "examples/docs_server/aster.toml",
            "integration_server", false));
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
    int result = exercise_server((uint16_t)parsed);
    (void)fclose(ports);
    int child_status = 0;
    if (waitpid(child, &child_status, 0) != child ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
        return 16;
    return result;
#endif
}
