# Language reference

`ref T` and `out T` parameters are direct aliases of caller storage. Reads and
writes therefore go through the caller's place immediately, including when two
arguments name the same local or field. An `out` parameter starts definitely
unassigned: it cannot be read until assigned, and every normally returning
control-flow path must assign it. Assignment through an `out` alias is visible
immediately, so a value assigned before a propagated exception remains visible
to caller-side exception handling; if no assignment occurred, the caller's
previous value is unchanged.

Aster source uses UTF-8 bytes. Identifiers are currently ASCII. `//` comments,
nested `/* ... */` comments, strings, integers, and decimal floats are lexed
with byte-accurate spans. Integers may be decimal or use `0x`, `0o`, and `0b`
prefixes for hexadecimal, octal, and binary. `_` separators are ignored in
numeric literals. String escapes `\n`, `\r`, `\t`, `\\`, `\"`, and `\0` are
decoded into length-carrying UTF-8 byte views; embedded NUL bytes do not
terminate a language string.

`T?` is the C#-shaped spelling for a nullable value and uses the same runtime
representation as `Option<T>`. `null` constructs the empty case, while an
ordinary `T` initializer is lifted into the present case:

```aster
string? missing = null;
string? present = "Aster";

if (missing == null) { }
if (present != null) { }
```

Nullable values cannot be used as `T` without an explicit checked extraction.
The C#-named `Value` property performs that extraction and traps when the value
is null. Flow-sensitive C#-style narrowing is not implemented yet.

Functions have statically typed parameters and results:

```text
private long add(long a, long b) {
    return a + b;
}
```

Functions and methods may be overloaded by parameter signature. Resolution
first uses argument count and exact argument types. When numeric literals are
the only difference, C# defaults apply: an integer literal prefers `int` and a
floating-point literal prefers `double`; an explicitly typed value still
selects its exact overload. Aster does not otherwise rank implicit
conversions, inheritance, or generic preference. `ref` is part of an overload
signature and may be written at a direct call site when needed to select the
mutable-reference overload:

```aster
private int Change(Counter value) { return value.Value + 1; }
private int Change(ref Counter value) { value.Value += 2; return value.Value; }

Change(counter);
Change(ref counter);
```

The compiler reports every candidate when a call remains ambiguous. An
overloaded function used as a value is selected from its target delegate type.

Functions use C-style declarations: there is no `fn` or `function` keyword.
Every ordinary function declaration begins with exactly one explicit
visibility keyword: `public` exports the function from its module and
`private` keeps it module-local. Visibility is never inferred from naming or
usage. The entry point is the sole exception and retains the conventional
unmodified spelling `int main()`. Writing `public int main()` or
`private int main()` is an error. The former `pub` abbreviation is not valid
for functions.

Top-level classes and interfaces likewise require explicit visibility.
`public class`/`public interface` export the declaration, while `private`
keeps it module-local; a bare class or interface declaration is rejected.
Aster does not use C#'s implicit `internal` default or provide an `internal`
keyword.

Non-`void` functions require an explicit `return`; ordinary function bodies
do not have implicit tail returns.

Typed locals are mutable and always require an initializer, as in
`int count = 0;`. `var` creates an inferred mutable local and keeps the same
runtime, copy, and cleanup semantics as spelling the inferred type explicitly:

```text
var user = new User { name = "Ada" };
var result = GetUser();
var count = 5;
```

Inference uses the initializer's compile-time type; `var` is not a dynamic or
variant type. The declaration still requires an initializer, and its local can
be assigned later under the ordinary static type-checking rules.
The executable subset includes `if`, `while`, array/`List` `for`,
`break`, `continue`, `return`, blocks, arrays, struct construction/field
access, and concrete enum variant construction. Arithmetic operands must have matching types. The
interpreter traps on signed overflow, division by zero, and invalid indexes.

`for` uses the C-family three-clause form:

```text
for (int index = 0; index < count; index++) {
    process(index);
}
```

The initializer and increment may be omitted. `break` and `continue` use the
same cleanup rules as other loops; `continue` proceeds through the increment
clause before testing the condition again.

`foreach` traverses a fixed array, slice, or vector without consuming the
collection. The loop variable has an explicit type and receives an ordinary
value copy of each element:

```text
foreach (Article article in articles) {
    render(article);
}
```

The collection remains usable after the loop. Assigning the loop variable or
mutating its fields does not mutate the collection element. There is no hidden
reference and no ownership or borrow syntax in Aster.

Fixed arrays use C declarators. Their length remains part of the static type:

```text
int values[3] = [7, 11, 13];
int matrix[2][2] = [[1, 2], [3, 4]];

private int sum(int values[3]) {
    // ...
}
```

`if` and `switch` may also produce values. Every branch or case must end in an
unterminated expression, and all produced values must have the same type:

```text
var label = if (ready) { "ready" } else { "waiting" };
var number = switch (result) {
    case Result.Ok(value): { value }
    case Result.Err(_): { 0 }
};
```

These forms lower to ordinary typed-IR branches and one synthetic result local;
they do not imply allocation or dynamic dispatch.

Mutable direct locals, their fields, and their fixed-array elements support
`+=`, `-=`, `*=`, `/=`, and `%=`. These use the same contextual typing and
checked arithmetic as their expanded forms. The receiver and index of a
compound assignment are evaluated exactly once.
Boolean expressions support `!`, `&&`, and `||`. Both binary operators
short-circuit: `false && expression` and `true || expression` do not evaluate
their right-hand side. Their operands must be `bool`; Aster does not use
integer truthiness.

Integer values support bitwise `&`, `|`, `^`, and `~`, plus the compound
forms `&=`, `|=`, `^=`, `<<=`, and `>>=` on the same mutable places. Operations
preserve the operand width, require matching integer types, and do not coerce
floats or booleans to integers.
Plain enums also support `&`, `|`, `^`, `~`, `&=`, `|=`, and `^=` so
C#-style flag values can be combined without integer casts. Unions remain
tagged values and do not support bitwise operations.

Statically resolved functions may use a type-qualified name and be called
with method syntax. Receivers use the same explicit type-first syntax as every
other parameter:

```text
public long Point.Offset(Point self, long amount) {
    return self.x + amount;
}

var result = point.Offset(2);
```

The method call is ordinary typed call sugar for `Point.Offset(point, 2)`.
These type-qualified extension-style functions are statically dispatched.
Declared class members may separately use single inheritance and
`abstract`/`virtual`/`override` dynamic dispatch. Mutation of a value-type
receiver uses an explicit `ref` parameter:

```text
public void Router.Add(ref Router self, Route route) {
    self.routes.Add(route);
}
```

`self` must be the first parameter of a type-qualified function. An ordinary
parameter is a value copy. `ref` is the C#-shaped mutable reference form.
Aster has no consuming parameter modifier and does not invalidate a named
value after a call.

Classes may implement nominal interfaces using the C# inheritance-list form,
and interfaces may inherit multiple interfaces. Interface members are
implicitly public and abstract. A concrete implementation must be a public
instance method or property with an exact signature. Interface calls and bound
delegates dispatch from the runtime class; interface reference assignment does
not box, retain, or take ownership of the object.

`List<T>` provides `.Add`, `.Insert`, `.RemoveAt`, `.Clear`, the `.Count` and
`.Capacity` properties, and checked `values[index]` reads and assignments.
`Clear` retains allocated capacity, matching .NET `List<T>`. Constructing the
element directly in `.Add` is the normal zero-copy path:

```text
articles.Add(new()
{
    title = "Direct",
    views = 7
});
```

For element types with defined equality, lists also provide `.Contains`,
`.IndexOf`, `.LastIndexOf`, and `.Remove`. The index queries return an `int`
and use `-1` for no match, following the C# API. Aggregate equality is not
guessed: structs and unions need an explicit Aster equality design before
these methods accept them.

`.AddRange`, `.InsertRange`, `.RemoveRange`, and `.GetRange` provide checked
range operations. Collection inputs currently use another `List<T>` and may
be the destination itself. The source remains available, while the destination
receives independent Aster values.

`.Reverse()` and `.Reverse(index, count)` reorder values in place.
`.EnsureCapacity`, writable `.Capacity`, and `.TrimExcess` expose allocation
control without changing `Count`; capacity can never be set below `Count`.

Lists accept ordinary Aster function values for `.Exists`, `.FindAll`,
`.FindIndex`, `.FindLastIndex`, `.RemoveAll`, `.ForEach`, and `.TrueForAll`.
The list is inspected directly; callback arguments receive normal Aster value
copies, and `FindAll` constructs an independent result.

