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

Aster is not inspired by Rust and is not intended to converge on Rust's
ownership model. It deliberately does not adopt borrow checking, lifetime
annotations, move-only-by-default values, or Rust-style ownership ceremony.
Like C and C++, Aster leaves pointer and alias lifetime correctness to the
programmer while making value copying and resource destruction deterministic.

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

## Build

Aster requires CMake 3.16 or newer and a C17 compiler.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

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
./build/lang project run examples/issue_tracker/aster.toml render
./build/lang project test examples/testing_project/aster.toml
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
- Containers have defined copy behavior; shared external resources use
  cleanup-managed native handles.

## Documentation

- [Language reference](docs/language.md)
- [.NET-referenced standard-library map](docs/standard-library-api-map.md)
- [Values and cleanup](docs/values-and-cleanup.md)
- [Projects and targets](docs/projects.md)
- [Typed IR](docs/ir.md)
- [Backend architecture](docs/architecture.md)
- [C interoperability](docs/ffi.md)
- [Implementation status](docs/status.md)
- [Project thesis](docs/thesis.md)

Examples are in [`examples/`](examples/). The public embedding API is
[`include/lang/lang.h`](include/lang/lang.h).

## License

Aster is distributed under the [BSD 3-Clause License](LICENSE).
