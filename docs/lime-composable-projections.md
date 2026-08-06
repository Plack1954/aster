# Composable, compiler-checked browser projections

## Verdict

The retained-DOM model is still feasible. The current flat handler-result ABI
is the part that has reached its limit.

The recommended next model is a hybrid of:

1. **compiler-generated template parts** for text, attributes, properties,
   classes, and bounded conditional regions;
2. **a compact transition batch ABI** for transferring all changes from Wasm
   to JavaScript in one result;
3. **explicit typed keyed operations** only for structural collection changes;
4. **ordinary typed state transitions**, without a public signal graph or a
   virtual DOM.

This is closest to Lit's static template parts and Svelte's compiled updates,
with an operation stream at the Wasm boundary. It is not Incremental DOM: Lime
should not rerun an entire render function and walk every element after each
event. It is not Solid's model: Lime does not currently need runtime signals,
subscriber lists, or effects.

The source-level state boundary still needs one focused prototype before any
syntax is committed. The ABI and compiler architecture can be selected now;
the spelling cannot.

## Problem demonstrated by the Vue comparison

[`examples/browser_compare`](../examples/browser_compare/) established that
the retained implementation can be small and competitive:

- 9.4 KB gzip versus 41.9 KB for the compared Vue client after adding the
  experimental batch decoder;
- direct sparse replacement, swap, deletion, and clear were faster locally;
- Vue was faster for bulk create and append.

It also exposed the exact abstraction failure. Clear and swap fit as isolated
structural operations, but selection needs several effects in one transition:

- persist the newly selected key;
- remove a class from the previous row;
- add a class to the new row;
- update ARIA state or detail text;
- preserve both row nodes and their browser-owned state.

The current ABI can return one scalar, one aggregate, one `Html`, or one keyed
operation. Adding a nominal result type for every effect would turn the public
API into an ad-hoc DOM command language. Replacing the entire row solves the
visual problem but loses focus, selection, input values, and element identity.
That is the boundary this design must address.

## Relevant prior art

### Lit: static template parts

Lit separates static template strings from dynamic expressions and retains a
representation of the expression locations. Later renders update the dynamic
parts rather than recreating all static DOM.

Useful lesson for Lime:

- native Aster HTML already exposes statically known expression locations;
- the compiler can assign those locations stable part identifiers;
- text, property, attribute, and class updates do not need a VDOM.

Difference: Lit reevaluates JavaScript template expressions. Lime must evaluate
checked Aster expressions in Wasm and transfer their values with deterministic
ownership.

### Svelte: compiler-known dependencies

Svelte demonstrates that a compiler can order derived computations and generate
direct DOM updates. Its documentation also describes the limitation of static
dependency analysis: dependencies hidden behind calls may not be visible to the
compiler.

Useful lesson for Lime:

- compile direct dependencies where they are explicit in native HTML;
- do not pretend arbitrary calls are statically transparent;
- permit an explicit derived-state helper when dependency analysis stops at a
  function boundary.

Difference: modern Svelte also has a substantial reactive runtime model. Lime
does not need to copy runes, proxies, or JavaScript closure semantics.

### Solid: runtime signals and observers

Solid tracks signal subscribers and reruns effects when signal values change.
This gives excellent fine-grained behavior for dynamic dependency graphs.

Useful lesson for Lime: signals solve dependency discovery, not DOM binding or
Wasm ownership. They remain an option if dynamic dependencies become dominant.

Why not now:

- Aster has deterministic ownership rather than tracing GC;
- subscriptions need allocation, cleanup, scheduling, and reentrancy rules;
- the current examples have statically visible projections;
- the Vue comparison exposed composition and identity problems before it
  exposed a need for dynamic dependency tracking.

### Incremental DOM: an operation-stream compilation target

Incremental DOM mutates the existing DOM in place without allocating an
intermediate virtual tree and is explicitly intended as a compilation target
for template languages.

Useful lesson for Lime: a linear operation stream is a good boundary between
Wasm and JavaScript.

Why not use its rendering algorithm directly:

- rerunning a complete render still visits unrelated elements;
- Lime already knows many exact projection targets;
- cursor-based reconciliation introduces ordering and identity semantics that
  simple direct parts avoid;
- server-rendered hydration and browser-owned state need explicit treatment.

### Vue and virtual DOM rendering

Vue provides the capability control: arbitrary class/style/attribute bindings,
component composition, keyed diffing, and mature lifecycle behavior. The local
comparison shows that Lime can be much smaller and faster for some direct
operations, but Vue currently wins breadth and some bulk operations.

Lime should copy the capability requirements, not Vue's VDOM architecture.

## Recommended semantic model

### 1. A browser region has typed state

A retained region has:

- one explicit state type;
- one native Aster HTML render plan;
- one or more event transitions;
- compiler-known dynamic expression locations;
- a deterministic lifetime.

A possible, non-final source sketch is:

