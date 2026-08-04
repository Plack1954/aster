# Lime HTTP adapters

## Roles

Lime application code is independent of the HTTP implementation. Routes,
forms, sessions, middleware, exception handling, static-file policy, and typed
responses belong to Lime. Connection management and protocol mechanics belong
to an adapter.

`Lime.CurrentHttp` is the development and conformance adapter. It is a small,
blocking HTTP/1.1 implementation used for local development, VM/C differential
tests, benchmarks, and specifying adapter behavior. It is not the recommended
Internet-facing production server. New protocol features are not added to it
unless they are required to test Lime's adapter contract; correctness and
security defects are still fixed.

`Lime.H2O` is the production-adapter track. It embeds libh2o and must preserve
the same Lime `Request`, `Response`, routing, and exception semantics. H2O owns
HTTP parsing, connection persistence, protocol negotiation, transport
timeouts, and response I/O. Lime owns application decisions.

## Contract

Every adapter must provide these operations without changing handlers:

1. Open a configured server and deterministically close it.
2. Produce one request containing the method, original target, authority,
   content type, cookie header, all headers, and a bounded body.
3. Preserve exact body bytes. Text decoding is an application/library choice.
4. Send Lime's validated status, headers, cookies, and typed content kind.
5. Send native `Html`, UTF-8 text, CSS, and binary assets correctly.
6. Report routine accept and write failures as ordinary errors.
7. Close or cancel an unfinished request deterministically.
8. Never allow an exception to cross the C ABI boundary.

The first H2O slice may use H2O's bounded buffered request entity. The complete
adapter will additionally expose bounded request-body streaming and response
streaming with disconnect and backpressure handling. Buffered JSON and normal
forms remain convenience APIs above that stream.

## H2O pin

The audited upstream is `h2o/h2o` commit
`706842c0f8c0d9422efb97a4d8ef7d6ec9df87b7` from 4 August 2026. H2O is
MIT-licensed, provides `libh2o` and `libh2o-evloop`, and implements HTTP/1.x
and HTTP/2; upstream describes HTTP/3 as experimental. Active development is
on `master`, while the newest published tags are 2.3.0 betas. Aster therefore
pins and tests a commit instead of tracking an unqualified latest version.

The adapter uses `libh2o-evloop` to avoid imposing libuv. The normal production
boundary places nginx in front of H2O: nginx owns public TLS and HTTP protocol
negotiation, while Aster/H2O listens on loopback. Direct H2O TLS is not on the
Lime roadmap. See `docs/lime-nginx-deployment.md`.

## Production configuration and shutdown

`H2OServerOptions()` supplies explicit defaults for the listen address, port,
maximum buffered request body, request/I/O timeout, HTTP/2 graceful-shutdown
timeout, bounded parsed-request queue, and signal handling. Applications change
named fields instead of passing an ordered group of transport numbers:

```aster
H2OServerOptions options = H2OServerOptions();
options.Port = 3000;
options.MaxRequestBodySize = 65536;
options.RequestTimeoutMilliseconds = 5000;
options.MaxQueuedRequests = 256;

NativeHandle server = try H2OTryServerOpen(options);
bool bound = try H2OBindStateful(server, app);
bool stopped = try H2OServeStateful(server, app);
```

With `HandleSignals` enabled, SIGINT and SIGTERM stop the accept loop. Lime
closes the listener, asks H2O to gracefully shut down existing HTTP/1 and
HTTP/2 connections, drains the event loop, and returns from `H2OServe` normally.
Dropping the server handle remains a deterministic cleanup fallback. The
integration suite proves request-body rejection, a stalled incomplete-request
timeout, normal dispatch after both failures, and clean signal-driven exit in
the VM and generated C.

H2O can make several parsed requests ready during one event-loop turn. Lime
keeps those requests in the bounded `MaxQueuedRequests` FIFO rather than
discarding every request after the first. The event loop is serviced between
queued dispatches so response writes, disconnects, and timeouts continue to
progress while Aster application handlers remain synchronous.

