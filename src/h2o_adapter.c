#include "lang/lang.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static LangNativeResult h2o_native_failure(const char *message) {
    return lang_native_result_error(message);
}

static LangNativeResult h2o_result_error(LangVM *vm,
                                         const char *message) {
    LangStringView view = {message, strlen(message)};
    LangValue error;
    if (!lang_string_value(vm, view, &error))
        return h2o_native_failure("could not allocate H2O error");
    LangValue result;
    if (!lang_result_err_value(vm, error, &result)) {
        lang_value_drop(vm, &error);
        return h2o_native_failure("could not construct H2O error Result");
    }
    return (LangNativeResult){true, result, NULL};
}

#if defined(LANG_HAVE_H2O)

static LangNativeResult h2o_result_value(LangVM *vm, LangValue value) {
    LangValue result;
    if (!lang_result_ok_value(vm, value, &result)) {
        lang_value_drop(vm, &value);
        return h2o_native_failure("could not construct H2O Result");
    }
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult h2o_string_value(LangVM *vm,
                                         const char *data,
                                         size_t length) {
    LangValue value;
    if (!lang_string_value(vm, (LangStringView){data, length}, &value))
        return h2o_native_failure("could not allocate H2O string");
    return (LangNativeResult){true, value, NULL};
}

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <h2o.h>
#include <h2o/http1.h>
#include <h2o/http2.h>

static volatile sig_atomic_t lime_h2o_signal_stop_requested = 0;

static void lime_h2o_on_stop_signal(int signal_number) {
    (void)signal_number;
    lime_h2o_signal_stop_requested = 1;
}

#define LIME_H2O_SERVER_MAGIC UINT32_C(0x4f483253)
#define LIME_H2O_REQUEST_MAGIC UINT32_C(0x4f483252)
#define LIME_H2O_STREAM_MAGIC UINT32_C(0x4f483247)

typedef struct LimeH2OServer LimeH2OServer;

typedef struct LimeH2OHandler {
    h2o_handler_t super;
    LimeH2OServer *server;
} LimeH2OHandler;

typedef struct LimeH2OStaticMount {
    char *root;
    size_t root_length;
    char *internal_prefix;
    struct LimeH2OStaticMount *next;
} LimeH2OStaticMount;

typedef struct LimeH2OStaticGuard {
    h2o_handler_t super;
    LimeH2OServer *server;
} LimeH2OStaticGuard;

typedef struct LimeH2OPendingRequest {
    h2o_req_t *request;
    struct LimeH2OPendingRequest *next;
} LimeH2OPendingRequest;

typedef struct LimeH2ORequest {
    uint32_t magic;
    LimeH2OServer *server;
    h2o_req_t *request;
    bool responded;
} LimeH2ORequest;

typedef struct LimeH2OStream {
    h2o_generator_t super;
    uint32_t magic;
    LimeH2OServer *server;
    h2o_req_t *request;
    char *inflight;
    bool ready;
    bool stopped;
    bool final_sent;
    bool abandoned;
} LimeH2OStream;

struct LimeH2OServer {
    uint32_t magic;
    size_t references;
    h2o_globalconf_t config;
    h2o_hostconf_t *host;
    h2o_context_t context;
    h2o_accept_ctx_t accept;
    h2o_evloop_t *loop;
    h2o_socket_t *listener;
    LimeH2OPendingRequest *pending_head;
    LimeH2OPendingRequest *pending_tail;
    size_t pending_count;
    size_t max_queued_requests;
    h2o_req_t *static_delegate;
    h2o_iovec_t static_headers;
    LimeH2OStaticMount *static_mounts;
    size_t static_mount_count;
    uint16_t port;
    bool context_initialized;
    bool handle_signals;
    bool stopping;
    bool shutdown_started;
    bool shutdown_complete;
};

static void h2o_server_release(LimeH2OServer *server);

static void h2o_static_mounts_dispose(LimeH2OStaticMount *mount) {
    while (mount != NULL) {
        LimeH2OStaticMount *next = mount->next;
        free(mount->root);
        free(mount->internal_prefix);
        free(mount);
        mount = next;
    }
}

static void lime_h2o_stream_clear_chunk(LimeH2OStream *stream) {
    free(stream->inflight);
    stream->inflight = NULL;
}

static void lime_h2o_stream_proceed(h2o_generator_t *base,
                                    h2o_req_t *request) {
    LimeH2OStream *stream = (LimeH2OStream *)base;
    lime_h2o_stream_clear_chunk(stream);
    stream->ready = true;
    if (stream->abandoned && !stream->final_sent) {
        stream->final_sent = true;
        stream->request = NULL;
        h2o_send(request, NULL, 0U, H2O_SEND_STATE_ERROR);
    }
}

static void lime_h2o_stream_stop(h2o_generator_t *base,
                                 h2o_req_t *request) {
    (void)request;
    LimeH2OStream *stream = (LimeH2OStream *)base;
    lime_h2o_stream_clear_chunk(stream);
    stream->request = NULL;
    stream->ready = true;
    stream->stopped = true;
}

static void lime_h2o_stream_dispose(void *opaque) {
    LimeH2OStream *stream = opaque;
    lime_h2o_stream_clear_chunk(stream);
    stream->magic = 0U;
}

static void lime_h2o_stream_drop(void *opaque) {
    LimeH2OStream *stream = opaque;
    if (stream == NULL) return;
    if (!stream->final_sent && !stream->stopped) {
        stream->abandoned = true;
        if (stream->ready && stream->request != NULL) {
            stream->final_sent = true;
            h2o_req_t *request = stream->request;
            stream->request = NULL;
            h2o_send(request, NULL, 0U, H2O_SEND_STATE_ERROR);
        }
    }
    h2o_mem_release_shared(stream);
}

static int lime_h2o_on_request(h2o_handler_t *base, h2o_req_t *request) {
    LimeH2OHandler *handler = (LimeH2OHandler *)base;
    LimeH2OServer *server = handler->server;
    if (server == NULL || server->magic != LIME_H2O_SERVER_MAGIC)
        return -1;
    if (server->pending_count >= server->max_queued_requests) {
        h2o_send_error_503(
            request, "Service Unavailable", "Server is busy.", 0);
        return 0;
    }
    LimeH2OPendingRequest *pending = malloc(sizeof(*pending));
    if (pending == NULL) {
        h2o_send_error_503(
            request, "Service Unavailable", "Server is busy.", 0);
        return 0;
    }
    pending->request = request;
    pending->next = NULL;
    if (server->pending_tail != NULL)
        server->pending_tail->next = pending;
    else
        server->pending_head = pending;
    server->pending_tail = pending;
    server->pending_count += 1U;
    return 0;
}

static void lime_h2o_on_accept(h2o_socket_t *listener,
                               const char *error) {
    if (error != NULL) return;
    LimeH2OServer *server = listener->data;
    if (server == NULL || server->magic != LIME_H2O_SERVER_MAGIC)
        return;
    h2o_socket_t *socket = h2o_evloop_socket_accept(listener);
    if (socket != NULL) h2o_accept(&server->accept, socket);
}

static bool h2o_server_start(LimeH2OServer *server) {
    if (server->context_initialized) return true;
    h2o_context_init(&server->context, server->loop, &server->config);
    server->context_initialized = true;
    server->accept.ctx = &server->context;
    server->accept.hosts = server->config.hosts;
    return true;
}

static void h2o_server_begin_shutdown(LimeH2OServer *server) {
    if (server->shutdown_started) return;
    server->stopping = true;
    server->shutdown_started = true;
    while (server->pending_head != NULL) {
        LimeH2OPendingRequest *pending = server->pending_head;
        server->pending_head = pending->next;
        h2o_send_error_503(pending->request, "Service Unavailable",
                           "Server is shutting down.",
                           H2O_SEND_ERROR_HTTP1_CLOSE_CONNECTION);
        free(pending);
    }
    server->pending_tail = NULL;
    server->pending_count = 0U;
    if (server->listener != NULL) {
        h2o_socket_read_stop(server->listener);
        (void)h2o_evloop_run(server->loop, 0);
        h2o_socket_close(server->listener);
        server->listener = NULL;
    }
    if (server->context_initialized)
        h2o_context_request_shutdown(&server->context);
}

static void h2o_server_drain(LimeH2OServer *server) {
    if (server->shutdown_complete) return;
    h2o_server_begin_shutdown(server);
    if (server->context_initialized) {
        while (server->context._conns.num_conns.idle != 0U ||
               server->context._conns.num_conns.active != 0U ||
               server->context._conns.num_conns.shutdown != 0U)
            (void)h2o_evloop_run(server->loop, INT32_MAX);
        h2o_context_dispose(&server->context);
        server->context_initialized = false;
    }
    server->shutdown_complete = true;
}

static void h2o_server_destroy(LimeH2OServer *server) {
    if (server == NULL || server->magic != LIME_H2O_SERVER_MAGIC) return;
    h2o_server_drain(server);
    h2o_config_dispose(&server->config);
    h2o_evloop_destroy(server->loop);
    h2o_static_mounts_dispose(server->static_mounts);
    server->magic = 0U;
    free(server);
}

static void h2o_server_release(LimeH2OServer *server) {
    if (server == NULL || server->magic != LIME_H2O_SERVER_MAGIC) return;
    if (--server->references == 0U) h2o_server_destroy(server);
}

static void h2o_server_drop(void *opaque) {
    h2o_server_release(opaque);
}

static void h2o_request_drop(void *opaque) {
    LimeH2ORequest *request = opaque;
    if (request == NULL || request->magic != LIME_H2O_REQUEST_MAGIC)
        return;
    if (!request->responded && request->request != NULL) {
        h2o_send_error_500(
            request->request, "Internal Server Error",
            "Request left without a response.",
            H2O_SEND_ERROR_HTTP1_CLOSE_CONNECTION);
    }
    LimeH2OServer *server = request->server;
    request->magic = 0U;
    request->request = NULL;
    request->server = NULL;
    free(request);
    h2o_server_release(server);
}

static LimeH2OServer *get_h2o_server(const LangValue *value) {
    LimeH2OServer *server = lang_native_handle_data(value);
    return server != NULL && server->magic == LIME_H2O_SERVER_MAGIC
        ? server : NULL;
}

static LimeH2ORequest *get_h2o_request(const LangValue *value) {
    LimeH2ORequest *request = lang_native_handle_data(value);
    return request != NULL && request->magic == LIME_H2O_REQUEST_MAGIC
        ? request : NULL;
}

static LimeH2OStream *get_h2o_stream(const LangValue *value) {
    LimeH2OStream *stream = lang_native_handle_data(value);
    return stream != NULL && stream->magic == LIME_H2O_STREAM_MAGIC
        ? stream : NULL;
}

static bool h2o_integer_arg(const LangValue *value, int64_t *result) {
    if (value->tag == LANG_VALUE_I64) {
        *result = value->as.i64;
        return true;
    }
    if (value->tag == LANG_VALUE_U64 && value->as.u64 <= INT64_MAX) {
        *result = (int64_t)value->as.u64;
        return true;
    }
    return false;
}

static char *copy_c_string(LangStringView source) {
    if (memchr(source.data, '\0', source.length) != NULL) return NULL;
    char *copy = malloc(source.length + 1U);
    if (copy == NULL) return NULL;
    memcpy(copy, source.data, source.length);
    copy[source.length] = '\0';
    return copy;
}

static LangNativeResult h2o_server_open_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LangStringView address;
    int64_t port;
    int64_t max_body;
    int64_t timeout;
    int64_t graceful_shutdown_timeout;
    int64_t max_queued_requests;
    if (arg_count != 7U ||
        !lang_value_string_view(&args[0], &address) ||
        !h2o_integer_arg(&args[1], &port) ||
        !h2o_integer_arg(&args[2], &max_body) ||
        !h2o_integer_arg(&args[3], &timeout) ||
        !h2o_integer_arg(&args[4], &graceful_shutdown_timeout) ||
        !h2o_integer_arg(&args[5], &max_queued_requests) ||
        args[6].tag != LANG_VALUE_BOOL)
        return h2o_result_error(vm,
            "H2OTryServerOpenNative expects valid server options");
    if (port < 0 || port > 65535 || max_body < 0 ||
        max_body > INT32_MAX || timeout < 1 || timeout > 300000 ||
        graceful_shutdown_timeout < 1 ||
        graceful_shutdown_timeout > 300000 ||
        max_queued_requests < 1 || max_queued_requests > 65536)
        return h2o_result_error(vm, "invalid H2O server configuration");
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return h2o_result_error(vm, "could not ignore SIGPIPE");
    char *address_c = copy_c_string(address);
    if (address_c == NULL)
        return h2o_result_error(vm, "invalid H2O listen address");

    LimeH2OServer *server = calloc(1U, sizeof(*server));
    if (server == NULL) {
        free(address_c);
        return h2o_native_failure("out of memory creating H2O server");
    }
    server->magic = LIME_H2O_SERVER_MAGIC;
    server->references = 1U;
    server->handle_signals = args[6].as.boolean;
    server->max_queued_requests = (size_t)max_queued_requests;
    if (server->handle_signals) {
        lime_h2o_signal_stop_requested = 0;
        if (signal(SIGINT, lime_h2o_on_stop_signal) == SIG_ERR ||
            signal(SIGTERM, lime_h2o_on_stop_signal) == SIG_ERR) {
            free(address_c);
            free(server);
            return h2o_result_error(vm,
                "could not install H2O shutdown signal handlers");
        }
    }
    h2o_config_init(&server->config);
    server->config.max_request_entity_size = (size_t)max_body;
    server->config.http1.req_timeout = (uint64_t)timeout;
    server->config.http1.req_io_timeout = (uint64_t)timeout;
    server->config.http2.graceful_shutdown_timeout =
        (uint64_t)graceful_shutdown_timeout;
    server->host = h2o_config_register_host(
        &server->config, h2o_iovec_init(H2O_STRLIT("default")), 65535);
    h2o_pathconf_t *path = h2o_config_register_path(
        server->host, "/", 0);
    LimeH2OHandler *handler = (LimeH2OHandler *)h2o_create_handler(
        path, sizeof(*handler));
    handler->super.on_req = lime_h2o_on_request;
    handler->server = server;

    server->loop = h2o_evloop_create();
    if (server->loop == NULL) {
        free(address_c);
        h2o_config_dispose(&server->config);
        free(server);
        return h2o_result_error(vm, "could not create H2O event loop");
    }
    int descriptor = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons((uint16_t)port);
    int reuse = 1;
    bool opened = descriptor >= 0 &&
        inet_pton(AF_INET, address_c, &endpoint.sin_addr) == 1 &&
        setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) == 0 &&
        bind(descriptor, (struct sockaddr *)&endpoint,
             sizeof(endpoint)) == 0 &&
        listen(descriptor, SOMAXCONN) == 0;
    free(address_c);
    if (!opened) {
        if (descriptor >= 0) (void)close(descriptor);
        h2o_server_destroy(server);
        return h2o_result_error(vm, "could not bind H2O server socket");
    }
    int flags = fcntl(descriptor, F_GETFL, 0);
    if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0) {
        (void)close(descriptor);
        h2o_server_destroy(server);
        return h2o_result_error(vm, "could not configure H2O server socket");
    }
    socklen_t endpoint_length = (socklen_t)sizeof(endpoint);
    if (getsockname(descriptor, (struct sockaddr *)&endpoint,
                    &endpoint_length) != 0) {
        (void)close(descriptor);
        h2o_server_destroy(server);
        return h2o_result_error(vm, "could not read H2O server port");
    }
    server->port = ntohs(endpoint.sin_port);
    server->listener = h2o_evloop_socket_create(
        server->loop, descriptor, H2O_SOCKET_FLAG_DONT_READ);
    if (server->listener == NULL) {
        (void)close(descriptor);
        h2o_server_destroy(server);
        return h2o_result_error(vm, "could not create H2O listener");
    }
    server->listener->data = server;
    h2o_socket_read_start(server->listener, lime_h2o_on_accept);

    LangValue handle;
    if (!lang_native_handle_value(vm, server, h2o_server_drop, &handle)) {
        h2o_server_destroy(server);
        return h2o_native_failure("could not wrap H2O server");
    }
    return h2o_result_value(vm, handle);
}

