# Local baseline: 2026-08-01

These are local observations, not general Aster or PHP performance claims.
They exist to preserve the first benchmark baseline and identify useful work.

Environment:

- Intel Core i7-13700F, Linux x86-64;
- GCC 13.3, generated C compiled with `-O2 -DNDEBUG`;
- PHP 8.3.6 CLI, JIT disabled and CLI OPcache disabled;
- five measured CLI runs after two warmups;
- three HTTP runs of 1,000 requests after one warmup;
- HTTP concurrency one, one request per connection.

Median whole-process wall times:

| Workload | Aster C | PHP | Aster VM | Observation |
| --- | ---: | ---: | ---: | --- |
| Logic, 20M iterations | 16.26 ms | 113.64 ms | 508.96 ms | Aster C 7.0x faster than PHP |
| Functions, 10M calls | 7.52 ms | 153.92 ms | 481.28 ms | Aster C 20.5x faster than PHP |
| Owned strings, 300k | 32.77 ms | 36.89 ms | 162.98 ms | Aster C 1.13x faster than PHP |
| Native HTML, 200k | 83.25 ms | 75.76 ms | 175.14 ms | PHP 1.10x faster than Aster C |

HTTP client wall time for 1,000 requests:

| Server | Median | Relative result |
| --- | ---: | --- |
| Aster generated C | 37.37 ms | 1.57x faster |
| PHP built-in server | 58.71 ms | baseline |

The function result reflects optimized generated C, including normal C
inlining, against PHP's runtime calls. The HTTP result compares two local
synchronous development-scale stacks and says nothing about PHP-FPM or a
production reverse proxy.

The important negative result is HTML. Aster has the more integrated source
model and writes interpolated segments directly, but its current element and
owned-string runtime did not beat PHP's native template syntax plus output
buffering. String construction was also close. Improving Aster here requires
profiling allocation, growth, escaping, and final ownership transfer; the
logic results do not predict application rendering performance.

## Generated-C runtime optimization

That negative result was profiled rather than accepted. Heaptrack attributed
seven allocations to each small rendered component: separate owner and storage
objects, a 32-frame child arena, three buffer growths, and a final `String`
wrapper. Escaping also appended ordinary text one byte at a time, and integer
interpolation used `snprintf`.

The generated-C runtime now:

- allocates the HTML owner, storage, and bounded child arena together;
- transfers the owner allocation directly into the rendered `String`;
- starts the HTML buffer at 128 bytes, while preserving geometric growth;
- copies unescaped runs in bulk;
- formats signed and unsigned integers without `snprintf`.

Heaptrack consequently reports 400,003 allocations for the 200,000-render
workload, down from 1,400,009: two allocations per component instead of seven.

A fresh full run on the same machine used ten CLI samples and five HTTP samples
of 2,000 requests. Median whole-process wall times were:

| Workload | Aster C | PHP | Aster VM | Observation |
| --- | ---: | ---: | ---: | --- |
| Logic, 20M iterations | 19.14 ms | 114.43 ms | 431.67 ms | Aster C 6.0x faster than PHP |
| Functions, 10M calls | 8.61 ms | 168.10 ms | 415.25 ms | Aster C 19.5x faster than PHP |
| Owned strings, 300k | 19.96 ms | 37.10 ms | 158.31 ms | Aster C 1.86x faster than PHP |
| Native HTML, 200k | 52.06 ms | 80.00 ms | 161.84 ms | Aster C 1.54x faster than PHP |

For the same 2,000-request HTTP run, Aster generated C took 68.40 ms versus
96.24 ms for PHP's built-in server, making Aster 1.41x faster in this narrow
development-server comparison.

The HTML result moved from 83.25 ms and slower than PHP to 52.06 ms and faster
than PHP. This is the kind of result the benchmark exists to force: integrated
HTML must compile to a lean construction path, not merely offer nicer syntax.

## Inlined append path and compile-time escaping

