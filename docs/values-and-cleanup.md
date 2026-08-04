# Values and cleanup

Aster does not use garbage collection and does not have a borrow checker.
Its model is deliberately C/C++-like:

- ordinary parameters and assignments copy values;
- `ref T` is an explicit mutable reference;
- `T*` and `const T*` are raw pointers;
- locals and fields with destructors are destroyed at scope exit;
- unsafe pointer mistakes remain the programmer's responsibility.

There is no `take`, `borrow`, `owned`, source invalidation, use-after-move
diagnostic, or lifetime proof in Aster source.

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
Copying a value also copies its destructor-bearing value, just as a normal C++
copy creates another object that will later be destroyed. Aster does not try
to prove that a user-written destructor is compatible with the default copy.

Raw pointers are intentionally outside any safety guarantee. An
`unsafe { ... }` block acknowledges programmer responsibility for validity,
alignment, lifetime, aliasing, bounds, nullability, and thread safety.
