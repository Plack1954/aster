# Values and cleanup

## Empty pointer/length values

String views, byte slices, buffers, and other pointer/length views may use a
null data pointer when their length is zero. A null data pointer with a nonzero
length is invalid. Runtime, FFI, VM, and generated-C operations must
short-circuit zero-length library calls and must not perform pointer arithmetic
on a null data pointer. Empty values therefore require no allocation while
remaining valid in both backends.

Aster does not use garbage collection and does not have a borrow checker.
Its model is deliberately C/C++-like:

- ordinary parameters and assignments copy values;
- `ref T` is an explicit mutable reference;
- `T*` and `const T*` are raw pointers;
- locals and fields with destructors are destroyed at scope exit;
- unsafe pointer mistakes remain the programmer's responsibility.

There is no `take`, `borrow`, `owned`, source invalidation, use-after-move
diagnostic, or lifetime proof in Aster source.

### Relationship to C and C++

C copies a struct by copying its stored members/representation. It has no
constructors, destructors, deleted copy operation, or type-defined copy hook.
A C resource-owning struct therefore normally uses explicit conventions and
functions such as `resource_init`, `resource_clone`, and `resource_destroy`;
an accidental plain copy of an owning pointer can alias the allocation and
lead to a double free. The C compiler does not prevent that programmer error.

Aster deliberately keeps that C/C++ trust boundary for pointer and alias
validity, while taking deterministic destruction and type-defined/deleted copy
operations from C++. This design is not derived from Rust and does not add
borrow checking or lifetime annotations.

User copy constructors propagate through enclosing structs, instantiated
generic structs, and fixed-size arrays. This propagation is based on the copy
policy of each field or element, independently of whether that type needs
destruction. Dynamic collections and tagged unions containing custom-copy
values are still rejected until their runtime copy dispatch is implemented.

Scalars copy directly. Immutable UTF-8 `string` values share reference-counted
storage. `Buffer` and other ordinary owning containers deep-copy their storage.
`NativeHandle` copies share one deterministically destroyed native resource,
similar to a C++ shared resource handle. Structs, arrays, enums, `Option`, and
`Result` copy their fields or payloads recursively.

These rules describe observable copies, not mandatory temporary work. Fresh
construction is lowered directly into its receiving value where possible, and
returning a managed local transfers that local's storage into the return slot.
Consequently a returned `Buffer`, `List`, `Html`, or aggregate does not incur a
deep copy merely because it crossed a function boundary.

`Arena` is deliberately noncopyable. Pass one with `ref Arena`, pass a pointer,
or construct it where it will live. This is the same kind of restriction as a
deleted C++ copy constructor; it is not lifetime analysis.

Fresh values can be constructed directly in their destination:

```text
articles.Add(new()
{
    Title = "Aster"
});
```

There is no separate copying API. Assignment, by-value calls, returns, field
reads, indexing, and `List.Add(value)` use the normal copy behavior of the
value's type.

User-defined destructors use:

```text
~Resource()
{
    Print("dropping");
}
```

The compiler runs destructors in reverse declaration order on normal scope
exit, return, `break`, `continue`, propagated errors, and VM trap unwinding.
Copying a value also creates another value that will later be destroyed. As in
C++, a destructor does not by itself disable copying: recursively copyable
fields receive their normal copies. A resource-owning value can instead define
`public T(const ref T source) { ... }` to perform custom duplication or declare
`private T(const ref T source) = delete;` to prohibit copying. `const ref` does
not extend the source lifetime or introduce lifetime analysis; it only gives
the constructor an immutable non-owning source during the call.

Raw pointers are intentionally outside any safety guarantee. An
`unsafe { ... }` block acknowledges programmer responsibility for validity,
alignment, lifetime, aliasing, bounds, nullability, and thread safety.