`Dictionary<TKey, TValue>` provides target-typed `new()`, `.Add`, `.TryAdd`,
`.Count`, `.ContainsKey`, `.ContainsValue`, `.Remove`, `.Clear`, and checked
indexer reads and assignments. Its read-only `.Capacity`,
`.EnsureCapacity(capacity)`, and both `.TrimExcess()` forms expose predictable
storage control using Aster `nuint` collection sizes.
`.KeyAt(index)` and `.ValueAt(index)` return checked value copies from the
dictionary's dense logical storage. They support allocation-free traversal;
indices are not stable across structural mutation.
Indexer assignment replaces an existing value or inserts a new key. The
initial implementation accepts scalar, character, `string`, and raw-pointer
keys with built-in equality and has matching VM and generated-C hash-table
behavior.

Named types can provide a read-only indexer with an ordinary `Item` member,
matching the CLR metadata name used for C# indexers:

```aster
public Entry Catalog.Item(Catalog self, int index)
{
    return self.Entries[index];
}

Entry entry = catalog[0];
```

Indexer assignment remains built in for arrays, lists, and dictionaries.
Named properties use declared `get` and `set` accessors; user-defined indexer
setters remain a separate feature.

`HashSet<T>` provides target-typed `new()`, Boolean `.Add`, `.Contains`,
Boolean `.Remove`, `.Clear`, `.Count`, `.Capacity`, `.EnsureCapacity`, and
both `.TrimExcess()` forms. It uses the same supported element equality and
hashing rules as Dictionary keys in both execution paths.

`Queue<T>` provides target-typed `new()`, `.Enqueue`, `.Dequeue`, `.Peek`,
`.Clear`, `.Count`, `.Capacity`, `.EnsureCapacity`, and `.TrimExcess()`. Both
execution paths use FIFO circular-buffer storage and independent value copies.
The .NET `TryDequeue(out T)` and `TryPeek(out T)` members await real Aster
`out` parameter semantics rather than receiving an invented substitute.

`Stack<T>` provides target-typed `new()`, `.Push`, `.Pop`, `.Peek`, `.Clear`,
`.Count`, `.Capacity`, `.EnsureCapacity`, and both `.TrimExcess()` forms. It
has a distinct type and LIFO surface while sharing the established contiguous
collection allocation and independent-copy implementation.

`Add(value)`, ordinary assignment, and by-value calls copy and always leave the
source available. The VM and generated C use the same recursive copy semantics
for strings, lists, arrays, structs, and union payloads.

The integer types `sbyte`, `short`, `int`, `long`, `byte`, `ushort`, `uint`, `ulong`,
`nint`, and `nuint` have distinct static identities. Arithmetic traps when
its result does not fit the selected width. `float` and `double` are distinct;
`float` operations round through the host's IEEE single-precision
representation. Arithmetic operands must match, contextual literals are
range-checked, and conversions use C-style `(Type)value`. Invalid narrowing,
signed/unsigned, float/integer, and Unicode-scalar conversions trap. `char`
represents a Unicode scalar value and is currently constructed through an
explicit integer cast. `string` is an immutable, reference-counted UTF-8 value.
Integer `<<` and `>>`
require matching operand types; negative or out-of-width counts trap, left
shift traps on overflow, and signed right shift has defined arithmetic-shift
semantics independent of the host C compiler.

Raw pointer types are written as `T*` and `const T*`; pointee type and
mutability participate in type identity. `null` is contextually typed and
therefore requires an expected raw-pointer type; raw pointers support equality
and inequality but not ordering. A null, expired, or undersized load/store
traps in the interpreter. `Span<T>` is a distinct typed,
non-owning generic form. `BufferAsMutSlice` exposes a call-scoped
`Span<byte>` from a mutable `Buffer` inside `unsafe`; the compiler does not
prove that the buffer remains alive. Inside `unsafe`, the initial
`*pointer` dereference supports `const long*` and `long*`, while
`*pointer = value` and its compound forms require `long*`. The interpreter
validates allocation state and size; generated C checks null and otherwise
uses native pointer operations.
Arena allocation is unsafe, uses its expected raw-pointer type, and reset
invalidates every pointer.

Array indexing in ordinary code is marked checked. Indexing within an
`unsafe { ... }` block is retained as an unchecked-intent operation in the
typed AST and bytecode, but Aster 0.1's interpreter deliberately still traps
on an invalid index so malformed guest access cannot corrupt the host process.
A future native backend may lower that explicit unsafe form without a check.

`Buffer`, `StringBuilder`, `Url`, `List<T>`, and `Html` are ordinary owning
values with deterministic cleanup and supported copy behavior. `NativeHandle`
copies share a reference-counted native resource. `Arena` is noncopyable.
Arrays, structs, and unions recursively clean up their fields and payloads.
Values are cleaned up at lexical scope exit and early return. See
`values-and-cleanup.md`.

