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

static int request(uint16_t port, const char *method, const char *target,
                   const char *request_content_type, const char *body,
                   int expected_status,
                   const char *expected_content_type,
                   const char *expected_body) {
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

    size_t body_length = body != NULL ? strlen(body) : 0U;
    char request_content_type_header[256] = "";
    if (request_content_type != NULL) {
        int header_length = snprintf(
            request_content_type_header,
            sizeof(request_content_type_header),
            "Content-Type: %s\r\n", request_content_type);
        if (header_length <= 0 ||
            (size_t)header_length >= sizeof(request_content_type_header)) {
            (void)close(socket_fd);
            return 3;
        }
    }
    char message[1024];
    int message_length = snprintf(
        message, sizeof(message),
        "%s %s HTTP/1.1\r\n"
        "Host: aster.test\r\n"
        "X-Aster-Test: header-value\r\n"
        "Cookie: malformed; theme = \"aster\" ; session=abc\r\n"
        "Connection: close\r\n"
        "%s"
        "Content-Length: %zu\r\n\r\n%s",
        method, target, request_content_type_header,
        body_length, body != NULL ? body : "");
    if (message_length < 0 ||
        (size_t)message_length >= sizeof(message) ||
        !send_all(socket_fd, message, (size_t)message_length)) {
        (void)close(socket_fd);
        return 3;
    }

    char response[4096];
    size_t used = 0U;
    while (used < sizeof(response) - 1U) {
        ssize_t received = recv(
            socket_fd, response + used,
            sizeof(response) - 1U - used, 0);
        if (received < 0) {
            (void)close(socket_fd);
            return 4;
        }
        if (received == 0) break;
        used += (size_t)received;
    }
    (void)close(socket_fd);
    response[used] = '\0';

    char status[64];
    int status_length = snprintf(
        status, sizeof(status), "HTTP/1.1 %d ", expected_status);
    if (status_length <= 0 || (size_t)status_length >= sizeof(status))
        return 5;
    char content_type[160] = {0};
    if (expected_content_type != NULL) {
        int content_type_length = snprintf(
            content_type, sizeof(content_type),
            "Content-Type: %s\r\n", expected_content_type);
        if (content_type_length <= 0 ||
            (size_t)content_type_length >= sizeof(content_type))
            return 5;
    }
    char *body_start = strstr(response, "\r\n\r\n");
    if (body_start == NULL) return 6;
    body_start += 4;
    bool body_ok = expected_body != NULL
        ? strstr(body_start, expected_body) != NULL
        : *body_start == '\0';
    if (expected_body == NULL) {
        char *length_header = strstr(response, "Content-Length: ");
        if (expected_status == 204)
            body_ok = body_ok && length_header == NULL;
        else if (length_header == NULL ||
                 strtoul(length_header + strlen("Content-Length: "),
                         NULL, 10) == 0UL)
            body_ok = false;
    }
    bool framework_headers_ok = true;
    if (strcmp(target, "/articles/first-post?ref=home") == 0)
        framework_headers_ok =
            strstr(response, "X-Aster-Web: Aster\r\n") != NULL;
    if (strcmp(target, "/cookie") == 0)
        framework_headers_ok = strstr(
            response,
            "Set-Cookie: theme=aster; Path=/; HttpOnly; Secure; SameSite=Lax\r\n"
        ) != NULL;
    if (strcmp(target, "/header") == 0)
        framework_headers_ok = strstr(body_start, "header-value") != NULL;
    if (strcmp(target, "/cookie-options") == 0)
        framework_headers_ok = strstr(
            response,
            "Set-Cookie: theme=aster; Path=/account; Domain=aster.test; Max-Age=3600; Secure; SameSite=Strict\r\n"
        ) != NULL;
    if (strcmp(target, "/cookie-delete") == 0)
        framework_headers_ok = strstr(
            response,
            "Set-Cookie: theme=; Path=/; Max-Age=0; HttpOnly; Secure; SameSite=Lax\r\n"
        ) != NULL;
    if (strcmp(target, "/stream") == 0)
        framework_headers_ok =
            strstr(response, "X-Aster-Web-Stream: yes\r\n") != NULL &&
            strstr(response, "Transfer-Encoding: chunked\r\n") != NULL;
    if (strcmp(target, "/articles/first-post") == 0 &&
        (strcmp(method, "OPTIONS") == 0 || strcmp(method, "POST") == 0))
        framework_headers_ok = strstr(
            response,
            "Allow: GET, HEAD, PUT, PATCH, DELETE, OPTIONS\r\n"
        ) != NULL;
    bool content_type_ok = expected_content_type != NULL
        ? strstr(response, content_type) != NULL
        : strstr(response, "Content-Type:") == NULL;
    return strstr(response, status) != NULL &&
           content_type_ok && body_ok &&
           framework_headers_ok ? 0 : 6;
}
#endif

