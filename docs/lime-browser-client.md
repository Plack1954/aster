# Lime browser client and Wasm direction

## Direction

Lime Browser should add typed interaction to ordinary server-rendered or
materialized Aster HTML without introducing another application language or UI
framework.

The current evidence supports a retained-DOM model without a virtual DOM for
modest interactions. It does not yet prove the final model for large browser
applications. Signals, fine-grained reactivity, bounded reconciliation, and
even a virtual-DOM control remain questions to be means-tested rather than
accepted or rejected ideologically.

The detailed composition research and recommended transition-batch design are
in [Composable, compiler-checked browser projections](lime-composable-projections.md).

The guiding rule is:

> Begin with the smallest retained-DOM model that provides excellent developer
> experience, and add reactive machinery only when real applications show that
> it removes more complexity than it creates.

Aster should not host React, Vue, Svelte, or another client component runtime.
Native Aster HTML remains the component syntax, browser behavior remains
checked Aster, and JavaScript remains small browser/Wasm infrastructure rather
than the application platform.

## Existing Browser 0.1 foundation

A binary project target may declare a `browser_entry`. `lang project
build-web` currently:

1. emits portable server C from the ordinary target entry;
2. compiles the browser entry through portable C to freestanding `wasm32`;
3. exports only checked handlers referenced by native HTML event properties;
4. writes the optimized Wasm module;
5. copies the generic `aster.js` runtime;
6. writes a tiny target loader.

`Lime.Browser` provides typed browser-asset paths, a loader element, and normal
static responses for those generated files.

The current runtime hydrates server-rendered HTML through compiler-generated
typed metadata. There is no second component syntax and no handwritten marker
or export adapter. Supported experiments cover:

- synchronous scalar and Boolean transitions;
- borrowed UTF-8 string inputs;
- owned string results and deterministic drops;
- direct owned `Html` results;
- supported typed patch structs;
- one-to-many named text projections and named Boolean visibility, checkbox,
  or button-enabled projections, alongside ARIA and controlled-property
  updates;
- hydration-time diagnostics for aggregate patch fields that have no matching
  projection target;
- island-local persistent scalar state initialized from SSR output;
- multiple isolated component instances;
- state shared by handlers in one semantic scope;
- keyed collection insertion, replacement, and removal;
- preserving the collection node and unaffected child identity;
- hydrating handlers inside newly inserted Aster HTML;
- normal form actions and methods as progressive fallbacks;
- asynchronous event handlers suspended by `Task.Delay`, with JavaScript host
  clock polling, typed task results, fault transport, and deterministic task
  release.

The retained state is independent of the current DOM projection. Tests
intentionally corrupt rendered values and prove that later transitions use the
stored typed value and repair the DOM.

The first asynchronous slice proves real Wasm suspension and resumption with
`Task.Delay`; it does not yet provide browser I/O. An async input trial exposed
two browser-specific requirements: editable controls must not be disabled while
deriving results, and an older completion must not overwrite a newer input.
The runtime now versions async transitions per hydrated event source, drops all
owned results from stale completions, and commits only the latest result. A
rapid `a` then `ab` trial deliberately completes `ab` first and retains its
projection, focus, and caret after the slower `a` task finishes. This is
latest-result-wins, not host cancellation: stale Wasm work still runs to
completion and is then discarded safely.

Current explicit gaps include Fetch-backed HTTP, cancellation driven by the
browser host, general nested aggregate ABI generation, broad browser APIs,
navigation and history policy, and evidence from a substantial interactive
application.

## Why a virtual DOM is not the starting point

A virtual DOM answers:

> Given a newly rendered tree, which DOM operations transform the old tree into
> it?

Many ordinary interactions do not need that general answer. A counter changes
text; validation changes messages, ARIA, and visibility; a todo operation
changes one keyed child. Compiler-known typed projections can perform those
updates directly while retaining focus, selection, scroll position, element
identity, and progressive server behavior.

Avoiding a VDOM is currently an evidence-based implementation choice, not a
permanent thesis. If a measured substantial application shows that another
model provides materially better total developer productivity at acceptable
runtime cost, that evidence should guide the design.