Fields and fixed-array elements can be replaced only through a mutable direct
local:

```text
point.x = 10;
values[index] = 20;
point.x += 5;
values[next_index()] *= 2; // next_index() runs once
```

Replacing a field or element cleans up its old value before installing the new
one. Reading a copyable field or element copies only the selected value.

Struct construction supports field shorthand when a local has the same name:

```text
Point point = new() { x = x, y = y };
```

The explicit form is `Point point = new Point { x = x, y = y };`. Each
initialized value uses its type's normal copy behavior.

`var _ = expression;` evaluates and immediately discards the result. An owning
result is destroyed at that statement rather than being retained until the
surrounding scope exits.

An `extern struct` uses the target C aggregate layout and is restricted to
C-ABI-compatible fields. `dump-layout` exposes its size, alignment, and field
offsets. Ordinary structs do not yet promise a stable external ABI.

A user-defined struct or enum can declare one C#-style destructor. `self` is
an implicit mutable local:

```text
~File() {
    Console.WriteLine("closing file");
}
```

The destructor is called automatically and cannot be invoked explicitly. Its
body runs before fields are recursively destroyed. Destructors also run for
nested aggregate values, discarded temporaries, early returns, propagated
`Result.Err` values, and VM trap unwinding. Moving a value transfers its
pending destructor to the destination.

User value types may control copying with a C++-style copy constructor. Its
single `const ref T` parameter is a non-owning immutable reference to the
source, while the constructor initializes an independent destination:

```aster
struct BufferOwner {
    Arena storage;

    public BufferOwner(const ref BufferOwner other) {
        storage = Arena.new();
    }
}
```

Every ordinary copy of `BufferOwner` invokes this constructor, including
initialization, assignment, by-value parameters, and explicit
`new BufferOwner(existing)`. Assignment finishes constructing the replacement
before destroying the destination's previous value, so self-assignment is
well-defined. The source cannot be mutated through `const ref`.

A unique owner can reject copying instead:

```aster
struct UniqueOwner {
    Arena storage;

    private UniqueOwner(const ref UniqueOwner other) = delete;
}
```

Copying then produces a diagnostic at the copy site with the deleted
declaration as secondary context. A destructor alone does not disable ordinary
recursive field copying. Class-variable assignment remains an alias copy and
does not use value copy constructors; explicit `new ClassName(existing)` may
invoke a declared class constructor.

Custom copy operations compose recursively through user structs, generic user
struct instantiations, fixed-size arrays, `Option`, `Result`, and payload-bearing
user unions. Each enclosing value is rebuilt from independently copied fields,
elements, or the active union payload, so a scalar-only member with a custom
copy constructor is not mistaken for a trivial representation copy. Copying a
tagged union never moves from or changes its source. Dynamically sized `List`,
`Stack`, and `Queue` collections copy their elements through the same recursive
policy. `Dictionary` copies keys and values independently and rebuilds its hash
index from the copied keys. User structs are not currently admissible
`Dictionary` or `HashSet` keys because those collections require built-in
equality; consequently a user-defined custom copy constructor can presently
participate in a dictionary value, but not a set or dictionary key.
Reads from fields, fixed arrays, `List`, `Queue`, `Stack`, and `Dictionary`
apply the stored value's copy policy. For a custom-copy value, the runtime
borrows the selected element long enough to call its copy constructor; the
source aggregate or collection remains unchanged. Returning a `const ref`
parameter likewise copies because borrowed parameters are never eligible for
return-value ownership transfer. The conditional read APIs `Queue.TryPeek`,
`Stack.TryPeek`, and `Dictionary.TryGetValue` follow the same rule on success:
they borrow the stored value and initialize the `out` destination through its
copy constructor. On failure they replace the `out` destination with that
type's default value, without invoking its copy constructor.

`string` is Aster's single immutable UTF-8 string type. Assignment, argument
passing, field reads, and returns retain the underlying reference-counted byte
allocation rather than copying its contents. There are no source-level string
view or ownership-conversion constructors, string moves, or post-call
invalidation. Native adapters may use temporary pointer-and-length views internally;
those views are not a second language type.