static LangNativeResult h2o_server_port_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)vm;
    LimeH2OServer *server = arg_count == 1U
        ? get_h2o_server(&args[0]) : NULL;
    if (server == NULL)
        return h2o_native_failure("H2OServerPort expects an H2O server");
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_I64, .as.i64=server->port}, NULL
    };
}

static void h2o_server_update_stop_requested(LimeH2OServer *server) {
    if (server->handle_signals && lime_h2o_signal_stop_requested != 0)
        server->stopping = true;
}

static LangNativeResult h2o_stop_requested_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)vm;
    LimeH2OServer *server = arg_count == 1U
        ? get_h2o_server(&args[0]) : NULL;
    if (server == NULL)
        return h2o_native_failure(
            "H2OStopRequested expects an H2O server");
    h2o_server_update_stop_requested(server);
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_BOOL, .as.boolean=server->stopping}, NULL
    };
}

static LangNativeResult h2o_shutdown_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LimeH2OServer *server = arg_count == 1U
        ? get_h2o_server(&args[0]) : NULL;
    if (server == NULL)
        return h2o_result_error(vm,
            "H2OTryShutdown expects an H2O server");
    h2o_server_drain(server);
    return h2o_result_value(vm, (LangValue){
        .tag=LANG_VALUE_BOOL, .as.boolean=true
    });
}

