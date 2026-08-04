# Aster TODO

## Async/await

The C#-shaped front end, typed IR, VM, and generated-C backend implement
`async`, `await`, `Task`, task combinators, timers, exceptions, deterministic
cleanup, and cooperative `CancellationToken` behavior. Continue
`docs/async.md`:

- integrate libcurl-multi-backed `HttpClient` operations with the executor;
- make asynchronous I/O cancellation-aware;
- add cancellation registrations and linked-token sources when real code needs
  them;
- accept `Task<Response>` handlers in Lime.

## Remaining bounded-overload extension

Bounded C#-style overloads are implemented for free functions, static and
instance methods, function values, `ref`, and generic overloads separated by
arity. Calls resolve by arity and exact argument types; duplicate signatures
and ambiguous calls are diagnosed. The selected declaration is preserved
through the VM, typed IR, and generated C. Lime and Nook use overloaded `Get`.

- Add trial inference when multiple generic templates share both a name and
  an arity. Do not add conversion ranking or generic-preference rules.