`StringBuilder builder = new()`, `builder.Append(value)`, and
`builder.ToString()` provide C#-shaped mutable construction. `ToString()` does
not consume the builder; it can be appended to or cleared afterward.
`StringBuilder.AppendByte` appends one raw byte without formatting or a
temporary string; Aster-written parsers use it when reconstructing encoded
UTF-8 and byte strings.
`$"issue-{id}: {title}"` is string interpolation. Interpolated expressions
may be strings, booleans, characters, or numeric values. In an ordinary value
position interpolation produces a `string`. Its builder formats scalar holes
directly without allocating a temporary string for each value. `\{` and `\}`
insert literal braces. Component parameters receive normal read-only `string`
values without conversion syntax.
`string.Length` returns the UTF-8 byte length. `StringLen`, `StringByteAt`, and
`StringSlice` are byte-oriented runtime/parser primitives; a slice result is an ordinary
immutable `string`. Byte offsets are explicit and do not claim Unicode scalar
indexing.
`StartsWith`, `EndsWith`, `Contains`, `IndexOf`, `LastIndexOf`, and `Substring`
are ordinal byte-oriented string members. `StringFindByte` remains an explicit
parser primitive. Numeric values expose `ToString()`; the runtime formatting
bridges are not the application-facing API.

`foreach (char scalar in text)` decodes UTF-8 as Unicode scalar values without
allocating a character collection. Invalid UTF-8 traps during language-level
iteration; Unicode library decoding APIs instead throw `FormatException`.
`ToUpperInvariant`, `ToLowerInvariant`, Unicode character classification,
`ToCharArray`, and `Encoding.UTF8()` use deterministic Unicode 15.1 data rather
than the process locale.

Numeric types expose C#-named static `Parse(string)` members. Integer parsing
is checked and accepts decimal digits with an optional leading sign and ASCII
edge whitespace. Floating parsing additionally accepts an invariant `.` and
scientific notation. Aster does not consult ambient process locale. `TryParse`
is intentionally absent until Aster has a real C#-contract `out` parameter;
`ref` is not presented as an equivalent substitute.

`std.directory` exposes a cleanup-managed directory handle. Opening and advancing
the host stream are registered primitives, while filtering and traversal
policy can be written in Aster. Dropping the handle closes the directory
deterministically.
`std.content` provides `DiscoverFiles(root, suffix)` for the common bounded content-directory
case: it returns matching regular files immediately below `root` in
deterministic filename order. It does not recurse or infer routes.

`std.process` exposes application arguments and environment lookup. With
`lang run app.as -- first second`, the Aster argument list contains only
`first` and `second`; launcher and source-path details are excluded.
Argument and environment lookups return `string` values inside
`Result`, so guest code never retains pointers into host process storage.
Missing indexes and variables are ordinary typed errors.

`std.cli` parses process arguments in Aster into a consuming
`List<CliArgument>`. It recognizes long flags, short flag names,
`--name=value`, positional values, and the conventional `--` end-of-options
marker. Short flag groups remain one name. Named options own one `name=value`
string; borrowed helpers split its name and value without allocation.

`std.filesystem` provides explicit path queries and mutations. Existence,
regular-file, and directory queries return `Result<bool, FilesystemError>`.
Directory creation, rename, file removal, and empty-directory removal return
`Result<unit, FilesystemError>`. Paths are ordinary UTF-8 `string` values at the
language boundary; the host adapter determines platform path interpretation.
Operations are single-path primitives: recursive creation and recursive
removal are intentionally not implicit.

`std.file` supports both complete text reads and bounded byte streaming.
`NativeFileReadInto` fills a caller-provided `Span<byte>` and returns zero
at end of file. `NativeFileWriteBytes` writes exactly the requested prefix
or returns an error. `CopyFileBuffered` is an Aster-written loop that owns
both RAII file handles and one reusable `Buffer`; it never allocates storage
proportional to the input file.
The public `Stream` facade exposes the same byte-native path through
`ReadInto(Span<byte>)` and `Write(ReadOnlySpan<byte>)` for both file-backed and
memory-backed streams. Byte spans also provide bounded non-owning ranges,
overlap-safe copying, try-copying, fill/clear, equality, byte search, and
prefix/suffix tests. A returned range does not extend its source storage's
lifetime.
`ReadLinesBuffered` assembles LF-delimited strings in Aster and
supports lines larger than its reusable input buffer. Empty interior lines are
retained; a final unterminated line is returned; a trailing LF does not create
an additional empty line. Carriage returns remain ordinary bytes.
`ForEachLineBuffered` instead invokes a non-capturing
`Result<bool, IoError>(string)` callback for each line. `true` continues,
`false` stops successfully, and an error propagates after deterministic
cleanup. It retains only the current line, not all preceding lines. The
callback receives a borrowed view into an owned temporary line; that view is
valid only for the duration of the callback and must not be retained.

