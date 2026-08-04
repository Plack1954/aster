# Lime

Lime is Aster's deliberately small web framework. Its core is ordinary
Aster code and owns application routing, typed requests and responses, and
handler dispatch. It adds no language syntax, reflection, annotations, macros,
global route registration, MVC, ORM, dependency injection, or async runtime.

`Lime.CurrentHttp` is the zero-dependency development and conformance adapter,
not the production-server claim. Lime applications target the transport-neutral
`Request` and `Response` model. The production track is `Lime.H2O`; see
`docs/lime-http-adapters.md` in the Aster repository.

```aster
using Lime;

App app = app_new(missing);
app.get("/", home);
app.get("/articles/:slug", article);
app.post("/articles", create);

Response response = app.dispatch(request);
```

Routes also have `put`, `patch`, `delete`, and `head` helpers. An explicit
HEAD route wins when present; otherwise HEAD dispatches through the matching
GET handler and the transport suppresses its response body. Lime copies route
methods and paths into application-owned storage at registration, so a route
assembled from configuration remains valid after the source `String` leaves
scope; dispatch only borrows those stored values and performs no route clones.

Application middleware is an explicit two-phase pipeline. `app.filter(fn)`
registers a typed request filter which either continues or returns a response
early. `app.after_html(fn)` registers native HTML middleware which receives the
request and owns the page; Lime preserves the response status and leaves text,
CSS, and redirects untouched. Both phases run in registration order, use
non-capturing function values, and allocate no middleware environments.

```aster
FilterResult protect(Request request)
{
    if (request.path == "/private")
    {
        return FilterResult.Respond(
            Response.Redirect(String.from("/login"))
        );
    }
    return FilterResult.Continue();
}

Html frame(Request request, Html page)
{
    return <main data-path=request.path>{page}</main>;
}

app.filter(protect);
app.after_html(frame);
```

Responses are transport-neutral owned data: status, a typed body, and a vector
of validated headers. `Response.Ok(Html)` uses native HTML directly, while
`Response.Text(String)` and `Response.Css(String)` serve owned UTF-8 text
through the transport adapter's fixed safe content types. Response
deconstruction transfers those three owned parts without cloning them.
Handlers which need an explicit status use the body-specific
`Response.Html(status, page)`, `Response.Plain(status, text)`,
`Response.CssStatus(status, css)`, `Response.AssetStatus(status, bytes, kind)`,
or `Response.JsonStatus(status, json)` constructors. The current HTTP adapter
accepts the common registered HTTP status codes and rejects unsupported ones
instead of writing a malformed status line.

Applications add ordinary headers and secure default cookies explicitly:

```aster
Response response = Response.Ok(<h1>Saved</h1>);
switch (response_header("Cache-Control", "private, no-store"))
{
    case Result.Ok(header): { response.add_header(header); }
    case Result.Err(error): { return Response.BadRequest(<p>{error}</p>); }
}
switch (response_cookie("session", sessionId))
{
    case Result.Ok(cookie): { response.add_header(cookie); }
    case Result.Err(error): { return Response.BadRequest(<p>{error}</p>); }
}
return response;
```

Header names and values reject control-byte injection and transport-owned
framing fields. `response_cookie` emits `Path=/; HttpOnly; Secure; SameSite=Lax`
and rejects unsafe cookie names and values. `cookie_options()` supplies those
secure defaults; applications can change the path, optional domain, optional
max age, HttpOnly, Secure, and SameSite settings before passing the value to
`response_cookie_with`. `response_delete_cookie` emits the same cookie scope
with an empty value and `Max-Age=0`. SameSite=None is rejected unless Secure is
enabled. The current HTTP adapter serializes the validated headers; a future
H2O adapter consumes the same response data.

Static assets use the separate ordinary-Aster `lime.static` module. Asset
bodies retain an explicit media kind rather than accepting an arbitrary
content-type string, and the transport adapter maps that kind through its own
allowlist. The built-in set covers JavaScript, JSON, XML, SVG, common web images,
WOFF/WOFF2 fonts, WebAssembly, and generic binary data.

```aster
using Lime.Static;

app.Static("/assets/", string.Concat(root, "/assets"));
```

The declaration serves every safe file below the directory during SSR and
recursively copies the same tree during SSG. Nested files retain their URL,
CSS receives the correct response type, other known extensions receive their
typed asset kind, and output collisions fail the static build. The asset root
is trusted application configuration and must not contain untrusted symlinks.
Files are currently read into one owned buffer, so static directories are for
bounded website assets rather than large downloads or streaming.

