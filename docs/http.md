# Development/reference HTTP adapter

The current HTTP adapter is a blocking, single-threaded HTTP/1.1 server. Its
socket and bounded request parser live behind the registration-based C FFI;
routing, handlers, control flow, and HTML construction are ordinary Aster
code.

This adapter exists for local development, conformance tests, differential
VM/generated-C verification, and benchmarks. It is not Lime's recommended
production or Internet-facing transport and will not grow into a competing web
server. The production-adapter contract and H2O track are documented in
`docs/lime-http-adapters.md`.

Aster 0.2 also exposes `HttpServerOpenConfig(address, port,
max_header_bytes, max_body_bytes, timeout_ms)`. Header limits may be 1–64 KiB,
body limits 0–16 MiB, and socket read/write timeouts 1 ms–5 minutes. The
two-argument entry point defaults to 16 KiB, 1 MiB, and five seconds.

`HttpServerOpenKeepAlive` adds a sixth limit: the maximum number of
sequential requests accepted on one connection, from 1 through 1000. The
five-argument and two-argument entry points retain close-per-response behavior.

## Version A: fixed response

The first API serves one pre-rendered response:

```text
extern NativeHandle HttpServerOpen(string address, long port);
extern long HttpServerPort(NativeHandle server);
extern long HttpServeOnce(
    NativeHandle server,
    string body
);
```

`HttpServerOpen` currently accepts a numeric IPv4 address. Port `0` asks the
operating system to select an available port. The returned handle owns the
listening socket and closes it during deterministic destruction.

`HttpServeOnce` blocks until one connection arrives, reads at most 16 KiB of
request headers, and accepts `GET` or `HEAD`. It returns the emitted HTTP status
code. Responses contain:

- `Content-Type: text/html; charset=utf-8`
- an exact `Content-Length`
- `Connection: close`
- `X-Content-Type-Options: nosniff`

Each connection is closed after its response. Unsupported methods receive 405;
oversized request headers receive 431.

Run the example:

```sh
./build/lang run examples/http_fixed.as
curl http://127.0.0.1:8080/
```

The example serves one request and exits. The response begins as a typed `Html`
tree and passes through the escaping SSR renderer before reaching the socket.

The POSIX backend works on Linux and macOS. The source compiles on Windows but
returns a clear unsupported-platform native error until the Winsock adapter is
implemented.

## Version B: request data

The request API separates accepting a connection from producing its response:

```text
extern NativeHandle HttpAccept(NativeHandle server);
extern string HttpRequestMethod(NativeHandle request);
extern string HttpRequestPath(NativeHandle request);
extern string HttpRequestHeader(
    NativeHandle request,
    string name
);
extern string HttpRequestBody(NativeHandle request);
extern long HttpRespondHtml(
    owned NativeHandle request,
    long status,
    string body
);
```

`HttpAccept` produces a cleanup-managed request handle that owns its client socket.
Dropping an unanswered request closes the socket. `HttpRespondHtml` consumes
the request, reads the immutable response string, writes the response, and closes the
connection. It accepts status 200, 201, 204, 400, 404, 405, or 500.

Method, path, header, and body results are ordinary immutable `string` values.
The native adapter copies request storage before returning, so these values do
not depend on the request handle's lifetime. Header lookup is ASCII
case-insensitive and returns an empty string when the header is absent.

Bodies require one valid decimal `Content-Length`. Conflicting or malformed
lengths and transfer encodings are rejected. Bodies over the configured bound
receive 413 before allocation. `HttpRequestBody` returns exact-length UTF-8
bytes as a string; arbitrary binary bodies require a byte-slice API.

## Versions C and D: routing and reusable handlers

Routing requires no native registration and no routing DSL. It is an ordinary
language function. The Aster-written `std.router` module accepts literal
paths and `:name` segments:

```text
private Html user(string path) {
    string id = HttpPathParam("/users/:id", path, "id");
    return <strong>{id}</strong>;
}

Router router = RouterNew(NotFound);
router = RouterAdd(router, "/", home);
router = RouterAdd(router, "/users/:id", user);
```

`HttpPathMatches` and `HttpPathParam` ignore the query suffix and compare
complete path segments. Extracted parameters are ordinary strings, are not
percent-decoded, and remain valid independently of the request handle.