`std.bytes` provides slice length, checked byte access, and half-open
range-to-`string` copying. These operations are byte-oriented and do not claim
UTF-8 validation. Invalid indexing traps in the development VM; range copying
returns a typed error.

`new()` uses its surrounding `List<T>` type. `Add` mutates a list and can store
a fresh element directly; passing an existing value copies it. `Count` reads
the list, and `foreach` traverses it without consuming it.
`list.Get(index)` performs checked indexing and returns a copy,
so it is restricted to copyable element types.

Ordinary aggregate parameters are values:

```text
private nuint Inspect(List<long> values) {
    return values.Count();
}
```

The callee receives a copy. A `ref` parameter instead permits in-place
mutation:

```text
private void add_route(ref Router router, Route route) {
    router.routes.Add(route);
}
```

The argument must be an available mutable place. The alias remains limited to
the call; this is not lifetime analysis and it does not make retained raw
pointers safe.

An `out` parameter uses the C# call shape for a value produced by the callee:

```text
private bool TryParse(string text, out int value) {
    value = 42;
    return true;
}

int value = 0;
if (TryParse("42", out value)) {
    Console.WriteLine(value);
}
```

The argument must currently be an existing initialized mutable local. Aster
preserves `ref` and `out` as distinct function signatures and requires the
matching modifier at the call. VM and generated-C calls both write replacements
back to the caller. Inline declarations such as `out int value` and compile-time
proof that every returning path assigns an `out` parameter are not implemented
yet.

Assigning a fresh value to a mutable local reinitializes it after a complete
move or destruction. If the old value remains live, assignment destroys it
before replacement. Use between move and reinitialization remains an error.

`Aster.Testing` defines `TestResult`, boolean/string/integer assertions, and a
copyable `TestSummary`. Assertions return structured results with owned failure
messages instead of trapping, so `try` can stop one test while deterministic
cleanup still runs. `TestRecord` prints the case result and updates the
summary; `TestFinish` returns a process status suitable for a manifest test
target.

`Url.relative` and `Url.fragment` create a typed URL used by HTML `src` and
`href` properties. They read an ordinary immutable `string`; implementations
may retain its allocation where safe. Thus `Url.relative($"/issues/{id}")`
needs one interpolation builder rather than a second byte copy.
`panic(message)` and `trap(message)` have type
`never`, report the source location and stack, and unwind owning values.
`Html.ToHtmlString()` returns an owned `string` without consuming the `Html`
receiver. HTTP applications normally skip that string conversion entirely:
`HttpTryRespondHtml` consumes `Html`, borrows its
contiguous bytes for the synchronous response write, and then releases it.

One file normally declares one namespace. `using App.Math;` loads `math.as`
beside the using file. Standard-library namespaces such as `System.Text`,
`System.IO`, and `Aster.Html` are resolved by the compiler to the bundled
standard-library modules; their physical filenames are not public API.
Dependencies are loaded once, their declarations participate in ordinary
resolution, cycles are rejected, and only `public` declarations are visible
from another namespace.
Local declarations shadow used public declarations. Multiple used
public declarations with the same used name are diagnosed as ambiguous.
Same-spelled named types in different namespaces retain distinct identities,
as do their destructors. Manifest projects support whole-namespace using
declarations and namespace aliases such as `using Math = Demo.Math;`.
Used public functions, types, and elements can also be addressed through a
namespace suffix or full namespace path, such as `Math.double`,
`Demo.Math.double`, or `<Html.section>`. Qualified enum values, union
constructors, and switch cases are canonicalized to the resolved declaration,
so `Status.Ready` and `ResponseBody.Html(page)` retain their declared tags.

Errors are reported as diagnostics during lexing, parsing, name resolution, and
type checking. Ordinary operation failures may use unchecked C#-style
exceptions. Functions do not declare thrown types, and callers do not write a
propagation operator:

```aster
private void Load()
{
    throw new IOException("database unavailable");
}

try
{
    Load();
}
catch (Exception error)
{
    Console.WriteLine(error.Message);
}
```

Inside a catch, bare `throw;` rethrows the currently handled exception.

Propagation follows typed IR cleanup edges, so live cleanup-managed locals are
destroyed before control reaches a handler or leaves a function. `finally`
executes after normal completion, caught or uncaught failure, return, break, and
continue. As in C#, `return`, `break`, and `continue` cannot leave a `finally`
block; throwing from it replaces an exception already being propagated. The
built-in hierarchy provides `FormatException`, `OverflowException`,
`ArgumentException`, `InvalidOperationException`, `IOException`, and
`JsonException`. Each carries the base `Exception.Message` member and may be
caught at a general `catch (Exception error)` boundary. A failed registered
native call is converted to a base `Exception`, so FFI failures follow the same
propagation and cleanup rules as Aster `throw`. Programming failures still
trap. A trap prints its source position and call stack, then unwinds live
runtime objects.

