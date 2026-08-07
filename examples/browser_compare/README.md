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
| create 1,000 keyed rows | 32.7 ms | **11.3 ms** |
| update every tenth row | 43.8 ms | **4.4 ms** |
| swap two rows | 44.3 ms | **2.6 ms** |
| append 1,000 keyed rows | 134.6 ms | **8.0 ms** |
| delete one middle row | 116.0 ms | **4.2 ms** |
| clear 1,999 rows | 7.7 ms | **7.4 ms** |
| client code, raw | **55.7 KB** | 111.9 KB |
| client code, gzip | **15.1 KB** | 41.9 KB |

Treat timings as local smoke measurements, not universal benchmark results.
The native class/snapshot client remains substantially smaller, but this run
exposes a major regression: rendering and planning the complete retained list
after every mutation is much slower than Vue for all operations except roughly
equivalent clear. The benchmark intentionally records that result rather than
retaining the old command-based fast path.

## Capability status

Working and exercised:

- SSR HTML retained during hydration;
- bulk keyed creation and append;
- ordinary `List<T>` update, deletion, clear, and two-key swap followed by a
  native keyed snapshot;
- handlers in dynamically inserted rows;
- inferred scalar text, visibility, checkbox, and button-property parts;
- focus/caret-preserving input updates;
- async latest-result-wins updates.

Not currently supported at Vue-equivalent capability:

- URL-bearing and explicitly controlled form-property bindings;
- compiler-generated sparse structural deltas that avoid rendering and scanning
  the complete list;
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

The retained DOM model preserves identity for the operations listed above, but
it is not yet a Vue-performance replacement. Public keyed command and projection
result types have been deleted. The next optimization must be compiler-internal
mutation-aware structural deltas while application code remains ordinary class
and `List<T>` mutation; reintroducing public DOM commands is not acceptable.