## Signals and fine-grained reactivity

Signals answer a different question:

> When a state value changes, which computations and DOM projections depend on
> it?

Signals still require rendering bindings such as value-to-text,
value-to-property, value-to-attribute, conditional regions, and keyed
collections. They may reduce repeated derived-state and patch code, but they
also introduce persistent state, dependencies, subscriptions, scheduling,
equality policy, error propagation, and cleanup.

A direct JavaScript-style signal API may fit Aster poorly because Aster has no
tracing GC or general closure environments. Runtime signals would need clear
allocation, ownership, subscription lifetime, disposal, and re-entrancy
semantics in both Wasm and generated C.

Fine-grained behavior might instead be statically compiled from checked HTML
projections and typed state. That could obtain signal-like update precision
without a general runtime dependency graph. It must not make an ordinary
struct field read silently acquire surprising reactive semantics.

No public signal API or new browser-specific language syntax should be added
before application evidence identifies the exact ceremony or consistency
problem it solves.

## Candidate update models

### Typed transitions and explicit patches

The current baseline lets handlers return a scalar or a typed patch:

```aster
private struct CounterPatch
{
    int Count;
    bool CanDecrement;
}

public CounterPatch Increment(int count)
{
    int next = count + 1;
    return new()
    {
        Count = next,
        CanDecrement = next > 0
    };
}
```

Strengths:

- small runtime and Wasm ABI;
- explicit update and allocation costs;
- no general dependency graph;
- straightforward deterministic cleanup;
- predictable DOM writes;
- good progressive enhancement.

Risks:

- large patch structs may become repetitive;
- derived values may be maintained manually in several handlers;
- shared state across many controls may become awkward;
- conditional interfaces may require too much projection wiring.

This remains the baseline against which richer mechanisms should be measured.
A small derived-counter trial found that one patch can cleanly update primary
state, derived numbers, derived text, and conditional visibility while
preserving the component DOM. Allowing one field to project to every matching
named target removes duplicate patch fields. This is sufficient evidence to
continue improving statically known projections; it is not evidence that a
runtime signal graph is needed. Extending the same typed Boolean projection to
a named button also handles derived enabled/disabled state without handler-side
DOM code. Artifact size is now tracked by the reproducible Vue comparison
below rather than copied from this changing fixture. Hydration also rejects
a misspelled or absent aggregate projection with the handler and field name,
rather than silently retaining state that never reaches the DOM. This improves
feedback but remains runtime validation; complete compile-time validation is
harder because targets can cross component and conditional HTML boundaries.

An input-driven query trial sends every `input` event through Wasm and projects
length, preview text, and submit-button validity. Because the runtime changes
only derived targets, the browser-owned input retains focus and caret position.
A local headless-Chrome run processed 1,000 short synchronous input transitions
in about 21 ms; that is a smoke measurement rather than a benchmark, but it
shows no immediate need for scheduling or batching at this scale. SSR can call
the same pure projection helper used by the event transition, eliminating
repeated initial derived-value logic without a reactive runtime. The remaining
ceremony is placing the projection fields and invoking that initializer.

### Statically compiled fine-grained projections

A component may read one state field in several HTML positions. The compiler
could record those checked dependencies and update only the affected text,
properties, or attributes when a transition returns new state.

This may offer signal-like DX and update precision without exposing a runtime
signal graph. Open questions include the explicit state boundary, how
components declare persistent state, whether computations are static, how
conditional dependencies behave, and how costs remain understandable.

### Runtime signals

Persistent `Signal<T>` and computed values could make shared and derived state
more declarative. This should be prototyped only against real pain in explicit
patch applications. Evaluation must include allocation, subscription cleanup,
scheduling, Wasm size, API quality without closures, and debugger behavior,
not merely update microbenchmarks.

### Bounded region rendering

A handler may return owned native `Html` for a bounded target. This reuses
ordinary Aster rendering for complex conditionals and new collection items.
Whole replacement is simple but may lose DOM identity, focus, selection,
scroll state, and browser-owned element state. DOM morphing preserves more but
introduces reconciliation machinery and can become a string-rendered VDOM in
practice.

