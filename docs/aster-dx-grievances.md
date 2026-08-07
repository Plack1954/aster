# Aster DX grievances

Status: recorded on 7 August 2026 after migrating and verifying the external
Nook application. This is criticism, not a feature roadmap or a claim that
every item deserves immediate work. After this pass the document is deliberately
set aside while the CLI is rebuilt against the .NET CLI contract.

## Standard of judgment

Aster is judged by how quickly one developer can turn an intention into a
correct running web application. Small fixtures, line counts, elegant compiler
internals, and benchmark wins do not substitute for that. Nook is the current
external application proof because it has real content, design, assets, routes,
forms, persistence, SSR, SSG, feeds, metadata, and a production-server path.

The strongest parts of Aster are not in dispute here: native HTML is pleasant,
ordinary classes are viable component state, the unified `string` model removed
ownership ceremony, and one route graph can genuinely serve SSR and SSG. The
grievances below are the distance between that foundation and exceptional daily
development speed.

## P0: the feedback loop is much too slow

The measured Nook baseline is:

| Feedback path | Elapsed | Peak RSS |
|---|---:|---:|
| Server type-check | 5.05 s | 20,056 KB |
| Development start through first successful request | 8.15 s | not measured |
| Static plus generated-C build | 28.41 s | 766,460 KB |
| Full application verification | 29.22 s | 766,952 KB |

Five seconds to learn whether a normal edit type-checks and eight seconds to see
the first page are already disruptive. A 28-second application build is slow
enough to encourage batching changes instead of exploring. Roughly 749 MiB peak
RSS for this application is disproportionate.

The build repeats expensive work across checking, SSG execution, C emission,
and host compilation. There is no useful incremental application build, shared
front-end result, persistent compiler process, or development watcher. The
developer pays near-production startup cost during ordinary iteration.

## P0: the current command line is not a coherent product interface

The executable is still named `lang`, and ordinary work is expressed through
commands such as:

```text
lang project check aster.toml server
lang project run aster.toml server
lang project build-site aster.toml public site
lang project emit-c aster.toml server
```

This exposes compiler architecture instead of developer intent. `project` is a
redundant namespace, target placement is positional and command-specific, and
`emit-c` makes applications own host compiler/linker details. Repository-local
scripts hard-code the toolchain checkout and invoke `./build/lang`; there is no
normal installed-tool experience.

Help, version, project discovery, configuration, output, verbosity, no-restore,
argument forwarding, and exit behavior are not organized under one familiar
contract. Similar operations do not consistently accept projects and targets in
the same places. The CLI cannot yet be learned once and then used by reflex.

The replacement must follow the .NET CLI. Aster-specific command or option
invention would make this grievance worse, not better.

## P0: ordinary web applications still own framework plumbing

Nook needs a `NookApplication` lifetime struct solely to keep the
`WebApplication` and its bound state object alive and destroy both correctly.
It must explicitly convert every instance method into a `Handler` or
`BuildSource` local before registering routes. Its VM development server owns an
accept loop and adapter dispatch. Its production entry owns H2O creation,
binding, forwarded-header setup, serving, and error reporting. Its SSG entry
owns output argument decoding and result reporting.

This is plumbing, not application policy. It is understandable at the platform
layer, but it should not dominate the entry files of every application.

The same application currently has separate `server`, `production_server`, and
`site` targets. They share the route graph, which is good, but the developer
still maintains several entrypoints and shell scripts to reach the expected
deployment forms.

## P0: application work still discovers backend disagreements

Nook found that CSS was served with the wrong content type, that native `<meta>`
omitted the Open Graph `property` attribute, and that VM and generated-C static
HTML text had different escaping behavior. The retained-DOM application found
allocator fragmentation and major structural-update regressions.

These are useful discoveries, but they show that VM, generated C, SSG, SSR, and
browser execution are not yet one sufficiently enforced semantic product.
Developers should not need byte-comparison scripts to determine whether two
official Aster execution paths mean the same thing.

## P1: diagnostics cascade instead of protecting attention

Four obsolete `if` expressions in Nook produced a very large cascade of parser
errors. Once the parser lost the containing function, it diagnosed subsequent
tokens as invalid top-level declarations. The first message was actionable; the
dozens following it were noise.

Missing imports or obsolete framework types similarly generated long chains of
unknown methods, invalid switches, missing variants, and missing names. The
compiler often reports consequences after the root cause is already known.

Diagnostics need stronger recovery boundaries, root-cause suppression, concise
summaries, stable error codes, and a mode suitable for editors. A developer
should be able to fix the first real problem without mentally filtering hundreds
of lines.

## P1: source text is unnecessarily hostile to normal web content

The lexer rejected direct Unicode arrows in Nook source. Because native HTML
text escapes ampersands, named entities such as `&rarr;` render literally unless
the application writes `Html.UnsafeRaw("&rarr;")`. Five harmless design glyphs
therefore require an API whose name correctly communicates security danger.

A modern web language must accept UTF-8 source throughout identifiers where
appropriate, strings, comments, and native HTML text. Authors should be able to
paste ordinary editorial copy and symbols without encoding tricks.

## P1: language evolution currently creates migration tax without tooling

