# Portable C backend

Aster's primary backend is its portable C17 emitter:

```sh
lang emit-c examples/c_backend.as > generated.c
cc -std=c17 generated.c -o generated
./generated
```

For a cacheable component stylesheet instead of inline scoped styles:

```sh
mkdir -p public/assets
lang emit-c-site app.as public/assets > generated.c
cc -std=c17 generated.c -o generated
```

The command writes exactly one `site-<content-hash>.css` into the supplied
directory. Generated HTML references it as `/assets/site-<content-hash>.css`.
The filename and bytes are deterministic. Normal `emit-c` remains the optional
inline mode, matching VM development behavior. Manifest targets use
`lang project emit-c-site aster.toml public/assets [TARGET]`.

Executable lowering consumes verified typed IR. The IR owns parameter, ABI,
type-policy, aggregate, async, render, static-CSS, and native-call metadata.
Frontend declaration and checked-type links are cleared before the backend is
invoked. Site assets are emitted from IR static-CSS entries rather than by
walking raw function bodies.

The optional site-asset pass is build orchestration around that backend: it
reads the already-checked static CSS metadata retained by reachable typed
declarations, writes the asset, then leaves executable lowering to typed IR.

The current implementation supports:

- scalar `bool`, signed integer, unsigned integer, `char`, floating, and
  `unit` representations;
- top-level typed functions, direct calls, function values, and indirect calls;
- locals and explicit IR load, move, store, clone, drop, and discard;
- CFG blocks, branches, loops, expression-valued `if`/`switch`, returns, and
  traps;
- checked integer addition, subtraction, multiplication, and negation;
- floating arithmetic and scalar comparisons;
- copyable fixed arrays and structs whose members are themselves supported;
- aggregate construction in source evaluation order, aggregate parameters and
  returns, struct field reads/mutation, and fixed-array reads/mutation.
- plain enums and copyable discriminated unions, including generic user
  unions, `Option`, and `Result`;
- union construction, tag testing, payload extraction, exhaustive `switch`, and
  copyable `try` success/error propagation.
- narrow cleanup-managed aggregate support with typed destructor calls and
  deterministic cleanup on scope exit, return, loop exit, and `try`.
- immutable reference-counted `string`, `StringBuilder`, byte-exact literals,
  content equality, printing, retaining, and allocation-transferring finish;
- monomorphized `List<T>` allocation, growth, `Add`, `Insert`, `RemoveAt`,
  value-based `Remove`, linear `Contains`/`IndexOf`/`LastIndexOf`, `Clear`,
  `AddRange`, `InsertRange`, `RemoveRange`, `GetRange`, `Count`, `Capacity`,
  writable capacity, `EnsureCapacity`, `TrimExcess`, whole/range `Reverse`,
  callback-driven `Exists`, `FindAll`, `FindIndex`, `FindLastIndex`,
  `RemoveAll`, `ForEach`, and `TrueForAll`, checked read/write indexing,
  copyable-element cloning, consuming iteration, and non-consuming iteration;
- monomorphized `Dictionary<TKey, TValue>` construction, `Add`, `Count`,
  `TryAdd`, `ContainsKey`, `ContainsValue`, `Remove`, `Clear`,
  `Capacity`, `EnsureCapacity`, both `TrimExcess` forms, checked indexer reads
  and assignments, open-addressed hash lookup, and deep key/value cloning;
- monomorphized `HashSet<T>` construction, Boolean `Add`, `Contains`, Boolean
  `Remove`, `Clear`, `Count`, capacity controls, open-addressed lookup, and
  independent value copying;
- monomorphized `Queue<T>` construction, FIFO `Enqueue`, `Dequeue`, `Peek`,
  `Clear`, `Count`, capacity controls, circular-buffer growth, and independent
  value copying;
- monomorphized `Stack<T>` construction, LIFO `Push`, `Pop`, `Peek`, `Clear`,
  `Count`, capacity controls, and independent value copying;
- zero-initialized `Buffer`, mutable byte slices, byte access, and borrowed
  slice iteration;
- typed `Url` and HTML element builders, escaped attributes/text, conditional
  `Option` attributes, HTML-presence booleans, numeric attributes, owned and
  borrowed children, optional/array/vector child collections, raw HTML, deep
  cloning, standard void-element finishing, and zero-copy render transfer;
- per-document static component-style deduplication across attached, detached,
  and cloned HTML, plus optional one-file hashed stylesheet extraction;
