# Aster

Aster is a statically typed application language with a C-family syntax, a
C#-shaped application programming model, and a .NET-referenced standard
library. C# and .NET are the primary guides for application-facing conventions,
API names, casing, organization, overloads, nullable values, exceptions,
delegates, `ref`/`out`, and `async`/`await` with `Task`. They are not exclusive
sources for Aster's syntax or semantics: conventional C and C++ syntax and
resource-management designs are welcome when they fit Aster better.

Aster does not target the CLR or adopt its runtime model. It uses deterministic
manual memory management, ordinary value semantics, explicit references and
raw pointers, narrow reference counting for shared values, UTF-8 strings, and
no tracing garbage collector. Native typed HTML is part of the language.

Aster does not adopt a general borrow checker or lifetime annotations. It
leaves pointer and alias lifetime correctness to the programmer while making
ownership transfer, explicit copying, and resource destruction deterministic.
Non-trivial values move by default; `copy(value)` makes potentially expensive
duplication visible.

The compiler and runtime are implemented in C. Programs can run on the
bytecode VM or compile to portable C17 through the same verified IR. C is
Aster's implementation, deployment, interoperability, and unsafe-systems
boundary—not the main model for its application-facing language or libraries.

The language is being developed for web applications. It is not production-ready.

## Example

```aster
using Aster.Html;

int main() {
    string name = "Ada";
    bool admin = true;

    Html card = <article class="user-card">
        <h2>{name}</h2>
        if (admin) {
            <strong>Administrator</strong>
        }
    </article>;

    Console.WriteLine(card.ToHtmlString());
    return 0;
}
```

HTML is ordinary Aster syntax and normal control flow works inside elements.

## Aster Web

Aster Web is Aster's explicit, minimal-API web framework. It provides
transport-neutral routing, requests and responses, middleware, forms, sessions,
static files, typed content, server-side rendering, and static publication. It
has no controllers, MVC layer, reflection-based registration, dependency
injection container, filesystem routing, or separate template language.

```aster
using Aster.Web;

private Response Home(Request request)
{
    return Results.Html(<main><h1>Hello from Aster Web</h1></main>);
}

int main()
{
    WebApplication app = WebApplication.Create();
    app.MapGet("/", Home);
    delete app;
    return 0;
}
```

The same endpoint graph runs through the development VM, generated-C servers,
tests, and static generation. Every Aster Web application remains a complete SSR
application; SSG evaluates eligible GET responses ahead of time as a
publication mode rather than requiring a separate site architecture. An
application can remain entirely server-rendered, publish finite routes as
files, or add browser behavior where useful.

### Optional retained Wasm enhancement

The Wasm client is one optional Aster Web capability, not Aster Web's
identity. It adds
typed interaction to ordinary server-rendered HTML while preserving progressive
enhancement. The same native HTML and checked Aster code renders on the server
and handles browser events through WebAssembly. The internal browser build emits
an optimized Wasm module, the small generic browser runtime, and a target
loader alongside the server application.

Browser components are ordinary classes—there is no component keyword, second
template language, JavaScript application layer, virtual DOM, runtime signal
API, or public DOM-command API:

```aster
private class Counter
{
    private int count;

    public Counter(int count)
    {
        this.count = count;
    }

    private void Increment()
    {
        this.count += 1;
    }

    public Html Render()
    {
        return <button type="button" onclick=this.Increment>
            Count: {this.count}
        </button>;
    }
}

private Html Page()
{
    return <main><Counter count=0 /></main>;
}
```

The server constructs and renders the class normally. In the browser, Aster Web
retains one isolated Wasm instance per component region and reconciles
compiler-owned text, attributes, styles, and explicitly keyed children after a
successful handler. It preserves browser-owned input values, checked state,
focus, selection, and caret. Private synchronous and asynchronous methods can
be handlers; pending async work is versioned so stale completions cannot
overwrite newer state. Component destruction and explicit root teardown are
deterministic.