`Option<T>` and `Result<T, E>` are compiler-known generic tagged values with
exhaustive matching. They remain available for expected/domain outcomes rather
than being mandatory error propagation. The legacy `try expression`
extracts `Ok`, or returns the matching `Err` from the current function while
unwinding owning locals. It performs no implicit error conversion.

User-defined structs and unions may declare type parameters:

```text
struct Pair<A, B> {
    A first;
    B second;
}

union Maybe<T> {
    None,
    Some(T),
}
```

Applied types such as `Pair<long, string>` are canonicalized by exact
declaration identity and exact argument types, including across imported
modules. The checker monomorphically substitutes field and payload types. An
aggregate is copyable only when every substituted field is copyable, and it
requires deterministic destruction when any substituted field requires it.
There is no type erasure, implicit clone, constraint system, specialization,
or variance.

In this first stage, aggregate construction obtains generic arguments from its
expected type:

```text
Pair<long, string> pair = new() {
    first = 1,
    second = "one",
};
```

Generic functions infer their type arguments from parameters and, when
available, the expected return type:

```text
private T Identity<T>(T value) {
    return value;
}

long number = Identity(42);
```

Every concrete argument list creates one canonical specialized function across
the complete checked module graph. Each specialization receives its own typed
AST and bytecode body, so cleanup-managed locals are not type-erased.
Recursive calls reuse the in-progress specialization. Conflicting or
uninferred parameters are diagnostics. The initial syntax is inference-only:
explicit `identity<long>(value)` calls, generic constraints, specialization,
and generic extern functions are not supported.

Generic `extern struct` declarations are rejected because Aster has not
defined a cross-language generic ABI.

Language functions and bound class methods are copyable typed values:

```text
delegate long Operation(long value);

private long apply(long value, Operation operation) {
    return operation(value);
}

Operation operation = AddOne;
long result = operation(4);
```

Aliases use C# spelling, for example `using Count = uint;`. A `delegate`
declaration gives a reusable name to an exact function signature.

Parameter and result types must match exactly. A function value contains an
invocation target and an optional borrowed class receiver; indirect calls still
receive compile-time signature and arity checks. Imported language functions
work through the same normal symbol resolution. A registered extern function
can become an exact delegate value; the compiler emits an ordinary language
wrapper whose body performs the registered native call. A target delegate type
selects an overloaded extern signature exactly. Non-capturing language
functions can also be passed into registered extern calls as call-scoped
callbacks; native code invokes them through `lang_vm_call_function` and must
not retain the delegate or borrowed arguments after returning. There are no
arbitrary capturing closures or heap environments. A bound delegate does not
extend its receiver's lifetime. Binding a virtual class method resolves the
override for the receiver's runtime class, as in C#.

The compiler implementation uses an internal bump arena for syntax and type
data. Guest `Arena` is a noncopyable owner of allocation blocks. `ArenaAlloc`
returns a typed non-owning raw pointer and `ArenaReset` frees every block.
Pointers are invalidated by reset or destruction.

An external function may view a cleanup-managed native value for exactly one call:

```text
extern long server_poll(NativeHandle server);
```

The argument must be an available local. The call neither moves nor destroys
it, and it remains usable afterward. This does not introduce a general
reference or lifetime system.

Elements are expressions and produce `Html`. Bare content is static
HTML text; tag-shaped `<` starts markup and `{ expression }` inserts a dynamic
child. String literals and interpolated strings used as child expressions must
also be inside braces. For example:

```text
<a
    href=destination
    aria-label=$"Open issue {issue_id}"
>
    Issue #{issue_id}: {title}
</a>
```

Static and dynamic text are escaped automatically. Formatting-only indentation
is omitted. Interior whitespace runs normalize to one space; leading or
trailing whitespace containing a newline is omitted, while meaningful
same-line boundary spacing is preserved as one space. At a child boundary,
syntactically recognized Aster statement forms remain code; literal text that
intentionally looks exactly like a statement can be written as
`{"if (ready) { ... }"}`.