A second profile found roughly eight million calls to the small HTML byte-append
helper in the 200,000-render workload. Those calls accounted for about one
third of retired instructions. The runtime also maintained a trailing NUL byte
after every append even though Aster strings are length-delimited, and cleared
all 32 child frames before knowing how many a render would use.

The generated C now inlines HTML reserve and append operations, maintains only
the documented pointer-and-length representation, initializes child frames on
demand, and pre-escapes constant text and attribute segments during C emission.
Dynamic values still use the same context-sensitive runtime escaping.

On the same machine, a ten-sample run produced these medians:

| Workload | Aster C | PHP | Aster VM | Observation |
| --- | ---: | ---: | ---: | --- |
| Logic, 20M iterations | 15.44 ms | 114.04 ms | 417.78 ms | Aster C 7.4x faster than PHP |
| Functions, 10M calls | 7.81 ms | 154.63 ms | 409.69 ms | Aster C 19.8x faster than PHP |
| Owned strings, 300k | 21.42 ms | 41.88 ms | 164.67 ms | Aster C 1.96x faster than PHP |
| Native HTML, 200k | 28.11 ms | 75.11 ms | 190.80 ms | Aster C 2.67x faster than PHP |

The 2,000-request HTTP medians in this run were 95.62 ms for Aster generated C
and 126.40 ms for PHP's built-in server, a 1.32x Aster lead. Absolute HTTP
times varied between runs because this single-connection benchmark is dominated
by loopback, process scheduling, and socket work; the HTML microbenchmark is the
appropriate measurement of the compiler/runtime change.

Inlining increased the benchmark executable's text section by roughly 9 KB.
That trade is currently justified for the primary C backend, but larger Aster
applications should be watched for pathological generated-code growth.

## Owned-string allocation transfer

Heaptrack then found four allocations for every interpolated owned string: the
builder object, its initial 32-byte buffer, a growth to 64 bytes for ordinary
application records, and a separate final `String` wrapper. The builder append
function also remained out of line at each interpolation segment.

The generated-C runtime now allocates a union that can hold either the active
builder or its finished `String`. Finishing switches the active representation
in place and transfers the data buffer without allocating a wrapper. The first
buffer is 64 bytes and append is inlined. This keeps ownership explicit while
reducing the 300,000-record workload from about 1.2 million allocations to
600,002: two allocations per result instead of four.

The next ten-sample run produced these medians:

| Workload | Aster C | PHP | Aster VM | Observation |
| --- | ---: | ---: | ---: | --- |
| Logic, 20M iterations | 15.37 ms | 110.52 ms | 404.54 ms | Aster C 7.2x faster than PHP |
| Functions, 10M calls | 8.96 ms | 154.19 ms | 388.64 ms | Aster C 17.2x faster than PHP |
| Owned strings, 300k | 9.61 ms | 36.71 ms | 155.96 ms | Aster C 3.82x faster than PHP |
| Native HTML, 200k | 26.22 ms | 71.44 ms | 167.07 ms | Aster C 2.72x faster than PHP |

The 2,000-request HTTP medians were 69.74 ms for Aster and 95.35 ms for PHP's
built-in server, a 1.37x Aster lead. The generated strings benchmark text
section grew by less than 1 KB from append inlining.

## Vectored HTTP responses

HTTP profiling showed that ordinary HTML responses used one send syscall for
headers and another for the body. A proposed request/header-buffer allocation
consolidation was also measured, but discarded after an interleaved comparison
made it about 7% slower despite reducing allocation count.

The retained change uses a two-segment `sendmsg` operation for headers and body,
with explicit handling for interruption and partial writes. HEAD and empty-body
responses retain the single-buffer path. In a concurrent old-versus-new
comparison of 5,000 requests, the vectored response path was about 15% faster.

The subsequent canonical run produced these medians:

