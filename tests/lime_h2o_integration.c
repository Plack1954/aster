#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "lang/lang.h"

#include <ctype.h>
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
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool send_all(int descriptor, const char *data, size_t length) {
    while (length != 0U) {
        ssize_t sent = send(descriptor, data, length, 0);
        if (sent <= 0) return false;
        data += (size_t)sent;
        length -= (size_t)sent;
    }
    return true;
}

static const char *find_ascii_case(const char *text, const char *needle) {
    size_t needle_length = strlen(needle);
    if (needle_length == 0U) return text;
    for (; *text != '\0'; ++text) {
        size_t index = 0U;
        while (index < needle_length && text[index] != '\0' &&
               tolower((unsigned char)text[index]) ==
                   tolower((unsigned char)needle[index]))
            ++index;
        if (index == needle_length) return text;
    }
    return NULL;
}

static bool contains_ascii_case(const char *text, const char *needle) {
    return find_ascii_case(text, needle) != NULL;
}

static int exchange(uint16_t port, const char *method, const char *target,
                    const char *content_type, const char *body,
                    const char *request_headers,
                    char *response, size_t response_capacity) {
    int descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0) return 1;
    struct sockaddr_in endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(descriptor, (const struct sockaddr *)&endpoint,
                (socklen_t)sizeof(endpoint)) != 0) {
        (void)close(descriptor);
        return 2;
    }

    size_t body_length = body != NULL ? strlen(body) : 0U;
    char type_header[256] = "";
    if (content_type != NULL) {
        int written = snprintf(type_header, sizeof(type_header),
                               "Content-Type: %s\r\n", content_type);
        if (written < 0 || (size_t)written >= sizeof(type_header)) {
            (void)close(descriptor);
            return 3;
        }
    }
    char message[2048];
    int message_length = snprintf(
        message, sizeof(message),
        "%s %s HTTP/1.1\r\n"
        "Host: lime.test\r\n"
        "Connection: close\r\n"
        "%s%sContent-Length: %zu\r\n\r\n",
        method, target, type_header,
        request_headers != NULL ? request_headers : "", body_length);
    if (message_length < 0 || (size_t)message_length >= sizeof(message) ||
        !send_all(descriptor, message, (size_t)message_length) ||
        (body_length != 0U && !send_all(descriptor, body, body_length))) {
        (void)close(descriptor);
        return 4;
    }

    size_t used = 0U;
    while (used < response_capacity - 1U) {
        ssize_t received = recv(descriptor, response + used,
                                response_capacity - 1U - used, 0);
        if (received < 0) {
            (void)close(descriptor);
            return 5;
        }
        if (received == 0) break;
        used += (size_t)received;
    }
    (void)close(descriptor);
    response[used] = '\0';
    return 0;
}

static int request(uint16_t port, const char *method, const char *target,
                   const char *content_type, const char *body,
                   const char *request_headers,
                   int expected_status, const char *expected_type,
                   const char *expected_body, const char *expected_header) {
    char response[131072];
    int exchange_result = exchange(
        port, method, target, content_type, body, request_headers,
        response, sizeof(response));
    if (exchange_result != 0) return exchange_result;

    char status[64];
    char type[256] = "";
    if (snprintf(status, sizeof(status), "HTTP/1.1 %d ",
                 expected_status) < 0)
        return 6;
    if (expected_type != NULL &&
        snprintf(type, sizeof(type), "content-type: %s", expected_type) < 0)
        return 6;
    char *body_start = strstr(response, "\r\n\r\n");
    if (body_start == NULL) return 7;
    body_start += 4;
    bool body_matches = expected_body == NULL
        ? *body_start == '\0'
        : strstr(body_start, expected_body) != NULL;
    bool header_matches = expected_header == NULL ||
        contains_ascii_case(response, expected_header);
    bool type_matches = expected_type == NULL ||
        contains_ascii_case(response, type);
    if (strstr(response, status) == NULL ||
        !type_matches || !body_matches ||
        !header_matches) {
        (void)fprintf(stderr, "unexpected H2O response for %s %s:\n%s\n",
                      method, target, response);
        return 8;
    }
    return 0;
}