A parameterized handler remains ordinary Aster:

```aster
Response article(Request request)
{
    string slug = request.param("slug");
    return Response.Ok(<article>{slug}</article>);
}
```

Applications which need configuration or a database can own one explicit,
concrete state value. Lime passes it read-only to ordinary handler functions;
there are no globals, capturing closures, service locators, or dependency
injection containers.

```aster
struct Site
{
    String title;
    Database database;
}

Response Home(Site site, Request request)
{
    return Response.Ok(<h1>{string_view(site.title)}</h1>);
}

StatefulApp<Site> app = stateful_app_new(site, missing);
app.get("/", home);
```

`StatefulApp<State>` has the same route methods, HEAD behavior, and fallback
behavior as `App`. Its request filters and native HTML middleware receive the
same `in State` value as handlers, so authentication guards and layouts can
use configuration or database handles without global state. The current HTTP
adapter exposes `current_http_dispatch_stateful`; a future H2O adapter can do
the same without changing the application.

```aster
FilterResult Protect(Site site, Request request)
{
    // Query site.database or inspect configuration here.
    return FilterResult.Continue();
}

Html Frame(Site site, Request request, Html page)
{
    return <body data-site=string_view(site.title)>{page}</body>;
}

app.filter(protect);
app.after_html(frame);
```

`request_new` separates the path and query string without allocating. Routing
uses the path only, and `request.param(name)` returns a borrowed route segment.
`request.header(name)` performs case-insensitive, allocation-free lookup over
the parser-owned request headers. The current adapter preserves all headers in
a compact borrowed view, so adding general header access does not make Request
cleanup-managed and does not allocate a map for every request.
`request.query(name)` and `request.form(name)` return decoded owned values as
`Result<Option<String>, string>`; `+` and percent escapes are handled in Lime's
Aster code. Form access requires an
`application/x-www-form-urlencoded` request media type and accepts ordinary
case variations and parameters such as `; charset=utf-8`.
Applications that accept files import `Lime.Forms` and call
`request.ReadForm()`. It accepts both URL-encoded and multipart form data.
`FormCollection.Get` and `GetValues` expose fields; `GetFile` and `Files`
expose `FormFile` values with `Name`, `FileName`, `ContentType`, `Length`, and
`OpenReadStream`. Multipart framing and disposition metadata are validated,
the number of parts is bounded, and the HTTP server's configured body limit is
enforced before parsing begins. Uploaded bytes are not decoded as Unicode.
`request.json()` checks for `application/json` (also accepting media-type
parameters) and returns the already body-limited request bytes as a borrowed
string. It is intentionally not a JSON parser or deserializer. JSON responses
use `Response.Json` or `Response.JsonStatus`; parsing policy belongs in an
ordinary Aster JSON package.
`request.query_raw(name)` remains available for allocation-free raw lookup.
For several fields, `request.query_values()` and
`request.form_values()` decode once into an owned `UrlValues`; repeated
`values.get(name)` calls return borrowed views without further allocation.
`request.cookie(name)` scans the borrowed Cookie header without allocation,
trims ordinary whitespace, ignores malformed pairs, and accepts quoted values.
Outgoing cookies use the transport-neutral response-header model above rather
than a server-specific side channel.
A known path registered for another method produces HTTP 405 through a
transport adapter instead of falling into the 404 handler.

Reverse-proxy headers are ignored unless the application explicitly enables
them and trusts the transport peer. The API follows ASP.NET's familiar names;
`ForwardLimit` defaults to one and Lime currently supports exact proxy
addresses rather than implicit private-network trust.

```aster
using Lime.Forwarding;

ForwardedHeadersOptions forwarded = ForwardedHeadersOptions();
forwarded.ForwardedHeaders = ForwardedHeaders.All;
forwarded.KnownProxies.Add("127.0.0.1");
forwarded.KnownProxies.Add("::1");
app.UseForwardedHeaders(forwarded);
```

After trusted forwarding is applied, handlers read `request.Scheme()`,
`request.Host()`, and `request.RemoteIpAddress()`. Unknown peers cannot replace
those values with spoofed `X-Forwarded-*` headers. `Lime.H2O` obtains the actual
scheme and peer address from H2O; `Lime.CurrentHttp` reports its accepted socket
peer and uses `http`.

`Lime.Sessions` supplies opt-in server-side sessions without putting
application values in browser cookies. `SessionStore.Create()` uses a private
in-memory SQLite database; `SessionStore.Create(path)` persists sessions in an
application database file. SQLite's `randomblob` generates 256-bit identifiers.
The public value API is `GetString`, `SetString`, `Remove`, and `Clear`.

