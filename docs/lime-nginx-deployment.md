# Deploying Lime behind nginx

The normal Lime production boundary is:

```text
Internet -> nginx (TLS and public HTTP) -> Aster/H2O on 127.0.0.1
```

Aster does not manage certificates, TLS policy, public HTTP-to-HTTPS
redirection, or virtual hosting. nginx owns those concerns. The Aster process
embeds H2O for application HTTP, routing, bounded requests, streaming, static
responses, and deterministic connection cleanup.

## Application configuration

Keep H2O on loopback, enable signal handling, and trust only the local nginx
peer:

```aster
using Lime;
using Lime.Forwarding;
using Lime.H2O;

ForwardedHeadersOptions forwarded = ForwardedHeadersOptions();
forwarded.ForwardedHeaders = ForwardedHeaders.All;
forwarded.KnownProxies.Add("127.0.0.1");
app.UseForwardedHeaders(forwarded);

H2OServerOptions serverOptions = H2OServerOptions();
serverOptions.Address = "127.0.0.1";
serverOptions.Port = 8080;
serverOptions.HandleSignals = true;
serverOptions.GracefulShutdownTimeoutMilliseconds = 30000;
```

Do not bind the Aster process to a public address in this deployment mode.
Do not trust arbitrary private networks or forwarded headers from every peer.

nginx and H2O must agree on important limits. The supplied nginx template uses
a 1 MiB request-body limit, matching the `H2OServerOptions` default, and waits
35 seconds for upstream I/O, slightly longer than H2O's default 30-second
timeout and graceful drain.

## Build a relocatable server bundle

Build the pinned H2O dependency and H2O-enabled Aster compiler once:

```sh
./tools/build_h2o.sh
```

Then build an application target:

```sh
./tools/build_lime_h2o_app.sh \
    /path/to/site/aster.toml production_server \
    /path/to/release server
```

The release contains:

```text
release/
|-- server
|-- server.c
`-- lib/
    `-- libh2o-evloop.so.0.16
```

The executable uses a relative `$ORIGIN/lib` runtime search path. It does not
depend on Aster's build directory remaining on the production machine.
SQLite and H2O's ordinary system dependencies must be installed by the host.

## Filesystem layout

A simple release layout is:

```text
/srv/aster/example/
|-- current -> releases/2026-08-04-120000
|-- releases/
|   `-- 2026-08-04-120000/
|       |-- server
|       |-- lib/
|       `-- application assets and content
`-- shared/
    `-- writable application data
```

The application executable and release content remain read-only. Put SQLite
databases, uploads, and other mutable state in `shared` or
`/var/lib/aster/example`. An application-specific environment file at
`/etc/aster/example.env` can provide paths and secrets. Do not store secrets
inside the release directory.

## systemd

Install the supplied template:

```sh
sudo install -D -m 0644 deploy/systemd/aster-site@.service \
    /etc/systemd/system/aster-site@.service
sudo systemctl daemon-reload
sudo systemctl enable --now aster-site@example.service
```

The service runs as the unprivileged `aster` user, grants write access only to
the shared/state directories, restarts after abnormal failure, and sends
SIGTERM on stop or restart. `TimeoutStopSec=40s` gives H2O's default 30-second
graceful drain time to finish before systemd forcefully terminates it.

There is deliberately no `ExecReload`: Aster does not currently reload its
program image in place.

## nginx

Copy `deploy/nginx/aster-site.conf`, replace the domain, upstream name,
certificate paths, and port, then validate before reloading:

```sh
sudo nginx -t
sudo systemctl reload nginx
```

The template overwrites `X-Forwarded-For` with nginx's actual peer address
instead of preserving an untrusted client-supplied chain. Lime accepts those
headers only because the immediate socket peer is the configured loopback
proxy. nginx keeps upstream connections alive and disables response buffering
so Lime streaming responses remain streaming.

nginx certificate provisioning and renewal are host administration tasks, not
Aster application behavior.

## Deploy, restart, and roll back

Build each release into a new timestamped directory. Switch `current` only
after the complete binary, library, content, and assets are present. Then:

```sh
sudo systemctl restart aster-site@example.service
sudo systemctl status aster-site@example.service
```

`restart` sends SIGTERM to the old process. Lime closes its listener, asks H2O
to drain existing connections, and exits normally before systemd starts the
new binary. This is graceful but not currently zero-downtime: there is a brief
interval while the loopback listener changes processes.

To roll back, point `current` at the preceding complete release and restart the
same service. nginx configuration does not change between application
releases.