static LangNativeResult h2o_accept_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LimeH2OServer *server = arg_count == 1U
        ? get_h2o_server(&args[0]) : NULL;
    if (server == NULL)
        return h2o_result_error(vm, "H2OTryAccept expects an H2O server");
    h2o_server_update_stop_requested(server);
    if (server->stopping)
        return h2o_result_error(vm, "H2O server is stopping");
    if (!h2o_server_start(server))
        return h2o_result_error(vm, "could not start H2O server");
    if (server->pending_head != NULL) {
        int loop_result = h2o_evloop_run(server->loop, 0);
        h2o_server_update_stop_requested(server);
        if (server->stopping)
            return h2o_result_error(vm, "H2O server is stopping");
        if (loop_result != 0 && errno != EINTR)
            return h2o_result_error(vm, "H2O event loop failed");
    }
    while (server->pending_head == NULL) {
        int loop_result = h2o_evloop_run(server->loop, INT32_MAX);
        h2o_server_update_stop_requested(server);
        if (server->stopping)
            return h2o_result_error(vm, "H2O server is stopping");
        if (loop_result != 0 && errno != EINTR)
            return h2o_result_error(vm, "H2O event loop failed");
    }
    LimeH2ORequest *request = calloc(1U, sizeof(*request));
    if (request == NULL)
        return h2o_native_failure("out of memory accepting H2O request");
    request->magic = LIME_H2O_REQUEST_MAGIC;
    request->server = server;
    LimeH2OPendingRequest *pending = server->pending_head;
    server->pending_head = pending->next;
    if (server->pending_head == NULL) server->pending_tail = NULL;
    server->pending_count -= 1U;
    request->request = pending->request;
    free(pending);
    server->references += 1U;
    LangValue handle;
    if (!lang_native_handle_value(vm, request, h2o_request_drop, &handle)) {
        h2o_request_drop(request);
        return h2o_native_failure("could not wrap H2O request");
    }
    return h2o_result_value(vm, handle);
}

