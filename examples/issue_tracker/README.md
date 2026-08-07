# Aster Issue Tracker

A small SQLite-backed, server-rendered Aster application.

```text
./build/aster run --project examples/issue_tracker/Render.asproj
./build/aster run --project examples/issue_tracker/Proof.asproj
./build/aster run --project examples/issue_tracker/IntegrationServer.asproj
./build/aster test examples/issue_tracker/Tests.asproj
./build/aster run --project examples/issue_tracker/Server.asproj
```

The server listens on `127.0.0.1:8081` and stores application data in
`examples/issue_tracker/issues.db`.

The `proof` target is the Aster 0.3 application pressure test. In one
generated-C-compatible program it loads and validates configuration, reads
seed records through a deliberately small reusable buffer, validates
application input, creates and queries an isolated SQLite database, dispatches
a typed route, and renders typed HTML. It is exercised through both the VM and
the primary C backend under sanitizers.

The `integration_server` target is deliberately bounded to five connections
for automated VM/generated-C comparison. Its harness performs SQLite-backed
GET and POST requests, route-parameter lookup, form decoding, redirects, and
typed HTML rendering, then verifies oversized and malformed request rejection
before checking deterministic process cleanup.

Routes:

- `GET /issues`
- `GET /issues/new`
- `GET /issues/:id`
- `POST /issues`
- `POST /issues/:id/close`

The interface deliberately follows a compact, conventional 2010-era style:
literal `font-family: sans-serif`, 12–13px text, blue underlined links, gray
panels, bordered tables, and ordinary server-rendered forms.
