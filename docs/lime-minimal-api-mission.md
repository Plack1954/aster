# Lime Minimal API mission

## Decision

Lime is Aster's explicit, transport-neutral HTTP framework. Its primary public
reference is ASP.NET Core Minimal APIs. Lime will not implement controllers,
MVC, Razor Pages, action discovery, attribute-scanned routing, a routing DSL,
or implicit dependency injection.

This is a behavioral and API reference, not a request to reproduce the .NET
runtime. Lime should copy the parts that make small HTTP applications direct:

- `MapGet`, `MapPost`, `MapPut`, `MapPatch`, `MapDelete`, and `MapMethods`;
- route templates, parameters, constraints, precedence, and diagnostics;
- typed conversion of request data into handler parameters;
- route groups with shared prefixes and endpoint policy;
- ordered application middleware and endpoint filters;
- endpoint metadata and named endpoints;
- link generation from the registered endpoint graph;
- explicit results for HTML, text, JSON, redirects, files, and streams;
- request cancellation, bounded bodies, streaming, and graceful shutdown;
- one application surface across the VM, generated C, H2O, tests, and SSG.

The reference documentation used for this design is Microsoft's current
ASP.NET Core documentation:

- Minimal API parameter binding:
  <https://learn.microsoft.com/aspnet/core/fundamentals/minimal-apis/parameter-binding>
- Minimal API responses:
  <https://learn.microsoft.com/aspnet/core/fundamentals/minimal-apis/responses>
- Minimal API filters:
  <https://learn.microsoft.com/aspnet/core/fundamentals/minimal-apis/min-api-filters>
- ASP.NET Core routing:
  <https://learn.microsoft.com/aspnet/core/fundamentals/routing>
- ASP.NET Core middleware:
  <https://learn.microsoft.com/aspnet/core/fundamentals/middleware>
- `HttpContext` and request cancellation:
  <https://learn.microsoft.com/aspnet/core/fundamentals/use-http-context>

The implementation reference is the official `dotnet/aspnetcore`
`release/10.0` source at commit
`061d555ab10ac0babb8d0701e9edb11bc47db3e0` from 3 August 2026, cloned at
`/home/brandon/learning/comparison/aspnetcore`. The relevant implementation
areas are:

- `src/Http/Routing/src/Patterns` for structural route parsing and matching;
- `src/Http/Routing/src/Matching` for matcher construction and policies;
- `src/Http/Routing/src/RouteEndpointBuilder.cs` and
  `RouteEndpointDataSource.cs` for endpoint construction;
- `src/Http/Routing/src/RouteGroupBuilder.cs` for route groups;
- `src/Http/Routing/src/DefaultLinkGenerator.cs` for outbound paths;
- `src/Http/Http.Extensions/src/RequestDelegateFactory.cs` for the runtime
  handler adapter;
- `src/Http/Http.Extensions/gen/Microsoft.AspNetCore.Http.RequestDelegateGenerator`
  for the compile-time/AOT handler adapter.

The source confirms four architectural choices for Lime. A route pattern is a
structure, not a string repeatedly interpreted by dispatch. Endpoint mapping
builds endpoint descriptions before matcher construction. Matching policy and
handler binding are separate subsystems. Compile-time handler adaptation is a
normal production architecture, not an Aster-specific invention.

## Permanent non-goals

Lime will not acquire any of the following as a consequence of this mission:

- `Controller`, `ControllerBase`, or controller activation;
- Razor, Razor Pages, view discovery, or a second template language;
- conventional `{controller}/{action}` routing;
- attributes which cause endpoints to be discovered by scanning assemblies;
- global route registration;
- service locators or a dependency-injection container;
- binding a handler parameter from DI merely because its type was registered;
- reflection-driven handler invocation;
- runtime code generation;
- a language-level `routes` block or any other routing grammar;
- an ORM, authentication product, or session policy in the routing core;
- requiring Wasm or a browser runtime for server-rendered HTML.

Classes remain part of Aster's intended object model. An application may
explicitly register a bound object method as a handler. That does not make the
object a controller and does not authorize discovery by inheritance, naming,
or attributes.

## Current-state audit

### What is already strong

Lime's transport boundary is substantially better than its application API.
The following work should be preserved:

- `Request` and `Response` are independent of the socket implementation.
- Response bodies distinguish native `Html`, text, CSS, assets, streams, and
  files instead of reducing everything to an untyped byte/string convention.