`std.http_app` builds the typed application surface above those primitives:

```text
struct Request {
    string method;
    string path;
    string host;
    string body;
}

public delegate Response Handler(Request request);
```

Its `Response` variants own `Html`, and its router stores exact non-capturing
`Handler` values. The borrowed fields still point into the socket-owning
request handle; the language intentionally does not prove that they cannot
escape.

The router owns a growable `List<Route>` and is borrowed during dispatch.
Routes match method and path in insertion order:

```text
Router router = RouterNew(NotFound);
router = RouterGet(router, "/", home);
router = RouterGet(router, "/users/:id", ShowUser);
RouterPostMut(router, "/users", CreateUser);
```

Likewise, a handler is an ordinary function that owns one request. A reusable
server loop accepts one RAII connection handle and may reuse it:

```text
NativeHandle request = HttpAccept(server);
bool active = true;
while (active) {
    string path = HttpRequestPath(request);
    Html document = route(path);
    string body = document.ToHtmlString();
    active = HttpRespondHtmlReuse(
        request,
        200,
        body,
    );
    if (active) {
        active = HttpRequestNext(request);
    }
}
```

The request handle remains the sole owner of the client socket. A reusable
response borrows it; leaving scope closes any still-open connection.
`HttpRespondHtmlReuse` returns false when the client requested closure, the
configured request count was reached, pipelined bytes were detected, or the
connection cannot be reused. `HttpRequestNext` waits under the configured
read timeout and resets all borrowed request views before parsing the next
request.

The `http_try_*` entry points return `Result` values for server creation,
accept/parsing, connection advancement, and reusable response writes. This
lets the Aster server loop map routine transport failures without converting
them into VM traps. Allocation failure while constructing the `Result` remains
a native runtime failure.

Application responses include typed redirects:

```text
return Response.Redirect("/issues");
```

`ResponseSend(request, response)` handles successful HTML, redirects,
not-found pages, and internal-error pages in one place. Redirects use
`303 See Other`, carry no response body, and accept only local paths beginning
with `/`. Carriage returns and line feeds are rejected so a location cannot
inject response headers. This supports the usual POST-redirect-GET flow.

`HttpFormValue(body, name)` extracts one
`application/x-www-form-urlencoded` field into a `string`. It decodes
`+` and percent escapes and returns malformed or missing fields through
`Result`; it does not retain native request storage.

See `examples/http_server.as` for status selection, request headers and
bodies, typed function-value routing, middleware, configured limits, and the
reusable accept loop:

```sh
./build/lang run examples/http_server.as
# Substitute the collision-free port printed by the server:
curl http://127.0.0.1:PORT/
curl http://127.0.0.1:PORT/health
curl http://127.0.0.1:PORT/missing
```

## Deliberate limits

Responses may be streamed without buffering the complete body:

```text
HttpStreamBegin(request, 200, "text/plain; charset=utf-8");
HttpStreamChunk(request, "first");
HttpStreamChunk(request, "second");
HttpStreamFinish(request);
```

The stream uses HTTP/1.1 chunk framing. Only fixed HTML, plain-text, and CSS
content types are accepted, preventing header injection. The request remains
the RAII owner; dropping it during a partial stream closes the socket.

Fixed-length reusable responses use a content-type allowlist through
`HttpRespondReuse`. In addition to UTF-8 HTML, plain text, and CSS, the
header-capable response boundary accepts Lime's explicit static-asset media
types: JavaScript, JSON, XML, SVG, PNG, JPEG, GIF, WebP, icons, WOFF/WOFF2,
WebAssembly, and generic binary data. Application strings never become raw
HTTP content-type syntax. The documentation-server example uses this API for
`/assets/site.css`.

The parser stores at most 64 headers. Sequential HTTP/1.0 and HTTP/1.1
keep-alive is bounded by time and request count. Pipelining is deliberately
not implemented: detecting bytes beyond the framed request disables reuse.
Inbound chunked encoding, TLS, concurrency, URL decoding, multipart parsing,
and binary bodies are not implemented. This is a hardened integration
experiment, not a production or internet-exposure claim.
