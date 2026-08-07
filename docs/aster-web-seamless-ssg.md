# Aster Web seamless SSR and static generation

## Direction

Aster Web is not an SSG framework and should not become an Aster version of Astro.
It is a productive server application framework whose ordinary GET responses
may also be evaluated and materialized ahead of time.

The central invariant is:

> Every Aster Web application remains a complete runnable SSR application.

Static generation is a publication capability, not a second application
architecture. The same application, endpoint graph, handlers, filters,
middleware, HTML components, content values, and fallback behavior must remain
usable through the development VM, a generated-C server, tests, and static
publication.

A content-heavy site may be SSR during development, prerender public pages for
production, retain dynamic search and form endpoints, provide authenticated or
preview responses at runtime, and change that deployment split later without
being restructured.

## What this rejects

Aster Web should not adopt the parts of Astro that conflict with Aster's model:

- JavaScript as the application or build-system foundation;
- a separate `.astro` component and template language;
- filesystem routing;
- a parallel content type system;
- React, Vue, Svelte, or other framework containers as islands;
- implicit server/client prop serialization;
- separate static-page and server-route definitions;
- hydration directives as a second stringly typed language.

High developer productivity does not require hidden route registration,
filesystem conventions, dependency injection, or another template syntax.
It should come from short feedback loops, strong diagnostics, normal typed
composition, and one coherent application model.

## Existing foundation

The current `Aster.Web.Ssg` implementation already follows the correct semantic
shape:

- `SiteBuild` creates ordinary GET requests and calls `app.Dispatch`;
- `SiteBuildAsync` uses the same endpoint graph through `DispatchAsync`;
- fixed non-parameterized GET routes are registered as build pages;
- parameterized routes can supply a finite `BuildSource` of concrete paths;
- filters, HTML middleware, routing, route values, and fallback rendering are
  shared with SSR;
- mounted static directories are recursively copied through their normal
  resolver;
- HTML, text, CSS, assets, bytes, files, and streams can be materialized;
- directory-style HTML routes produce `index.html`;
- the real configured 404 fallback produces root `404.html`;
- duplicate URLs and output-file collisions are diagnosed;
- `lang project build-site` exposes the output directory to an ordinary target
  rather than introducing a compiler-owned page model.

The external Nook application demonstrated one route graph across VM SSR,
generated-C SSR, and SSG. It generated articles, archives, feeds, sitemap,
search data, 404 output, and static assets, with checked byte equality between
representative SSR and SSG HTML.

This is a useful foundation, not yet a complete publication experience.

## Desired application model

An application should be written once:

```aster
public WebApplication CreateApplication(ContentStore content)
{
    WebApplication app = WebApplication.Create();

    app.MapGet("/", Home);
    app.MapGet("/about/", About);
    app.MapGet("/articles/{slug}/", Article, content.ArticlePaths);
    app.MapGet("/search/", Search);
    app.MapPost("/contact/", SubmitContact);

    return app;
}
```

The application should not contain separate `Page`, `Layout`, static route,
or SSG rendering APIs when ordinary functions, native HTML, and endpoints
already express those concepts.

Fixed GET paths are finite and can be materialized automatically. A
parameterized path does not reveal its finite values, so some source of build
entries is unavoidably required. Aster Web's existing `BuildSource` is a valid
minimal answer:

```aster
app.MapGet("/articles/{slug}/", Article, ArticlePaths);
```

This is route input, not a second SSG model. A future fluent spelling may be
more readable, but should preserve the same endpoint graph:

```aster
app.MapGet("/articles/{slug}/", Article)
    .BuildFrom(ArticlePaths);
```

Content is ordinary typed application data. It may come from Markdown,
frontmatter, JSON, SQLite, HTTP, generated values, or hardcoded records. Aster Web
may provide deterministic discovery, parsing, typed decoding, Markdown, slug
utilities, and dependency tracking, but should not require an independent
content schema or collection language.

## Publication modes

The application is always SSR-capable. Publication may choose among:

- **Server:** all routes remain runtime responses.
- **Static materialization plus server:** eligible public GET responses are
  emitted as files while the complete server remains available.
- **Static artifact:** a deployment containing only the finite materialized
  surface, when explicitly requested and valid for that deployment.

The third mode is an output choice, not the identity of the application. Aster Web
should optimize primarily for the first two because Aster/Aster Web applications
are not assumed to be permanently static-only.

A future publication command may select policy without requiring another
entrypoint:

```text
aster publish
aster publish --server
aster publish --hybrid
aster publish --static
```

Names are illustrative. The important point is that publication policy does
not duplicate application code.

## Inference and explicit policy

Aster Web should infer only behavior that is obvious and explainable:

- a fixed GET route is eligible for materialization;
- a parameterized GET route is eligible when it has finite entries;
- mutating HTTP methods remain runtime endpoints;
- a parameterized route without entries remains runtime;
- streaming, request-specific, authenticated, or otherwise unsuitable routes
  remain runtime unless an application deliberately supplies a safe build
  request model.

Explicit controls should be uncommon escape hatches. Aster Web should avoid opaque
inference where reading a cookie, header, clock, or configuration value
silently changes a route's deployment mode. Publication should produce a clear
report, for example:

```text
/                           materialized
/about/                     materialized
/articles/{slug}/           materialized: 42 entries from ArticlePaths
/search/                    runtime
/account/                   runtime: session-dependent
/contact/ [POST]            runtime
```

## Build-time request context

Materialization executes normal requests, so publication must provide
predictable values for origin, scheme, host, base path, locale, environment,
time policy, and configuration. These belong in publication configuration or
an explicit build request context, not conditionals scattered through
handlers.

Application code should rarely need to ask whether it is running under SSG.
If build-time and runtime requests have equivalent inputs, their observable
responses should be equivalent.

## Development experience

The primary workflow is the normal application development loop, not a full
static build:

```text
aster dev
```

The desired experience is:

- quick VM startup;
- source, content, and asset watching;
- precise source-aware diagnostics;
- browser refresh;
- rebuilding only affected work;
- no production C compilation in the ordinary edit loop;
- no complete static publication after every change;
- no optimized browser-Wasm rebuild after a server-only edit.

Production publication may perform generated-C compilation, complete route
materialization, asset optimization, collision checks, and deployment
manifest generation. Semantic equivalence does not require using the slow
production pipeline during development.

## Publication work still needed

The current SSG implementation is deliberately basic. A production-shaped
seamless publication path should be driven by real applications and may need:

- a build and deployment manifest;
- stale-output reconciliation rather than leaving old files behind;
- atomic output replacement;
- incremental dependency tracking;
- source-aware reporting of the route that produced each artifact;
- content and route invalidation without rebuilding unrelated pages;
- asset fingerprinting and URL rewriting;
- scoped CSS and browser-Wasm asset integration;
- redirect materialization;
- a deliberate policy for static response headers and metadata;
- canonical origin and base-path handling;
- optional link validation or crawling as verification, not route truth;
- parallel materialization if measurements justify it;
- one publication pipeline for server C, materialized responses, browser Wasm,
  runtime assets, and deployment metadata.

Fingerprinting, incremental builds, and stale-file handling are practical
priorities. Taxonomies, feeds, sitemap, JSON-LD, pagination, and article
metadata should generally be normal reusable Aster/Aster Web libraries rather than
hardcoded assumptions about what a site contains.

## Grounding

This direction is grounded in existing systems without copying their complete
application models:

- **SvelteKit:** prerendering executes the real route and deployment adapters
  choose static or server output.
- **Angular:** routes can use server, prerendered, or hybrid render modes, and
  parameterized prerendering requires finite parameter providers.
- **Nuxt/Nitro:** one deployment may combine materialized routes, server
  handlers, redirects, caching policy, and an output manifest.
- **Next.js:** `generateStaticParams` demonstrates the unavoidable finite-input
  problem, while its complex static/dynamic inference is a warning.
- **React Router framework mode:** configured URLs execute ordinary application
  routes during prerendering.
- **Traditional Rails/Django page freezing:** SSG can be understood simply as
  executing a normal request ahead of time and saving its response.

Aster Web should retain explicit code routing and typed endpoint metadata rather
than adopting filesystem routing or a separate pattern-matching deployment
configuration.

## Browser enhancement and SSG

Materialized pages and browser Wasm should eventually compose in one
publication command:

1. determine browser-reachable checked handlers;
2. compile their Wasm artifact;
3. produce and fingerprint the runtime and loader assets;
4. materialize eligible endpoint responses;
5. include browser loading only where required;
6. copy referenced static assets;
7. emit the complete deployment manifest;
8. retain the same server handlers and progressive fallbacks.

The application should not manually maintain separate SSG and browser-asset
pipelines.

## Acceptance principles

Future work should preserve these principles:

1. The endpoint graph is the source of truth.
2. SSR remains available and is the semantic baseline.
3. SSG executes normal requests rather than a parallel page renderer.
4. Static publication never requires another component syntax.
5. Content remains ordinary typed application data.
6. Parameterized routes expose finite entries explicitly when needed.
7. Build policy is inspectable and produces a clear report.
8. Development does not require the production publication pipeline.
9. SSR and materialized output are different execution times, not different
   applications.
10. Features are added from substantial application evidence rather than a
    goal of rushing out an Astro-shaped product.