- Response headers and cookies are validated before reaching an adapter.
- The development HTTP implementation and H2O adapter consume the same request
  and response model.
- H2O owns protocol mechanics, connection persistence, backpressure, file
  delivery, and graceful shutdown rather than leaking those concerns into
  handlers.
- Explicit route matching, static files, filters, and fallback behavior remain
  inside Lime and therefore behave consistently across adapters.
- SSG executes the real GET route graph instead of introducing a separate page
  framework.
- Application state is explicit; Lime has no global service container.
- Request size, timeout, queue, and streaming behavior already have meaningful
  integration coverage.

These are foundations for the new design, not throwaway prototype work.

### Implementation progress

The first routing and HTTP slice is implemented and verified in the current
worktree:

- `Lime.Routing.RoutePattern` parses route templates during registration.
- `{name}`, terminal `{name?}`, and `{*name}` are structural route segments.
  `{*name}` encodes embedded slashes during outbound generation while
  `{**name}` preserves them, matching ASP.NET's catch-all distinction. Both
  forms match an empty remainder.
  The `int`, `long`, `bool`, `alpha`, `required`, `min`, `max`, `range`,
  `length`, `minlength`, and `maxlength` constraints have direct tests.
  Constraints can be chained on one parameter and are evaluated as a
  conjunction, for example `{id:int:min(10):max(20)}`. Registration rejects
  empty chains, incompatible type constraints, contradictory numeric or
  length ranges, and more than eight constraints on one segment.
- malformed patterns, duplicate parameter names, duplicate endpoints, and
  equal-precedence overlapping endpoints are rejected during registration.
- `MapMethods` validates its complete method/route set before mutation, so a
  rejected multi-method endpoint cannot leave a partially registered route.
  It now registers one endpoint containing immutable HTTP-method metadata,
  one parsed pattern, and one handler rather than expanding into duplicated
  per-method routes. Later mutation of the caller's method list cannot alter
  the endpoint graph.
- inbound selection uses structural precedence rather than registration order.
- literal segments and parameter names use ASCII ordinal case-insensitive
  comparison. A trailing request slash does not create a distinct inbound
  endpoint; a trailing slash written in a template is retained for outbound
  generation.
- dispatch carries the already-parsed selected `RoutePattern` on `Request`, so
  `Request.RouteValue` does not reparse the template.
- explicit HEAD, GET-to-HEAD fallback, automatic OPTIONS, 405, and `Allow`
  behavior are tested through in-memory dispatch and live HTTP adapters.
- outbound paths use the same parsed pattern for required values, constraints,
  optional and catch-all segments, query values, and UTF-8 percent encoding.
- Lime's registration surface is now `MapGet`, `MapPost`, `MapPut`, `MapPatch`,
  `MapDelete`, `MapHead`, and `MapMethods`; the old verb spellings were removed.
- `Results` now constructs `Response` values, redirects have explicit 301,
  302, 303, 307, and 308 names, and `StatusCodes` supplies named values.
- `Request.RouteValue` returns `Option<string>`; the empty-string sentinel and
  old `request.param` API were removed.
- request and form-file metadata now use C#-shaped properties.
- the singular `Request.Header` API resolves duplicate fields to the first wire
  occurrence in both CurrentHttp and H2O; it no longer changes meaning by
  adapter. A future multi-value API must be explicit.
- `Request.Path` is normalized once before dispatch: valid percent-encoded
  UTF-8 is decoded, `%2F` remains encoded, plus is not form-decoded, and RFC
  dot segments are removed. The raw target and query spelling remain separate.
  Null escapes produce a controlled 400 in both adapters.
- `ResponseBody.Empty` represents no content directly. `Results.NoContent`
  uses it, body-bearing factories reject 1xx/204/304 statuses, and CurrentHttp
  emits a 204 without `Content-Type` or `Content-Length`.
- `ProblemDetails` and `Results.Problem` produce escaped RFC problem JSON with
  `application/problem+json` through the shared response union and both
  adapter content-type maps.
- CurrentHttp and H2O both close response streams deterministically. HEAD
  response handling no longer reads a CurrentHttp stream or file merely to
  discard its bytes.
- `App.MapGet`, `MapPost`, `MapPut`, `MapPatch`, `MapDelete`, `MapHead`, and
  `MapMethods` accept either `Response` or `Task<Response>` handlers under the
  same names. `DispatchAsync` executes both shapes, preserves selected route
  values across suspension, and is exposed through CurrentHttp and H2O async
  dispatch entry points. This is real task dispatch, but not yet concurrent
  transport execution.
