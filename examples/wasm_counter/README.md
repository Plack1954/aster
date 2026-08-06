# Lime Browser 0.1 retained-DOM example

This is the executable coverage application for Lime Browser 0.1. Native
Aster HTML is server rendered through the primary C backend and hydrated with
state transitions compiled from the same Aster project target to browser
WebAssembly.

There is no second component syntax and no handwritten hydration marker:

```aster
public int Increment(int count) {
    return count + 1;
}

private Html Counter(CounterState state) {
    return <section id="counter-island">
        <output name="count">{state.Count}</output>
        <button onclick=Increment>Increment</button>
        <button onclick=ResetCount>Reset</button>
    </section>;
}
```

The checker verifies that event handlers are public, statically named, and
have a supported browser ABI. The compiler turns `onclick=Increment` into typed
metadata such as `click|Increment|l|l:count`; generated C exposes the stable
symbol `aster_export_Increment`. `build.sh` discovers every generated export,
so neither C adapters nor linker export lists are maintained by hand.

The example also renders two independent contact-form islands. Their handlers
borrow the actual UTF-8 `string` field values. `SubmitContact` returns an owned
`String` containing the submitted name and email; JavaScript decodes it and
then calls the generated drop boundary. The browser runtime updates text
content, `aria-invalid`, and `hidden` directly. It does not build or diff a
virtual DOM.

The generic loader lives at `runtime/browser/aster.js`; the application
loader is only an import plus one `hydrateAster` call. String input buffers
are allocated in Wasm, borrowed for one synchronous handler call, and freed in
a `finally` block. Owned results are likewise dropped after decoding.

Scalar metadata also initializes an island-local typed state store from the
SSR DOM. Later transitions read that store instead of reparsing rendered text
or ARIA attributes. Multiple handlers inside the nearest semantic `id` or
`form` scope share state, while separate component instances remain isolated.
Scalar results commit back to state and project directly to named text,
`aria-expanded`, and an `aria-controls` target's `hidden` property.

Aggregate handler results use an opaque generated ABI. `AddTodo` returns a
real Aster struct containing the next numeric state and an owning native
`Html` item. Generated accessors transfer owning fields once and a generated
drop export cleans whatever remains. The runtime renders the transferred
`Html`, keys collection children by their ordinary `id`, updates only the
matching child, and hydrates event bindings inside newly inserted HTML. The
keyed map is initialized from SSR children and then persists independently of
the DOM projection. `RemoveTodo` returns the explicit owning
`Aster.Html.KeyedRemove` protocol value; generated accessors transfer its key
and the runtime applies the typed removal to the controlled collection.

`build.sh` exercises the normal web-project workflow:

1. run `lang project build-web` for the manifest target;
2. emit portable server C from the target entry;
3. compile the browser entry through portable C to freestanding `wasm32`;
4. discover and export stable Aster entry points;
5. copy the reusable runtime and generate the tiny application loader;
6. run the server C natively to produce initial HTML;
7. verify ownership and typed result boundaries in Node;
8. verify real retained-DOM behavior in Chrome.

Run it with:

```sh
examples/wasm_counter/build.sh
python3 examples/wasm_counter/verify_browser.py
```

The browser verifier uses headless Chrome. It deliberately changes rendered
counter text to `999`; the next transition still advances persisted state from
5 to 6, and a second handler resets that shared state. It also falsifies a
disclosure's rendered ARIA and visibility state, then proves Aster repairs
both projections while a second disclosure remains isolated. Form validation,
the owned personalized response, and 100 repeated submissions remain covered.

The verifier also adds escaped todo items, changes the rendered next-ID output
to `999`, and proves aggregate state still advances correctly. It removes both
SSR and dynamically inserted items through Aster handlers, performs 50 more
add/remove cycles, and confirms the original list node survives throughout.

The example covers synchronous scalar and Boolean results, borrowed UTF-8
inputs, owned String and direct Html results, supported patch structs,
persistent island state, and keyed collection updates. Its forms retain normal
action and method attributes for progressive enhancement. Async handlers using
`Task.Delay` are covered by the Lime browser fixture; Fetch and general nested
aggregate ABI generation remain outside Browser 0.1.