```aster
struct TableState
{
    int selectedId;
    string status;
}

private Html Table(TableState state, List<Row> rows)
{
    return <section>
        <output>{state.status}</output>
        <table>
            <tbody>
                {RenderRows(rows, state.selectedId)}
            </tbody>
        </table>
    </section>;
}

public TableTransition Select(TableState state, int id)
{
    return new()
    {
        state = new() {
            selectedId = id,
            status = $"Selected {id}"
        }
    };
}
```

The important semantics are explicit state and ordinary native HTML. Whether a
region is declared through a component modifier, a standard wrapper, or a
checked convention remains open. Inferring persistent state from arbitrary
struct reads would be too surprising.

### 2. Dynamic HTML expressions become projection parts

The compiler assigns a part to each browser-reachable dynamic location:

- text content;
- DOM property;
- Boolean property;
- attribute;
- token/class membership;
- CSS custom property;
- bounded conditional region;
- keyed item-local projection.

The part is created from the HTML expression itself, so target existence and
value type are checked by construction. Applications should not identify new
projections with matching strings such as `name="count"`.

SSR renders the normal initial value. Browser publication emits only compact
part markers and a projection plan for browser-reachable regions. Static HTML
without handlers remains unchanged.

### 3. Handlers return state plus structural effects

Non-structural DOM changes come from reevaluating projection expressions against
the returned state. Applications do not manually issue `SetText`, `SetClass`,
or `SetDisabled` commands.

Structural changes remain explicit because they carry real identity policy:

- keyed insert or replace;
- keyed remove;
- keyed clear;
- keyed move or swap;
- bounded region replacement.

A transition may compose state and structural operations recursively:

```aster
struct TableTransition
{
    TableState state;
    List<KeyedChange> rows;
}
```

This is not possible with the current flat aggregate accessor ABI. The compiler
must recursively lower nested transition values into one internal batch.

### 4. The compiler emits one transition batch

A handler invocation should cross back into JavaScript once with an opaque
batch handle. A generated encoder lowers the result into records such as:

```text
SetText(part, utf8)
SetBoolProperty(part, value)
SetAttribute(part, utf8)
SetClassToken(part, token, enabled)
ReplaceRegion(part, html)
InsertKeyed(collection, key, html)
RemoveKeyed(collection, key)
MoveKeyed(collection, key, beforeKey)
ClearKeyed(collection)
CommitState(region, encodedState)
```

These records are compiler/backend IR, not the primary application API.
JavaScript decodes and applies them. Standard structural types are merely typed
ways to request the structural subset.

Benefits over the current ABI:

- nested structs and lists compose naturally;
- JavaScript no longer scans exports for every aggregate shape;
- one Wasm-to-JavaScript return replaces many generated field accessor calls;
- owned strings and HTML have one batch lifetime;
- stale async batches can be dropped without decoding individual fields;
- diagnostics can name the source projection, not a generated export.

### 5. State and projection evaluation stay in Wasm

JavaScript should not reimplement Aster expressions. For each transition, the
backend generates code that:

1. executes the handler;
2. obtains the next typed state;
3. evaluates affected projection expressions;
4. encodes changed values and structural effects;
5. returns the batch handle.

A first implementation may reevaluate every projection in the region and omit
equal scalar values. A later optimization may use compiler-visible field
reads to emit a dirty-field-to-part table. Correctness must not depend on
perfect dependency analysis.

This fallback is important. It avoids Svelte-style hidden-dependency errors:
unknown dependencies cost extra reevaluation instead of producing stale UI.

## Composition rules

### Nested components

A component boundary needs a generated projection contract:

- state type;
- part schema;
- child region slots;
- structural collections;
- cleanup function.

A parent may replace a child region, but it may not mutate a child's internal
parts by string name. This preserves local reasoning and permits compile-time
checking across separately compiled modules.

### Conditions

A condition controls a bounded region with stable anchors. If only text or
properties change, direct parts update. If the active branch changes, Lime
replaces that bounded region and hydrates its child parts.

Identity inside a branch-changing region is not guaranteed. The compiler should
make that boundary visible in diagnostics and documentation rather than quietly
morphing arbitrary trees.

### Keyed collections

Each keyed item has its own part namespace and ordinary DOM key. Item-local
text, class, property, and attribute updates mutate the existing row. Structural
operations alter only collection membership or order.

This fixes the current sparse-update weakness: changing row text or selection
class must not require replacing the row.

### Async transitions

Async handlers produce the same batch type. Existing latest-result-wins
versioning applies to whole batches. A stale batch is dropped before any DOM or
state commit. Host cancellation remains a separate optimization.

## Compiler checks

The checker should reject:

- a property projection whose expression type cannot be assigned to that DOM
  property;
- class/style token projections with unsafe dynamic names;
- duplicate part identifiers after lowering;
- structural operations targeting a non-keyed region;
- a keyed operation with a key type different from the collection key type;
- a handler returning state incompatible with its region;
- a child component projection that violates the child's exported contract;
- browser-reachable projection expressions that call server-only APIs;
- owned values whose transition-batch transfer is ambiguous.