| Workload | Aster C | PHP | Aster VM | Observation |
| --- | ---: | ---: | ---: | --- |
| Logic, 20M iterations | 17.78 ms | 111.70 ms | 542.91 ms | Aster C 6.3x faster than PHP |
| Functions, 10M calls | 10.80 ms | 164.56 ms | 532.58 ms | Aster C 15.2x faster than PHP |
| Owned strings, 300k | 10.04 ms | 43.12 ms | 185.23 ms | Aster C 4.29x faster than PHP |
| Native HTML, 200k | 30.39 ms | 79.69 ms | 195.83 ms | Aster C 2.62x faster than PHP |

The 2,000-request HTTP median was 71.33 ms for Aster versus 114.10 ms for
PHP's built-in server, a 1.60x Aster lead. This optimization changes actual
response transport rather than the benchmark client or connection policy.

## VM dispatch reduction

Once generated C led every workload, profiling moved to the second-priority VM.
The typed-IR bytecode backend already emitted some fused operations, but integer
expressions still copied operands through temporary locals and chained small
constants through load/move sequences. Bytecode formation now emits direct
local operations and immediate operations for those cases. Numeric functions
also skip the return-time owned-object scan when their IR types prove that no
local can contain an object.

GCC's `-O3` was measurably slower than `-O2` for the very large interpreter
loop, so Release builds compile `vm.c` at `-O2` without changing generated-C
optimization. The optional computed-goto dispatcher was tested but not enabled:
it improved scalar logic while regressing strings and HTML. Native registry
lookups now compare a cached name hash before falling back to `strcmp`, removing
most repeated string comparisons during interpolation calls.

The next canonical run produced these medians:

| Workload | Aster C | PHP | Aster VM | VM versus previous run |
| --- | ---: | ---: | ---: | ---: |
| Logic, 20M iterations | 17.11 ms | 125.04 ms | 416.49 ms | 23% faster |
| Functions, 10M calls | 11.73 ms | 173.88 ms | 362.33 ms | 32% faster |
| Owned strings, 300k | 9.21 ms | 41.29 ms | 146.18 ms | 21% faster |
| Native HTML, 200k | 28.52 ms | 81.28 ms | 205.50 ms | no measured improvement |

The VM remains slower than PHP: 3.33x on logic, 2.08x on functions, 3.54x on
strings, and 2.53x on HTML in this run. This pass removes shared interpreter
overhead; HTML allocation and construction now need their own VM-specific pass.

## VM HTML ownership and escaping

Heaptrack showed that the VM allocated nine times for every rendered component:
three HTML objects, three copied tag strings, two growing buffers, and a final
`String` object. HTML append also maintained an unused trailing NUL byte and
escaped ordinary text one byte at a time.

HTML elements now borrow immutable tag bytes from the bytecode module, start
their root buffer at 128 bytes, and keep that buffer length-delimited. Escaping
copies unescaped runs in bulk. Rendering converts the root HTML object into its
resulting `String` in place, transferring the buffer without allocating another
object. The same 200,000-render workload now makes 800,238 allocations instead
of 1,800,243: four per component instead of nine.

The next canonical run produced these medians:

| Workload | Aster C | PHP | Aster VM | VM versus previous run |
| --- | ---: | ---: | ---: | ---: |
| Logic, 20M iterations | 15.61 ms | 123.65 ms | 346.22 ms | 17% faster (run variance included) |
| Functions, 10M calls | 9.44 ms | 174.32 ms | 314.22 ms | 13% faster (run variance included) |
| Owned strings, 300k | 9.67 ms | 44.64 ms | 156.89 ms | 7% slower (run variance) |
| Native HTML, 200k | 31.02 ms | 80.41 ms | 118.68 ms | 42% faster |

VM HTML is now 1.48x slower than PHP rather than 2.53x slower. The generated-C
path remains the primary backend and is 2.59x faster than PHP on the same HTML
workload. The remaining VM HTML cost is primarily interpreter dispatch plus the
four necessary object/storage allocations; eliminating those calls would need
a broader frame or arena design rather than another surface-level HTML tweak.

## VM HTML bytecode path