```aster
Session session = sessions.Open(request);
session.SetString("user", "brandon");

Response response = Response.Redirect("/account");
session.Commit(ref response);
return response;
```

Only a newly issued identifier needs committing to the response. Session
cookies retain Lime's HttpOnly, Secure, and SameSite=Lax defaults, receive the
configured idle timeout as Max-Age, and expired server-side records are
deleted as requests open sessions.

`Response.Stream(stream, kind)` and `Response.StreamStatus` return readable
`System.IO.Stream` values without materializing the whole response. The current
HTTP adapter reads fixed 64 KiB blocks and emits HTTP chunked framing with the
same validated response headers as buffered responses. Mounted files use the
adapter-neutral `Response.File`: H2O delegates delivery to its native file
handler, CurrentHttp copies bounded blocks, and SSG writes the same source file.

The current socket server enforces header, body, timeout, and keep-alive request
limits in `HttpTryServerOpen`. Lime's `OnException` boundary catches route,
filter, middleware, and fallback exceptions before a response begins. Once a
stream has sent its headers, later I/O failure closes the connection because an
HTTP error response can no longer safely replace it.

The core `lime` module does not open sockets and does not depend on Aster's
hand-written HTTP server. `lime.current_http` is the development/conformance
adapter. `lime.h2_o` is the production track and uses `H2OServerOptions` for
its address, port, body bound, request timeout, graceful-shutdown timeout,
bounded request queue, and SIGINT/SIGTERM behavior. `H2OServe` drains H2O
connections before returning.
The supported public deployment shape is nginx terminating TLS in front of a
loopback-only Aster/H2O process. Aster does not own certificates or TLS
configuration. The build, nginx, systemd, restart, and rollback recipe is in
`docs/lime-nginx-deployment.md` in the Aster repository.
Static cache policy is configured with `StaticFileOptions` on `app.Static`, so
it is preserved across adapters rather than hidden in the transport.

## Lime Browser 0.1

`lime.browser` is optional. A server-rendered page remains ordinary native
Aster HTML; only elements with native `onclick`, `oninput`, `onchange`, or
`onsubmit` handlers participate in browser hydration. There is no VDOM, SPA
lifecycle, macro, hook, signal graph, or Aster async runtime.

A normal binary target pairs its server entry with a browser entry:

```toml
[target.site]
kind = "bin"
entry = "app.server"
browser_entry = "app.browser"
```

```sh
lang project build-web aster.toml build/browser site
```

The output contains `site-server.c`, `site.wasm`, `aster.js`, and the tiny
`site.js` loader. `BrowserAssets` restricts serving to those three deployable
browser files, maps them through Lime's typed static responses, and emits the
ordinary module script tag:

```aster
Response BrowserAsset(Site site, Request request)
{
    switch (site.browser.serve(request))
    {
        case Result.Ok(response): { return response; }
        case Result.Err(error): {
            return Response.NotFound(<p>{error}</p>);
        }
    }
}

Html Page(Site site) {
    return <body>
    <Counter />
    {site.browser.loader()}
    </body>;
}
```

Application startup constructs `site.browser` with
`browser_assets("build/browser", "/browser", "site")` and registers
`browser_asset` at `/browser/:name`.

Handler metadata and ABI boundaries are generated by Aster. Browser handlers
support integer scalars, Boolean values, borrowed UTF-8 strings, owned String
and Html results, and checked flat patch structs containing supported scalar,
String, or Html fields. State is persistent and local to the nearest ordinary
form or element ID. The small runtime projects results directly into retained
DOM nodes and applies collection Html by child ID; it never rerenders a page
or constructs a virtual tree.

Only forms carrying a native `onsubmit` handler are intercepted after WASM has
loaded. Their normal `action` and `method` remain intact, so the same server
handler is the no-JavaScript fallback. Forms without browser metadata are
never touched.

## Lime SSG 0.1

`lime.ssg` materializes the real Lime application; it does not introduce a
second page tree, file router, or build-only handler API. Every concrete GET
route is included automatically:

```aster
App app = app_new(missing);
app.get("/", home);
app.get("/about/", about);
```

Parameterized routes take an ordinary build-source function. The function
derives finite concrete URLs from the same content or application state used
by SSR, so there is no second handwritten page list:

```aster
List<string> ArticlePagesFrom(List<Article> articles)
{
    List<string> paths = new();
    foreach (Article article in articles)
    {
        paths.Add(article.destination);
    }
    return paths;
}

List<string> ArticlePages(Site site)
{
    return ArticlePagesFrom(site.articles);
}

app.Get("/articles/:slug/", Article, ArticlePages);
```