static int session_roundtrip(uint16_t port) {
    char first[8192];
    int result = exchange(port, "GET", "/session", NULL, NULL, NULL,
                          first, sizeof(first));
    if (result != 0 || strstr(first, "HTTP/1.1 200 ") == NULL ||
        strstr(first, "created") == NULL)
        return 1;
    static const char prefix[] = "set-cookie: lime.session=";
    const char *cookie = find_ascii_case(first, prefix);
    if (cookie == NULL) return 2;
    cookie += strlen("set-cookie: ");
    const char *cookie_end = strchr(cookie, ';');
    if (cookie_end == NULL || cookie_end <= cookie) return 3;
    size_t cookie_length = (size_t)(cookie_end - cookie);
    char header[256];
    int written = snprintf(header, sizeof(header), "Cookie: %.*s\r\n",
                           (int)cookie_length, cookie);
    if (written < 0 || (size_t)written >= sizeof(header)) return 4;

    char second[8192];
    result = exchange(port, "GET", "/session", NULL, NULL, header,
                      second, sizeof(second));
    if (result != 0 || strstr(second, "HTTP/1.1 200 ") == NULL ||
        strstr(second, "brandon") == NULL ||
        find_ascii_case(second, "set-cookie: lime.session=") != NULL)
        return 5;
    return 0;
}

static int disconnect_during_response(uint16_t port) {
    int descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0) return 1;
    struct sockaddr_in endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(descriptor, (const struct sockaddr *)&endpoint,
                (socklen_t)sizeof(endpoint)) != 0) {
        (void)close(descriptor);
        return 2;
    }
    static const char message[] =
        "GET /disconnect HTTP/1.1\r\n"
        "Host: lime.test\r\n"
        "Connection: close\r\n\r\n";
    if (!send_all(descriptor, message, sizeof(message) - 1U)) {
        (void)close(descriptor);
        return 3;
    }
    char byte;
    if (recv(descriptor, &byte, 1U, 0) <= 0) {
        (void)close(descriptor);
        return 4;
    }
    (void)close(descriptor);
    return 0;
}

static int incomplete_request_times_out(uint16_t port) {
    int descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0) return 1;
    struct sockaddr_in endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(descriptor, (const struct sockaddr *)&endpoint,
                (socklen_t)sizeof(endpoint)) != 0) {
        (void)close(descriptor);
        return 2;
    }
    struct timeval timeout = {.tv_sec=2, .tv_usec=0};
    if (setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, (socklen_t)sizeof(timeout)) != 0) {
        (void)close(descriptor);
        return 3;
    }
    static const char partial[] =
        "GET /hello/Aster HTTP/1.1\r\nHost: lime.test\r\n";
    if (!send_all(descriptor, partial, sizeof(partial) - 1U)) {
        (void)close(descriptor);
        return 4;
    }
    char byte;
    ssize_t received = recv(descriptor, &byte, 1U, 0);
    (void)close(descriptor);
    return received == 0 ? 0 : 5;
}
#endif