- A live CurrentHttp integration drives an async constrained endpoint over a
  socket and verifies the awaited response in both the VM and generated-C
  executables. Async adapter behavior is therefore exercised, not inferred
  from module compilation.
- `Lime.Ssg.SiteBuildAsync` dispatches the same async endpoint graph during
  static generation. Its integration test awaits an async GET, writes the
  resulting page and 404 output, and verifies both files; async endpoints are
  not silently downgraded to synchronous SSG dispatch.
- `AppNew()` now installs the conventional 404 automatically. Applications
  replace it explicitly with sync or async `MapFallback`; fallback behavior is
  no longer a mandatory constructor argument.

Package tests, generated-C Lime tests, SSG, docs-server, and both VM and
generated-C CurrentHttp integrations pass this slice. Route groups, endpoint
builders, bound typed handlers, and continuation middleware intentionally
await Aster's class and bound-method substrate: `List<T>` has independent value
copy semantics, so implementing live builders as structs would silently mutate
copies of the endpoint graph.

### What must change

The routing foundation is now materially closer to Minimal APIs, but the
application architecture is not yet the target design:

1. `App` and `StatefulApp<State>` still duplicate route registration,
   dispatch, filters, HTML middleware, static mounts, fallback, SSG pages,
   exception handling, and forwarded-header configuration.
2. Route handlers still receive only `Request`. `Request.RouteValue` is now
   optional and uses the selected parsed pattern, but handlers cannot receive
   typed route parameters directly.
3. Binding adapters, conversion failures, and explicit query/header/body/form
   binding sources do not yet exist.
4. Route patterns are structural and selected by precedence, but endpoints are
   still scanned linearly rather than compiled into one matcher graph.
5. `UseFilter` is only a before-handler short circuit and `AfterHtml` transforms
   only HTML. Together they do not form general continuation middleware.
6. Endpoint-level policy, route groups, endpoint builders, names, metadata,
   and graph-level link generation do not exist.
7. Asynchronous non-stateful handlers now participate in the real endpoint
   graph, but `StatefulApp<State>` and continuation middleware remain
   synchronous. H2O's current `ServeAsync` loop awaits one handler at a time;
   its event loop is not yet integrated with Aster's executor.
8. Request bodies are buffered strings in the core request. CurrentHttp reads
   the declared body completely during native request parsing, and H2O exposes
   its already-buffered request entity. Response streaming exists, but neither
   transport currently supplies incremental body reads or a disconnect event
   to Aster's task executor. Consequently a truthful `RequestAborted` token
   cannot yet be implemented; attaching `CancellationToken.None` would only
   counterfeit the ASP.NET API.
9. The public surface exposes transport construction helpers such as
   `RequestNewTransport` beside application-facing request operations.
10. `Results`, `StatusCodes`, the empty-body variant, and `ProblemDetails` now
    provide the primary response vocabulary. Concrete typed result metadata
    still depends on the endpoint metadata/builder architecture.

`StatefulApp<State>` was introduced before Aster had classes and bound method
delegates. Those language features now exist, so the duplication must not
become permanent framework architecture. Lime can migrate stateful endpoints
to ordinary service objects and bound handlers without introducing owned
closures or implicit service injection.

## Target application shape

The ordinary application should be recognizable to a Minimal API user:

```aster
using Lime;

WebApplication app = WebApplication.Create();

app.MapGet("/", Home);
app.MapGet("/articles/{slug}", Article);
app.MapPost("/articles", CreateArticle);

app.Run();
```

Native HTML remains an ordinary handler result:

```aster
Html Home()
{
    return <main><h1>Aster</h1></main>;
}
```

Handlers which need HTTP control return `Response`:

```aster
Response Article(Request request, string slug)
{
    Article? article = Articles.Find(slug);
    if (article == null)
        return Results.NotFound(<h1>Not found</h1>);

    return Results.Html(<article>{article.Title}</article>);
}
```

This spelling depends on Aster's class, property, nullable, and bound-method
work. The framework should not preserve a worse permanent design merely so it
can be expressed by today's incomplete language surface.

## Endpoint mapping

`WebApplication` and `RouteGroup` expose the same mapping family:

```aster
app.MapGet(pattern, handler);
app.MapPost(pattern, handler);
app.MapPut(pattern, handler);
app.MapPatch(pattern, handler);
app.MapDelete(pattern, handler);
app.MapMethods(pattern, methods, handler);
```

`MapHead` and `MapOptions` may be supplied for explicit protocol behavior.
Whether they are prominent in the introductory API is a documentation choice,
not a reason to make them impossible.

Mapping returns an `EndpointBuilder` reference so configuration composes:

```aster
app.MapGet("/articles/{slug}", Article)
    .WithName("GetArticle")
    .Produces(StatusCodes.Ok)
    .Produces(StatusCodes.NotFound);
```

Registration owns the route pattern and compiles it once. Dispatch must not
reparse the pattern for every request.

## Route templates

The initial grammar is an ordinary string grammar owned by Lime:

```text
/
/articles/{slug}
/users/{id:int}
/archive/{year:int}/{month:int}
/search/{term?}
/files/{*path}
```

This is not Aster syntax. It is data passed to `MapGet` and related methods.

The parser must produce structural segments and reject at registration:

- a pattern not beginning with `/`;
- empty or malformed parameter names;
- duplicate parameter names;
- unbalanced braces;
- text after a catch-all segment;
- unknown constraints;
- invalid optional/default placement;
- duplicate method-pattern endpoints;
- endpoints whose patterns are structurally ambiguous at equal precedence.

The first constraints should be small and useful:

- `int`, `long`, `bool`;
- `min(n)`, `max(n)`, and `range(min,max)` for integers;
- `length(n)`, `minlength(n)`, and `maxlength(n)` for text;
- `alpha`;
- `required`.

Regex routing should not be in the first implementation. It complicates
portability, error reporting, worst-case behavior, and the runtime footprint.

Literal segments outrank constrained parameters, which outrank unconstrained
parameters, which outrank catch-all parameters. Registration order must not be
the undocumented conflict-resolution mechanism.

## Handler binding without reflection

ASP.NET Core turns Minimal API handlers into request delegates using runtime
reflection or its compile-time Request Delegate Generator. Aster should follow
the compile-time model.

The public call remains ordinary Aster:

```aster
app.MapGet("/users/{id:int}", GetUser);
```

```aster
Response GetUser(Request request, int id)
{
    // ...
}
```

Internally, generic `MapGet` overloads or compiler-emitted package metadata
adapt supported delegate shapes to one erased dispatch delegate. This is not a
routing grammar feature. No parameter type inspection occurs in production.

The initial binding rules should be deliberately explicit:

- `Request` receives the current request.
- Route parameters bind by name to handler parameters and are converted to
  their declared Aster types. The compile-time adapter records that mapping;
  dispatch does not search parameter names dynamically.
- Registration rejects a route parameter with no handler binding and a
  route-bound handler parameter with no route parameter. An explicit low-level
  `Request`-only handler remains available when manual access is intended.
- A route constraint participates in matching. A handler conversion without a
  matching constraint occurs after selection and can produce a bad-request
  result.
- Query, header, JSON, form, file, service, and application-state binding are
  not guessed from arbitrary types.
- Explicit wrapper types or explicit request APIs introduce those sources until
  Aster has a conventional metadata facility that can express them without a
  routing-specific language feature.
- Bound instance methods are accepted only when the application explicitly
  passes the method delegate.

Examples of possible explicit source wrappers, subject to implementation
prototyping:

```aster
Response Search(Query<string> term, Query<int?> page);
Response Create(Json<CreateArticle> input);
Response Upload(FormFile image);
```

These wrappers must earn their place through application code. The fallback is
always direct, typed request access; Lime must not accumulate invented wrapper
types solely to imitate every ASP.NET binding shortcut.

Binding failures need stable HTTP behavior and diagnostics. Route-constraint
failure means the endpoint did not match. Failure after endpoint selection is
a 400 response. Application exceptions are not binding failures.

## Route groups

Groups share a prefix and policy without creating a second router:

```aster
RouteGroup articles = app.MapGroup("/articles");

articles.MapGet("/", ListArticles);
articles.MapGet("/{slug}", Article);
articles.MapPost("/", CreateArticle);
```

A group contributes:

- a prefix;
- middleware or endpoint filters;
- endpoint metadata;
- authorization requirements when an authorization package is present;
- tags or descriptions for documentation tooling.

Groups may nest. They hold a reference to the owning endpoint graph and do not
copy an `App` value. Group registration order must not change route precedence.

