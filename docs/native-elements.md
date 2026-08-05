# Native elements

`<...>` is core typed expression syntax, not an embedded template language.
The parser has no table of HTML names. It parses a path, properties, and body;
the normal checker resolves that name against the registered element symbols.
The `std.html` module declares the standard tag vocabulary with ordinary
source declarations. It covers the current conforming HTML element vocabulary;
tag-specific attributes remain ordinary typed declaration fields:

```text
public element Html img {
    Url src;
    string alt;
    Option<string> loading;
    Option<long> width;
    Option<long> height;
}

public element Html section {
    Option<string> class;
    Html children;
}
```

`Option<T>` marks an optional property and an `Html children` field permits an
element body. At a use site an optional property accepts either `T` or an
`Option<T>` value. `None` omits the whole attribute and `Some(value)` renders
the value. User modules can declare new element symbols without modifying the
parser, checker, compiler, or VM. An ordinary function returning `Html` is
also an element component: its named parameters become required typed
properties, except `Option<T>` parameters. An optional component property may
be omitted, supplied as `T`, or supplied as `Option<T>`; typed IR inserts the
corresponding `Some` or `None` explicitly. When an element declaration and
function have the same name, markup resolves the element declaration; this
keeps `<main>` distinct from the program entry function.

`std.html` also exposes the safe zero-property `<doctype />` component. A full
document can remain entirely in native syntax:

```text
return <>
    <doctype />
    <html lang="en">
        <head><title>Aster</title></head>
        <body>{page}</body>
    </html>
</>;
```

Ordinary content is native HTML text rather than an Aster string literal:

```text
<section>
    <h2>{title}</h2>
    <p>Benchmark content & escaping.</p>
</section>
```

Tag-shaped `<` starts a nested element and `{...}` evaluates an Aster
expression. Static and dynamic text are HTML-escaped automatically. Formatting-
only indentation between structural children is discarded. Within meaningful
text, whitespace runs collapse to one space; a leading or trailing run that
contains a newline is discarded, while same-line boundary spaces are preserved
as one space. This rule is independent of platform newline spelling.
Syntactically recognized statement forms at a child boundary—`if`, `foreach`,
`switch`, and the other normal Aster statements—remain code. If literal content
is deliberately indistinguishable from code, make that exceptional case explicit:
`<code>{"if (ready) { ... }"}</code>`.

String-valued expressions always use braces. For example,
`<p>{GetMessage()}</p>` and `<p>{"computed text"}</p>` are expressions;
`<p>computed text</p>` is a static text node. Quoted text without braces is not
a second child-expression grammar.

`<style>` is the one native element whose body enters another parsed source
language. It accepts ordinary CSS directly:

```text
<style>
    .card {
        display: grid;

        & > .title { color: var(--accent); }
    }

    @future-rule example(value) {
        .card { future-property: unknown-function(); }
    }
</style>
```

Aster retains a structural CSS AST but emits the authored bytes unchanged.
Unknown properties, values, functions, descriptors, and at-rules pass through;
only malformed structure is rejected. CSS is the final embedded parser in the
language. `<script>` remains raw text rather than embedding a JavaScript parser.

An `Html` component can make a style region local to itself with the compile-time
`scoped` marker:

```text
private Html Card() {
    return <article class="card">
        <style scoped>
            .card { padding: 1rem; }
            .card > .title::before { content: "Card: "; }
        </style>
        <h2 class="title">Aster</h2>
    </article>;
}
```

Aster appends a stable component attribute to selectors and to native elements
created by that function. The `scoped` word itself is not rendered. This is a
source transformation over the parsed CSS tree: it performs no runtime CSS
parsing, allocation, or selector lookup. Rules nested under at-rules are scoped;
keyframe steps are not. A plain `<style>` is global and remains byte-for-byte
unchanged. Scope markers do not implicitly cross into a called child component.

Dynamic values enter static CSS through custom properties, not through CSS
grammar interpolation:

```text
private Html Card(string accent) {
    return <article class="card" --accent=accent>
        <style scoped>
            .card { color: var(--accent); }
        </style>
    </article>;
}

<Card accent="#e45b20" />
```

The native `--accent=accent` property renders inside one HTML style attribute as
`style="--accent: #e45b20"`. Multiple custom properties share that attribute.
String values must be a single conservative CSS atom: a color, dimension,
number, or identifier. Declaration separators, braces, quotes, functions, URLs,
and whitespace are rejected at compile time for literals and at runtime for
dynamic strings. Numeric values are formatted directly. Dynamic `style=` and
string interpolation in a custom-property value are rejected; static CSS stays
fully visible to the CSS parser and never requires runtime CSS construction.

Static scoped styles are document resources rather than ordinary repeated
children. Rendering the same component many times emits its stylesheet once;
detached `Html` values carry their stylesheet identities into the document when
they are appended. The registry belongs to each root `Html`, so independently
built documents cannot suppress one another's styles.

VM development and ordinary generated C keep these styles inline. A production
C build can extract all reachable scoped component styles into one asset:

```sh
mkdir -p public/assets
lang emit-c-site app.as public/assets > app.c
```

