# Lime browser component semantics

Status: normative for the retained-DOM browser client. Experimental projection
and keyed-command APIs described by older design notes are not part of the
language.

## Component eligibility

An ordinary class is usable in native HTML element position when all of the
following hold:

1. it declares exactly one explicit constructor;
2. it declares `public Html Render()` with no explicit arguments;
3. each component property matches a constructor parameter by name and checked
   type;
4. body children, when accepted, are passed through an `Html children`
   constructor parameter.

No component keyword, base class, interface, runtime registration, or lifecycle
method is involved. A class becomes interactive when native HTML rendered by
that class binds one of its instance methods as an event handler. Such handlers
may be private. Static and top-level handlers retain the ordinary public export
rules.

A component that fails these rules is rejected during checking; the compiler
does not silently lower it as a custom browser element.

## SSR and browser construction

SSR constructs the class, calls `Render()`, and deterministically destroys that
temporary instance after taking ownership of the resulting `Html`.

The browser creates one retained Wasm instance for each compiler-marked
component region on its first event. Boolean, integer, and `string` constructor
parameters transfer through typed `data-aster-component-*` metadata only when
the class stores them in same-named, same-typed fields. This metadata is public
HTML and MUST NOT contain credentials, authorization data, private request
objects, or other secrets.

The first `List<T>` field may be restored from SSR keyed HTML when `T` is a flat
struct whose represented fields are Boolean, integer, or `string`. The compiler
emits a field-ID schema and raw escaped values. Restoration clears constructor
fixture data and rebuilds the list through generated checked Wasm exports before
the first handler runs. A missing field, malformed integer, allocation failure,
or ABI mismatch rejects that instance, destroys its partial state, reports the
fault, and permits a later retry. Nested structs, multiple transferred lists,
references, unions, arbitrary object graphs, and request-only values are not
supported state-transfer types.

## Handler completion and rendering

Handler behavior is determined by its checked completion type:

| Completion | Browser behavior |
|---|---|
| `void` | After success, call the owning component's `Render()` and reconcile its controlled region or inferred root parts. |
| `Task` / `Task<void>` | Apply the same `void` render rule after the current task succeeds. |
| Boolean or integer | Commit to the matching named/ARIA scalar target; this does not implicitly render the complete component. |
| `string` | Transfer and drop the owned string, then update the matching named target or submission result. |
| `Html` | Render the owned value and reconcile the bounded `aria-controls` target. |
| supported struct | Consume generated owning field accessors and drop the aggregate exactly once. |

A handler fault never commits a render. A `Render()` fault preserves the last
committed DOM. Constructor, handler, render, decode, and destructor exception
slots are consumed before another instance is invoked.

Async instance transitions have one generation counter per component, not per
button. Starting a newer transition makes older results stale. A pending task
holds a component lease. Disconnect invalidates its generation immediately;
work may finish, but its result is dropped and cannot render. Destruction occurs
when the final lease is released.

## DOM ownership

Compiler-owned parts are updated from successful renders:

- scalar text and mixed-content text ranges;
- `class`, `disabled`, `hidden`, and `title`;
- `aria-*`, non-framework `data-*`, `alt`, `role`, `lang`, and `dir`;
- CSS custom properties.

Browser-owned state is retained after hydration:

- `value` and `checked`;
- selected options;
- focus, caret, and text selection;
- scroll position and other browser-maintained interaction state.

A normal render MUST NOT copy browser-owned properties from its incoming
snapshot. There is currently no explicit controlled-property spelling for
`value`, `checked`, or selection; applications requiring one must wait for that
feature rather than relying on replacement behavior. Dynamic `style` strings
and URL-bearing inferred attributes remain unsupported; use checked CSS custom
properties and URL APIs.

Incoming and retained part layouts are validated before mutation. Missing,
duplicated, unterminated, or ownership-crossing parts reject the render without
a partial commit where prevalidation is possible.

## Keyed identity

`key=expression` is compiler-only native HTML identity. Keys MUST be nonempty
strings or integers and unique among siblings in one controlled collection.
Identity is scoped by component region, collection, and keyed item. Existing
keys retain their DOM nodes; new keys insert, omitted keys remove, and order
follows the incoming snapshot. Application code mutates ordinary `List<T>` and
does not return DOM command values.

Changing a key means removing one identity and inserting another. Index,
rendered text, or object address is not implicit identity.

## Lifetime and disposal

SSR component instances are temporary. Browser instances are owned by their
marked DOM regions. Nested components own independent instances and parent part
updates cannot cross nested ownership boundaries. Removing a keyed ancestor,
removing a component root, or calling `disposeAsterRoot(root)` invalidates every
owned instance exactly once. Root teardown is idempotent and disconnects its
observer.

A destructor fault is reported after the runtime marks the instance disposed,
so it cannot cause a second destructor call. One component's fault does not
invalidate sibling instances.

## Explicitly unsupported semantics

The current language does not promise:

- nested or cyclic state serialization;
- multiple restored list fields;
- conditional mounting of new nested component regions during an uncontrolled
  whole-root render;
- inferred URL, `value`, `checked`, or selection ownership;
- public `ProjectionState`, `ProjectionTransition`, `project_*`, packed batch,
  or keyed structural command APIs;
- cancellation of host async work (detached work is safely drained instead).