## Middleware and endpoint filters

Application middleware wraps the complete downstream request pipeline:

```text
middleware A before
  middleware B before
    routing and endpoint
  middleware B after
middleware A after
```

It must be able to inspect the request, return early, invoke the next stage,
inspect or replace the response, and run cleanup through deterministic scope
exit. This replaces the current split between request filters and HTML-only
post-processing for general concerns.

Endpoint filters wrap one endpoint or route group after binding and before the
handler. They are appropriate for validation and endpoint-specific policy.
Application middleware is appropriate for exception handling, forwarded
headers, request logging, security headers, and other cross-cutting behavior.

Native HTML layout transformation remains useful but should be implemented as
an ordinary result-aware endpoint filter or a small helper above the general
pipeline, not as a privileged second middleware architecture.

Middleware ordering is security-sensitive and must be visible in startup code.
Lime must not silently insert authentication, authorization, CORS, or forwarded
header behavior based on package presence.

## Results and responses

Lime should retain its transport-neutral `Response` value and typed body union.
The public factories should settle around one conventional `Results` surface:

```aster
Results.Html(page);
Results.Text(text);
Results.Json(json);
Results.Created(location, body);
Results.NoContent();
Results.BadRequest(body);
Results.NotFound(body);
Results.Conflict(body);
Results.Redirect(location);
Results.File(path, contentType);
Results.Stream(stream, contentType);
Results.Problem(problem);
```

`StatusCodes` supplies named constants. Application examples should not spread
unexplained integers.

Native `Html`, `string`, and explicitly supported JSON values may be adapted to
responses by the same compile-time handler adapter system. Implicit conversion
must remain narrow enough that content type is predictable from source.

Lime should learn from `TypedResults`: concrete result types can carry useful
metadata for tests and documentation. It should not reproduce .NET's most
verbose nested result signatures until Aster has evidence that the complexity
improves real applications.

## Endpoint metadata and links

Every registered endpoint owns a metadata list. The routing core defines only
general metadata contracts: name, description, tags, accepted content types,
produced content types/statuses, and arbitrary package-owned typed metadata.

Named endpoints support link generation:

```aster
app.MapGet("/articles/{slug}", Article).WithName("GetArticle");

string path = app.Links.GetPathByName(
    "GetArticle",
    RouteValues.From("slug", article.Slug)
);
```

This intentionally begins close to ASP.NET's `LinkGenerator`. It is more
important to establish one correct endpoint graph than to invent a generic
typed-route hierarchy prematurely.

Registration must reject duplicate names. Generation must percent-encode path
segments, enforce required values and constraints, distinguish query values
from route values, and never silently return a malformed path.

A future typed endpoint handle may improve compile-time checking without new
language syntax:

```aster
Endpoint<ShowArticle> showArticle = app.MapGet(
    "/articles/{slug}", Article
);
string path = showArticle.GetPath(slug);
```

This remains a later experiment, not a prerequisite for the first coherent
Minimal API surface.

## HTTP behavior

The route redesign must not weaken protocol behavior already delegated to H2O.
It must additionally define:

- path matching uses Lime's adapter-neutral normalized path, never the query
  string; valid UTF-8 escapes decode before dot-segment removal while `%2F`
  remains encoded so it cannot create a path boundary;
- method matching is case-sensitive over normalized registered methods;
- an explicit HEAD endpoint wins; otherwise GET may service HEAD while the
  adapter suppresses the body;
- 405 includes a deterministic `Allow` header;
- OPTIONS behavior is explicit and tested;
- redirects state their default status and provide permanent/preserve-method
  alternatives;
- response statuses which forbid bodies use the empty-body representation;
  body-bearing factories reject 1xx, 204, and 304;
- content length and transfer framing remain adapter-owned;
- the singular header API uses the first wire occurrence in every adapter;
  future multi-value access, cookie coalescing, and singleton-header rejection
  must remain explicit and adapter-neutral;
- request body limits are enforced before unbounded buffering;
- request body streams are forward-only unless buffering is requested;
- disconnect cancellation reaches long-running handlers and stream producers;
- exceptions never cross a C adapter boundary;
- server shutdown stops acceptance, signals request cancellation where
  necessary, drains bounded work, and releases owned state deterministically.

## Hosting

`WebApplication.Run()` is the application-level entry point. Adapter choice and
options are explicit configuration, not route concerns:

```aster
WebApplication app = WebApplication.Create();
app.MapGet("/", Home);

H2OOptions options = H2OOptions.Create();
options.Port = 3000;

app.Run(H2O.Create(options));
```

The exact construction API may change with classes. The invariant is that the
same `WebApplication` graph is dispatchable in memory, through CurrentHttp,
through H2O, and through SSG. Application handlers never name an adapter.

Development hosting should eventually provide watch-and-reload behavior, but
hot reload is a compiler/VM mission layered over this stable endpoint graph.
It must not distort production HTTP semantics.

## Implementation sequence

### Stage 1: structural routing core

- Introduce parsed route-pattern, segment, constraint, endpoint, and metadata
  representations.
- Parse and validate patterns at registration.
- Implement deterministic precedence, ambiguity detection, 404, 405 with
  `Allow`, HEAD, and explicit OPTIONS behavior.
- Add route-generation tests using the same parsed patterns.
- Preserve the existing adapter-facing `Request` and `Response` contract.

### Stage 2: Minimal API surface

- Introduce `WebApplication.Create`, `MapGet`, `MapPost`, `MapPut`, `MapPatch`,
  `MapDelete`, `MapMethods`, and `MapGroup`.
- Return endpoint builders and attach names and metadata.
- Normalize all public member naming to Aster's C# convention.
- Supply a default 404; make fallback replacement explicit.
- Remove `App`/`StatefulApp` implementation duplication rather than layering
  the new names over both copies indefinitely.

### Stage 3: compile-time handler adapters

- Define the supported handler signatures and return shapes.
- Generate or instantiate erased request delegates without runtime reflection.
- Bind and convert route parameters with stable failure responses.
- Support explicit query, header, body, form, and request abstractions only
  after each API is proven against real Lime applications.

### Stage 4: pipeline

- Implement general ordered middleware.
- Implement endpoint/group filters over bound arguments and results where the
  language can express that safely.
- Rebuild exception handling, forwarded headers, static files, sessions, and
  HTML layout helpers as explicit pipeline or endpoint facilities.

### Stage 5: results, links, and metadata

- Consolidate result construction under `Results` and `StatusCodes`.
- Add endpoint names and `LinkGenerator` using the compiled route graph.
- Expose metadata sufficient for tests and future OpenAPI generation without
  coupling Lime's core to an OpenAPI implementation.

### Stage 6: async HTTP and lifecycle

- Carry request cancellation through every adapter.
- Expose bounded request-body streaming.
- Support asynchronous handlers without blocking H2O's event loop.
- Verify disconnect, timeout, streaming, exception, and graceful-shutdown
  cleanup under VM and generated-C execution.

### Stage 7: application conversion and removal

- Convert Lime tests, the blog fixture, Nook, docs-server, and issue-tracker
  applications to the final API.
- Remove `:name` patterns, `request.param`, lowercase registration methods,
  legacy router modules, and duplicated application types.
- Do not retain compatibility aliases: Aster has no external user base that
  justifies carrying two HTTP frameworks.

## Completion gates

This mission is complete only when all of the following are true in the current
tree:

1. Public examples use `WebApplication`, `Map*`, and route groups where useful.
2. No controller, MVC, Razor, action-discovery, or routing-language machinery
   exists.
3. Route patterns are parsed once and structurally validated.
4. Route precedence, constraints, ambiguity, 404, 405/Allow, HEAD, and OPTIONS
   behavior have direct tests.
5. Handlers can receive typed route parameters without `request.param`.
6. Binding failures have documented and tested responses.
7. General middleware can run before and after handlers.
8. Endpoint/group filters and metadata are supported or explicitly removed
   from the mission by a later user decision; silent omission is not success.
9. Named endpoints generate validated, encoded paths from the same route graph.
10. `App` and `StatefulApp<State>` do not remain as duplicated architectures.
11. Native HTML, JSON/text, files, streams, redirects, cookies, and headers work
    through the final response surface.
12. In-memory dispatch, CurrentHttp, H2O, SSG, VM, and generated C agree on the
    relevant route and response behavior.
13. Request limits, cancellation, disconnects, exceptions, streaming, and
    graceful shutdown have adapter-level verification.
14. The real application baseline is converted and no longer performs string
    route-parameter lookup.
15. The final README documents the actual API concisely and does not advertise
    unfinished behavior.

Passing a narrow unit test or adding `MapGet` as an alias is not evidence that
this mission is complete.
