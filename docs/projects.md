# Projects and targets

Aster 0.2 introduces a deliberately small project manifest. It is not a
package registry or dependency-version solver. A manifest defines one source
root and named binary, library, and test targets:

```toml
name = "example"
source_root = "src"
default_target = "app"

[target.app]
kind = "bin"
entry = "app.main"
browser_entry = "app.browser"

[target.core]
kind = "lib"
entry = "core.lib"

[target.smoke]
kind = "test"
entry = "tests.smoke"
```

Project commands are:

```sh
lang project run aster.toml
lang project run aster.toml app
lang project run-ir aster.toml app
lang project check aster.toml core
lang project emit-c aster.toml app > app.c
lang project build-web aster.toml build/web app
lang project build-site aster.toml public site
lang project test aster.toml
```

Local packages are declared by path relative to the manifest:

```toml
[dependencies]
lime = "../orange/packages/lime"
```

The dependency directory must contain its own `aster.toml`, and the key must
match that manifest's `name`. Project namespace lookup checks the
application's source root first and then its declared direct dependencies.
No source copying or symlinked module tree is required.

The mapping from a PascalCase namespace to a snake_case file path is deterministic:

```text
source_root + App.Main → source_root/app/main.lang
source_root + Lime.CurrentHttp → source_root/lime/current_http.lang
```

Every project file must declare the namespace implied by its path. A used
declaration must be public, and project-mode lookup only exposes direct
dependencies. Legacy single-file commands retain their historical relative
behavior for compatibility.

Whole-namespace using declarations expose public names and may have an alias:

```text
using Math.Operations;
using Ops = Math.Operations;

var first = Operations.calculate();
var second = Ops.calculate();
```

Selective item imports are intentionally not part of the language.

Library targets are checkable but not directly runnable. `project run` and
`project test` use the verified typed IR and IR-to-bytecode adapter.
`project run-ir` is an explicit alias useful in backend tests.
`project run-direct` retains the legacy AST-to-bytecode path as a comparison
oracle. Every form uses the same manifest, source-root, namespace mapping, and
entrypoint rules.

`project build-site` runs the selected binary target through typed IR with the
output directory exposed as its sole `std.process` argument. A Lime SSG target
uses that argument with `SiteBuild`; the command remains an ordinary project
execution convention rather than introducing a compiler-owned page model.

`project emit-c` loads the complete target module graph and emits one C17
translation unit. This is the primary native project path; compile it with the
host C compiler and link any explicitly required system libraries.

A binary target may optionally declare `browser_entry`. `project build-web`
then treats the ordinary target as one deployable web unit: it emits
`TARGET-server.c` from `entry`, compiles `browser_entry` through generated C to
an optimized `TARGET.wasm`, and writes the generic `aster.js` runtime plus a
tiny target loader. Clang, wasm-ld, and wasm-opt are explicit build-tool
requirements. The browser compilation is freestanding and exports only the
checked Aster handlers referenced by native event properties. Server-only
targets and `project emit-c` are unchanged.

Targets using only the self-contained generated runtime need only a C17
compiler. Targets calling registered file, directory, HTTP, or SQLite
mechanisms also include `include/lang/lang.h` and link against `langlib` plus
the relevant system library. The generated-C project tests are the canonical
warning-clean examples of this linking contract.

`lang project test` executes every target whose kind is `test`, reports each
target, and returns failure if any target returns nonzero. Tests within a
target can use `std.testing`: case functions return `TestResult`,
`TestRecord` accumulates a `TestSummary`, and `TestFinish` turns the summary
into the target's exit status. The example under
`examples/testing_project` demonstrates this pattern.