The Nook migration required `.lang` to `.as`, `orange.toml` to `aster.toml`,
Orange/Lime namespace changes, `usize` to `nuint`, global output functions to
`Console`, `HtmlRender` to `ToHtmlString`, expression syntax changes, explicit
function visibility, request/response API changes, route syntax changes, and the
removal of `StatefulApp<T>`.

Some changes were good decisions. The grievance is that the developer had to
discover and repair them manually. There is no formatter-assisted migration,
code action, compatibility diagnostic with an exact replacement, or project
upgrade command. Aster currently makes early adopters pay the full cost of its
design iteration.

## P1: the project and dependency model stops before normal ecosystem work

Local path dependencies are useful, but there is no package restore workflow,
lock file, package source configuration, version constraint model, offline
cache contract, reproducible dependency graph, package creation, or publication
path. Applications point directly at a neighboring Aster Web checkout.

The manifest describes targets but does not yet provide the familiar build
configurations, framework/runtime selection, generated output conventions, or
project-reference operations expected from the CLI model Aster has chosen to
copy.

Without restore and reproducibility, moving a working application to another
machine remains a repository-layout exercise.

## P1: production building leaks the C toolchain into the application

Nook's build script runs `project emit-c`, invokes `cc`, supplies include paths,
links `liblanglib.a` and SQLite, and separately manages an H2O shared library and
rpath. This is valuable as an escape hatch but poor default DX.

The application should ask to build or publish. The toolchain should know how
to produce the selected deployable artifact, report missing native prerequisites
once, and emit outputs under a predictable configuration-specific directory.

## P1: development serving lacks an edit workflow

The development command starts a server, but there is no observed file watcher,
automatic rebuild, browser refresh, CSS-only fast path, error overlay, or clear
distinction between development and production compilation. Starting from the
current checkout takes 8.15 seconds before the first Nook response.

For web design, the relevant unit is save-to-visible-result, not compiler-only
runtime. Aster does not yet measure or optimize that loop continuously.

## P1: testing a real application requires bespoke shell orchestration

Nook's verification script creates an isolated tree, runs SSG, counts files,
greps HTML, emits C, invokes the host compiler, starts a server, polls it with
`curl`, submits forms, and queries SQLite. The coverage is good; the amount of
application-owned orchestration is not.

There is no first-class way to run the same integration suite against VM and
published backends, allocate a test server, request the in-memory application,
snapshot output, or declare temporary content/database roots. This makes strong
application testing harder than it should be and encourages weaker fixture-only
tests.

## P1: web-design asset work is behind the application model

Ordinary CSS, JavaScript, fonts, and images work, which is the right baseline.
But Nook still lacks framework-owned fingerprinted assets, incremental asset
copying, manifest-based URLs, image processing, and production cache policy
derived from content hashes. The application can ship assets, but it cannot yet
manage them with modern production ergonomics.

The static build recopies the complete tree and participates in the slow full
build. A one-line CSS change should not require rebuilding unrelated application
and native artifacts.

## P2: forms and validation are low-level

Nook manually reads `FormCollection`, checks missing fields, trims byte ranges,
validates email shape, chooses error markers, and returns the original page.
Prepared SQLite use is appropriately explicit, but routine request binding and
validation create repetitive control flow.

Aster Web has typed form facilities, yet the real application path still feels
closer to parsing a transport than declaring an application input. Any future
abstraction must preserve transparent HTTP behavior and ordinary types; opaque
magic would not be an improvement.

## P2: browser capability is still incomplete for application work

The retained-DOM foundation is credible, but important daily capabilities remain
limited: typed Fetch/HTTP integration, navigation and history, controlled form
properties, URL-bearing inferred attributes, cancellation, richer component
composition, and general structural changes outside proven keyed-list cases.

Fallback correctness is more important than premature breadth, but these gaps
still determine what applications can be expressed without handwritten browser
code.

## P2: documentation is extensive but not yet task-shaped

The repository has unusually detailed design records, but developers need a
short authoritative path for creating, running, testing, building, publishing,
and debugging an application. Historical architecture and experiment documents
can conflict with the current surface or preserve stale measurements.

The small in-repository blog fixture was previously easy to mistake for
application proof. Documentation now labels it correctly, but the broader risk
remains: internal capability descriptions can sound complete before an external
application verifies the workflow.

## P2: naming and casing still reveal multiple eras

The product is Aster and Aster Web, while the compiler executable and some
internal artifacts still say `lang`. Repository paths, environment variables,
generated symbols, scripts, and older documents have passed through Orange and
Lime terminology. Even after the public rename, this residue makes the system
feel less settled.

The CLI work should establish the public naming boundary. Internal legacy names
may remain temporarily, but users should not need to know them.

## What should be measured from here

The following measurements should be treated as product regressions, not
occasional research exercises:

- fresh project creation through first successful page;
- no-change check and build;
- one-file code edit through diagnostic or visible result;
- CSS edit through visible result;
- adding an article across page, archive, feed, search, SSR, and SSG;
- adding a route and fixed page;
- adding a validated form field and persistence column;
- clean build, incremental build, publish, and test peak RSS;
- diagnostic count and root-cause quality for representative mistakes;
- commands and application-owned files required for each task.

The goal is not to win every preference contest. It is to make Aster unusually
fast and direct for the way its author wants to build and design web software.
