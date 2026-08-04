# C Interpreter Performance Research

This note compares Aster with bytecode interpreters implemented primarily in
C. It identifies techniques worth testing; it does not claim benchmark
equivalence between languages with different semantics.

## Current Aster evidence

Aster's typed-IR VM uses register-like local slots but still stores every
runtime value in the general `LangValue` representation. Its hot bytecode
record is 12 bytes, with exact source spans in a separate cold array.

The twenty-million-iteration workloads currently measure:

- 427.1 ms for an inline integer loop;
- 776.0 ms when each iteration calls an Aster function.

The inline disassembly still contains separate `CONSTANT_LOCAL` instructions
before arithmetic. Function calls recursively enter `execute_function()`.
These are now more important than allocation or branch-prediction misses.

## Lua 5.4

Lua is a register VM. Each instruction is a packed 32-bit word, and the
instruction set contains register/register, register/constant, and
register/immediate variants. Integer loop preparation and stepping also have
dedicated instructions. Its VM keeps `pc`, register base, constants, and trap
state in C locals. On GCC-compatible compilers it enables jump-table dispatch,
with a portable switch fallback.

Sources:

- [Lua 5.4 VM](https://www.lua.org/source/5.4/lvm.c.html)
- [Lua 5.4 opcode format](https://www.lua.org/source/5.4/lopcodes.h.html)
- [Lua 5.4 manual](https://www.lua.org/manual/5.4/manual.html)

Relevant Aster lessons:

1. Add typed local/immediate arithmetic forms so a constant operand does not
   require its own dispatch.
2. Keep common operands in the instruction itself.
3. Eventually test a compact packed instruction encoding, but only after
   operand forms stabilize.
4. Computed-goto dispatch can be an optional GCC/Clang fast path while the
   switch interpreter remains the portable definition.

## Wren

Wren uses a byte-oriented stack VM. It hoists the current frame, stack base,
instruction pointer, and function into C locals, writing them back only around
frame changes and runtime errors. Calls push VM frames and continue within the
same interpreter function rather than recursively invoking a new C evaluator.
It provides computed-goto and switch dispatch implementations. Common local
loads and calls have small-operand opcode families such as `LOAD_LOCAL_0` and
`CALL_0`.

Source:

- [Wren interpreter loop](https://github.com/wren-lang/wren/blob/main/src/vm/wren_vm.c)

Relevant Aster lessons:

1. Replace recursive Aster-to-Aster C calls with frame push/pop inside one
   dispatch loop.
2. Keep the current function, locals base, initialized-state base, and
   instruction position in C locals; synchronize only at calls, returns, and
   traps.
3. Keep direct-call forms specialized by common arity.

This is the clearest route to reducing the large difference between Aster's
inline and call-heavy benchmark.

## mruby

mruby is a register VM. A call frame points into a shared VM value stack;
arguments, locals, and temporaries occupy registers in that frame. Ruby
methods push a VM frame, update the active instruction sequence and stack
base, and continue in the main VM loop. C methods are called directly. The
default GCC/Clang build uses computed goto and retains switch dispatch for
other compilers.

Source:

- [mruby VM documentation](https://mruby.org/docs/api/file.vm.html)

Relevant Aster lessons:

1. A shared register stack and iterative frame loop are established,
   maintainable designs, not unusual interpreter tricks.
2. Direct native calls and Aster calls should have separate fast paths.
3. Frame metadata should be compact and independent of the full local value
   storage.

## CPython

CPython 3.11 introduced adaptive specialization: frequently executed generic
instructions rewrite themselves into narrow forms, with inline caches and
superinstructions. The design reports specialization speedups in the 10–60%
range, with calls and lookups among the major contributors. CPython 3.14 also
offers an opt-in tail-call interpreter generated as small C opcode functions;
reported gains are about 3–5% with a suitable Clang/PGO build.

Sources:

- [PEP 659: specializing adaptive interpreter](https://peps.python.org/pep-0659/)
- [CPython 3.14 tail-call interpreter](https://github.com/python/cpython/blob/main/Doc/whatsnew/3.14.rst)

Aster already knows static operand types, so runtime type speculation would
mostly duplicate its checker. The useful part is the specialization
principle: produce narrow opcode families and superinstructions from typed IR.
Inline caches become relevant later for dynamic module symbols, indirect
calls, or component/property lookup—not for current integer arithmetic.

## Recommended Aster sequence

### 1. Local/immediate typed operations

Add general forms equivalent to:

```text
ADD_LOCAL_CONSTANT_TO
SUB_LOCAL_CONSTANT_TO
COMPARE_LOCAL_CONSTANT_BRANCH
```

The comparison form already exists and performs well. Arithmetic should use
the same approach. This removes a constant-load dispatch and a temporary slot
from common increments, bounds calculations, and arithmetic with literals.

### 2. Prepare for iterative call frames

An initial direct conversion to iterative Aster-to-Aster calls passed
semantic tests but regressed both performance workloads. Aster's current
large switch requires more mutable active-frame state than the compiler keeps
in registers effectively. Before retrying, split cold opcode families out of
the hot dispatch path or redesign frame/value storage. Preserve the current
recursive implementation until a replacement wins differential tests and
benchmarks covering:

- ordinary and indirect calls;
- recursion-depth traps;
- deterministic cleanup;
- `try` propagation;
- native calls;
- stack traces.

This should be implemented once, not as a second permanent interpreter.

### 3. Statically typed scalar slot storage

Aster currently copies a 24-byte tagged `LangValue` for scalar locals even
though bytecode already knows their types. A future slot layout can store
scalar bits directly and reserve general `LangValue` storage for objects,
strings, raw pointers, and polymorphic boundaries.

This is higher risk than immediate operations or iterative frames. It affects
FFI, aggregates, ownership cleanup, diagnostics, and every opcode. It should
follow a slot-layout design and representative benchmarks, rather than another
whole-VM rewrite.

The concrete representation, metadata, conversion boundaries, ownership
invariants, and migration sequence are specified in
[typed-vm-slots.md](typed-vm-slots.md).

### 4. Optional computed-goto dispatch

Computed goto is now available behind `LANG_VM_COMPUTED_GOTO`. Clang 18
improves both measured workloads by 8–13%, while GCC 13 regresses both.
Switch dispatch remains mandatory, tested, and the default. The extension is
therefore an explicit per-toolchain choice rather than an assumed universal
optimization.

### 5. Packed bytecode

Lua's 32-bit format is attractive, but Aster currently needs several
ten-bit local indices plus operation/type data. A 64-bit prototype used an
8-bit opcode, two signed 28-bit operands, and a canonical-instruction escape
for uncommon wide operands. It passed the full normal suite but regressed all
compiler/dispatch combinations measured: GCC switch, Clang switch, and Clang
computed goto on both scalar-loop and call-heavy workloads. Per-dispatch
unpacking outweighed the cache-density benefit, so the prototype was removed.

Reconsider packing only after the opcode set settles. A future experiment must
retain source spans out of line and have handlers consume packed fields
directly instead of reconstructing the current C instruction record.

## Techniques not recommended now

- A tracing JIT or adaptive type speculation: generated C supplies the native
  path, and Aster has static types.
- A second permanently maintained interpreter.
- Hand-written assembly.
- Large basic-block superinstructions that duplicate an optimizer.
- Removing runtime checks required by Aster semantics merely to improve a
  benchmark.
- A wholesale typed-slot rewrite before calls and immediate operands are
  fixed.

The practical model is: use typed IR to create fewer, narrower operations;
execute them in one iterative register-style VM loop; then consider a more
compact scalar storage representation.
