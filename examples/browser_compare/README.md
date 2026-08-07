# Aster retained DOM versus Vue

This is a deliberately narrow, reproducible client comparison. Both clients
create 1,000 keyed table rows, update every tenth row, swap two rows, append
1,000 rows, delete one middle row, and clear the collection in headless Chrome.
The rows and controls are equivalent. It is not a claim that this is a complete
framework benchmark.

Run:

```sh
examples/browser_compare/build.sh
```

A representative local run after fixing the failures exposed by this example:

| operation | Aster retained DOM | Vue 3.5 runtime |
|---|---:|---:|
| create 1,000 keyed rows | 11.3 ms | **9.7 ms** |
| update every tenth row | **2.6 ms** | 4.2 ms |
| swap two rows | **0.4 ms** | 3.0 ms |
| append 1,000 keyed rows | 13.7 ms | **8.2 ms** |
| delete one middle row | **0.5 ms** | 4.0 ms |
| clear 1,999 rows | **5.2 ms** | 7.2 ms |
| client code, raw | **44.4 KB** | 111.9 KB |
| client code, gzip | **11.3 KB** | 41.9 KB |

Treat timings as local smoke measurements, not universal benchmark results.
The useful result is that the retained implementation is in the same range for
bulk creation, faster for direct update/swap/delete/clear, and substantially
smaller. It is **not consistently faster than Vue**: Vue won both bulk create
and append in this run.

## Capability status

Working and exercised:

- SSR HTML retained during hydration;
- bulk keyed creation and append;
- sparse keyed replacement, direct deletion, clear, and two-key swap;
- handlers in dynamically inserted rows;
- scalar, text, visibility, checkbox, and button-property projections;
- focus/caret-preserving input updates;
- async latest-result-wins updates.

Not currently supported at Vue-equivalent capability:

- arbitrary class/style/attribute bindings;
- sparse updates that preserve the identity and browser-owned state of every
  updated row rather than replacing those rows;
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
not yet a Vue-capability replacement. Clear and swap fit as small explicit
keyed operations, but selection exposes the current design boundary: changing
a selected class while also persisting the selected key requires either another
one-off command type or composable/nested patches, which the flat result ABI
does not support. Adding more nominal command types is not an elegant path.
The next design step must be composable typed projections or a checked patch
list; until that exists, arbitrary Vue-style bindings remain unsupported.