int main(int argc, char **argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    return 0;
#else
    if (argc != 2) return 9;

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
                "packages/aster_web/CurrentHttpServer.asproj"));
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
        port, "GET", "/articles/first-post?ref=home", NULL, NULL, 200,
        "text/html; charset=utf-8",
        "<article>first-post:home</article>");
    if (result == 0)
        result = request(
            port, "GET",
            "/ignored/%2E%2E/articles/Ada%20Lovelace?ref=encoded",
            NULL, NULL, 200, "text/html; charset=utf-8",
            "<article>Ada Lovelace:encoded</article>");
    if (result == 0)
        result = request(
            port, "OPTIONS", "/articles/first-post", NULL, NULL, 204,
            NULL, NULL);
    if (result == 0)
        result = request(
            port, "POST", "/articles/first-post", NULL, NULL, 405,
            "text/html; charset=utf-8", "<h1>Method not allowed</h1>");
    if (result == 0)
        result = request(
            port, "HEAD", "/articles/first-post?ref=head",
            NULL, NULL, 200, "text/html; charset=utf-8", NULL);
    if (result == 0)
        result = request(
            port, "HEAD", "/head-priority", NULL, NULL, 200,
            "text/css; charset=utf-8", NULL);
    if (result == 0)
        result = request(
            port, "PUT", "/articles/first-post", NULL, NULL, 200,
            "text/plain; charset=utf-8", "PUT");
    if (result == 0)
        result = request(
            port, "PATCH", "/articles/first-post", NULL, NULL, 200,
            "text/plain; charset=utf-8", "PATCH");
    if (result == 0)
        result = request(
            port, "DELETE", "/articles/first-post", NULL, NULL, 200,
            "text/plain; charset=utf-8", "DELETE");
    if (result == 0)
        result = request(
            port, "POST", "/submit",
            "Application/X-Www-Form-Urlencoded; charset=utf-8",
            "title=Aster+Web+forms%21&kind=client+site", 200,
            "text/html; charset=utf-8",
            "<p>Aster Web forms!:client site</p>");
    if (result == 0)
        result = request(
            port, "POST", "/submit", "application/json",
            "title=wrong+type&kind=ignored", 400,
            "text/html; charset=utf-8",
            "<p>The request does not contain form data.</p>");
    if (result == 0)
        result = request(
            port, "POST", "/submit",
            "multipart/form-data; boundary=aster-web-test",
            "--aster-web-test\r\nContent-Disposition: form-data; name=\"title\"\r\n\r\nMultipart\r\n--aster-web-test\r\nContent-Disposition: form-data; name=\"kind\"\r\n\r\nupload\r\n--aster-web-test--\r\n",
            200, "text/html; charset=utf-8",
            "<p>Multipart:upload</p>");
    if (result == 0)
        result = request(
            port, "GET", "/robots.txt", NULL, NULL, 200,
            "text/plain; charset=utf-8",
            "User-agent: *\nDisallow: /private\n");
    if (result == 0)
        result = request(
            port, "GET", "/assets/site.css", NULL, NULL, 200,
            "text/css; charset=utf-8",
            "body { color: #e45b20; }\n");
    if (result == 0)
        result = request(
            port, "GET", "/feed.xml", NULL, NULL, 200,
            "application/xml; charset=utf-8",
            "<rss version=\"2.0\"></rss>");
    if (result == 0)
        result = request(
            port, "GET", "/mark.svg", NULL, NULL, 200,
            "image/svg+xml", "<svg xmlns=");
    if (result == 0)
        result = request(
            port, "GET", "/cookie", NULL, NULL, 200,
            "text/plain; charset=utf-8", "aster");
    if (result == 0)
        result = request(
            port, "GET", "/header", NULL, NULL, 200,
            "text/plain; charset=utf-8", "header-value");
    if (result == 0)
        result = request(
            port, "GET", "/cookie-options", NULL, NULL, 200,
            "text/plain; charset=utf-8", "configured");
    if (result == 0)
        result = request(
            port, "GET", "/cookie-delete", NULL, NULL, 200,
            "text/plain; charset=utf-8", "deleted");
    if (result == 0)
        result = request(
            port, "POST", "/json", "application/json; charset=utf-8",
            "{\"ok\":true}", 201,
            "application/json; charset=utf-8", "{\"ok\":true}");
    if (result == 0)
        result = request(
            port, "GET", "/problem", NULL, NULL, 409,
            "application/problem+json; charset=utf-8",
            "{\"type\":null,\"title\":\"Conflict\",\"status\":409,\"detail\":\"The article already exists.\",\"instance\":\"/problem\"}");
    if (result == 0)
        result = request(
            port, "GET", "/filtered", NULL, NULL, 404,
            "text/html; charset=utf-8",
            "<main data-path=\"/filtered\"><h1>Filtered</h1></main>");
    if (result == 0)
        result = request(
            port, "GET", "/missing", NULL, NULL, 404,
            "text/html; charset=utf-8",
            "<main data-path=\"/missing\"><h1>Missing</h1></main>");
    if (result == 0)
        result = request(
            port, "GET", "/invalid/%00", NULL, NULL, 400,
            "text/html; charset=utf-8", "<h1>Bad request</h1>");
    if (result == 0)
        result = request(
            port, "GET", "/stream", NULL, NULL, 200,
            "application/octet-stream", "Aster");

    (void)fclose(ports);
    if (result != 0) (void)kill(child, SIGTERM);
    int child_status = 0;
    bool child_ok =
        waitpid(child, &child_status, 0) == child &&
        WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0;
    return child_ok ? result : 17;
#endif
}