Static checking cannot prove that externally modified DOM remains intact.
Hydration and application must retain precise runtime diagnostics for missing
markers, duplicate keys, and browser extensions that mutate controlled regions.

## Ownership and failure behavior

A batch owns every transferred string, HTML value, nested operation, and next
state until one of two actions occurs:

- JavaScript applies the batch and calls its drop export;
- JavaScript discards a stale or failed batch and calls the same drop export.

DOM mutation should begin only after the runtime validates record framing,
part identifiers, collection identifiers, and payload bounds. This prevents a
malformed batch from partially mutating the page. Application-level exceptions
remain task faults and do not commit state.

Persistent region state needs an explicit drop when its root disconnects. A
small mutation observer may manage region lifetime, or Lime may constrain
region removal to its structural runtime. This must be decided and leak-tested
before persistent opaque Wasm state handles are introduced.

## What not to build

Do not build:

- a public stringly typed `SetAttribute(id, name, value)` patch API as the main
  model;
- one nominal result struct per visual effect;
- a VDOM hidden inside an `Html` string diff;
- automatic dependency semantics for every ordinary field read;
- runtime signals before static projection parts fail a substantial example;
- general DOM morphing as the default for state changes.

A low-level escape hatch may eventually expose checked browser operations, but
ordinary application code should express visual state in native HTML.

## Prototype sequence

### Prototype A: compiled parts without new public syntax

Use one internal fixture where a component function accepts an explicit state
struct. Compile direct child text and existing Boolean properties into part
markers. A handler returns the next state. Generate one batch and preserve the
existing nodes.

Required proof:

- SSR and browser use the same expression;
- no matching `name` convention;
- text and disabled/class state update together;
- one batch crosses the ABI;
- stale async batches drop cleanly.

### Prototype B: keyed item-local parts

Add selected-row class and label updates without replacing either row. Then run
the Vue comparison's select and sparse-update operations.

Required proof:

- row identity, focus, and input values survive;
- update cost is proportional to affected parts or remains competitive at
  1,000 rows;
- compressed client size stays materially below Vue.

### Prototype C: recursive structural composition

Allow a transition to contain next state plus a list of insert/remove/move/clear
operations. Replace aggregate field exports with the batch encoder.

Required proof:

- selection state and structural changes commit atomically;
- nested ownership drops correctly on success, stale completion, and fault;
- diagnostics point to source fields and projection locations.

Only after these prototypes should Lime select source syntax and make a public
API commitment.

## Implemented prototype status

The first prototype now exists behind deliberately experimental conventions:

- a state struct name ends in `ProjectionState`;
- `project_text`, `project_disabled`, and `project_class` are checked native
  HTML properties whose values must be direct fields of that state;
- synchronous handlers returning the state are lowered to one owned packed
  batch instead of flat aggregate accessors;
- JavaScript validates the complete batch before mutating text, `disabled`, or
  `class` parts;
- one drop export handles success and discarded results.

The Lime browser fixture updates all three projection kinds and proves in Chrome
that every projected DOM node retains identity. Its Node integration verifies
that four state fields arrive in one batch and that no per-field result exports
exist.

This validates the batch and retained-part architecture, not the temporary
source syntax. Current prototype limits are intentional: projection handlers
are synchronous, state fields are flat Boolean/integer/string values, text
initialization is still written as an ordinary child expression, and the
`ProjectionState` suffix is a marker rather than a final region declaration.
Async batches, keyed item-local parts, recursive transition composition, and a
final explicit state-boundary spelling remain to be designed.

The generic decoder increased the comparison client from 8.8 KB to 9.4 KB
gzip. A future production implementation should tree-shake the decoder from
applications without projection-state handlers.

## Stop criteria

Stop and reconsider retained DOM if any of these become true in the comparison
application:

- ordinary class/property updates require public imperative DOM command lists;
- component contracts cannot be checked without whole-program HTML analysis;
- keyed item-local updates require general tree reconciliation to preserve
  identity;
- batch/marker/runtime size approaches the Vue control without matching its
  capability;
- deterministic cleanup requires a tracing-GC-like graph;
- development diagnostics cannot identify the Aster projection that failed.

None of these has been proven yet. The current evidence says the retained DOM
is viable, while the flat handler ABI is not sufficient.

## References

- Lit templates overview: <https://lit.dev/docs/templates/overview/>
- Lit expression locations: <https://lit.dev/docs/templates/expressions/>
- Incremental DOM overview: <https://google.github.io/incremental-dom/>
- Solid fine-grained reactivity:
  <https://docs.solidjs.com/advanced-concepts/fine-grained-reactivity>
- Svelte reactive dependency behavior:
  <https://svelte.dev/docs/svelte/legacy-reactive-assignments>
- Local Vue comparison: [`examples/browser_compare`](../examples/browser_compare/)
