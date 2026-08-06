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
    WebApplication app = WebApplication.Create();
    app.MapGet("/", Home);
    delete app;
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

Mapping returns an `EndpointBuilder` for conventional endpoint metadata:

```aster
app.MapGet("/articles/{slug}", Article)
    .WithName("GetArticle")
    .WithDescription("Gets one article")
    .WithTag("articles")
    .Produces(StatusCodes.Status200OK)
    .Produces(StatusCodes.Status404NotFound);
```

Named endpoints generate outbound paths from the same parsed patterns used
for inbound matching:

```aster
using Lime.Routing;

RouteValues values = RouteValues.From("slug", "Aster & C#");
Result<string, string> path = app.Links.GetPathByName(
    "GetArticle", values
);
```

Generation checks required values and route constraints and percent-encodes
path segments. Additional values become encoded query parameters. Unknown
endpoint names and invalid values return `Result.Err`; malformed paths are
never returned. `LinkGenerator` borrows its application, so it must not outlive
the `WebApplication`.

The application also exposes its registered endpoint metadata without copying
or rebuilding the graph:

```aster
EndpointDataSource endpoints = app.Endpoints;
RouteEndpoint endpoint = endpoints.GetEndpoint(0);

Console.WriteLine(endpoint.Pattern);
Console.WriteLine(endpoint.GetMethod(0));
```

`RouteEndpoint` exposes the optional name and description plus indexed methods,
tags, and produced statuses. The data source and its endpoints are borrowed
views; the application owns the stable registered endpoint objects and must
outlive every view. This surface is sufficient for tests and future tooling
without coupling Lime itself to OpenAPI.

Groups contribute a prefix while registering into the same endpoint graph.
They can be nested and expose the same `Map*` family:

```aster
RouteGroup api = app.MapGroup("/api").WithTag("api");
RouteGroup articles = api.MapGroup("/articles")
    .WithDescription("Article endpoints");

articles.MapGet("/", ListArticles);
articles.MapGet("/{slug}", Article);
articles.MapPost("/", CreateArticle);
```

`WithTag` and `WithDescription` apply group metadata to existing and future
endpoints, including endpoints in nested groups. Parent tags are inherited,
duplicate tags are suppressed, and endpoint-builder calls can add or replace
metadata afterward. Route groups remain borrowed handles: the application owns
their policy state and must outlive them.

The same `Map*` names accept synchronous `Response` handlers and asynchronous
`Task<Response>` handlers. Async applications dispatch with `DispatchAsync`;
there is no separate `MapGetAsync` vocabulary.

`WebApplication.Create()` supplies the normal 404 fallback. Use `MapFallback` only when the
application needs to replace it; sync and async fallback handlers are accepted.

Patterns are parsed and validated when registered. Literal routes outrank
constrained parameters, which outrank ordinary parameters and catch-alls.
Chained constraints are conjunctive. Conflicting equal-precedence endpoints
and contradictory or incompatible constraint chains are rejected. Registration
also compiles endpoints by HTTP method and first literal path segment. Normal
dispatch therefore avoids unrelated methods and literal path families and does
not recompute precedence. Parameter-first and catch-all routes remain a
precedence-ordered fallback. The canonical endpoint graph remains the source
for metadata, links, `Allow`, and automatic `OPTIONS` behavior.

Typed route binding supports one route parameter across `MapGet`, `MapPost`,
`MapPut`, `MapPatch`, `MapDelete`, `MapHead`, and `MapMethods`. The parameter
may be `string`, `int`, `long`, or `bool`, and a handler may optionally receive
`Request` first:

```aster
private Response Article(string slug)
{
    return Results.Html(<article>{slug}</article>);
}

private Response User(Request request, int id)
{
    return Results.Text($"{request.Method}:{id}");
}

app.MapGet("/articles/{slug}", Article);
app.MapGet("/users/{id:int}", User);
```

The same shapes work for asynchronous handlers and bound class methods, and
they work through route groups. The selected route value is converted before
the handler runs. A conversion failure after route selection returns 400; a
constraint failure means the endpoint did not match. Registration rejects a
typed handler unless its pattern contains exactly one route parameter.