Build the pinned dependency and an H2O-enabled Aster compiler with:

```sh
./tools/build_h2o.sh
```

This produces `build/h2o/lang`. A manual build can instead set both
`ASTER_H2O_SOURCE_ROOT` and `ASTER_H2O_BUILD_ROOT`; leaving both unset keeps
the ordinary Aster build free of the H2O dependency. Setting only one is a
configuration error.

`tools/build_lime_h2o_app.sh` emits and compiles an application target into a
relocatable server bundle with the pinned H2O shared library and a relative
runtime search path. The nginx and systemd production templates are described
in `docs/lime-nginx-deployment.md`.

Lime `Response.Stream` uses an H2O generator. Aster reads at most one 64 KiB
chunk ahead, and the next native write waits until H2O invokes `proceed`, so a
slow client cannot cause the complete response to accumulate in memory. H2O's
`stop` callback turns a disconnect into an ordinary adapter error, and Lime's
`finally` cleanup closes the source stream. HEAD responses close the source
without reading it. The integration suite covers multi-chunk delivery and a
client disconnect during an unbounded `/dev/zero` response in both the VM and
generated C.

This is application-level response streaming, not the end of transport work.
Request-body streaming and further load measurements remain separate
production slices. Public TLS and HTTP/2 configuration belong to nginx rather
than the Aster adapter.

## Static files

A mounted static file becomes an adapter-neutral Lime `Response.File`; the
application does not name an H2O API. Filters, explicit routes, static-path
validation, fallback behavior, and SSG discovery still execute in Lime.

Before its first accept, `H2OBind` or `H2OBindStateful` registers the roots
already present in that Lime route graph. When Lime selects a file response,
the native adapter delegates the same request internally to H2O's registered
file handler. H2O therefore owns sendfile/pread delivery, exact content length,
HEAD, ETag, Last-Modified, conditional 304 responses, byte ranges, and 416
handling. CurrentHttp opens the same `Response.File` as a normal bounded stream,
and SSG copies it into the static output.

The internal H2O paths are guarded by request identity. A client requesting an
internal path directly receives 404, so registering a root does not create a
second public routing surface or bypass Lime's request filters.

Static cache behavior is application policy rather than adapter configuration.
The optional `StaticFileOptions` argument to `app.Static` adds the same validated
`Cache-Control` header in every adapter:

```aster
StaticFileOptions assets = StaticFileOptions();
assets.MaxAgeSeconds = 3600;
app.Static("/assets/", "public/assets", assets);
```

`Immutable` is accepted only with a positive maximum age. H2O preserves this
Lime response header when delegating the file to its native file handler.

## Nook load validation

The generated-C Nook production server was measured locally on 4 August 2026
using an optimized C17 build and ApacheBench against its complete 16,045-byte
home page. These figures are development-machine measurements, not portable
capacity claims:

| Workload | Requests/second | Failures |
| --- | ---: | ---: |
| 5,000 requests, concurrency 1 | 15,504 | 0 |
| 20,000 keep-alive requests, concurrency 20 | 26,575 | 0 |
| 50,000 keep-alive requests, concurrency 100 | 27,337 | 0 |
| 100,000 keep-alive soak, concurrency 100 | 27,593 | 0 |
| 20,000 new-connection requests, concurrency 100 | 3,798 | 0 |

RSS started at 5.2 MB, warmed to about 9.1 MB, and remained there across the
next 100,000-request soak and the 20,000-connection churn run. The exercise
found and fixed two defects: a single pending-request slot that caused 503s
under concurrency, and an uninitialized generated-C reference count in
`Url.fragment` that leaked memory on Nook renders.

The H2O conformance application also exercises URL-encoded and multipart forms,
file metadata, cookies, SQLite-backed session round trips, redirects,
centralized exception handling, response streaming, native static files,
request limits, timeouts, disconnects, and graceful shutdown through both the
VM and generated C.