The legacy keyed collection trial constructs safe HTML for one item and uses an
explicit `Aster.Html.KeyedRemove` result. That proved the structural mechanism
but is too low-level as an ordinary application model.

The newer trial uses normal `List<T>` mutation and native HTML children carrying
`key=todo.key`. Returning the resulting keyed snapshot inserts new keys, removes
missing keys, and orders retained keys without replacing existing nodes. Chrome
coverage exercises `Add`, `RemoveAt`, `Insert`, and `Clear`; retained rows and a
browser-edited input survive. No tree VDOM or signal graph is involved.

Interactive native-HTML class components now provide the first Wasm-owned
region state. A zero-argument constructor initializes ordinary fields, bound
methods mutate the same object across events, and a generated component ABI
drops it when its compiler-marked DOM region disconnects. Two successive appends
and removal of an appended item prove persistence. Constructor-state transfer,
automatic rerendering, async methods, and compiled item-local updates remain
unsupported; the explicit keyed command can already remain backend
infrastructure rather than normal application syntax.

### Virtual DOM control

A VDOM may be built as an experimental control if substantial applications
justify the comparison. It should not be selected because it is conventional,
nor rejected because it is unfashionable. It must win on total application DX,
correctness, artifact size, startup, update cost, memory, and deterministic
lifetime behavior.

## A likely layered model

Lime may not need one universal browser update mechanism. A coherent hierarchy
could be:

1. scalar result for one simple projection;
2. typed patch result for several related projections;
3. owned `Html` for a bounded insertion or replacement;
4. keyed operations for collection changes;
5. statically compiled fine-grained dependencies if repetition appears;
6. runtime signals or bounded reconciliation only where measured applications
   justify them.

Simple interactions should not pay for the most general mechanism.

## SSR, SSG, and progressive enhancement

Browser interaction is an enhancement of the normal Lime application:

- SSR and SSG produce the initial native HTML;
- ordinary links, forms, actions, and methods remain meaningful;
- the complete Lime server application remains available;
- Wasm attaches typed behavior to the retained DOM;
- pages without browser-reachable handlers should not ship browser artifacts;
- failure to load Wasm should preserve the useful server path where the
  interaction has one.

An event property is already a natural opt-in:

```aster
<button onclick=Increment>Increment</button>
```

The compiler knows that `Increment` is browser-reachable. A future publication
pipeline should compile the required Wasm, fingerprint and copy its assets,
and include the loader only where needed. The application should not manually
coordinate separate SSG, server, and browser asset graphs.

## Development experience

High productivity and development speed are primary design goals. Browser
support should preserve the fast server loop:

- use a fast debug Wasm build during development;
- rebuild browser output only when browser-reachable code changes;
- avoid `wasm-opt` during ordinary edits;
- do not rebuild Wasm after a server-only or content-only change;
- reload or rehydrate predictably;
- report browser reachability and unsupported APIs at check time;
- retain Aster source locations across generated exports and runtime failures;
- explain ownership and ABI errors at the source boundary;
- make browser state inspectable without requiring knowledge of generated JS.

The compiler should eventually distinguish server-only, browser-only, and
shared reachable code. A browser handler that reaches a database, native
filesystem call, or another server-only mechanism should receive a direct
source-aware diagnostic before linking.

## Near-term capabilities to prove

Before selecting a general reactive model, the retained-DOM foundation should
be tested with practical missing capabilities:

- asynchronous handlers beyond the initial `Task.Delay` executor slice;
- Fetch-backed `System.Net.Http` or another typed browser HTTP boundary;
- cancellation and stale-response handling;
- loading, success, and error projections;
- richer typed DOM properties and attributes;
- focus-safe keyed updates and reorder behavior;
- bounded nested patch values where ownership is clear;
- navigation and history primitives;
- development rebuild and debugging behavior.

These capabilities are likely to reveal the real state-management pressure.

## Current Vue comparison