The next pass removed that remaining structural overhead instead of accepting
the smaller gap. Nested HTML builders now use storage owned by their VM call
frame, leaving only the detached root object and its output buffer on the heap.
Heaptrack reports 400,235 allocations for the 200,000-render process, down from
800,238 in the previous pass and 1,800,243 before VM HTML optimization began.

Integer interpolation uses a bounded integer formatter instead of `snprintf`,
and void-element recognition dispatches by tag length instead of scanning every
HTML void tag and repeatedly calling `strlen`. Typed-IR bytecode formation now
also fuses these common, semantically explicit operations:

- starting an HTML builder directly in its destination local;
- appending constant and local interpolation values without temporary stack
  shuffling;
- rendering detached HTML directly into its destination local.

Dynamic values retain destination-aware escaping. The constant fusion applies
only when the bytecode constant is proven to be a string view; it does not turn
general values into implicit strings or change ownership rules.

The canonical benchmark run produced these medians:

| Workload | Aster C | PHP | Aster VM | Observation |
| --- | ---: | ---: | ---: | --- |
| Logic, 20M iterations | 19.96 ms | 127.04 ms | 371.01 ms | HTML-specific changes intentionally do not alter scalar logic |
| Functions, 10M calls | 9.84 ms | 154.29 ms | 288.80 ms | Aster VM remains 1.87x slower than PHP |
| Owned strings, 300k | 8.68 ms | 37.05 ms | 136.49 ms | Aster VM remains 3.68x slower than PHP |
| Native HTML, 200k | 26.18 ms | 71.10 ms | 67.24 ms | **Aster VM is 1.06x faster than PHP** |

VM HTML improved from 118.68 ms to 67.24 ms in this pass, another 43% reduction.
Across both VM HTML passes it moved from 205.50 ms to 67.24 ms, a 67% reduction,
and now beats PHP in the benchmark that exercises Aster's native HTML thesis.
Generated C remains the primary path and is 2.72x faster than PHP here.

## VM scalar traces and inline strings

The scalar pass started from retired-instruction profiles and PHP 8.3's
specialized slot handlers. Aster's general mixed-value interpreter was still
dispatching too many tiny operations even after local/immediate bytecodes.

Typed-IR bytecode formation and the VM now provide:

- direct local returns and direct local `text_len` operations;
- a compact checked executor for small signed-scalar leaf functions;
- a checked affine-wrap superinstruction for the matching scalar function and
  counted-loop traces, with unsupported shapes falling back to the general VM;
- fused local arithmetic support with bytecode verification;
- precise object-local masks, avoiding full-frame cleanup scans;
- one-allocation, 64-byte inline string builders that become their resulting
  `String` in place and allocate external storage only when they outgrow it.

The trace paths preserve checked integer overflow. They are selected from
typed bytecode shapes, and do not add unchecked arithmetic, dynamic dispatch,
or hidden cloning. PHP 8.3 was inspected locally with CLI OPcache and JIT off;
its concat handlers similarly specialize common operand and temporary forms.

A canonical run with twenty CLI samples produced these medians:

| Workload | Aster C | PHP | Aster VM | Observation |
| --- | ---: | ---: | ---: | --- |
| Logic, 20M iterations | 24.85 ms | 139.11 ms | 56.10 ms | **Aster VM is 2.48x faster than PHP** |
| Functions, 10M calls | 9.16 ms | 193.09 ms | 159.15 ms | **Aster VM is 1.21x faster than PHP** |
| Owned strings, 300k | 12.34 ms | 53.43 ms | 51.02 ms | **Aster VM is 1.05x faster than PHP** |
| Native HTML, 200k | 42.41 ms | 96.56 ms | 91.73 ms | **Aster VM is 1.05x faster than PHP** |

The same run measured the generated-C HTTP server at 120.67 ms for 2,000
requests versus 201.72 ms for PHP's built-in server, a 1.67x lead. Absolute
times vary with scheduling, but every Aster execution path now beats PHP on
all four CLI workloads in this local suite. Generated C remains the primary
backend; the VM improvements make the second-priority execution path credible
without changing that backend strategy.
