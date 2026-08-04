# Asynchronous programming

Status: initial end-to-end implementation in the generated-C and VM backends.

Aster follows C#'s public async vocabulary and source model. Public names are
not to be replaced with Aster-specific synonyms without an explicit design
reason and review.

```aster
async Task<User> GetUserAsync(HttpClient client, int id)
{
    HttpResponseMessage response = await client.GetAsync($"/users/{id}");
    response.EnsureSuccessStatusCode();
    return await response.Content.ReadFromJsonAsync<User>();
}
```

The accepted public surface begins with:

- `async` and `await`;
- `Task` and `Task<T>`;
- the `Async` method suffix;
- `CancellationToken`;
- `Task.Delay`, `Task.WhenAll`, and `Task.WhenAny`;
- exception capture in a faulted task and rethrow at `await`;
- no `async void`.

`await` is a suspension point, not a thread creation operation. An incomplete
task saves the async function's continuation and returns control to its
executor. A completed task continues immediately. CPU-bound work does not
become asynchronous merely because it is called by an async function.

## Aster runtime constraints

Aster copies the C# source experience, not the CLR implementation:

- the compiler lowers async functions to typed-IR state machines;
- generated C and the VM must have identical observable behavior;
- the initial server executor is a single-threaded event loop;
- task frames retain locals that remain live across suspension;
- normal deterministic cleanup runs on success, exception, and cancellation;
- exceptions never cross a C ABI boundary;
- blocking native work must use a separately identified blocking-work path;
- async does not imply garbage collection.

The task representation may use shared runtime state because a task is an
observable completion handle. Allocation and reference-count costs must be
measured, and the compiler may elide storage when completion is synchronous or
the task does not escape. Those optimizations may not change source semantics.

There is no initial synchronization-context equivalent. Server continuations
resume on the owning Aster executor. A later browser executor may resume on
the browser's DOM thread without changing the task API.

## Exceptions and cancellation

An exception thrown by an async function faults its returned task. Awaiting the
task rethrows that exception. `finally` and deterministic cleanup execute before
the task reaches its terminal state.

Cancellation is cooperative and uses `CancellationToken`. A
`CancellationTokenSource` created with target-typed `new()` shares state with
its `.Token`; `.Cancel()` requests cancellation, and
`.IsCancellationRequested` observes it. `CancellationToken.None` is the
non-cancelable token. `ThrowIfCancellationRequested()` throws
`OperationCanceledException`.

`Task.Delay(int, CancellationToken)` observes cancellation both when it starts
and while its timer is pending. A canceled delay enters the canceled task state
and throws `TaskCanceledException` when awaited; as in .NET, that exception is
caught by an `OperationCanceledException` handler. Cancellation is not
permission to destroy a running frame at an arbitrary instruction.

## Implementation stages

1. Front-end syntax and type checking for `async Task<T>` and `await`.
2. Typed-IR async-function and suspension-point representation.
3. VM task frames, executor, completion, exception, and cancellation behavior.
4. Equivalent generated-C runtime and cleanup paths.
5. Timers and `Task.Delay`.
6. Libcurl-multi-backed `HttpClient` asynchronous operations.
7. Lime handlers returning `Task<Response>`.
8. Streaming and a controlled worker pool for genuinely blocking operations.

Stages 1, 2, 4, and the non-cancellation parts of stage 3 are implemented.
Stage 5 is implemented for `Task.Delay(int)`. Typed IR records the public task
return type, coroutine completion type, and explicit `await` instructions.
Generated C lowers async functions to state machines. The VM stores suspended
locals, operand stack, and instruction pointer in heap-backed frames. Both
executors support multiple pending timer tasks, continuation registration,
immediate completion, fault capture/rethrow at `await`, and deterministic
cleanup of a suspended frame.

`Task.FromResult` and `Task.CompletedTask()` are ordinary standard-library
async functions. The latter is temporarily callable because Aster does not
yet have C#-style static properties; its final intended surface is the .NET
`Task.CompletedTask` property.

Generated-C tasks now carry scalar and cleanup-managed completion values,
including strings, structs, lists, HTML, and response-shaped aggregates. A task
retains one owned result, clones it for each successful `await`, and invokes a
generated type-specific destructor when its final reference is released. VM
tasks carry the corresponding normal `LangValue` results. Remaining 0.1 work
includes executor integration for real asynchronous I/O, cancellation-aware
I/O operations, registrations, and linked-token sources.

`Task.WhenAll` and `Task.WhenAny` accept `List<Task<T>>`; `WhenAll` also accepts
`List<Task>`. `WhenAll` waits for every participating task, preserves input
order in its result list, and then propagates the first fault in input order.
`WhenAny` completes with the first terminal task and does not cancel or consume
the remaining operations. An empty `WhenAll` succeeds immediately; an empty
`WhenAny` produces an `ArgumentException` when awaited. Aster does not yet
have .NET's `AggregateException`, so multiple `WhenAll` faults are retained by
their original tasks but only the first is propagated by the combined task.

`await` is not implemented as a hidden blocking call: only the outer executor
waits when it has no runnable continuations.

Signals in Lime Browser are a separate, undecided reactive-state feature. They
are neither required nor prohibited by Aster async.