This writes `public/assets/site-<hash>.css`; the generated program renders one
`<link rel="stylesheet" href="/assets/site-<hash>.css">` at the first component
style position. The content hash and concatenation order are deterministic.
There is no runtime CSS parsing or construction. Use ordinary `emit-c` when an
inline stylesheet is preferable. Projects use
`project emit-c-site MANIFEST ASSET_DIR [TARGET]`.

Qualified names use normal namespace path syntax. After `using Aster.Html;`,
both `<Html.section>...</Html.section>` and
`<Aster.Html.section>...</Aster.Html.section>` resolve the public `section`
declaration, while the renderer uses the declaration's tag name (`section`),
not the qualification written at the call site.

Control flow inside an element body uses the same parser nodes, typing rules,
scope rules, ownership rules, and runtime semantics as control flow elsewhere.

Fragments use `<>...</>` and produce the same cleanup-managed `Html` type:

```text
private Html IssueRows(List<Issue> issues) {
    return <>
        foreach (Issue issue in issues) {
            <IssueRow issue=issue />
        }
    </>;
}
```

A fragment accepts the same children, declarations, and control flow as an
element body but emits no opening or closing tag. It is a typed builder, not a
virtual node or untyped template splice. Fragments may be returned, stored,
nested in elements, or used as a component root.

The body may contain static text, nested elements, `{ expression }`,
declarations, and ordinary
`if`, `while`, `foreach`, and `switch` statements. A braced body
beginning with a statement-only token such as `var` is parsed by the ordinary
block parser and produces `STMT_BLOCK`; braces beginning with an expression
retain the child-expression meaning.
Children produce `string`, `Html`, `Option<Child>`, fixed arrays of
children, `List<Child>`, or `void`. A nullable value appends zero or one child; arrays
and vectors append in order. Owning child values are consumed by the parent
builder, while fresh temporaries need no explicit move.

Element properties are normal expressions. Markup property names may contain
dashes and may use language keywords, so native spellings such as
`aria-label`, `http-equiv`, `for`, and `type` need no quoting or aliases.
Schema lookup treats `-` and `_` as equivalent while rendering preserves the
spelling used at the call site.

The checker rejects duplicates, unknown properties, missing required
properties, wrong types, unknown element symbols, and mismatched closing
names. Tag-specific metadata comes from `std/html.as`, not a parser tag
table. A deliberately small core contract permits real global HTML
attributes on every declared HTML element:

- text attributes such as `id`, `class`, `title`, `lang`, `dir`, `role`,
  `style`, `slot`, `part`, and editing/spelling hints;
- `tabindex` as an integer;
- `hidden` and `inert` as booleans;
- arbitrary `data-*` and `aria-*` string attributes.

Boolean attributes use HTML presence semantics: `true` emits the bare name and
`false` omits it. Numeric attributes are formatted without allocation in the
VM and generated C. Text and URL values are attribute-escaped as before.
An interpolated attribute, such as
`title=$"Issue {id}: {title}"`, writes its segments directly into the open tag
using attribute escaping. Direct body interpolation writes into the same
builder using text escaping. Neither path constructs a temporary `string`;
interpolation used as an ordinary value does construct and return a `string`.
Direct HTML interpolation reads an available `string` local or direct field
for the append, leaving the value available afterward. A
boolean inside interpolation is textual `true` or `false`, distinct from a
direct boolean property's HTML-presence semantics.
HTML void elements such as `input`, `img`, `meta`, `link`, `br`, and `hr`
never receive synthetic closing tags.

Function components are lowered to ordinary direct calls. A parameter named
`Html children` opts a component into body children. The compiler lowers the
body to an `HTML_FRAGMENT`, preserving ordinary control flow, and passes that
cleanup-managed fragment to the by-value `Html children` parameter. The consuming
signature makes the transfer explicit without call-site syntax.

Interpolation may supply a component's `string` or `Option<string>` property:
`<Badge label=$"Issue {id}" />`. Unlike a native HTML attribute, that call has
no active attribute destination to write into. Aster therefore constructs one
temporary string for the component invocation and releases its reference
afterward. Existing `string` values use ordinary retain/release semantics.

This remains a value-level language model, but the primary C backend does not
materialize every syntactic node as a separate buffer. Directly nested elements
and fragments write into their parent's destination. A component with a proven
direct returned element or fragment root receives the same destination through
an internal generated variant; ordinary calls and genuinely escaping `Html`
values still materialize their own owner. Fragments occupy a temporary builder
slot for ownership and cleanup but add no tag bytes and allocate no independent
render buffer. No cloning or reference counting is introduced.

The synchronous HTTP boundary accepts completed `Html` directly. It borrows
the value's existing contiguous bytes for one response write, preserving
`Content-Length` and keep-alive, and consumes the owner afterward. There is no
intermediate owning `string` and no body copy. This is distinct from constructing
HTML directly into a socket: construction-time streaming requires chunked
framing and explicit partial-write/error semantics and remains a separate API.

The SSR backend escapes `&`, `<`, and `>` in ordinary text and additionally
escapes quotes in attributes. In accordance with HTML parsing, `style` and
`script` bodies render as raw text rather than entity-escaped. Script expressions
must therefore produce trusted JavaScript, not user input. CSS is parsed as the
structural `<style>` body described above. Unescaped insertion in every other
context requires the deliberately named `Html.UnsafeRaw(source)` operation.
There is no DOM, CSS parser, or desktop lifecycle.
