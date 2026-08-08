# Aster vs Sailfish HTML rendering benchmark

This benchmark compares complete, returned-string rendering through Aster's
generated-C backend and Sailfish's `TemplateSimple::render_once`. It has two
cases with the same dynamic input:

- `escaped`: interpolation in ordinary HTML text, so the five special HTML
  characters are escaped;
- `raw`: interpolation in a raw-text `<script>` element in Aster and an
  explicitly unescaped Sailfish interpolation (`<%- ... %>`).

Each render also interpolates its changing loop index, preventing the compiler
from treating the output as a constant. Each process performs a warmup and then
250,000 renders. The runner reports the
median of seven fresh-process samples. Process startup is therefore amortized,
but allocation and creation of the returned string remain in the measurement.
Both implementations render the same dynamic value and equivalent markup.

Run from the repository root:

```sh
python3 benchmarks/sailfish_compare/run.py
```

The runner uses the local `build/lang`, downloads the pinned Sailfish Git
revision through Cargo, compiles Aster's generated C and Rust with native CPU
optimizations, verifies output byte counts, and prints renders/second and
nanoseconds/render. Override the sample count with `--samples`.