HTML interpolation is destination-aware. Each literal and formatted value is
escaped and appended directly to the active element buffer: text uses text
escaping, attributes use attribute escaping, and no intermediate `string` is
allocated. Assigning the same `$"..."` expression to a `string` instead builds
a normal immutable value. An available `string` local or direct field is read
when interpolated straight into HTML, so `$"{issue.title}"` neither invalidates
nor copies the title bytes. Interpolation holes contain ordinary Aster expressions,
including calls and `if`/`switch` expressions with their own string literals:
`class=$"tab {if (selected) { "selected" } else { "idle" }}"`. Complex property
expressions should be parenthesized. Element comparisons containing `>` are
parenthesized because bare `>` closes the opening tag.

Native `<style>` switches to Aster's CSS grammar until `</style>`. CSS is
written directly, without string delimiters, and is stored as a source-spanned
tree of style rules, at-rules, and declarations. Property names, values, and
unknown at-rules are deliberately open-ended and their original bytes are
emitted unchanged. The structural parser understands comments, strings,
balanced component values, custom properties, nesting, and block/statement
boundaries; it is not a browser cascade or property validator. Aster does not
interpolate strings into this CSS grammar. A native element property such as
`--accent=accent` instead emits a validated CSS custom property in its HTML
`style` attribute, while static CSS refers to `var(--accent)`. Strings are
restricted to one color, dimension, number, or identifier atom; numeric values
are emitted directly. Dynamic `style=` values are rejected.

`<style scoped>` is available inside an `Html`-returning component. Aster
rewrites every style-rule selector with a stable attribute for the declaring
module and function, and adds that presence attribute to native elements built
by the same function. Selector lists, nested rules, at-rule blocks, and
pseudo-elements are handled structurally; keyframe selectors are deliberately
left alone. `scoped` is compiler metadata and is not emitted as a style
attribute. Plain `<style>` remains global and preserves its authored bytes.
The transformation is compile-time only and does not cross a child component
call boundary.

Each static scoped style also has a content identity. A root `Html` value tracks
those identities, including across detached child buffers and clones, so a
component rendered repeatedly contributes its style only once per document.
The VM and `emit-c` use inline mode. `emit-c-site SOURCE ASSET_DIR` instead
combines reachable scoped styles into one deterministic `site-<hash>.css` and
generates a single link to `/assets/site-<hash>.css`. This is a build-time
choice; Aster source and VM development behavior do not change.

`script` retains HTML raw-text behavior: its Aster string children are written
verbatim, so JavaScript operators such as `<`, `>`, and `&&` retain their source
spelling. Aster will not embed a JavaScript parser; client code remains an
external script or Aster-compiled Wasm.

Function-component properties use the same ordinary string behavior:

```text
<Badge label=$"Issue {issue_id}" tone=$"tone-{issue_id}" />
```

When the parameters are `string label` and `Option<string> tone`, each
interpolation is one temporary `string` whose reference is released after the
component call. Native HTML attributes remain more direct: they
write interpolation segments into the active HTML destination and allocate no
temporary string.

`<>...</>` is a source-level `Html` fragment. It accepts the same ordinary
expressions, declarations, `if`, `switch`, and loops as an element body while
emitting no wrapper tag. A fragment can be nested or returned from a component;
direct composition lowers to the surrounding render destination.

Element properties retain native HTML spelling, including dashed names and
keyword names: `<label for="query">`, `<input type="text">`, and
`<div aria-label="Status" data-state="ready">` are ordinary Aster syntax.
`Option<T>` properties accept either `T` or `Option<T>`; `None` omits the
attribute. Boolean attributes render by presence, not as `"true"` or
`"false"`. Global HTML attributes and `data-*`/`aria-*` are valid on every
source-declared HTML element, while tag-specific properties remain declared in
`std/html.as`.

Concrete enums, unions, and compiler-known results support exhaustive statement
matching:

```text
switch (result) {
    case Result.Ok(T value): { Console.WriteLine(value); }
    case Result.Err(E error): { Console.WriteLine(error); }
}
```

Payload bindings use their declared payload type. Duplicate, foreign, and
missing variants are diagnosed. Switch inside an element body is the same
statement node and can append children from each arm.

Plain enums contain only payloadless members. A member is a value, so code
uses `AssetKind.Xml`, never `AssetKind.Xml()`. Generated C represents a plain
enum as a declaration-order `uint32_t`, and the VM uses the same scalar value
model. Payload-bearing alternatives belong to a `union`; its value is an
inline tag plus variant-aware payload storage.

## Current limits

Raw-pointer arithmetic and capturing closures remain planned. The 0.1 REPL
evaluates
one complete expression at a time and does not persist declarations between
entries.