`Get` registers both the GET route and its build source. Lime rejects
unsafe URLs, duplicate URLs, and concrete URLs that do not match the declared
route pattern. Stateless apps use a no-argument `BuildSource`. The older
`app.page(path)` remains available for individual exceptional pages.
`TryGetFrom` is the value-based alternative when registration failure should
be inspected rather than thrown. Static directories follow the same pairing:
`Static` throws and `TryStatic` returns `Result`.

Route, filter, middleware, and fallback exceptions are caught at the app
boundary. The default response is HTTP 500; applications can replace it once:

```aster
Response HandleException(Exception error)
{
    Log(error.Message);
    return Response.InternalError(<h1>Something went wrong</h1>);
}

app.OnException(HandleException);
```

`lime.content` combines deterministic discovery with a small explicit
TOML-frontmatter document model:

```aster
using Lime.Content;

List<ContentDocument> articles = LoadContentDirectory(
    contentRoot, ".md"
);

foreach (ContentDocument article in articles)
{
    string title = article.Required("title");
    List<string> categories = article.Strings("categories");
    string body = article.body;
}
```

The collection returns documents in deterministic filename order. Each keeps
its source path, parsed fields, and body following the closing `+++`. The
parser deliberately supports the frontmatter surface used by Lime sites:
scalar values and quoted string lists. It does not pretend to be a complete
TOML implementation. `LoadContentDocument(path)` loads an individual page or
site record. These ordinary names throw on I/O or malformed required content;
`TryLoadContentDocument`, `TryLoadContentDirectory`, `TryRequired`, and
`TryStrings` preserve explicit `Result` handling where it is useful.

`lime.markdown` provides the safe native `Markdown` component. Its current
bounded surface handles paragraphs, ordered lists, and strong emphasis. Source
text is escaped before Lime's known markup is emitted; applications do not
need to call `Html.UnsafeRaw`. Adding a content file can now change loaded
content, SSR data, and parameterized SSG output without editing the route graph.

The site entry constructs that same app and hands it to `site_build`:

```aster
String outputRoot = try native_process_arg(0);
App app = try create_app();
SiteBuild built = try site_build(app, string_view(outputRoot));
```

Build it with a normal project target:

```sh
lang project build-site aster.toml public site
```

The selected target is an ordinary binary and receives `public` as its sole
process argument. The build dispatches actual GET requests through filters,
routing, handlers, and HTML middleware. A `/`-terminated HTML URL becomes an
`index.html`; text, CSS, and typed asset responses retain their concrete file
path. The configured fallback runs through the same filters and HTML
middleware, must return HTTP 404, and is written directly to root `404.html`.
Other non-200 responses, runtime headers, unsafe URLs, duplicate URLs, and
output collisions fail the build. The current builder overwrites generated
files it owns but does not delete stale files, so release builds should use a
clean output directory.

The blog routing fixture uses one `CreateApp()` for both modes:

```sh
./build/lang project build-site packages/lime/aster.toml public blog_fixture
./build/lang project check packages/lime/aster.toml blog_fixture_server
```

Its source is under `packages/lime/src/examples/blog`. A typed post collection
drives the index and concrete parameterized pages, so post metadata and route
instances are not maintained separately. It checks native HTML and CSS, an
XML-escaped RSS response, HTTP dispatch, and SSG dispatch. It is intentionally
classified as a fixture: one hardcoded post and placeholder copy do not test
real authoring, publishing, assets, search, archives, or application DX.

The real application baseline and current gap are recorded in
[`../../docs/lime-application-baseline.md`](../../docs/lime-application-baseline.md).

## Non-goals

Lime is not waiting to grow MVC, an ORM, dependency injection,
WebSockets, a JavaScript framework, compiler-integrated
routing, file-routing magic, reflection, annotations, or decorators. These are
deliberate exclusions, not deferred roadmap items.

Aster async and Lime handlers returning `Task<Response>` are accepted future
work. They are language/runtime capabilities, not framework magic. Lime Browser
signals remain an undecided and independent browser-reactivity question.

```sh
./build/lang project check packages/lime/aster.toml library
./build/lang project check packages/lime/aster.toml current_http
./build/lang project check packages/lime/aster.toml static
./build/lang project check packages/lime/aster.toml browser
./build/lang project check packages/lime/aster.toml ssg
./build/lang project test packages/lime/aster.toml
```
