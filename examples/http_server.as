using Aster.Html;
using Aster.Net.Http;
using Aster.Web.Middleware;
using Aster.Web.Router;

private Html home(const ref string path) {
    return <section>
        <h2>Aster HTTP server</h2>
        <p>{path}</p>
    </section>;
}

private Html health(const ref string path) {
    return <strong>healthy</strong>;
}

private Html NotFound(const ref string path) {
    return <section>
        <h2>Not found</h2>
        <p>{path}</p>
    </section>;
}

private Html frame(Html page) {
    return <div>{page}</div>;
}

private Html IdentityMiddleware(Html page) {
    return page;
}

private long RouteStatus(const ref string path) {
    if (path == "/") {
        return 200;
    }
    if (path == "/health") {
        return 200;
    }
    return 404;
}

private Html RenderRequest(
    const ref string path,
    const ref Router router,
    MiddlewareChain middleware
) {
    Html routed = RouterDispatch(router, path);
    return MiddlewareApply(middleware, routed);
}

int main() {
    NativeHandle server = HttpServerOpenKeepAlive(
        "127.0.0.1",
        0,
        16384,
        1048576,
        5000,
        100,
    );
    Console.WriteLine("listening on 127.0.0.1, port:");
    Console.WriteLine(HttpServerPort(server));

    Router router = RouterNew(NotFound);
    router = RouterAdd(router, "/", home);
    router = RouterAdd(router, "/health", health);
    MiddlewareChain middleware =
        MiddlewareChain(frame, IdentityMiddleware);

    while (true) {
        NativeHandle request = HttpAccept(server);
        bool active = true;
        while (active) {
            string method = HttpRequestMethod(request);
            string path = HttpRequestPath(request);
            string host = HttpRequestHeader(request, "host");
            string body = HttpRequestBody(request);
            Console.WriteLine(method);
            Console.WriteLine(host);
            Console.WriteLine(body);

            long status = RouteStatus(path);
            Html document =
                RenderRequest(path, router, middleware);
            string rendered = document.ToHtmlString();
            active = HttpRespondHtmlReuse(
                request,
                status,
                rendered,
            );
            if (active) {
                active = HttpRequestNext(request);
            }
        }
    }
    return 0;
}