static LangNativeResult h2o_request_iovec(
    LangVM *vm, const LangValue *args, size_t arg_count,
    h2o_iovec_t (*select)(h2o_req_t *)
) {
    LimeH2ORequest *request = arg_count == 1U
        ? get_h2o_request(&args[0]) : NULL;
    if (request == NULL || request->request == NULL)
        return h2o_native_failure("invalid H2O request");
    h2o_iovec_t value = select(request->request);
    return h2o_string_value(vm, value.base, value.len);
}

static h2o_iovec_t select_method(h2o_req_t *request) {
    return request->method;
}
static h2o_iovec_t select_target(h2o_req_t *request) {
    return request->path;
}
static h2o_iovec_t select_authority(h2o_req_t *request) {
    return request->authority;
}
static h2o_iovec_t select_body(h2o_req_t *request) {
    return request->entity;
}

static LangNativeResult h2o_request_method_value(
    LangVM *vm, const LangValue *args, size_t count
) { return h2o_request_iovec(vm, args, count, select_method); }
static LangNativeResult h2o_request_target_value(
    LangVM *vm, const LangValue *args, size_t count
) { return h2o_request_iovec(vm, args, count, select_target); }
static LangNativeResult h2o_request_authority_value(
    LangVM *vm, const LangValue *args, size_t count
) { return h2o_request_iovec(vm, args, count, select_authority); }
static LangNativeResult h2o_request_body_value(
    LangVM *vm, const LangValue *args, size_t count
) { return h2o_request_iovec(vm, args, count, select_body); }

static LangNativeResult h2o_request_remote_ip_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LimeH2ORequest *wrapper = arg_count == 1U
        ? get_h2o_request(&args[0]) : NULL;
    if (wrapper == NULL || wrapper->request == NULL)
        return h2o_native_failure("H2ORequestRemoteIpAddress expects a request");
    struct sockaddr_storage address;
    memset(&address, 0, sizeof(address));
    h2o_conn_t *connection = wrapper->request->conn;
    socklen_t length = connection->callbacks->get_peername(
        connection, (struct sockaddr *)&address);
    if (length == 0U) return h2o_string_value(vm, "", 0U);
    char text[INET6_ADDRSTRLEN];
    const void *source = NULL;
    if (address.ss_family == AF_INET) {
        source = &((struct sockaddr_in *)&address)->sin_addr;
    } else if (address.ss_family == AF_INET6) {
        source = &((struct sockaddr_in6 *)&address)->sin6_addr;
    } else {
        return h2o_string_value(vm, "", 0U);
    }
    if (inet_ntop(address.ss_family, source, text, sizeof(text)) == NULL)
        return h2o_string_value(vm, "", 0U);
    return h2o_string_value(vm, text, strlen(text));
}

