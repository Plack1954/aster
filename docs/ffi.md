# C FFI

The public embedding API is registration based:

```c
bool lang_register_native(
    LangVM *vm,
    const char *name,
    LangNativeFn callback,
    size_t arity);
```

A callback receives a borrowed argument array and returns a tagged
`LangNativeResult`. Public values cover `Unit`, Boolean, signed and unsigned
integers, float, borrowed string views, mutable byte slices, opaque objects,
and raw pointers. The callback must not retain borrowed string or slice data
beyond the call.

Use `lang_native_result_error(message)` to copy a dynamic or stack-backed
failure diagnostic before returning. Read it with
`lang_native_result_error_message`, then release the failed result with
`lang_native_result_drop`. A callback may initialize `error` directly only
with a static-lifetime message such as a string literal; dropping such a result
is still safe.

Source declares registered functions with `extern Type name(...);`.
These calls lower to `CALL_NATIVE`; missing registration or a failed native
result becomes a runtime trap.

Move-only resources can be inspected without transfer:

```text
extern long native_server_poll(
    NativeHandle server
);
```

The callback receives the same value as a borrowed argument. It must not retain
the value, its object pointer, or data returned by `lang_native_handle_data`
after returning. The VM keeps the caller's owning local initialized. Arguments
without `in` or `ref` retain value semantics and are destroyed by the call
after transfer.

Embedders can invoke a registered callback through `lang_vm_call_native`; this
performs name and arity lookup and returns the callback's typed result. Duplicate
registrations are rejected.

Opaque resources are created with `lang_native_handle_value`, inspected in a
callback with `lang_native_handle_data`, and released deterministically through
their registered C destructor. `NativeHandle` is cleanup-managed and cannot be
cloned. `lang_value_drop` releases an embedding-owned value.

`BufferAsMutSlice(buffer)` is an unsafe, call-scoped bridge from a mutable
cleanup-managed `Buffer` to `Span<byte>`. C callbacks inspect it with
`lang_value_byte_slice`. The slice does not own or extend the buffer lifetime.
The interpreter deliberately does not add a reference count or lifetime check
to buffers and raw slices. The reference count used by immutable `string` does
not extend these resource lifetimes.

Native callbacks can construct typed tagged results without accessing VM
internals:

```c
LangValue result;
lang_result_ok_value(vm, payload, &result);
lang_result_err_value(vm, error, &result);
```

On success these constructors take ownership of the payload. Returning that
object from a callback declared as `Result<T, E>` lets ordinary `switch` and
`try` consume it.

`std.file` demonstrates the complete boundary. `NativeFileOpen` returns
`Result<File, IoError>`, where `File` is a cleanup-managed opaque handle whose C
destructor closes the stream. Borrowed read/write calls return typed Results,
and `NativeFileReadAll` creates a `string` through
`lang_string_value`. A file is closed on normal scope exit, early return,
exception propagation, or VM trap. Application code can use the C#-named
throwing facade directly:

```aster
string text = File.ReadAllText(path);
File.WriteAllText(outputPath, text);
List<string> lines = File.ReadAllLines(path);
```

The `NativeFile*` and buffered `Result` APIs remain available for callers that
want to branch on I/O failure as data.
The byte-streaming calls borrow both the file handle and a `Span<byte>` derived
from a live mutable `Buffer`. Reads return the number of initialized bytes;
zero means EOF for a non-empty slice. Writes validate the requested prefix
against slice length, handle partial host writes internally, and flush before
reporting success. Neither operation retains the slice pointer.
`std.bytes` uses the same borrowed slice boundary for length, indexed byte
inspection, and explicit range copying. Range copying creates `string`
storage before returning and does not extend the source slice lifetime.

`std.process` copies selected `argv` and environment strings into immutable
language `string` values. The VM borrows the host argument array only for one
execution and native callbacks never expose those pointers directly. Argument
indexes and environment lookup failures are returned as `Result` errors.
CLI classification and option splitting are implemented in Aster; there is
no native parser registry.

Decimal integer formatting uses two narrow native conversions because the
current builder appends string views rather than individual bytes. Both return
`string` values. Prefix, suffix, containment, and byte search remain
Aster library code.

`std.filesystem` similarly keeps host path operations behind registered
primitives. Paths are copied into bounded, null-terminated adapter buffers;
embedded null bytes are rejected. Queries distinguish a missing path from
other host errors. Removal is split between files and empty directories, so
the API does not hide recursive deletion.

Dynamic library loading and automatic C ABI binding are not implemented.

The optional SQLite adapter follows the same registration model. Database and
prepared-statement pointers are wrapped in tagged cleanup-managed native handles;
the adapter validates the tag before every call. SQLite error messages and
column text are copied into Aster `string` values. See
`docs/sqlite.md`.

The HTTP experiment uses this exact boundary: listening sockets and accepted
requests are opaque native handles. Server operations borrow the listening
handle; request accessors borrow the accepted request; response emission
consumes it. Both kinds close their socket from the handle's C destructor when
still live.