This is deliberately not advertised as arbitrary Minimal API binding yet.
Multiple route parameters and query/header/body/form binding remain future
work. Until a handler shape is supported, the low-level route-value API
represents absence explicitly:

```aster
switch (request.RouteValue("slug"))
{
    case Option.Some(slug): {
        return Results.Html(<article>{slug}</article>);
    }
    case Option.None: {
        return Results.InternalServerError(<h1>Missing route value</h1>);
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
ReadOnlySpan<byte> bytes = request.BodyBytes();
```

`BodyBytes` is a zero-copy borrowed view over Lime's owned buffered request
body. It can contain arbitrary bytes, including zero, and must not outlive the
`Request`. Applications default to a 1 MiB body limit; configure a smaller or
larger policy before dispatch when required:

```aster
WebApplication app = WebApplication.Create();
app.SetMaxRequestBodySize(8 * 1024 * 1024);
```

Oversized bodies receive 413 before filters or handlers run. Transport-level
limits still bound buffering before Lime dispatch (for H2O, configure
`H2OServerOptions.MaxRequestBodySize` as well).

Handlers return a transport-neutral `Response` constructed through `Results`:

```aster
return Results.Html(<h1>Saved</h1>);
return Results.Text("ready");
return Results.Json("{\"ready\":true}");
return Results.Bytes(encodedBytes);
return Results.NotFound(<h1>Missing</h1>);
return Results.SeeOther("/articles");
return Results.Problem(ProblemDetails.Create(409, "Conflict"));
```

Conventional bodyless results use the real empty-body representation:

```aster
return Results.Ok();
return Results.Accepted("/jobs/42");
return Results.Created("/articles/aster");
return Results.BadRequest();
return Results.InternalServerError();
return Results.StatusCode(418);
```

`Results.Redirect` is 302. `SeeOther`, `PermanentRedirect`,
`TemporaryRedirectPreserveMethod`, and `PermanentRedirectPreserveMethod`
provide 303, 301, 307, and 308 explicitly. Redirects have an empty body and a
validated `Location` header. `StatusCodes` contains named HTTP status values.

Responses can also carry validated headers and cookies, CSS, typed assets,
owned byte lists, files, and bounded streams. `Results.Bytes` copies its
`List<byte>` into the response's ownership. CurrentHttp and H2O send byte and
stream bodies through byte spans without translating each chunk through a
string. Adapters own framing fields such as
`Content-Length`, `Content-Type`, `Connection`, and `Transfer-Encoding`.

## Explicit state

Application state lives in ordinary classes. Pass bound instance methods when
an endpoint needs that state:

```aster
class ArticleService
{
    private Database Database;

    public ArticleService(Database database)
    {
        Database = database;
    }

    public Response List(Request request)
    {
        return Results.Html(<h1>Articles</h1>);
    }
}

ArticleService articles = new ArticleService(database);
Handler listArticles = articles.List;

WebApplication app = WebApplication.Create();
app.MapGet("/articles", listArticles);

delete app;
delete articles;
```

There is no service locator, hidden injection, captured closure, or parallel
stateful router. Bound delegates borrow their receiver, so the service must
outlive the application endpoint graph and remains explicitly managed. Delete
the application before deleting any service object borrowed by its handlers.

## Optional modules

- `Lime.Forms` parses URL-encoded and multipart forms. Multipart parsing walks
  the borrowed request bytes incrementally, enforces configurable body,
  header, field, file, and part-count limits, and spills files beyond
  `MemoryBufferThreshold` to RAII-owned temporary files:

  ```aster
  FormOptions options = FormOptions();
  options.MemoryBufferThreshold = 64 * 1024;
  options.MultipartFileLengthLimit = 16 * 1024 * 1024;
  FormCollection form = request.ReadForm(options);
  ```

  `FormFile.OpenReadStream()` has the same API for buffered and spilled files.
  Copies share temporary-file ownership; the final owner release removes the
  spill file.
- `Lime.Sessions` provides explicit server-side sessions. Session identifiers
  use 256 bits from the operating-system cryptographic random source; SQLite
  stores sessions but is no longer their RNG.
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