static LangNativeResult h2o_request_scheme_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LimeH2ORequest *wrapper = arg_count == 1U
        ? get_h2o_request(&args[0]) : NULL;
    if (wrapper == NULL || wrapper->request == NULL)
        return h2o_native_failure("H2ORequestScheme expects a request");
    h2o_conn_t *connection = wrapper->request->conn;
    bool secure = connection->callbacks->get_ptls != NULL &&
        connection->callbacks->get_ptls(connection) != NULL;
    return h2o_string_value(vm, secure ? "https" : "http",
                            secure ? 5U : 4U);
}

static LangNativeResult h2o_request_header_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LimeH2ORequest *request = arg_count == 2U
        ? get_h2o_request(&args[0]) : NULL;
    LangStringView name;
    if (request == NULL || request->request == NULL ||
        !lang_value_string_view(&args[1], &name) || name.length == 0U)
        return h2o_native_failure(
            "H2ORequestHeader expects a request and header name");
    char *lower = malloc(name.length);
    if (lower == NULL) return h2o_native_failure("out of memory");
    for (size_t i = 0U; i < name.length; ++i) {
        unsigned char byte = (unsigned char)name.data[i];
        lower[i] = (char)(byte >= 'A' && byte <= 'Z'
            ? byte + ('a' - 'A') : byte);
    }
    ssize_t index = h2o_find_header_by_str(
        &request->request->headers, lower, name.length, -1);
    free(lower);
    if (index < 0) return h2o_string_value(vm, "", 0U);
    h2o_iovec_t value = request->request->headers.entries[index].value;
    return h2o_string_value(vm, value.base, value.len);
}

static LangNativeResult h2o_request_headers_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LimeH2ORequest *request = arg_count == 1U
        ? get_h2o_request(&args[0]) : NULL;
    if (request == NULL || request->request == NULL)
        return h2o_native_failure("H2ORequestHeaders expects a request");
    size_t length = 0U;
    for (size_t i = 0U; i < request->request->headers.size; ++i) {
        h2o_header_t *header = &request->request->headers.entries[i];
        if (SIZE_MAX - length < header->name->len + header->value.len + 2U)
            return h2o_native_failure("H2O request headers are too large");
        length += header->name->len + header->value.len + 2U;
    }
    char *data = malloc(length == 0U ? 1U : length);
    if (data == NULL) return h2o_native_failure("out of memory");
    size_t cursor = 0U;
    for (size_t i = 0U; i < request->request->headers.size; ++i) {
        h2o_header_t *header = &request->request->headers.entries[i];
        memcpy(data + cursor, header->name->base, header->name->len);
        cursor += header->name->len;
        data[cursor++] = '\0';
        memcpy(data + cursor, header->value.base, header->value.len);
        cursor += header->value.len;
        data[cursor++] = '\0';
    }
    LangNativeResult result = h2o_string_value(vm, data, length);
    free(data);
    return result;
}

static const char *h2o_status_reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 422: return "Unprocessable Content";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default: return NULL;
    }
}

static bool h2o_content_type_valid(LangStringView type) {
    static const char *const allowed[] = {
        "text/html; charset=utf-8", "text/plain; charset=utf-8",
        "text/css; charset=utf-8", "text/javascript; charset=utf-8",
        "application/json; charset=utf-8",
        "application/xml; charset=utf-8", "image/svg+xml", "image/png",
        "image/jpeg", "image/gif", "image/webp", "image/x-icon",
        "font/woff", "font/woff2", "font/ttf", "application/wasm",
        "application/octet-stream"
    };
    for (size_t i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        size_t length = strlen(allowed[i]);
        if (type.length == length &&
            memcmp(type.data, allowed[i], length) == 0) return true;
    }
    return false;
}

static bool h2o_add_response_headers(h2o_req_t *request,
                                     LangStringView block) {
    size_t cursor = 0U;
    while (cursor < block.length) {
        size_t end = cursor;
        while (end + 1U < block.length &&
               !(block.data[end] == '\r' && block.data[end + 1U] == '\n'))
            ++end;
        if (end == cursor || end + 1U >= block.length) return false;
        size_t colon = cursor;
        while (colon < end && block.data[colon] != ':') ++colon;
        if (colon == cursor || colon == end || colon - cursor > 255U)
            return false;
        char lower[256];
        for (size_t i = cursor; i < colon; ++i) {
            unsigned char byte = (unsigned char)block.data[i];
            bool token =
                (byte >= 'a' && byte <= 'z') ||
                (byte >= 'A' && byte <= 'Z') ||
                (byte >= '0' && byte <= '9') ||
                strchr("!#$%&'*+-.^_`|~", byte) != NULL;
            if (!token) return false;
            lower[i - cursor] = (char)(byte >= 'A' && byte <= 'Z'
                ? byte + ('a' - 'A') : byte);
        }
        size_t value_start = colon + 1U;
        while (value_start < end &&
               (block.data[value_start] == ' ' ||
                block.data[value_start] == '\t')) ++value_start;
        for (size_t i = value_start; i < end; ++i) {
            unsigned char byte = (unsigned char)block.data[i];
            if ((byte < 32U && byte != '\t') || byte == 127U) return false;
        }
        if (h2o_add_header_by_str(
                &request->pool, &request->res.headers,
                lower, colon - cursor, 1, block.data + cursor,
                block.data + value_start, end - value_start) < 0)
            return false;
        cursor = end + 2U;
    }
    return true;
}

