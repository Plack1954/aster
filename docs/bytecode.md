# Bytecode

Aster 0.1 uses a typed stack bytecode with register-like local
superinstructions. Each decoded instruction is a 12-byte C record containing
an opcode and two signed 32-bit operands. Exact source spans occupy a parallel
cold array and are fetched only for calls, cleanup, and diagnostics. Modules
own a constant pool and function table. This is intentionally simple; a
serialized byte format has not been frozen.

Instruction families cover constants, local copy/move/drop, checked typed
arithmetic, comparisons, jumps, direct/indirect/native calls, returns, arrays,
structs, iteration, cloning, result propagation, unchecked exception state,
HTML builders, and traps.
Enum matching uses borrowed `GET_TAG` inspection followed by ownership-aware
`TAKE_PAYLOAD` extraction on the selected arm.

`MOVE_LOCAL` transfers a runtime value and marks its slot unavailable.
`DROP_LOCAL` destroys it if still initialized. `RETURN` and trap unwinding
clean remaining live object slots. Heap aggregate values carry their resolved
language-destructor identity, allowing recursive cleanup of nested values and
temporaries. A destructor body executes before structural field cleanup.
`CLONE` records whether its input is borrowed from a local or is an owned
temporary. A borrowed source remains available; an owned source is destroyed
after the clone is constructed.
`TRY` extracts `Result.Ok` or performs an early `Result.Err` return with
cleanup. `CALL_NATIVE` resolves an `extern fn`
through the VM registry. Its encoded argument metadata includes a borrowed
mask, so the VM destroys owned arguments but preserves borrowed ones on both
success and failure. `HTML_BEGIN`, `HTML_ATTR`, `HTML_APPEND`,
and `HTML_END` are typed builder operations, not string-template evaluation.
`HTML_APPEND` recursively consumes optional, fixed-array, and vector child
collections in source order without shared ownership.
Segmented interpolation uses `HTML_ATTR_BEGIN_LOCAL`,
`HTML_ATTR_APPEND_LOCAL`, and `HTML_ATTR_END_LOCAL` for attributes, plus
`HTML_APPEND_FORMATTED_LOCAL` for text. These operations format directly into
the live builder and select attribute or text escaping without constructing a
temporary string.
Direct-local field/index instructions borrow the owning aggregate and copy or
replace only the selected slot. This avoids duplicating a cleanup-managed parent or
an embedded native handle. Index instructions retain an unsafe-intent flag from
their typed AST node; the development VM validates bounds for both flag values.

`FUNCTION` pushes a validated function-table index. `CALL_INDIRECT` consumes
that copyable value and its arguments, then creates an ordinary VM frame.
Static checking verifies the complete `ReturnType(ParameterTypes)` signature. Bytecode
verification checks stack effects and function constants, while dispatch also
checks target range, arity, and recursion depth.

Use `lang dump-bytecode file.as` for a stable human-readable disassembly.
Before dispatch, the VM validates opcode ranges, constant/function/local
indexes, aggregate counts, jump targets, operand-stack underflow/overflow,
reachable fallthrough, and consistent stack depth at control-flow joins.
Malformed compiler-produced bytecode is reported as an internal runtime error
rather than executed. Guest traps carry source position and a function stack
trace.

The typed-IR adapter keeps the operand stack empty at CFG boundaries by
materializing virtual values in temporary VM locals. During emission it
removes strictly local artifacts that cannot affect ownership or control flow:

- an immediately stored and moved temporary;
- a move back into the same local followed by discarded `unit`;
- a produced `unit` immediately discarded;
- a numeric result moved once into its final local;
- a small signed integer constant used as one arithmetic operand;
- jumps whose target is the next laid-out basic block.

These rewrites never cross a CFG block boundary. Branch targets therefore
remain stack-balanced, and owning values are never duplicated or silently
dropped.
