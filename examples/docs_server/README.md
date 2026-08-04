# Aster documentation server

This Aster 0.2 pressure-test application is split across model, configuration,
page, site, server, render, and test modules. It uses:

- user-defined generic documents and Pair metadata
- generic Aster-written library functions
- typed handler values, routing, and middleware
- native Html and deterministic cleanup
- C-backed RAII file, directory, and socket handles
- configuration tokenization and numeric parsing written in Aster
- documentation-directory traversal and Markdown filtering in Aster
- a CSS asset loaded through file RAII and served with a bounded content type
- a finite integration target used to verify CSS and HTML over one connection
- bounded HTTP request configuration and sequential keep-alive
- manifest binary and test targets

From the repository root:

```sh
./build/lang project run examples/docs_server/aster.toml render
./build/lang project check examples/docs_server/aster.toml server
./build/lang project test examples/docs_server/aster.toml
./build/lang project run examples/docs_server/aster.toml server
```

The server prints its selected port. It remains blocking; each connection is
bounded by timeout and a 100-request cap. See `docs/http.md` for the transport
limits. The configured static asset is available at `/assets/site.css`.
The CTest integration launches the finite target, requests that asset, reuses
the same connection for `/guide`, and verifies both wire responses.