static int lime_h2o_static_guard(h2o_handler_t *base,
                                 h2o_req_t *request) {
    LimeH2OStaticGuard *guard = (LimeH2OStaticGuard *)base;
    LimeH2OServer *server = guard->server;
    if (server == NULL || server->magic != LIME_H2O_SERVER_MAGIC ||
        server->static_delegate != request) {
        h2o_send_error_404(request, "Not Found", "not found", 0);
        return 0;
    }
    server->static_delegate = NULL;
    h2o_iovec_t headers = server->static_headers;
    server->static_headers = h2o_iovec_init(NULL, 0U);
    if (!h2o_add_response_headers(
            request, (LangStringView){headers.base, headers.len})) {
        h2o_send_error_500(request, "Internal Server Error",
                           "invalid delegated headers", 0);
        return 0;
    }
    return -1;
}

static LangNativeResult h2o_register_static_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LimeH2OServer *server = arg_count == 2U
        ? get_h2o_server(&args[0]) : NULL;
    LangStringView root;
    if (server == NULL ||
        !lang_value_string_view(&args[1], &root) || root.length == 0U)
        return h2o_result_error(vm,
            "H2ORegisterStatic expects a server and static root");
    if (server->context_initialized)
        return h2o_result_error(vm,
            "H2O static roots must be registered before accepting requests");
    if (memchr(root.data, '\0', root.length) != NULL)
        return h2o_result_error(vm, "invalid H2O static root");
    size_t root_length = root.length;
    while (root_length > 1U && root.data[root_length - 1U] == '/')
        --root_length;
    for (LimeH2OStaticMount *mount = server->static_mounts;
         mount != NULL; mount = mount->next) {
        if (mount->root_length == root_length &&
            memcmp(mount->root, root.data, root_length) == 0)
            return h2o_result_value(vm, (LangValue){
                .tag=LANG_VALUE_BOOL, .as.boolean=true
            });
    }

    LimeH2OStaticMount *mount = calloc(1U, sizeof(*mount));
    if (mount == NULL) return h2o_native_failure("out of memory");
    mount->root = malloc(root_length + 1U);
    char internal[96];
    int internal_length = snprintf(
        internal, sizeof(internal), "/.aster-static/%zu/",
        server->static_mount_count);
    if (mount->root == NULL || internal_length < 0 ||
        (size_t)internal_length >= sizeof(internal)) {
        free(mount->root);
        free(mount);
        return h2o_native_failure("could not allocate H2O static mount");
    }
    memcpy(mount->root, root.data, root_length);
    mount->root[root_length] = '\0';
    mount->root_length = root_length;
    mount->internal_prefix = copy_c_string(
        (LangStringView){internal, (size_t)internal_length});
    if (mount->internal_prefix == NULL) {
        free(mount->root);
        free(mount);
        return h2o_native_failure("could not allocate H2O static mount");
    }

    h2o_pathconf_t *path = h2o_config_register_path(
        server->host, mount->internal_prefix, 0);
    LimeH2OStaticGuard *guard = (LimeH2OStaticGuard *)h2o_create_handler(
        path, sizeof(*guard));
    guard->super.on_req = lime_h2o_static_guard;
    guard->server = server;
    (void)h2o_file_register(path, mount->root, NULL, NULL, 0);

    mount->next = server->static_mounts;
    server->static_mounts = mount;
    server->static_mount_count += 1U;
    return h2o_result_value(vm, (LangValue){
        .tag=LANG_VALUE_BOOL, .as.boolean=true
    });
}

static LimeH2OStaticMount *h2o_static_mount_for_path(
    LimeH2OServer *server, LangStringView path
) {
    LimeH2OStaticMount *best = NULL;
    for (LimeH2OStaticMount *mount = server->static_mounts;
         mount != NULL; mount = mount->next) {
        bool prefix = path.length > mount->root_length &&
            memcmp(path.data, mount->root, mount->root_length) == 0 &&
            (mount->root_length == 1U && mount->root[0] == '/' ? true
                : path.data[mount->root_length] == '/');
        if (prefix && (best == NULL ||
                       mount->root_length > best->root_length))
            best = mount;
    }
    return best;
}

