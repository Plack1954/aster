#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "lang/lang.h"

#include <stddef.h>
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

static bool send_all(int fd, const char *data, size_t length) {
    while (length != 0U) {
        ssize_t sent = send(fd, data, length, 0);
        if (sent <= 0) return false;
        data += (size_t)sent;
        length -= (size_t)sent;
    }
    return true;
}

static int request(uint16_t port, const char *method, const char *path,
                   const char *body, int expected_status,
                   const char *expected_text) {
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
    size_t body_length = body != NULL ? strlen(body) : 0U;
    char message[1024];
    int length = snprintf(
        message, sizeof(message),
        "%s %s HTTP/1.1\r\n"
        "Host: generated.test\r\n"
        "Connection: close\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: %zu\r\n\r\n%s",
        method, path, body_length, body != NULL ? body : "");
    if (length < 0 || (size_t)length >= sizeof(message) ||
        !send_all(fd, message, (size_t)length)) {
        (void)close(fd);
        return 3;
    }
    char response[16384];
    size_t used = 0U;
    while (used < sizeof(response) - 1U) {
        ssize_t received = recv(
            fd, response + used, sizeof(response) - 1U - used, 0);
        if (received < 0) {
            (void)close(fd);
            return 4;
        }
        if (received == 0) break;
        used += (size_t)received;
    }
    (void)close(fd);
    response[used] = '\0';
    char status[64];
    int status_length = snprintf(
        status, sizeof(status), "HTTP/1.1 %d ", expected_status);
    if (status_length <= 0 ||
        (size_t)status_length >= sizeof(status))
        return 5;
    return strstr(response, status) != NULL &&
           strstr(response, expected_text) != NULL ? 0 : 6;
}

static int rejected_request(uint16_t port, const char *message,
                            const char *expected_status) {
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
    if (!send_all(fd, message, strlen(message))) {
        (void)close(fd);
        return 3;
    }
    char response[1024];
    ssize_t received = recv(fd, response, sizeof(response) - 1U, 0);
    (void)close(fd);
    if (received <= 0) return 4;
    response[(size_t)received] = '\0';
    return strstr(response, expected_status) != NULL ? 0 : 5;
}
#endif

int main(int argc, char **argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    return 0;
#else
    if (argc != 2) return 9;
    static const char database[] =
        "examples/issue_tracker/integration-test.db";
    (void)unlink(database);
    int descriptors[2];
    if (pipe(descriptors) != 0) return 10;
    pid_t child = fork();
    if (child < 0) return 11;
    if (child == 0) {
        (void)close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(12);
        (void)close(descriptors[1]);
        if (strcmp(argv[1], "vm") == 0) {
            (void)setvbuf(stdout, NULL, _IONBF, 0);
            _exit(lang_project_run_ir(
                "examples/issue_tracker/aster.toml",
                "integration_server"));
        }
        execl(argv[1], argv[1], (char *)NULL);
        _exit(13);
    }
    (void)close(descriptors[1]);
    FILE *ports = fdopen(descriptors[0], "r");
    if (ports == NULL) return 14;
    char line[64];
    if (fgets(line, sizeof(line), ports) == NULL) {
        (void)fclose(ports);
        (void)kill(child, SIGTERM);
        (void)waitpid(child, NULL, 0);
        return 15;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(line, &end, 10);
    if (end == line || parsed == 0UL || parsed > UINT16_MAX) {
        (void)fclose(ports);
        (void)kill(child, SIGTERM);
        (void)waitpid(child, NULL, 0);
        return 16;
    }
    uint16_t port = (uint16_t)parsed;
    int result = request(
        port, "GET", "/issues", NULL, 200, "<h2>Issues</h2>");
    if (result == 0)
        result = request(
            port, "POST", "/issues",
            "title=Generated+C+server", 303,
            "Location: /issues\r\n");
    if (result == 0)
        result = request(
            port, "GET", "/issues/1", NULL, 200,
            "<h2>Generated C server</h2>");
    if (result == 0)
        result = rejected_request(
            port,
            "POST /issues HTTP/1.1\r\n"
            "Host: generated.test\r\n"
            "Content-Length: 4097\r\n\r\n",
            "HTTP/1.1 413 Payload Too Large\r\n");
    if (result == 0)
        result = rejected_request(
            port,
            "GET /issues HTTP/1.1\r\n"
            "Host: generated.test\r\n"
            "Content-Length: invalid\r\n\r\n",
            "HTTP/1.1 400 Bad Request\r\n");
    (void)fclose(ports);
    if (result != 0) (void)kill(child, SIGTERM);
    int child_status = 0;
    bool child_ok =
        waitpid(child, &child_status, 0) == child &&
        WIFEXITED(child_status) &&
        WEXITSTATUS(child_status) == 0;
    (void)unlink(database);
    return child_ok ? result : 17;
#endif
}
