# Aster standard-library API map

Status: design baseline for the standard-library-depth milestone.

Aster uses .NET as the reference for application-facing library names,
casing, organization, and ordinary behavior. This is not a promise to copy
the CLR, garbage collection, UTF-16 storage, reflection, or every historical
overload. It is a promise that familiar operations should look familiar and
that Aster will not invent public terminology when a suitable .NET API
already exists.

Official reference surfaces:

- [.NET `String`](https://learn.microsoft.com/en-us/dotnet/api/system.string)
- [.NET `List<T>`](https://learn.microsoft.com/en-us/dotnet/api/system.collections.generic.list-1)
- [.NET `Dictionary<TKey, TValue>`](https://learn.microsoft.com/en-us/dotnet/api/system.collections.generic.dictionary-2)
- [.NET `Queue<T>`](https://learn.microsoft.com/en-us/dotnet/api/system.collections.generic.queue-1)
- [.NET `Stack<T>`](https://learn.microsoft.com/en-us/dotnet/api/system.collections.generic.stack-1)
- [.NET `System.IO`](https://learn.microsoft.com/en-us/dotnet/api/system.io)
- [.NET `DateTime`](https://learn.microsoft.com/en-us/dotnet/api/system.datetime)
- [.NET `JsonSerializer`](https://learn.microsoft.com/en-us/dotnet/api/system.text.json.jsonserializer)

## Precedence rules

When the influences agree, Aster copies .NET's public surface. When they do
not, use this boundary:

| Concern | Governing influence | Rule |
| --- | --- | --- |
| Application-facing names and casing | .NET | Use the .NET type/member name exactly. |
| Discoverability and API grouping | .NET | Prefer familiar type members over unrelated global helpers. |
| Language syntax | Aster, informed by C and C# | Keep Aster's C/C# surface, native HTML/CSS, and target-typed `new()`. |
| Memory and cleanup | Aster/C | No API may imply a CLR GC. Cleanup remains deterministic; immutable strings share storage cheaply. |
| Native ABI and OS mechanisms | C | Keep narrow `Native*` functions, handles, pointers, and status values below the application facade. |
| Text representation | Aster | Strings remain immutable, reference-counted UTF-8. Do not pretend they are .NET UTF-16 strings. |
| Errors | Aster/C# | Ordinary APIs throw; expected absence uses nullable values; `Try*`/`Result` remains available where useful. |
| Reflection-dependent behavior | Decision required | Do not imitate reflection with hidden code generation or invented syntax without approval. |
| Async APIs | C#/.NET | Use `Task`, `Task<T>`, `async`, `await`, `CancellationToken`, and established `*Async` families. Aster supplies its own no-GC runtime beneath that surface. |

An intentional public deviation must be recorded in this document and agreed
before implementation. Low-level `Native*` functions are implementation
surface and do not require .NET naming.

## Status vocabulary

| Status | Meaning |
| --- | --- |
| Exact | Aster already exposes the intended .NET-shaped API. |
| Adapt | Keep the .NET name but document an Aster representation or semantic difference. |
| Rename | Existing public helper should be replaced by the .NET-shaped name. |
| Low-level | Retain for runtime/library implementation, not normal application code. |
| Missing | Implement in the named milestone. |
| Decision | Requires an explicit design decision before implementation. |

## Language prerequisites

Several .NET surfaces cannot be represented honestly until these language
features exist:

| Requirement | Needed by | Current state |
| --- | --- | --- |
| Declared properties | `string.Length`, `List<T>.Count`, `DateTime.Year` | C#-style expression, custom-accessor, and automatic properties are implemented. Built-in types may expose the same surface through compiler-known accessors. |
| Indexers | `list[index]`, string indexing | Arrays, lists, and checked string byte indexing are implemented for reads. List index assignment remains future work. |
| Function overloading | `Substring`, `Split`, `File.Copy`, `Parse` families | Bounded arity/exact-type overloads are implemented; same-arity generic-template trial inference remains future work. |
| `out` parameters or an approved alternative | Numeric `TryParse` | Aster has `ref`, but `ref` is not the same contract as C# `out`. |
| Static members on primitive aliases | `int.Parse`, `string.Join` | Static-call syntax exists, but primitive-owner support needs confirmation. |
| Generic comparison/equality capability | collection search, sorting, dictionaries | Current generic constraints and comparer abstractions are incomplete. |
| Native asynchronous I/O completion | `HttpClient`, streams | `HttpClient` uses shared libcurl-multi transfers driven by nonblocking executor-timer polls and has bounded response and fixed-length upload streaming; generic native readiness registration and async file streams remain pending. |

The standard library should not compensate for these gaps with permanent
names such as `StringLen` or `ListGet`. Implement the prerequisite or record a
temporary internal bridge.

## Console output

Application output uses the discoverable .NET-shaped surface:

```aster
Console.WriteLine("Hello");
Console.Write("working");
Console.Error.WriteLine("failed");
Console.Error.Write("details");
```

`WriteLine` appends a newline and `Write` does not. The compiler and runtimes
may use specialized output operations internally, but the former global
`print` and `eprint` helpers are not part of the language surface.

## 1. Strings and text

Target source shape:

```aster
if (title.Contains("Aster") && title.StartsWith("A"))
{
    string excerpt = title.Substring(0, 20);
}

string joined = string.Join(", ", names);
```

### Current-to-target map

| Current Aster surface | .NET-shaped target | Status | Notes |
| --- | --- | --- | --- |
| `string` | `string` / `String` | Exact | Immutable source value; Aster storage remains UTF-8 and reference-counted. |
| `StringLen(value)` | `value.Length` | Exact/adapt | Application code uses the property. `StringLen` remains only as a runtime bridge. Aster `Length` is the UTF-8 byte length, unlike .NET's UTF-16 code-unit count. |
| `StringByteAt(value, index)` | no application equivalent | Low-level | Retain as an explicit parser/runtime byte primitive. |
| `StringSlice(value, start, end)` | `value.Substring(start, length)` | Exact/adapt | `StringSlice` remains a low-level half-open byte primitive. Public `Substring` uses byte start and length. |
| `StringStartsWith(value, prefix)` | `value.StartsWith(prefix)` | Exact |
| `StringEndsWith(value, suffix)` | `value.EndsWith(suffix)` | Exact |
| `StringContains(value, needle)` | `value.Contains(needle)` | Exact |
| `StringFindByte(value, byte)` | no direct application equivalent | Low-level | Public search uses `IndexOf` and returns `-1` when absent, as .NET does. |
| `TextJoin(a, b)` / `TextJoin3(a, b, c)` | `string.Concat(...)` | Exact | The old public helpers are gone. `.NET Join` means separator-based collection joining. |
| `I64ToString` / `U64ToString` | `value.ToString()` | Exact | The globals remain runtime bridges for the public numeric members. |
| `StringBuilder.new()` | `new()` | Exact | Target-typed construction matches current Aster direction. |
| `builder.append(value)` | `builder.Append(value)` | Exact |
| `builder.finish()` | `builder.ToString()` | Exact | Public `ToString()` snapshots without consuming the builder. Compiler-generated one-shot construction retains a private consuming finish operation. |
| `builder.AppendByte(value)` | no .NET string equivalent | Low-level/Aster | Needed by UTF-8 parsers; keep explicit and out of ordinary text construction. |

### Missing first-family surface

- `ToUpper`, `ToLower`, `ToUpperInvariant`, `ToLowerInvariant`
- `Compare`
- `ToCharArray` after character indexing is settled
- richer `StringBuilder` construction/capacity overloads

Implemented first-family members now include `Empty`, `Length`, `Contains`,
`StartsWith`, `EndsWith`, `IndexOf`, `LastIndexOf`, both `Substring` overloads,
`Insert`, both `Remove` overloads, `Replace`, `IsNullOrEmpty`,
`IsNullOrWhiteSpace`, `Trim`, `TrimStart`, `TrimEnd`, `CompareOrdinal`,
`Equals`, `Concat`, `Join`, the string-separator and parameterless `Split`
overloads, and
`StringBuilder.Append`, `AppendLine`, `Clear`, `Length`, and `ToString`.

`Split(string)` uses ordinal string separators and `Split()` uses the same
Unicode whitespace set as `Trim`. Both return `List<string>` because Aster's
growable collection is `List<T>`. The string-separator overloads support
`StringSplitOptions.None`, `RemoveEmptyEntries`, `TrimEntries`, combined flags,
and count limiting. Separator-collection overloads remain pending.

### Settled text semantics

Aster strings use a performance-transparent UTF-8 model:

1. `string.Length` is the UTF-8 byte length and is constant-time.
2. `text[index]` performs checked byte indexing and returns `byte`.
3. `Substring`, `IndexOf`, and related offsets use byte positions.
4. Ordinary comparison and search are ordinal. Culture-aware overloads are
   omitted until Aster has genuine culture support; they will not be
   approximated.
5. Aster does not add `ByteLength`. Because `Length` already counts bytes, a
   second name would be redundant and would deviate from .NET without adding
   information.
6. `foreach (char scalar in text)` decodes Unicode scalar values directly;
   `ToCharArray()` materializes them only when an owned collection is wanted.
   This does not make `Length`, indexing, or substring operations secretly
   linear-time.
7. The parameterless trim family and `IsNullOrWhiteSpace` recognize the Unicode
   whitespace scalar set directly in UTF-8. Numeric parsing deliberately keeps
   its narrower invariant ASCII whitespace grammar.

This is an intentional Aster/C semantic adaptation beneath .NET member names.
It preserves explicit costs and matches the UTF-8 representation used by web,
files, C libraries, and generated C. APIs that construct a `string` from
arbitrary byte ranges must document whether they validate UTF-8.

Implemented Unicode 15.1 invariant text includes `char.IsLetter`, `IsDigit`,
`IsLetterOrDigit`, `IsUpper`, `IsLower`, `IsWhiteSpace`, scalar and string
`ToUpperInvariant`/`ToLowerInvariant`, direct UTF-8 scalar iteration, and strict
`Encoding.UTF8().GetBytes`/`GetString`. Invalid UTF-8 passed to a Unicode-facing
API throws `FormatException`. Checked-in generated tables make behavior
independent of the host locale and require no ICU/runtime Unicode dependency.

## 2. Numeric types and conversion

| Current Aster surface | .NET-shaped target | Status | Notes |
| --- | --- | --- | --- |
| C-style casts `(int)value` | explicit casts | Exact/Aster | Preserve predictable checked narrowing already defined by Aster. |
| `I64ToString`, `U64ToString` | `value.ToString()` | Exact/adapt | All integer, native-size, `float`, and `double` types expose the member. Aster formatting is invariant and culture-free. |
| none | numeric `Parse(string)` members | Exact/adapt | Implemented for every integer, native-size, `float`, and `double` type. See the invariant parsing boundary below. |
| none | corresponding `TryParse(string, out value)` members | Exact/adapt | Implemented for every numeric primitive; invariant Aster parsing and zero-on-failure output. |
| none | `Convert` | Implemented core | Exact `ToBoolean`, `ToSByte`, `ToInt16`, `ToInt32`, `ToInt64`, `ToByte`, `ToUInt16`, `ToUInt32`, `ToUInt64`, `ToSingle`, `ToDouble`, and `ToString` names. |
| interpolation formatting | `ToString` format strings | Adapt | Keep allocation-free destination formatting where possible. |

Do not introduce Aster-specific names such as `ParseI32` or `StringToInt`.

### Invariant numeric text boundary

Aster has no ambient culture. Parameterless numeric `Parse` and `ToString`
therefore use deterministic invariant decimal text:

- integer parsing accepts ASCII leading/trailing whitespace, one leading `+`
  or `-`, and decimal digits, with checked target-width bounds;
- floating parsing accepts ASCII leading/trailing whitespace, a leading sign,
  a `.` decimal point, and an optional `e`/`E` exponent;
- grouping separators, currency, `NumberStyles`, format providers, `NaN`, and
  infinity spellings remain pending rather than depending on process locale;
- `float.ToString()` uses nine significant digits and `double.ToString()` uses
  seventeen, preserving round-trip information but not promising .NET's exact
  shortest-text algorithm yet;
- parse failures throw `FormatException` or `OverflowException` with stable
  messages.

The built-in exception hierarchy also includes `ArgumentException`,
`InvalidOperationException`, `IOException`, and `JsonException`. Derived
handlers match their exact type, while `catch (Exception error)` catches every
derived exception.

This is an explicit Aster/C adaptation under the .NET member names. The
future provider/style overloads may add culture-aware behavior without changing
the deterministic parameterless behavior.

`Convert` currently covers checked conversions among integral types, Boolean
and numeric conversions, invariant string parsing, integral-to-floating and
floating-width conversions, invariant string formatting, and nearest-even
floating-point conversion to every integer width, including explicit
double-precision boundary handling for `ToInt64` and `ToUInt64`. Base
conversion, object, character, date/time, decimal, and format-provider
overloads remain outside the current core rather than being approximated.

The web-relevant Base64 pair is implemented as
`Convert.ToBase64String(List<byte>)` and
`Convert.FromBase64String(string) -> List<byte>`. `List<byte>` is Aster's
current dynamic collection adaptation for .NET's `byte[]`. Decoding ignores
the same four ASCII whitespace characters as .NET and rejects malformed
alphabet, length, and padding. The offset/length encoding overload is also
available. `Base64FormattingOptions.None` and `InsertLineBreaks` are supported,
including the offset/length/options overload and CRLF insertion every 76
encoded characters. Span and character-array overloads remain pending.

## 3. `List<T>` and collections

Target source shape:

```aster
List<Post> posts = new();
posts.Add(post);
posts.Insert(0, featured);
int count = posts.Count;
Post first = posts[0];
```

| Current Aster surface | .NET-shaped target | Status | Notes |
| --- | --- | --- | --- |
| `List<T> values = new()` | target-typed `new()` | Exact |
| `values.Add(value)` | `values.Add(value)` | Exact |
| `values.Count` | `values.Count` | Exact | Read-only property; Aster currently exposes collection sizes as `nuint`. |
| `values.Capacity` | `values.Capacity` | Exact property shape | Read/write; Aster exposes collection sizes as `nuint`. Capacity cannot be set below `Count`. |
| `values[index]` | `values[index]` | Exact | Checked reads and assignments are implemented. |
| `values.Insert(index, value)` | `values.Insert(index, value)` | Exact name and behavior | Accepts insertion at `Count`; Aster indexes are `nuint`. |
| `values.RemoveAt(index)` | `values.RemoveAt(index)` | Exact name and behavior | Releases the removed element and shifts later elements. |
| `values.Contains(value)` | `values.Contains(value)` | Implemented for defined-equality types | Linear search using Aster value equality. |
| `values.IndexOf(value)` | `values.IndexOf(value)` | Exact name and result shape | Returns the first index or `-1`; range overloads remain pending. |
| `values.LastIndexOf(value)` | `values.LastIndexOf(value)` | Exact name and result shape | Returns the last index or `-1`; range overloads remain pending. |
| `values.Remove(value)` | `values.Remove(value)` | Exact name and behavior | Removes the first equal value and reports success. |
| `values.AddRange(other)` | `values.AddRange(collection)` | List source implemented | Preserves order and supports adding a list to itself. |
| `values.InsertRange(index, other)` | `values.InsertRange(index, collection)` | List source implemented | Accepts `Count` and supports self-insertion. |
| `values.RemoveRange(index, count)` | `values.RemoveRange(index, count)` | Exact name and behavior | Checked removal; a zero-length range at `Count` is valid. |
| `values.GetRange(index, count)` | `values.GetRange(index, count)` | Exact name and value behavior | Returns an independent `List<T>` value. |
| `values.Reverse()` / `values.Reverse(index, count)` | same | Exact overload family | Reverses the complete list or a checked range in place. |
| `values.EnsureCapacity(capacity)` | same | Exact name and behavior | Returns the resulting capacity as Aster `nuint`. |
| `values.TrimExcess()` | same | Exact behavior | Shrinks to `Count` only below the documented 90-percent threshold. |
| `values.Exists(predicate)` | same | Exact whole-list form | Stops at the first match. |
| `values.FindAll(predicate)` | same | Exact whole-list form | Returns an independent list of matching values. |
| `values.FindIndex(predicate)` | same | Whole-list overload implemented | Returns the first matching index or `-1`; range overloads remain pending. |
| `values.FindLastIndex(predicate)` | same | Whole-list overload implemented | Returns the last matching index or `-1`; range overloads remain pending. |
| `values.RemoveAll(predicate)` | same | Exact name and behavior | Removes all matches in place and returns the removed count. |
| `values.ForEach(action)` | same | Exact whole-list form | Calls an Aster function value once per element. |
| `values.TrueForAll(predicate)` | same | Exact name and behavior | Stops at the first false result. |
| `values.Clear()` | `values.Clear()` | Exact | Releases elements, sets `Count` to zero, and retains capacity. |
| `VecWithOne(value)` | collection initializer or ordinary `Add` | Removed | Rust-shaped legacy name had no .NET counterpart. |
| `VecPushed(values, value)` | ordinary copy plus `Add` | Removed | Ordinary assignment plus `Add` replaces it. |

Collection API family (implemented members above; remaining members are
capability-gated rather than name inventions):

`Contains`, `IndexOf`, `LastIndexOf`, and `Remove` currently accept scalar,
character, string, string-view, and raw-pointer element types. Aggregate
elements require Aster to settle user-defined/default equality first; the
checker rejects them rather than generating pointer or bytewise equality.

Range collection inputs currently accept `List<T>`. .NET accepts any
`IEnumerable<T>`, but Aster does not yet have that interface abstraction.
`ToArray` remains pending because Aster arrays currently have compile-time
lengths; it will not return a disguised list or another invented type.

Predicate members accept ordinary Aster function values with the corresponding
`Func<T, bool>` or `Func<T, void>` shape. `Find` and `FindLast` remain pending:
.NET returns `default(T)` when no match exists, and Aster has not settled a
general `default(T)` contract. They will not be changed to nullable or result
returns without an explicit language decision.

- `Capacity`, `Count`, indexer
- `Add`, `AddRange`
- `Insert`, `InsertRange`
- `Contains`, `IndexOf`, `LastIndexOf`
- `Remove`, `RemoveAt`, `RemoveAll`, `RemoveRange`
- `Clear`
- `CopyTo`, `GetRange`, `ToArray`
- `Reverse`, `Sort`, `BinarySearch`
- `Exists`, `Find`, `FindAll`, `FindIndex`, `FindLast`
- `ForEach`, `TrueForAll`
- `EnsureCapacity`, `TrimExcess`

### `Dictionary<TKey, TValue>`

The first bounded dictionary surface is implemented in the VM and generated C:

| Aster surface | .NET surface | Status |
| --- | --- | --- |
| `Dictionary<TKey, TValue> values = new()` | target-typed `new()` | Exact |
| `values.Add(key, value)` | same | Exact; duplicate keys fail |
| `values.TryAdd(key, value)` | same | Exact Boolean result; existing values are unchanged |
| `values.TryGetValue(key, out value)` | same | Exact Boolean and output shape; unsuccessful lookup writes the default value |
| `values.Count` | same | Exact property shape; Aster size is `nuint` |
| `values.ContainsKey(key)` | same | Exact |
| `values.ContainsValue(value)` | same | Exact for values with built-in equality |
| `values.Capacity` | same | Exact read-only property shape; Aster size is `nuint` |
| `values.EnsureCapacity(capacity)` | same | Exact behavior; returns current capacity as `nuint` |
| `values.TrimExcess()` | same | Exact behavior |
| `values.TrimExcess(capacity)` | same | Exact behavior; capacity cannot be below `Count` |
| `values[key]` | same | Checked read and insert-or-replace assignment |
| `values.Remove(key)` | same | Exact Boolean result |
| `values.Clear()` | same | Exact |
| `values.KeyAt(index)` | no direct equivalent | Checked key copy by dense logical index |
| `values.ValueAt(index)` | no direct equivalent | Checked value copy by dense logical index |

Keys are currently limited to scalar, character, `string`, and raw-pointer
types with defined built-in equality. The VM and generated C keep dense
key/value storage plus an open-addressed hash index, giving average constant-
time key lookup while retaining deterministic cleanup and copying. Dictionary
assignment performs an independent value copy, including keys and values.
`Keys` and `Values` remain pending because .NET exposes live read-only
collection views. Aster will not disguise copied `List<T>` snapshots under
those names. `KeyAt` and `ValueAt` are bounded Aster extensions used for
allocation-free indexed traversal; structural mutation can change the logical
order, so callers must not treat an index as a stable key identity.

### `HashSet<T>`

The first bounded set surface is implemented in the VM and generated C:

| Aster surface | .NET surface | Status |
| --- | --- | --- |
| `HashSet<T> values = new()` | target-typed `new()` | Exact |
| `values.Add(value)` | same | Exact Boolean result; duplicates are unchanged |
| `values.Contains(value)` | same | Exact |
| `values.Remove(value)` | same | Exact Boolean result |
| `values.Count` | same | Exact property shape; Aster size is `nuint` |
| `values.Capacity` | same | Exact read-only property shape; Aster size is `nuint` |
| `values.EnsureCapacity(capacity)` | same | Exact behavior; returns current capacity as `nuint` |
| `values.TrimExcess()` | same | Exact behavior |
| `values.TrimExcess(capacity)` | same | Exact behavior; capacity cannot be below `Count` |
| `values.Clear()` | same | Exact |

Elements have the same current equality restrictions as Dictionary keys.
`HashSet<T>` has its own static type identity and source surface, while sharing
the open-addressed hash storage implementation. Assignment creates an
independent set copy.

### `Queue<T>`

The first bounded FIFO surface is implemented in the VM and generated C:

| Aster surface | .NET surface | Status |
| --- | --- | --- |
| `Queue<T> values = new()` | target-typed `new()` | Exact |
| `values.Enqueue(value)` | same | Exact |
| `values.Dequeue()` | same | Exact FIFO result; empty queues fail |
| `values.Peek()` | same | Exact non-removing result; empty queues fail |
| `values.TryDequeue(out value)` | same | Exact Boolean/output shape; removes only on success |
| `values.TryPeek(out value)` | same | Exact Boolean/output shape; never removes |
| `values.Count` | same | Exact property shape; Aster size is `nuint` |
| `values.Capacity` | same | Exact read-only property shape; Aster size is `nuint` |
| `values.EnsureCapacity(capacity)` | same | Exact behavior; returns current capacity as `nuint` |
| `values.TrimExcess()` | same | Exact behavior |
| `values.Clear()` | same | Exact |

Queue storage is a circular buffer, so ordinary dequeue does not shift the
remaining elements. Assignment copies the active elements into an independent
queue in FIFO order. Unsuccessful `TryDequeue` and `TryPeek` calls write the
element type's zero/default representation to the output.

### `Stack<T>`

The first bounded LIFO surface is implemented in the VM and generated C:

| Aster surface | .NET surface | Status |
| --- | --- | --- |
| `Stack<T> values = new()` | target-typed `new()` | Exact |
| `values.Push(value)` | same | Exact |
| `values.Pop()` | same | Exact LIFO result; empty stacks fail |
| `values.Peek()` | same | Exact non-removing result; empty stacks fail |
| `values.TryPop(out value)` | same | Exact Boolean/output shape; removes only on success |
| `values.TryPeek(out value)` | same | Exact Boolean/output shape; never removes |
| `values.Count` | same | Exact property shape; Aster size is `nuint` |
| `values.Capacity` | same | Exact read-only property shape; Aster size is `nuint` |
| `values.EnsureCapacity(capacity)` | same | Exact behavior; returns current capacity as `nuint` |
| `values.TrimExcess()` | same | Exact behavior |
| `values.TrimExcess(capacity)` | same | Exact behavior; capacity cannot be below `Count` |
| `values.Clear()` | same | Exact |

Stack storage shares the contiguous allocation and copy engine used by lists,
while retaining a distinct static type and LIFO-only public API. Assignment
creates an independent stack copy. Unsuccessful `TryPop` and `TryPeek` calls
write the element type's zero/default representation to the output.

The initial predictable-collections baseline—`List<T>`,
`Dictionary<TKey, TValue>`, `HashSet<T>`, `Queue<T>`, and `Stack<T>`—is now
implemented in both primary execution paths.

LINQ is not part of this milestone. Complete predictable collections before
adding a query abstraction.

## 4. Files, directories, and paths

### Public facade map

| Current Aster surface | .NET-shaped target | Status |
| --- | --- | --- |
| `File.ReadAllText` | `File.ReadAllText` | Exact |
| `File.ReadAllBytes` | `File.ReadAllBytes` | Exact |
| `File.ReadAllLines` | `File.ReadAllLines` | Exact |
| `File.WriteAllText` | `File.WriteAllText` | Exact |
| `File.WriteAllBytes` | `File.WriteAllBytes` | Exact |
| `File.OpenRead`, `OpenWrite`, `Create` | same | Implemented with `FileStream` |
| `NativePathExists` | `File.Exists` and `Directory.Exists` | Exact facade implemented; native bridge retained |
| `NativeCreateDirectory` | `Directory.CreateDirectory` | Name implemented; `DirectoryInfo` return remains unavailable |
| `NativeRenamePath` | `File.Move` / `Directory.Move` | Exact two-path facade implemented; native bridge retained |
| `NativeRemoveFile` | `File.Delete` | Exact facade implemented |
| `NativeRemoveDirectory` | `Directory.Delete` | Non-recursive overload implemented |
| `CopyFileBuffered` | `File.Copy` | Both two-path and Boolean-overwrite overloads implemented |
| `DiscoverFiles` | `Directory.GetFiles` or `EnumerateFiles` | Adapt | Deterministic content discovery remains a Lime/content policy API. |

First `File` family:

- `Exists` (implemented)
- `ReadAllText`, `ReadAllLines`, `ReadAllBytes` (implemented)
- `WriteAllText`, `WriteAllLines`, `WriteAllBytes` (implemented)
- `AppendAllText`, `AppendAllLines` (implemented)
- `Copy`, `Move`, `Delete` (implemented)
- `OpenRead`, `OpenWrite`, `Create` (implemented)

First `Directory` family:

- `Exists`, `CreateDirectory`, `Delete`, `Move`
- `GetFiles`, `GetDirectories`, `GetFileSystemEntries` (path-only overloads
  implemented; return `List<string>` until Aster has runtime-sized arrays)
- `EnumerateFiles`, `EnumerateDirectories` only after Aster has an
  appropriate lazy enumeration abstraction
- `GetCurrentDirectory`, `SetCurrentDirectory` (implemented)

First `Path` family:

- `Combine`, `Join` (two-, three-, and four-string overloads implemented)
- `GetFileName`, `GetFileNameWithoutExtension` (implemented)
- `GetExtension`, `ChangeExtension` (implemented)
- `GetDirectoryName`, `GetPathRoot` (implemented with .NET nullable root
  behavior)
- `GetFullPath`, `GetRelativePath`
- `IsPathFullyQualified` (implemented)
- `DirectorySeparatorChar`, `AltDirectorySeparatorChar`

### Handle naming correction

Aster now keeps `File` and `Directory` separate from native resource handles.
In .NET, `File` and `Directory` are static facades; Aster follows that public
shape while retaining deterministic native handles beneath it:

- `File` and `Directory`: application-facing static API owners;
- `FileStream`: deterministic resource-owning stream;
- `DirectoryStream`: temporary low-level directory iteration handle pending a
  proper enumeration facade;
- `NativeFile*`, `NativeDirectory*`, and `NativePath*`: low-level ABI surface.

`Stream`, `FileStream`, and `MemoryStream` now provide byte reads and writes,
including allocation-free `ReadInto(Span<byte>)` and
`Write(ReadOnlySpan<byte>)`, position and length, seeking, flushing, closing,
and buffered `CopyTo`. The byte-span layer provides bounded ranges,
overlap-safe copy/try-copy, fill/clear, equality, search, and edge tests.
`BinaryReader` and `BinaryWriter` provide little-endian primitive and UTF-8
string I/O. Native handles remain behind the stream facade. `DirectoryStream`
is still the low-level directory handle pending a lazy enumeration API.

The HTTP bridge exposes request bodies as `MemoryStream` and accepts byte-list
responses without making text encoding part of the transport contract.

## 5. Environment and processes

| Current Aster surface | .NET-shaped target | Status | Notes |
| --- | --- | --- | --- |
| `NativeProcessEnvironment(name)` | `Environment.GetEnvironmentVariable(name)` | Low-level + missing facade | Public return should be `string?` for absence. |
| `NativeProcessArgCount/Arg` | `Environment.GetCommandLineArgs()` | Decision | .NET includes the executable; Aster currently exposes only application arguments. |
| `System.Diagnostics.Process` / `ProcessStartInfo` | `Process.Start` / `ProcessStartInfo` | Bounded POSIX implementation | Structured arguments, working directory, environment overrides, redirected standard streams, wait, status query, and termination; no implicit shell or async family. |

`CliArgument` parsing is Aster policy, not a .NET BCL equivalent. It may stay
in a separate CLI package but should consume the approved argument API.

`Process.Start` passes `FileName` and every `Arguments` entry directly to
`execvp`; it never concatenates a shell command. A shell remains available only
when the program explicitly starts one. `ProcessEnvironmentVariable` entries
override inherited variables, while an empty `WorkingDirectory` inherits the
current directory. Standard I/O methods operate on caller-provided byte spans.
When stdout and stderr are both redirected, callers must drain both often
enough to avoid ordinary pipe backpressure; async/concurrent draining is a
future executor integration. Dropping the final owning handle terminates and
reaps an unfinished child, making cleanup deterministic. The current spawning
backend is POSIX; Windows returns a typed unavailable error until a
`CreateProcessW` implementation lands.

## 6. Date and time

Implement after strings, numbers, collections, and filesystem:

- `DateTime`
- `DateTimeOffset`
- `TimeSpan`
- `DateOnly`
- `TimeOnly`

Use .NET member names such as `UtcNow`, `Now`, `Today`, `Year`, `Month`,
`Day`, `AddDays`, `Subtract`, `Parse`, `TryParse`, and `ToString`.

Implemented UTC/invariant core:

- `TimeSpan.FromTicks`, `FromMilliseconds`, `FromSeconds`, `FromMinutes`,
  `FromHours`, `FromDays`, totals, `Add`, `Subtract`, and `Negate`;
- `DateTime.UtcNow`, calendar-part accessors, `Add`, `AddDays`, both
  `Subtract` forms, `Date`, `TimeOfDay`, `Parse`, `TryParse`, and `ToString`;
- `DateTimeOffset.UtcNow`, Unix-second/millisecond conversion, calendar-part
  accessors, arithmetic, `UtcDateTime`, `Parse`, `TryParse`, and `ToString`;
- `DateOnly` invariant parsing and formatting, calendar parts, `DayNumber`,
  `AddDays`, `FromDateTime`, and `ToDateTime`;
- `TimeOnly` invariant parsing and formatting, time parts, ticks, wrapping
  arithmetic, and conversion to/from `DateTime` and `TimeSpan`;
- a single OS-backed UTC clock primitive with matching VM and generated-C
  behavior;
- deterministic `yyyy-MM-ddTHH:mm:ss.fffZ` parsing and formatting, including
  leap-year validation and dates before the Unix epoch;
- explicit ISO-8601 `±HH:mm` offsets, `Offset`, and `ToOffset`, preserving the
  UTC instant while exposing local calendar components.

`TimeSpan` stores .NET-compatible 100-nanosecond ticks. The current
`DateTime`/`DateTimeOffset` OS boundary has millisecond resolution. Until
declared properties are implemented, .NET properties use method-call spelling
such as `value.Year()` and `DateTime.UtcNow()`.

Still intentionally unresolved:

- bundled timezone database versus host OS timezone services;
- local `Now`, `Today`, automatic host offsets, and daylight-saving rules;
- culture-dependent parsing/formatting;
- broader ISO-8601 input shapes and custom format strings;
- the broader .NET `DateOnly` and `TimeOnly` overload families;
- the exact final supported calendar range.

Do not provide misleading culture or timezone APIs until their semantics are
real.

## 7. Native HTTP client

`System.Net.Http` has a bounded native foundation backed by the
optional libcurl component:

- `new HttpClient()` with configurable `TimeoutMilliseconds`,
  `MaximumResponseBodyBytes`, and `FollowRedirects`;
- `Get`, `Delete`, general `Send`, and byte/string `Post`;
- `GetAsync` and general `SendAsync`, with optional cooperative
  `CancellationToken`;
- `HttpResponseMessage.StatusCode`, raw `Headers`, final `RequestUri`,
  `IsSuccessStatusCode`, and `EnsureSuccessStatusCode`;
- owned `HttpContent` with `Length`, borrowed `ReadAsBytes`, and copied
  `ReadAsString`;
- headers-first `GetStreamAsync` returning `HttpResponseStream`, whose
  `ReadAsync` fills caller-provided spans and whose `Close` cancels an
  unfinished transfer;
- `StartUpload` returning `HttpUploadStream`; `WriteAsync` feeds borrowed byte
  spans through a bounded native queue and `CompleteAsync` verifies the
  declared content length before returning the response;
- HTTP/HTTPS-only redirects, a ten-redirect ceiling, one-MiB response-header
  limit, bounded response bodies, and deterministic native-handle cleanup;
- asynchronous transfers share a libcurl multi handle for concurrency and
  connection reuse; request bytes are copied before the first suspension;
- streaming downloads use a bounded 64-KiB native queue and libcurl
  pause/resume backpressure rather than buffering the complete response;
- fixed-length streaming uploads use the same bounded 64-KiB pause/resume
  design and never retain a caller span after a native write call;
- matching VM and generated-C behavior against a local
  binary/redirect/POST/concurrency/cancellation/large-stream fixture.

`ASTER_ENABLE_CURL=OFF` builds typed unavailable native stubs instead of
requiring libcurl. This is an implementation dependency of native
`System.Net.Http`, not a language dependency. The current `Headers` spelling is
a raw response-header block and request headers use a bounded raw block; typed
header collections remain pending. Unknown-length/chunked uploads, connection
policy objects, cookies, and proxies are not yet
implemented. Multi transfers currently use a one-millisecond nonblocking poll
through the existing timer executor; socket-readiness registration is a future
efficiency refinement, not a semantic dependency.

## 8. Cryptography

`System.Security.Cryptography` is a thin platform/native facade:

- `RandomNumberGenerator.Fill(Span<byte>)` uses the operating system secure
  random source and never a language PRNG;
- `GetHexString(byteCount)` provides bounded random hexadecimal text, and
  `UuidV4()` sets the RFC version and variant bits over OS-generated bytes;
- `SHA256.HashData` and `HMACSHA256.HashData` return owned 32-byte `Buffer`
  values and delegate to OpenSSL rather than implementing algorithms in Aster;
- `CryptographicOperations.FixedTimeEquals` compares equal-length byte spans
  without data-dependent early exit.

The component is isolated from `langlib`. `ASTER_ENABLE_CRYPTO=OFF` removes the
OpenSSL dependency and leaves hashing as typed-unavailable operations while OS
randomness, UUIDs, and the constant-time fallback remain available. Lime
sessions now obtain 256-bit identifiers through `GetHexString(32)` instead of
asking SQLite for `randomblob`. Lime also supports atomic session-ID rotation,
server-side destruction with deletion cookies, and amortized or explicit
expired-session sweeping.

## 9. JSON

The public reference is `System.Text.Json`:

- `JsonSerializer.Serialize`
- `JsonSerializer.Deserialize<T>`
- `JsonDocument`
- `JsonElement`
- `JsonValueKind`
- `Utf8JsonReader`
- `Utf8JsonWriter`
- converter support

Aster has no CLR reflection. Each concrete call to generic `Serialize<T>` or
`Deserialize<T>` is monomorphized, and the checker generates direct typed code
for that concrete shape before IR lowering. There is no runtime field lookup,
type metadata registry, or reflection fallback.

The parser and DOM can be implemented independently of that decision and
should preserve UTF-8 input rather than converting through UTF-16.

Implemented DOM slice:

- `JsonDocument.Parse(string)` and `RootElement`;
- `JsonElement.Parse(string)`, `ValueKind`, array indexing, `GetProperty`,
  `GetPropertyCount`, indexed `GetPropertyName`/`GetPropertyAt`, and
  `GetArrayLength`;
- `GetString`, `GetBoolean`, the supported integral and floating-point
  getters, `Clone`, and `GetRawText`;
- compiler-generated `JsonSerializer.Serialize<T>` and `Deserialize<T>` for
  strings, Boolean and numeric scalars, payloadless enums, `JsonElement`,
  `Option<T>`, `List<T>`, `Dictionary<string, T>`, and structs composed from
  supported fields;
- `JsonWriter`, an Aster-owned forward-only text writer with structural
  object/array validation, property names, escaped strings, Boolean/null and
  numeric values, and validated `JsonElement` insertion;
- strict JSON grammar, UTF-8 validation, escape decoding, and a depth limit;
- cheap immutable element views over the reference-counted source string,
  shared by the VM and generated-C paths.

Array indexing uses the CLR indexer member name `Item`, so an ordinary Aster
member supplies `element[index]` without a JSON-specific compiler rule.
`JsonElement.TryGetProperty(string, out JsonElement)` uses the C#-shaped API
and returns `JsonValueKind.Undefined` through the output for a missing member.
Struct property names are their source field names. Missing required fields,
wrong JSON kinds, invalid enum names, duplicate dictionary keys, and numeric
parse failures raise an exception; extra object properties are ignored.
Classes, payload-bearing unions, non-string-key dictionaries, and custom naming
or converter policies are intentionally rejected at compile time in this
bounded first version. `Deserialize<T>` normally infers `T` from its expected
result type, for example `User user = JsonSerializer.Deserialize(json);`.

`JsonWriter` deliberately owns a `StringBuilder` and returns one completed JSON
string; it is the bounded intermediate needed by Lime today. The distinct
`Utf8JsonWriter` name remains pending a byte destination such as `Stream` or
`IBufferWriter<byte>` so that Aster does not misrepresent .NET's
destination-backed contract.

## 10. Aster-native and non-BCL libraries

These areas should not be distorted merely to resemble .NET:

| Aster area | Direction |
| --- | --- |
| `Html` and native HTML components | Aster-native language/library surface. |
| Native CSS | Aster-native language feature. |
| `Option<T>` and `Result<T,E>` | Retain as ordinary types; they are not the default infrastructure error style. |
| Lime routing, SSR, SSG, forms, browser WASM | Lime framework surface, not the core standard library. |
| SQLite direct facade | Keep SQLite-shaped: typed statements, rows, transactions, and SQL-file migrations; Lime may add a higher application layer later. |
| `Arena`, raw pointers, slices, buffers | Systems layer governed by Aster/C semantics. |
| `Native*` functions | Stable narrow runtime boundary, not application-facing API design. |

## Settled namespace organization

Aster adopts .NET's `System.*` organization. C does not provide a competing
`std` namespace convention, while `Std.*` would otherwise become an
Aster-specific imitation of C++ rather than either C or .NET.

The implemented organization is:

```text
System.IO
System.Text
System.Net.Http
System.Collections.Generic

Aster.Html
Aster.Memory
Aster.Interop
Aster.Core
Aster.CommandLine
Aster.Content
Aster.Data.Sqlite
Aster.Net.Http
Aster.Testing
Aster.Web.HttpApp
Aster.Web.Middleware
Aster.Web.Router

Lime
Lime.Content
Lime.Ssg
Lime.Browser
```

`System.*` contains familiar general-purpose application APIs. Aster-native
language facilities, low-level systems facilities, and interoperability do
not masquerade as .NET BCL features. Lime remains an independent framework.

The former `Std.*` namespace has been removed rather than retained as a
compatibility alias. The `std/` directory remains an internal source-tree
location only.

## Implementation order

1. Keep the settled `System.*`/`Aster.*` namespace organization coherent.
2. Extend the implemented property/indexer/overload surface only as concrete APIs require it.
3. Complete `string` and `StringBuilder`; migrate Aster and Nook off global
   text helpers.
4. Add numeric parsing, formatting, and the approved `TryParse` contract.
5. Complete `List<T>`; remove `Vec*` terminology.
6. Build `File`, `Directory`, `Path`, streams, and binary I/O over `Native*`.
   (implemented; additional overloads remain demand-driven)
7. Add `Dictionary`, `HashSet`, `Queue`, and `Stack`.
8. Add date/time.
9. Extend the compiler-generated typed JSON subset only from concrete use cases.
10. Use Nook to measure remaining application ceremony before expanding the
    surface further.