static LangNativeResult h2o_respond_file_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LimeH2ORequest *wrapper = arg_count == 4U
        ? get_h2o_request(&args[0]) : NULL;
    int64_t status;
    LangStringView headers;
    LangStringView file_path;
    if (wrapper == NULL || wrapper->request == NULL || wrapper->responded ||
        !h2o_integer_arg(&args[1], &status) || status != 200 ||
        !lang_value_string_view(&args[2], &headers) ||
        !lang_value_string_view(&args[3], &file_path))
        return h2o_result_error(vm, "invalid H2O file response arguments");
    LimeH2OServer *server = wrapper->server;
    LimeH2OStaticMount *mount = h2o_static_mount_for_path(
        server, file_path);
    if (mount == NULL)
        return h2o_result_error(vm,
            "H2O file response is outside the registered static roots");
    if (server->static_delegate != NULL)
        return h2o_result_error(vm, "H2O static delegation is busy");

    size_t relative = mount->root_length;
    if (!(mount->root_length == 1U && mount->root[0] == '/'))
        relative += 1U;
    size_t relative_length = file_path.length - relative;
    size_t internal_length = strlen(mount->internal_prefix);
    char *internal = h2o_mem_alloc_pool(
        &wrapper->request->pool, char,
        internal_length + relative_length + 1U);
    memcpy(internal, mount->internal_prefix, internal_length);
    memcpy(internal + internal_length, file_path.data + relative,
           relative_length);
    internal[internal_length + relative_length] = '\0';
    h2o_iovec_t copied_headers = h2o_strdup(
        &wrapper->request->pool, headers.data, headers.length);

    h2o_req_t *request = wrapper->request;
    server->static_delegate = request;
    server->static_headers = copied_headers;
    h2o_reprocess_request_deferred(
        request, request->method, request->scheme,
        server->host->authority.hostport,
        h2o_iovec_init(internal, internal_length + relative_length),
        NULL, 0);
    wrapper->responded = true;
    wrapper->request = NULL;
    return h2o_result_value(vm, (LangValue){
        .tag=LANG_VALUE_BOOL, .as.boolean=true
    });
}

static bool h2o_prepare_response(h2o_req_t *request, int64_t status,
                                 LangStringView content_type,
                                 LangStringView headers) {
    const char *reason = h2o_status_reason((int)status);
    if (reason == NULL || !h2o_content_type_valid(content_type))
        return false;
    request->res.status = (int)status;
    request->res.reason = reason;
    h2o_add_header_by_str(
        &request->pool, &request->res.headers,
        H2O_STRLIT("content-type"), 1, NULL,
        content_type.data, content_type.length);
    return h2o_add_response_headers(request, headers);
}

static LangNativeResult h2o_respond_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LimeH2ORequest *wrapper = arg_count == 6U
        ? get_h2o_request(&args[0]) : NULL;
    int64_t status;
    LangStringView content_type;
    LangStringView headers;
    LangStringView body;
    if (wrapper == NULL || wrapper->request == NULL || wrapper->responded ||
        !h2o_integer_arg(&args[1], &status) ||
        !lang_value_string_view(&args[2], &content_type) ||
        !lang_value_string_view(&args[3], &headers) ||
        !lang_value_string_view(&args[4], &body) ||
        args[5].tag != LANG_VALUE_BOOL)
        return h2o_result_error(vm, "invalid H2O response arguments");
    h2o_req_t *request = wrapper->request;
    if (!h2o_prepare_response(request, status, content_type, headers))
        return h2o_result_error(vm, "invalid H2O response metadata");
    size_t length = (status == 204 || args[5].as.boolean) ? 0U : body.length;
    h2o_send_inline(request, body.data, length);
    wrapper->responded = true;
    wrapper->request = NULL;
    return h2o_result_value(vm, (LangValue){
        .tag=LANG_VALUE_BOOL, .as.boolean=true
    });
}

static LangNativeResult h2o_stream_begin_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LimeH2ORequest *wrapper = arg_count == 5U
        ? get_h2o_request(&args[0]) : NULL;
    int64_t status;
    LangStringView content_type;
    LangStringView headers;
    if (wrapper == NULL || wrapper->request == NULL || wrapper->responded ||
        !h2o_integer_arg(&args[1], &status) ||
        !lang_value_string_view(&args[2], &content_type) ||
        !lang_value_string_view(&args[3], &headers) ||
        args[4].tag != LANG_VALUE_BOOL)
        return h2o_result_error(vm, "invalid H2O stream arguments");
    h2o_req_t *request = wrapper->request;
    if (!h2o_prepare_response(request, status, content_type, headers))
        return h2o_result_error(vm, "invalid H2O response metadata");

    LimeH2OStream *stream = h2o_mem_alloc_shared(
        &request->pool, sizeof(*stream), lime_h2o_stream_dispose);
    memset(stream, 0, sizeof(*stream));
    stream->super.proceed = lime_h2o_stream_proceed;
    stream->super.stop = lime_h2o_stream_stop;
    stream->magic = LIME_H2O_STREAM_MAGIC;
    stream->server = wrapper->server;
    stream->request = request;
    stream->ready = true;
    h2o_mem_addref_shared(stream);

    LangValue handle;
    if (!lang_native_handle_value(
            vm, stream, lime_h2o_stream_drop, &handle)) {
        h2o_mem_release_shared(stream);
        return h2o_native_failure("could not wrap H2O response stream");
    }
    h2o_start_response(request, &stream->super);
    wrapper->responded = true;
    wrapper->request = NULL;
    return h2o_result_value(vm, handle);
}