The current checked SSR-state-transfer subset supports scalar constructor
state and one flat keyed `List<T>` made from Boolean, integer, and string
fields. See the normative contract and known limitations in
[`docs/aster-web-component-semantics.md`](docs/aster-web-component-semantics.md). The
complete two-instance todo proof is in
[`packages/aster_web/src/tests/final_todo_app.as`](packages/aster_web/src/tests/final_todo_app.as),
and the runnable browser examples are
[`examples/wasm_counter/`](examples/wasm_counter/) and
[`examples/browser_compare/`](examples/browser_compare/).

## Build

Aster requires CMake 3.16 or newer and a C17 compiler. Native
`System.Net.Http` support uses libcurl and is enabled in normal builds. It can
be omitted from compiler/runtime builds that do not need the native client:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

```sh
cmake -S . -B build-no-curl -DASTER_ENABLE_CURL=OFF
```

OpenSSL-backed SHA-256 and HMAC-SHA256 are enabled by default and can be
omitted with `-DASTER_ENABLE_CRYPTO=OFF`. Secure randomness still uses the
operating system directly in that configuration; it never falls back to a
non-cryptographic PRNG.

Libcurl is an optional standard-library transport dependency, not part of the
Aster language semantics. Generated programs that use `System.Net.Http` link
the Aster native runtime and libcurl; programs that do not reach that module do
not require its API. The native client provides synchronous calls plus
libcurl-multi-backed `SendAsync`/`GetAsync` with cooperative cancellation and
bounded response downloads and fixed-length streaming uploads.

Run a source file:

```sh
./build/lang run examples/hello.as
./build/lang run examples/html.as
```

Emit a standalone C translation unit:

```sh
./build/lang emit-c examples/hello.as > hello.c
cc -std=c17 -O2 hello.c -o hello
./hello
```

Manifest projects support named binary, library, and test targets:

```sh
./build/aster run --project examples/issue_tracker/Render.asproj
./build/aster test examples/testing_project/Tests.asproj
```

## Design

- Aster may use C, C++, or C# syntax and semantics where each provides the best
  fit; it is not restricted to exclusively C#-shaped syntax.
- C# shapes the application-facing programming model; .NET shapes the public
  standard library; C and C++ inform native interoperation, explicit lifetime
  responsibility, value semantics, copy control, and deterministic cleanup.
- Rust is not a design source or destination for Aster. Borrow checking,
  lifetime annotations, and Rust-style ownership syntax are non-goals.
- Aster keeps its own deterministic, no-GC runtime, UTF-8 representation, and
  native HTML rather than pretending to be a CLR implementation.
- Portable C17 is the primary deployment backend.
- The bytecode VM provides a fast development and differential-testing path.
- Cleanup is deterministic across normal returns, errors, exceptions, and
  loop exits.
- Non-trivial values move by default, and `copy(value)` explicitly invokes a
  type's defined copy behavior.
- Shared external resources use cleanup-managed native handles.

## Documentation

- [Language reference](docs/language.md)
- [.NET-referenced standard-library map](docs/standard-library-api-map.md)
- [Values and cleanup](docs/values-and-cleanup.md)
- [Ownership, moves, and explicit copies](docs/ownership-and-copy.md)
- [Projects and targets](docs/projects.md)
- [Aster Web framework overview and minimal APIs](packages/aster_web/README.md)
- [Aster Web seamless SSR and static generation](docs/aster-web-seamless-ssg.md)
- [Aster Web browser Wasm client](docs/aster-web-browser-client.md)
- [Normative Aster Web component semantics](docs/aster-web-component-semantics.md)
- [Final Aster Web application proof](docs/aster-web-final-application-proof.md)
- [Final retained-DOM measurement](docs/aster-web-final-measurement.md)
- [Aster DX grievances](docs/aster-dx-grievances.md)
- [Browser projection design history](docs/aster-web-composable-projections.md)
- [Typed IR](docs/ir.md)
- [Backend architecture](docs/architecture.md)
- [C interoperability](docs/ffi.md)
- [Implementation status](docs/status.md)
- [Project thesis](docs/thesis.md)

Examples are in [`examples/`](examples/). The public embedding API is
[`include/lang/lang.h`](include/lang/lang.h).

## License

Aster is distributed under the [BSD 3-Clause License](LICENSE).