[`examples/browser_compare`](../examples/browser_compare/) is now the concrete
capability and performance check. In a representative local Chrome run, Aster
created 1,000 keyed rows in 11.1 ms versus Vue's 8.7 ms, updated every tenth
row in 2.4 ms versus 4.5 ms, swapped two rows in 0.4 ms versus 2.6 ms, appended
1,000 rows in 13.9 ms versus 8.0 ms, deleted one row in 0.3 ms versus 4.3 ms,
and cleared 1,999 rows in 4.3 ms versus 6.7 ms. Aster's benchmark client was
10.4 KB gzip versus Vue's 41.9 KB gzip. These are smoke measurements, not
universal benchmark claims: Aster was not consistently faster, because Vue won
bulk create and append.

The comparison also gives a hard capability boundary. Aster now handles bulk
keyed creation, append, sparse replacement, removal, clear, and two-key swap.
It does not yet provide arbitrary class/style/attribute bindings, sparse update
while preserving every updated row's browser-owned state, or nested patch
composition. Vue supports those operations today. The retained model is proven
for its listed operations, not as a general Vue replacement.

The comparison found and fixed three concrete problems: context-free parsing
could not insert table rows, a 1 MB Wasm maximum trapped at 1,000 rows, and
eager per-row collection initialization made creation take roughly 140 ms.
Contextual fragment parsing, a growable 16 MB maximum, and lazy collection
initialization brought creation down to roughly 11 ms without increasing the
initial Wasm allocation.

Clear and swap remain small, explicit keyed operations. Selection revealed a
less elegant boundary: changing classes while persisting the selected key needs
multiple composed effects, but the browser ABI currently returns one flat
scalar, aggregate, or operation. Adding a nominal command type for every DOM
effect would become an ad-hoc command language. Work should stop at that point
and design composable checked patches or statically compiled bindings instead;
the current model must not claim arbitrary Vue-style binding support.

## Means-testing applications

Counters and small forms prove mechanics but cannot select the long-term
architecture. Competing models should be implemented in representative
applications.

### Form-heavy workflow

Test multiple fields, cross-field validation, conditional fields, async
submission, server fallback, error summaries, and focus preservation.

### Search and filtering

Test input events, debouncing, Fetch, loading and error states, several filters,
keyed results, cancellation, and URL synchronization.

### Dashboard

Test independent data sources, computed values, shared state, periodic refresh,
conditional sections, tables, and browser/server boundaries.

### Stateful collection editor

Test insertion, removal, reordering, editing, optimistic updates, rollback,
focus and selection, and hundreds or thousands of keyed rows.

### Complete application

A browser-enhanced issue tracker is a useful candidate: filtering, creation,
validation, optimistic status changes, navigation, sessions, server fallback,
and API requests exercise substantially more than an isolated demo.

## Evaluation ledger

Each prototype should record more than raw update speed:

- application-owned Aster lines;
- state, patch, binding, and framework declarations;
- duplicated derivations;
- concepts an application author must understand;
- edit/check/rebuild latency;
- diagnostic and debugging quality;
- generated Wasm and generic runtime size;
- hydration time;
- allocations and retained memory;
- DOM reads and writes per interaction;
- update latency under representative loads;
- focus, selection, scroll, and DOM identity preservation;
- deterministic cleanup across Aster/Wasm/JS boundaries;
- behavior when Wasm is absent or fails;
- SSR and SSG compatibility.

A tiny runtime is not a success if application code becomes excessively
ceremonial. A concise API is not a success if every input event serializes and
reconciles an entire tree. Total productivity and predictable behavior are the
criteria.

## Acceptance principles

1. Native Aster HTML remains the only component syntax.
2. Browser application behavior is checked Aster.
3. JavaScript remains small runtime infrastructure, not the application model.
4. Ordinary SSR and materialized HTML remain useful without Wasm.
5. The retained DOM is the initial baseline.
6. Simple interactions do not pay for a general reactive runtime.
7. State and cleanup costs remain explainable and deterministic.
8. Signals, reconciliation, and VDOM designs are selected by substantial
   application evidence.
9. Development speed and application ceremony are measured alongside runtime
   performance.
10. Browser work strengthens the normal Lime server application rather than
    creating a separate SPA framework.