static LangNativeResult h2o_stream_write_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    LimeH2OStream *stream = arg_count == 3U
        ? get_h2o_stream(&args[0]) : NULL;
    LangStringView chunk;
    if (stream == NULL ||
        !lang_value_string_view(&args[1], &chunk) ||
        args[2].tag != LANG_VALUE_BOOL)
        return h2o_result_error(vm, "invalid H2O stream write arguments");
    if (chunk.length > 1024U * 1024U)
        return h2o_result_error(vm, "H2O response chunk exceeds 1 MiB");
    while (!stream->ready && !stream->stopped) {
        if (stream->server == NULL ||
            stream->server->magic != LIME_H2O_SERVER_MAGIC)
            return h2o_result_error(vm, "H2O server closed during response");
        if (h2o_evloop_run(stream->server->loop, INT32_MAX) != 0 &&
            errno != EINTR)
            return h2o_result_error(vm, "H2O event loop failed");
    }
    if (stream->stopped || stream->request == NULL)
        return h2o_result_error(vm, "H2O client disconnected");
    if (stream->final_sent)
        return h2o_result_error(vm, "H2O response stream already finished");

    char *copy = NULL;
    if (chunk.length != 0U) {
        copy = malloc(chunk.length);
        if (copy == NULL) return h2o_native_failure("out of memory");
        memcpy(copy, chunk.data, chunk.length);
    }
    stream->inflight = copy;
    stream->ready = false;
    bool final = args[2].as.boolean;
    h2o_iovec_t output = h2o_iovec_init(copy, chunk.length);
    h2o_send(stream->request, chunk.length != 0U ? &output : NULL,
             chunk.length != 0U ? 1U : 0U,
             final ? H2O_SEND_STATE_FINAL : H2O_SEND_STATE_IN_PROGRESS);
    if (final) {
        stream->final_sent = true;
        stream->request = NULL;
    }
    return h2o_result_value(vm, (LangValue){
        .tag=LANG_VALUE_BOOL, .as.boolean=true
    });
}

#else

static LangNativeResult h2o_server_open_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)args;
    (void)arg_count;
    return h2o_result_error(vm,
        "Aster was built without libh2o-evloop support");
}

#define H2O_UNAVAILABLE(name, message)                               \
    static LangNativeResult name(LangVM *vm, const LangValue *args, \
                                 size_t arg_count) {                 \
        (void)vm; (void)args; (void)arg_count;                      \
        return h2o_native_failure(message);                         \
    }

H2O_UNAVAILABLE(h2o_server_port_value, "H2O is unavailable")
H2O_UNAVAILABLE(h2o_stop_requested_value, "H2O is unavailable")
H2O_UNAVAILABLE(h2o_accept_value, "H2O is unavailable")
H2O_UNAVAILABLE(h2o_request_method_value, "H2O is unavailable")
H2O_UNAVAILABLE(h2o_request_target_value, "H2O is unavailable")
H2O_UNAVAILABLE(h2o_request_authority_value, "H2O is unavailable")
H2O_UNAVAILABLE(h2o_request_header_value, "H2O is unavailable")
H2O_UNAVAILABLE(h2o_request_headers_value, "H2O is unavailable")
H2O_UNAVAILABLE(h2o_request_body_value, "H2O is unavailable")
H2O_UNAVAILABLE(h2o_request_remote_ip_value, "H2O is unavailable")
H2O_UNAVAILABLE(h2o_request_scheme_value, "H2O is unavailable")

static LangNativeResult h2o_respond_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)args;
    (void)arg_count;
    return h2o_result_error(vm, "H2O is unavailable");
}

static LangNativeResult h2o_shutdown_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)args;
    (void)arg_count;
    return h2o_result_error(vm, "H2O is unavailable");
}

static LangNativeResult h2o_register_static_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)args;
    (void)arg_count;
    return h2o_result_error(vm, "H2O is unavailable");
}

static LangNativeResult h2o_respond_file_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)args;
    (void)arg_count;
    return h2o_result_error(vm, "H2O is unavailable");
}

static LangNativeResult h2o_stream_begin_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)args;
    (void)arg_count;
    return h2o_result_error(vm, "H2O is unavailable");
}

static LangNativeResult h2o_stream_write_value(
    LangVM *vm, const LangValue *args, size_t arg_count
) {
    (void)args;
    (void)arg_count;
    return h2o_result_error(vm, "H2O is unavailable");
}

#endif

void lang_register_h2o_natives(LangVM *vm) {
    (void)lang_register_native(vm, "H2OTryServerOpenNative",
                               h2o_server_open_value, 7U);
    (void)lang_register_native(vm, "H2OServerPort",
                               h2o_server_port_value, 1U);
    (void)lang_register_native(vm, "H2OStopRequested",
                               h2o_stop_requested_value, 1U);
    (void)lang_register_native(vm, "H2OTryShutdown",
                               h2o_shutdown_value, 1U);
    (void)lang_register_native(vm, "H2OTryAccept",
                               h2o_accept_value, 1U);
    (void)lang_register_native(vm, "H2ORequestMethod",
                               h2o_request_method_value, 1U);
    (void)lang_register_native(vm, "H2ORequestTarget",
                               h2o_request_target_value, 1U);
    (void)lang_register_native(vm, "H2ORequestAuthority",
                               h2o_request_authority_value, 1U);
    (void)lang_register_native(vm, "H2ORequestHeader",
                               h2o_request_header_value, 2U);
    (void)lang_register_native(vm, "H2ORequestHeaders",
                               h2o_request_headers_value, 1U);
    (void)lang_register_native(vm, "H2ORequestBody",
                               h2o_request_body_value, 1U);
    (void)lang_register_native(vm, "H2ORequestRemoteIpAddress",
                               h2o_request_remote_ip_value, 1U);
    (void)lang_register_native(vm, "H2ORequestScheme",
                               h2o_request_scheme_value, 1U);
    (void)lang_register_native(vm, "H2OTryRespond",
                               h2o_respond_value, 6U);
    (void)lang_register_native(vm, "H2OTryStreamBegin",
                               h2o_stream_begin_value, 5U);
    (void)lang_register_native(vm, "H2OTryStreamWrite",
                               h2o_stream_write_value, 3U);
    (void)lang_register_native(vm, "H2ORegisterStatic",
                               h2o_register_static_value, 2U);
    (void)lang_register_native(vm, "H2OTryRespondFile",
                               h2o_respond_file_value, 4U);
}
