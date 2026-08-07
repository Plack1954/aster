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
| startup to ready | **17.8 ms** | 20.7 ms |
| post-DOM hydration/mount | 3.8 ms | **0.0 ms** |
| create 1,000 keyed rows | 15.0 ms | **11.8 ms** |
| update every tenth row | 7.2 ms | **5.1 ms** |
| swap two rows | 2.8 ms | **2.7 ms** |
| append 1,000 keyed rows | 13.1 ms | **8.2 ms** |
| delete one middle row | 5.1 ms | **3.5 ms** |
| clear 1,999 rows | 7.5 ms | **7.0 ms** |
| latest async completion | 1.8 ms | **1.3 ms** |
| JS heap delta after 3 create/clear cycles | 98.0 KB | **68.2 KB** |
| JS heap delta after 10 mount/unmount cycles | **112.6 KB** | 148.4 KB |
| active Wasm linear memory | 5.4 MiB | n/a |
| client code, raw | **85.2 KB** | 112.7 KB |
| client code, gzip | **21.8 KB** | 42.2 KB |
| clean Aster web build | 319.7 ms | n/a (direct browser modules) |
| repeated Aster web build | 329.4 ms | n/a (direct browser modules) |

Measurements are medians of seven fresh-page runs in headless Google Chrome on
the same machine, with no discarded warm-up run. Heap figures are taken after
CDP garbage collection; mount/unmount uses ten same-origin iframe lifetimes.
Build timing uses `date` around consecutive `project build-web` invocations.
Vue is served as direct production browser modules and therefore has no
comparable local build step in this fixture. Treat these as local smoke
measurements, not universal benchmark results.
The native class/snapshot client remains substantially smaller. Compiler-private
mutation deltas have removed the earlier order-of-magnitude gaps: keyed reorder
and clear are at parity, delete is within 1.5x, while Vue is 1.4x faster for
sparse update and 1.6x faster for append. The original full-list measurements
remain in repository history as the regression baseline.

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
- general sparse structural deltas for transformed projections and arbitrary
  mutation combinations (these safely use the full snapshot fallback);
- nested/composable patch operations;
- client routing, transitions, or general component lifecycle;
- Fetch and host cancellation.

The comparison immediately found two real defects. Table rows were parsed in
a context-free `template`, which produced no row elements, and the Wasm maximum
memory of 1 MB trapped while constructing 1,000 rows. Contextual parsing and a
16 MB growable maximum fixed those failures without increasing initial Wasm
memory. Repeated create/clear measurement later exposed allocator fragmentation:
the original unsorted free list exhausted that maximum on the second cycle.
Sorted insertion, adjacent-block coalescing, and top-of-heap reuse reduced the
active run to about 7.5 MB and allowed repeated cycles without growth failure.
It also found a severe hydration cost: eagerly rebuilding collection
state for every inserted row made creation take about 140 ms. Lazy collection
initialization reduced it to roughly 11 ms.

The retained DOM model preserves identity for the operations listed above.
Generated code records collection mutations in a private journal; the browser
validates a raw field-to-part mapping before direct scalar updates, computes a
stable subsequence for minimal keyed moves, and emits only the new suffix for
append. Public keyed command and projection result types remain absent, and
application code remains ordinary class and `List<T>` mutation.
