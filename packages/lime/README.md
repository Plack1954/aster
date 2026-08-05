# Lime

Lime is Aster's explicit web framework. It provides transport-neutral routing,
requests, responses, forms, sessions, static files, server-side rendering, and
static generation. It has no controllers, MVC, Razor-style templates,
reflection, global route registration, or dependency-injection container.

Lime applications use the same endpoint graph with the in-memory dispatcher,
the development HTTP server, H2O, and static generation.

## Endpoints

```aster
using Lime;

private Response Home(Request request)
{
    return Results.Html(
        <main>
            <h1>Lime</h1>
        </main>
    );
}

int main()
{
    App app = AppNew();
    app.MapGet("/", Home);
    return 0;
}
```

Lime supplies `MapGet`, `MapPost`, `MapPut`, `MapPatch`, `MapDelete`,
`MapHead`, and `MapMethods`. Patterns use braces:

```aster
app.MapGet("/articles/{slug}", Article);
app.MapGet("/users/{id:int}", User);
app.MapGet("/orders/{id:long:min(1):max(999999)}", Order);
app.MapGet("/search/{term?}", Search);
app.MapGet("/files/{*path}", File);
```

The same `Map*` names accept synchronous `Response` handlers and asynchronous
`Task<Response>` handlers. Async applications dispatch with `DispatchAsync`;
there is no separate `MapGetAsync` vocabulary.

`AppNew()` supplies the normal 404 fallback. Use `MapFallback` only when the
application needs to replace it; sync and async fallback handlers are accepted.

Patterns are parsed and validated when registered. Literal routes outrank
constrained parameters, which outrank ordinary parameters and catch-alls.
Chained constraints are conjunctive. Conflicting equal-precedence endpoints
and contradictory or incompatible constraint chains are rejected.

Typed handler binding is not implemented yet. The current low-level route
value API represents absence explicitly:

```aster
switch (request.RouteValue("slug"))
{
    case Option.Some(slug): {
        return Results.Html(<article>{slug}</article>);
    }
    case Option.None: {
        return Results.InternalError(<h1>Missing route value</h1>);
    }
}
```

An explicit HEAD endpoint wins. Otherwise GET handles HEAD and the adapter
suppresses the body. Lime answers automatic OPTIONS requests with 204 and
returns an `Allow` header for OPTIONS and 405 responses. A 204 uses a real
empty response body and emits neither a content type nor content length.

## Requests and results

`Request` exposes `Method`, `Path`, `Target`, `QueryString`, `Host`, `Scheme`,
`RemoteIpAddress`, `ContentType`, and `Body`. Header, cookie, route, query,
form, and JSON access remains explicit:

`Path` is the normalized routing path: valid percent-encoded UTF-8 is decoded,
`%2F` remains encoded so it cannot change segment boundaries, `+` remains a
plus, and dot segments are removed. `Target` and `QueryString` retain their
raw transport spelling.

```aster
Option<string> trace = request.Header("X-Trace");
Option<string> session = request.Cookie("session");
Result<Option<string>, string> page = request.Query("page");
Result<Option<string>, string> title = request.Form("title");
Result<string, string> json = request.Json();
```

Handlers return a transport-neutral `Response` constructed through `Results`:

```aster
return Results.Html(<h1>Saved</h1>);
return Results.Text("ready");
return Results.Json("{\"ready\":true}");
return Results.NotFound(<h1>Missing</h1>);
return Results.SeeOther("/articles");
return Results.Problem(ProblemDetails.Create(409, "Conflict"));
```

`Results.Redirect` is 302. `SeeOther`, `PermanentRedirect`,
`TemporaryRedirectPreserveMethod`, and `PermanentRedirectPreserveMethod`
provide 303, 301, 307, and 308 explicitly. `StatusCodes` contains named HTTP
status values.

Responses can also carry validated headers and cookies, CSS, typed assets,
files, and bounded streams. Adapters own framing fields such as
`Content-Length`, `Content-Type`, `Connection`, and `Transfer-Encoding`.

## Explicit state

Until Aster's class and bound-method model lands, an application with state uses
`StatefulApp<State>`:

```aster
struct Site
{
    Database database;
}

private Response Home(Site site, Request request)
{
    return Results.Html(<h1>Home</h1>);
}

StatefulApp<Site> app = StatefulAppNew(site, Missing);
app.MapGet("/", Home);
```

There is no hidden service lookup. This duplicated stateful surface is
temporary and will be replaced by the class-based application design rather
than retained as a second framework.

## Optional modules

- `Lime.Forms` parses URL-encoded and multipart forms.
- `Lime.Sessions` provides explicit server-side sessions.
- `Lime.Static` mounts safe static-file roots.
- `Lime.Browser` adds optional browser/Wasm assets and hydration support.
- `Lime.Ssg` executes the real GET endpoint graph to produce static output.
- `Lime.Content` and `Lime.Markdown` support content-oriented sites.
- `Lime.CurrentHttp` is the development and conformance adapter.
- `Lime.H2O` is the production adapter track.

Transport responsibilities and production deployment are documented in
[`../../docs/lime-http-adapters.md`](../../docs/lime-http-adapters.md) and
[`../../docs/lime-nginx-deployment.md`](../../docs/lime-nginx-deployment.md).
The Minimal API restructuring mission is specified in
[`../../docs/lime-minimal-api-mission.md`](../../docs/lime-minimal-api-mission.md).

## Verify

```sh
./build/lang project check packages/lime/aster.toml library
./build/lang project test packages/lime/aster.toml
```