- destination-aware `$"..."` interpolation for HTML text and attributes:
  generated C formats scalar segments on the stack and escapes them directly
  into the active `Html` buffer, while value-position interpolation uses an
  explicit owned `StringBuilder`; scalar holes append from stack buffers
  without per-hole string allocations, and `Url.relative` can take ownership
  of the finished string allocation;
- scoped interpolation arguments for component `string` and `Option<string>`
  properties: generated C retains the single owned interpolation result across
  the direct call, passes only its borrowed view, then drops the owner;
- one-destination lowering for directly nested markup and `<>...</>`
  fragments: generated child builders use a bounded stack in the root `Html`
  storage, so nested tags and wrapper-free groups add neither a separate byte
  buffer nor a per-node heap allocation;
- internal destination-taking variants for components whose direct returned
  element or fragment root has no early-return escape, while ordinary calls
  and function values retain their existing ABI;
- complete manifest-target emission as a single C17 translation unit;
- typed bridging to Aster's registered file, directory, SQLite, and bounded
  HTTP natives, including `Result` conversion, request views, response-body
  consumption, and exactly-once server/request-handle cleanup;
- allocation-free HTTP route matching and path-parameter views;
- integrated generated-C execution of configuration parsing, buffered file
  input, validation, SQLite models, typed routing, and HTML rendering in the
  issue-tracker proof target;
- live generated-C issue-tracker coverage for database-backed GET/POST,
  redirects, route parameters, malformed requests, body limits, and bounded
  server shutdown.

Narrow integers are represented in generated C with `int64_t` or `uint64_t`.
Generated checked helpers enforce the Aster type's actual width before the C
operation occurs. This avoids relying on signed C overflow or integer-promotion
behavior. The emitter uses the IR target description for `nint` and `nuint`
width.

The typed IR owns resolved field type IDs and union payload type IDs. The C
backend therefore lays out its internal structs from canonical IR metadata; it
does not consult AST declarations or rerun type checking. Fixed arrays use a
generated wrapper struct so Aster arrays retain their length and never decay
to pointers. Interpreted and generated-C indexing currently traps when out of
bounds, including for `unsafe` indexing.

These generated aggregate layouts are an internal backend representation, not
Aster's stable C ABI. Field order follows declaration order, but padding and
alignment are selected by the host C compiler. `extern struct` interoperability
will require a separately validated ABI emission path.

Generated plain enums are declaration-order `uint32_t` values. Generated
discriminated unions are structs containing a `uint32_t` tag and an inline C
union with one field per payload-bearing alternative. Payloadless alternatives
contain only the tag. Union layout remains backend-internal; plain enums retain
their predictable integer representation.

Owning C locals have an adjacent live flag. A store transfers ownership into
the slot, a move clears the source flag, and an explicit IR drop invokes a
generated typed drop helper only while the flag is set. Drop helpers call the
type's verified Aster destructor and then recursively destroy owning fields in
reverse order. This is compiler-emitted ownership state, not reference
counting.

Runtime-backed values use explicit pointer-sized handles in generated C.
Ownership transfers remain visible in IR and owning locals retain live flags;
there is no generated reference counting. Strings, builders, vectors, URLs,
HTML, and element builders have typed cleanup paths. Generated clone helpers
recursively copy supported lists, arrays, structs, and union payloads.
Unsupported cleanup-managed clones are diagnosed instead of becoming shallow C
copies.

A standalone `Html` owns one growing byte buffer. Directly nested syntax and
proven component roots append to that same storage. If an `Html` genuinely
escapes into a variable, collection, ordinary call, or return path without a
render destination, it remains an independent owning value and is appended by
an explicit transfer at the eventual composition point.

The generated translation unit is compiled in the test suite as C17 with the
same aggressive warning set used by Aster. Tests execute scalar, checked
overflow, struct, array, aggregate call, mutation, indexing, generic enum,
`Option`/`Result`, `switch`, `try`, reverse destruction, early return, loop
exit, moved-value cleanup, owned text, vectors, borrowed collection iteration,
indirect calls, HTML/URL construction, buffers/slices, whole-project emission,
raw pointers and arenas, the documentation-server renderer, and the SQLite
issue-tracker renderer.

This is deliberately not yet a complete native backend. Important gaps include:

- arbitrary user-registered native signatures and native-handle aggregates;
- non-integer casts;
- object-file orchestration and application/library linking.

Unsupported IR produces a source diagnostic. The backend must grow by adding
correct runtime and ABI contracts, not by silently erasing ownership behavior.

Programs using registered natives link the emitted translation unit with
`langlib`, add `include/` to the C include path, and link the established
system libraries used by those natives. CTest exercises this contract with the
documentation server and SQLite issue tracker.
