# Aster Web application baseline

## Status

The source under `packages/aster_web/src/examples/blog` is a routing and SSG test
fixture. It must not be described as a complete blog, a showcase application,
or evidence that Aster Web is pleasant to use. It contains one hardcoded post and
453 lines of Aster split across application, server, and site entry files.

Small fixtures remain useful in the automated suite. They are not application
proof.

## External application

The application baseline is Nook, using an existing local copy of Automattic's
GPL WordPress theme and the separate standalone Min port with its recovered
demo content. Those external application sources are not part of this
repository.

This target was created independently of Aster.Web. It therefore cannot be shaped
to flatter Aster Web or demonstrate only features Aster Web already has.

Nook's application surface includes:

- four authored articles and an About page;
- category archives and dated article permalinks;
- a home page, search page, article pages, About, and 404;
- local fonts, photographs, responsive CSS, and a small search script;
- RSS, Atom, sitemap, and a search index;
- canonical metadata, Open Graph, Twitter cards, and article metadata;
- one route definition per surface, shared by serving and static output;
- 48 published application artifacts before build bookkeeping files.

The existing Min application has ten content records, 33 asset files, and
1,572 lines of Rust application source. Those numbers are measurements, not
targets for Aster. Aster Web succeeds by expressing the same application with less
ceremony while retaining the supplied content, design, routes, and behavior.

## PHP control

Vanilla PHP is the development-experience control, not the source of invented
sample content. The comparison must use the Nook application surface above.
It should measure how directly each implementation performs ordinary shipping
work:

1. start the site locally;
2. edit shared layout and refresh;
3. add and edit an article;
4. add a fixed page and route;
5. render article and category collections;
6. process a search or form request;
7. serve local assets;
8. generate feeds, metadata, sitemap, and a 404;
9. produce static output from the same SSR application;
10. produce the production server artifact.

The PHP control and Aster application must use the same supplied content and
assets. A one-post mock-up, pre-rendered HTML wrapper, or application written
only to exercise its own framework is not an acceptable substitute.

## Ceremony ledger

For each task, record:

- commands required before the result is visible;
- application-owned files touched;
- declarations and framework concepts required;
- conversions and ownership/runtime annotations visible in application code;
- duplicated route, content, metadata, or build declarations;
- whether the edit-refresh loop requires production compilation;
- whether SSR and SSG execute the same route and rendering code.

Line count is supporting evidence only. A shorter implementation that omits a
real feature does not win.

## First Aster result

The standalone port lives in a separate private `nook-aster` project. It is an
SSR-first Aster Web application using the supplied Markdown, CSS, JavaScript, fonts,
and photographs. The same route graph runs through the VM development server,
the generated-C HTTP server, and SSG.

The implemented surface includes the four-post home page, four dated article
pages, four category archives, About, browser search, RSS, Atom, sitemap,
search JSON, robots, 404 behavior, and required local assets. The clean static
build emits 50 artifacts. Generated-C SSR and SSG produced byte-identical
home, article, and archive HTML during verification, and served assets matched
their source bytes. Headless Chrome comparisons at 1600px and 480px confirmed
the supplied responsive layout and styling.

The application has 17 Aster source files and 1,131 application-owned Aster
lines. The Min port has 1,572 Rust application lines,
but this small difference is not a DX victory: the implementations do not yet
have identical build and metadata facilities.

The application exposes no `take`, `borrow`, view conversion, or ownership
constructor for strings. The earlier port contained 110 such conversions;
the unified `string` model removes all of them. It now contains eight ordinary
route registrations, two parameterized route/build-source registrations, one
static-directory registration, and no explicit `app.page` calls.

## Findings against the PHP goal

What worked well:

- native HTML components express the supplied structure directly;
- ordinary CSS and assets could be preserved rather than rewritten;
- one route graph genuinely serves SSR and SSG;
- `./dev.sh`, `./build.sh`, and `./serve.sh` provide one-command development,
  production building, and production serving;
- generated C compiled and served the complete application correctly.

What remains materially worse than vanilla PHP:

- `foreach` cannot iterate an aggregate field directly and forces forwarding
  helpers;
- the generic stateful startup, server loop, and SSG entry remain visible
  application plumbing;
- fingerprinted assets, incremental builds, JSON-LD, and full article metadata
  remain behind the Min implementation.

Aster Web now renders the configured SSR fallback through the normal filters and
HTML middleware and writes it directly to root `404.html`. Nook no longer
registers a fake `/404/` page for static output.

Aster manifests now resolve Aster Web as a local path dependency, and Nook no
longer contains a source symlink. A single Aster Web static-directory declaration
serves assets during SSR and recursively copies all 33 supplied files during
SSG, replacing fourteen asset page registrations and two asset routes.

Nook now discovers Markdown articles from its content directory in
deterministic filename order. Its article and category GET routes use build
sources derived from the loaded `Content`, replacing the handwritten article
filename list and all eight explicit parameterized SSG page registrations.

Aster Web now owns the reusable frontmatter document parser and safe bounded
Markdown component. Nook's private parser and renderer were deleted, reducing
the application to 17 Aster files and 1,131 lines while producing identical
static output.

The port also found and fixed two concrete platform defects: Aster Web served CSS
as generic binary data, which browsers refused to apply, and native `<meta>`
lacked the standard Open Graph `property` attribute.

A same-surface vanilla PHP implementation has not yet been written, so Aster
must not claim measured parity with PHP. The current port is now substantial
enough to direct the next ergonomics work without returning to self-referential
fixtures.
