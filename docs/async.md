# Asynchronous programming

Status: initial end-to-end implementation in the generated-C and VM backends.

Aster follows C#'s public async vocabulary and source model. Public names are
not to be replaced with Aster-specific synonyms without an explicit design
reason and review.

```aster
private async Task<User> GetUserAsync(HttpClient client, int id)
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

There is no synchronization-context equivalent. Server continuations resume
on the owning Aster executor. The initial browser executor resumes generated-C
Wasm continuations on the browser's DOM thread: JavaScript polls the exported
Task boundary, supplies the monotonic-enough wall-clock value used by
`Task.Delay`, and applies the typed result after completion. Fetch and other
browser-hosted asynchronous operations remain later work and do not change the
public task API.

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
6. Libcurl-backed `HttpClient`: synchronous easy-handle calls and asynchronous
   multi-handle transfers driven by executor timers are implemented.
7. Lime handlers returning `Task<Response>` are implemented through the same
   `Map*` APIs and execute through `DispatchAsync`.
8. Streaming and a controlled worker pool for genuinely blocking operations.

Stages 1 through 7 are implemented. The optional native
`System.Net.Http` component now provides synchronous GET/POST/general Send and
asynchronous `SendAsync`/`GetAsync`,
byte request and owned response bodies, headers, redirects, timeouts, response
limits, cancellation, shared libcurl-multi connection reuse, and deterministic
handle cleanup in both backends. `GetStreamAsync` returns after final response
headers and feeds caller-provided spans through a bounded 64-KiB queue with
libcurl pause/resume backpressure. Fixed-length `HttpUploadStream` requests
use the corresponding bounded producer queue and reject early completion.
Pending multi transfers are advanced by short
nonblocking executor-timer polls; native socket-readiness registration can
later replace that bounded bridge without changing the public API. The broader
blocking-I/O strategy remains pending.
Typed IR records the
public task
return type, coroutine completion type, and explicit `await` instructions.
Generated C lowers async functions to state machines. The VM stores suspended
locals, operand stack, and instruction pointer in heap-backed frames. Both
executors support multiple pending timer tasks, continuation registration,
immediate completion, fault capture/rethrow at `await`, and deterministic
cleanup of a suspended frame. Buffered async HTTP copies the request body into
native request storage before suspension, so a borrowed caller span never
escapes. Streaming upload writes likewise copy only into bounded native
storage during the call; unknown-length/chunked uploads remain future work.

`Task.FromResult` and `Task.CompletedTask()` are ordinary standard-library
async functions. The language now supports C#-style static properties, so the
latter can be migrated to the intended .NET-shaped `Task.CompletedTask`
property without another language feature.

Generated-C tasks now carry scalar and cleanup-managed completion values,
including strings, structs, lists, HTML, and response-shaped aggregates. A task
retains one owned result, clones it for each successful `await`, and invokes a
generated type-specific destructor when its final reference is released. VM
tasks carry the corresponding normal `LangValue` results. Remaining 0.1 work
includes native socket-readiness registration, cancellation registrations,
linked-token sources, and async stream I/O.

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