int main(int argc, char **argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    return 0;
#else
    if (argc != 2) return 20;
    int ports[2];
    if (pipe(ports) != 0) return 21;
    pid_t child = fork();
    if (child < 0) return 22;
    if (child == 0) {
        (void)close(ports[0]);
        if (dup2(ports[1], STDOUT_FILENO) < 0) _exit(23);
        (void)close(ports[1]);
        (void)setvbuf(stdout, NULL, _IONBF, 0);
        if (strcmp(argv[1], "vm") == 0)
            _exit(lang_project_run_ir(
                "packages/lime/aster.toml", "h2o_http_server"));
        execl(argv[1], argv[1], (char *)NULL);
        _exit(24);
    }

    (void)close(ports[1]);
    FILE *port_stream = fdopen(ports[0], "r");
    if (port_stream == NULL) return 25;
    char line[64];
    if (fgets(line, sizeof(line), port_stream) == NULL) {
        (void)fclose(port_stream);
        (void)kill(child, SIGTERM);
        (void)waitpid(child, NULL, 0);
        return 26;
    }
    (void)fclose(port_stream);
    char *end = NULL;
    unsigned long parsed = strtoul(line, &end, 10);
    if (end == line || parsed == 0UL || parsed > UINT16_MAX) {
        (void)kill(child, SIGTERM);
        (void)waitpid(child, NULL, 0);
        return 27;
    }
    uint16_t port = (uint16_t)parsed;
    int result = incomplete_request_times_out(port);
    char *oversized = malloc(9001U);
    if (oversized == NULL) {
        (void)kill(child, SIGTERM);
        (void)waitpid(child, NULL, 0);
        return 28;
    }
    memset(oversized, 'a', 9000U);
    oversized[9000] = '\0';
    if (result == 0)
        result = request(
            port, "POST", "/form", "application/octet-stream", oversized,
            NULL,
            413, "text/plain; charset=utf-8",
            "request entity is too large", NULL);
    free(oversized);
    if (result == 0)
        result = request(
        port, "GET", "/hello/Aster?from=h2o", NULL, NULL, NULL, 200,
        "text/html; charset=utf-8", "<p>Aster:h2o</p>",
        "x-lime-adapter: h2o");
    if (result == 0)
        result = request(
            port, "GET", "/origin", NULL, NULL,
            "X-Forwarded-For: 198.51.100.20\r\n"
            "X-Forwarded-Host: aster.example\r\n"
            "X-Forwarded-Proto: https\r\n",
            200, "text/plain; charset=utf-8",
            "https|aster.example|198.51.100.20", NULL);
    if (result == 0)
        result = request(
            port, "POST", "/form", "application/x-www-form-urlencoded",
            "title=Fast+Lime", NULL, 200, "text/html; charset=utf-8",
            "<p>Fast Lime</p>", NULL);
    static const char multipart[] =
        "--lime-boundary\r\n"
        "Content-Disposition: form-data; name=\"title\"\r\n\r\n"
        "Aster & Lime\r\n"
        "--lime-boundary\r\n"
        "Content-Disposition: form-data; name=\"image\"; "
        "filename=\"mark.svg\"\r\n"
        "Content-Type: image/svg+xml\r\n\r\n"
        "<svg>binary-safe</svg>\r\n"
        "--lime-boundary--\r\n";
    if (result == 0)
        result = request(
            port, "POST", "/upload",
            "multipart/form-data; boundary=lime-boundary",
            multipart, NULL, 200, "text/plain; charset=utf-8",
            "Aster & Lime:mark.svg:image/svg+xml:22", NULL);
    if (result == 0)
        result = request(
            port, "GET", "/cookie", NULL, NULL, NULL, 200,
            "text/plain; charset=utf-8", "cookie",
            "set-cookie: theme=aster");
    if (result == 0) result = session_roundtrip(port);
    if (result == 0)
        result = request(
            port, "GET", "/redirect", NULL, NULL, NULL, 303,
            "text/plain; charset=utf-8", "",
            "location: /hello/Aster?from=redirect");
    if (result == 0)
        result = request(
            port, "GET", "/explode", NULL, NULL, NULL, 500,
            "text/plain; charset=utf-8",
            "caught:intentional H2O failure",
            "x-lime-exception: handled");
    if (result == 0)
        result = request(
            port, "HEAD", "/head", NULL, NULL, NULL, 200,
            "text/css; charset=utf-8", NULL, NULL);
    if (result == 0)
        result = request(
            port, "GET", "/binary", NULL, NULL, NULL, 200,
            "application/octet-stream", "Aster", NULL);
    if (result == 0)
        result = request(
            port, "GET", "/large", NULL, NULL, NULL, 200,
            "application/octet-stream", "stream-tail", NULL);
    if (result == 0)
        result = request(
            port, "GET", "/missing", NULL, NULL, NULL, 404,
            "text/html; charset=utf-8", "<h1>Missing</h1>", NULL);
    if (result == 0)
        result = request(
            port, "GET", "/files/icons/mark.svg", NULL, NULL, NULL, 200,
            "image/svg+xml", "<svg xmlns=", "accept-ranges: bytes");
    if (result == 0)
        result = request(
            port, "GET", "/files/icons/mark.svg", NULL, NULL, NULL, 200,
            "image/svg+xml", "<svg xmlns=",
            "cache-control: public, max-age=3600");
    if (result == 0)
        result = request(
            port, "HEAD", "/files/icons/mark.svg", NULL, NULL, NULL, 200,
            "image/svg+xml", NULL, "content-length: 88");
    if (result == 0)
        result = request(
            port, "GET", "/files/icons/mark.svg", NULL, NULL,
            "Range: bytes=0-15\r\n", 206, "image/svg+xml",
            "<svg xmlns=\"http", "content-range: bytes 0-15/88");
    if (result == 0)
        result = request(
            port, "GET", "/files/icons/mark.svg", NULL, NULL,
            "If-Modified-Since: Wed, 31 Dec 2099 23:59:59 GMT\r\n",
            304, NULL, NULL, "etag:");
    if (result == 0)
        result = request(
            port, "GET", "/.aster-static/0/icons/mark.svg",
            NULL, NULL, NULL, 404, "text/plain; charset=utf-8",
            "not found", NULL);
    if (result == 0) result = disconnect_during_response(port);

    if (kill(child, SIGTERM) != 0 && result == 0) result = 28;
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 28;
    if (result != 0) return 30 + result;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 29;
#endif
}
