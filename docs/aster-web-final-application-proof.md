# Aster Web final todo application proof

`packages/aster_web/src/tests/final_todo_app.as` is the realistic retained-DOM proof
application. It is intentionally separate from the browser kitchen-sink fixture.

The application uses only ordinary classes, fields, `List<T>`, private handlers,
`Task`, native HTML, and `key=todo.key`. It contains no public DOM command,
projection state, signal, or VDOM API.

Two `FinalTodoList` regions receive different server constructor values and SSR
lists. Browser verification in `tests/final_todo_browser.py` covers:

- isolated alpha and beta component instances;
- restoration of their SSR-rendered keyed list state;
- append, rename, reorder, remove, and clear through `void` handlers;
- pending/saved conditional item content through compiler-owned `hidden` parts;
- async save success and owned async failure reporting;
- stale/detached completion suppression during root teardown;
- retained row, input, and nested child identity;
- browser-edited value, focus, caret, and selection preservation;
- independently retained `TodoBadge` child components;
- exactly-once disposal of two parent instances and two active nested children;
- idempotent root teardown.

Run the targeted proof with:

```sh
cmake --build build -j4
ctest --test-dir build -R '^final_todo_application_proof$' --output-on-failure
```

This proof also makes the remaining performance boundary explicit: ordinary
whole-list snapshots preserve identity but currently render and plan the full
list after each structural mutation. Compiler-internal sparse structural deltas
are an optimization requirement, not a reason to restore public DOM commands.
