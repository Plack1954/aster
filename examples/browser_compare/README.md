# Aster retained DOM versus Vue

This is a deliberately narrow, reproducible client comparison. Both clients
create 1,000 keyed table rows, append 1,000 rows, and delete one middle row in
headless Chrome. The rows and controls are equivalent. It is not a claim that
this is a complete framework benchmark.

Run:

```sh
examples/browser_compare/build.sh
```

A representative local run after fixing the failures exposed by this example:

| operation | Aster retained DOM | Vue 3.5 runtime |
|---|---:|---:|
| create 1,000 keyed rows | 10.8 ms | 11.1 ms |
| append 1,000 keyed rows | 10.6 ms | 9.0 ms |
| delete one middle row | 0.5 ms | 5.3 ms |
| client code, raw | 26.8 KB | 111.1 KB |
| client code, gzip | 8.1 KB | 41.8 KB |

Treat timings as local smoke measurements, not universal benchmark results.
The useful result is that the retained implementation is in the same range for
bulk creation, much faster for direct deletion, and substantially smaller. It
is **not consistently faster than Vue**: Vue won the append measurement.

## Capability status

Working and exercised:

- SSR HTML retained during hydration;
- bulk keyed creation and append;
- direct keyed deletion;
- handlers in dynamically inserted rows;
- scalar, text, visibility, checkbox, and button-property projections;
- focus/caret-preserving input updates;
- async latest-result-wins updates.

Not currently supported at Vue-equivalent capability:

- clearing a keyed collection in one transition;
- keyed reordering or swapping;
- arbitrary class/style/attribute bindings;
- efficient partial updates to many existing rows without replacing them;
- nested/composable patch operations;
- client routing, transitions, or general component lifecycle;
- Fetch and host cancellation.

The comparison immediately found two real defects. Table rows were parsed in
a context-free `template`, which produced no row elements, and the Wasm maximum
memory of 1 MB trapped while constructing 1,000 rows. Contextual parsing and a
16 MB growable maximum fixed those failures without increasing initial Wasm
memory. It also found a severe hydration cost: eagerly rebuilding collection
state for every inserted row made creation take about 140 ms. Lazy collection
initialization reduced it to roughly 11 ms.

The retained DOM model therefore works for the operations listed above. It is
not yet a Vue-capability replacement, and unsupported operations should be
implemented and measured here rather than inferred from counters.
