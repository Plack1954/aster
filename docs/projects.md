# Aster project and package model

Status: normative.

This document defines the Aster project boundary, local project references,
and the constraints that the future Aster Packages system must preserve.
Compiler tests and implementation details do not override this contract.

## Project identity

An Aster project is one `.asproj` file and produces one logical output. The
project filename is meaningful and normally matches the project name:

```text
Shop/Shop.asproj
Shop.Tests/Shop.Tests.asproj
Aster.Web/Aster.Web.asproj
```

`.asproj` files use TOML syntax. Aster source files continue to use `.as`.
There is no generic `aster.toml` project filename, named target, default target,
or public target-selection option.

The supported project keys are:

```toml
name = "Shop"
output_type = "exe"
source_root = "src"
entry = "Shop.Program"
```

`name`, `output_type`, `source_root`, and `entry` are required.
`source_root` is relative to the `.asproj` file. `entry` is the namespace whose
module supplies the project entry point or public library root.

The output types are:

- `exe`: a runnable application;
- `library`: a reusable Aster library;
- `test`: an executable test project;
- `web`: one deployable Aster Web application.

A web project additionally requires `browser_entry`:

```toml
name = "Shop.Web"
output_type = "web"
source_root = "src"
entry = "Shop.Server"
browser_entry = "Shop.Browser"
```

The server program, browser Wasm, JavaScript loader, styles, and static assets
are physical artifacts of one logical web output. They are not separate CLI
targets.

`stdlib` may be set to an explicit standard-library tree for toolchain tests
and controlled bootstrapping. Normal projects omit it and use toolchain
discovery.

## Project references

Local source dependencies are explicit `.asproj` references:

```toml
[project_references]
"Aster.Web" = "../Aster.Web/Aster.Web.asproj"
"Shop.Core" = "../Shop.Core/Shop.Core.asproj"
```

The key must equal the referenced project's declared `name`. Names containing
dots are quoted because an unquoted dot has structural meaning in TOML. Paths
are relative to the referring `.asproj`. A referenced project must have
`output_type = "library"`.

Restore validates the complete reference graph before compilation. It rejects
missing projects, name mismatches, duplicate project identities, cycles, and
references to executable, test, or web projects. Graph traversal is
deterministic.

Project references provide source namespaces to the compiler without copying
or symlinking source trees. Namespace-to-file mapping remains deterministic:

```text
source_root + Shop.Program → source_root/shop/program.as
source_root + Aster.Web.Html → source_root/aster/web/html.as
```

## CLI contract

The Aster CLI follows the corresponding .NET CLI command names and option
names. Project arguments accept either a `.asproj` path or a directory. A
directory must contain exactly one `.asproj`; zero or multiple candidates are
errors.

```text
aster restore [PROJECT]
aster run --project <PROJECT> [-- applicationArguments...]
aster test [PROJECT]
```

`restore` validates local project references. When remote packages are added,
the same command will also resolve and materialize them. `run` accepts only an
`exe` or `web` project. `test` accepts only a `test` project.

Build, run, test, and publish must consume one shared resolved project graph.
No command may implement a second dependency resolver. Build and publish are
not considered implemented until they produce honest physical outputs.

## Aster Packages boundary

The package system is called Aster Packages until a different name is chosen.
It is Aster-native: it does not use NuGet packages, the NuGet server protocol,
or NuGet package compatibility.

Remote package-reference syntax is deliberately unspecified. This document
does not reserve a table name, version-range grammar, lock-file name, registry
protocol, or archive format. Those choices require a separate package-system
design and must not be inferred from the local-project syntax.

Local project references and restored package references must enter the same
project graph and namespace-loading path. Package management is not a separate
build system.

A future reproducible package design is expected to pin exact transitive
versions and integrity hashes for applications while allowing libraries to
declare compatible dependency ranges. The concrete lock-file behavior,
version-range grammar, registry protocol, archive format, signing policy, and
publishing workflow remain deliberately unspecified until the local graph and
real application proof are complete.

## Solution and IDE boundary

A solution groups projects; it does not change project semantics. Startup
projects, multiple startup projects, solution configurations, and IDE layout
belong to solution or user settings rather than `.asproj` target declarations.

The future Aster IDE must invoke the public CLI and consume the same project
graph. An IDE-only compiler or restore path is non-conforming.
