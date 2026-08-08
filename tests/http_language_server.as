private extern NativeHandle HttpServerOpen(
    const ref string address, long port);
private extern long HttpServerPort(const ref NativeHandle server);
private extern NativeHandle HttpAccept(const ref NativeHandle server);
private extern string HttpRequestPath(const ref NativeHandle request);
private extern long HttpRespondHtml(
    const ref NativeHandle request,
    long status,
    const ref string body
);
private extern Result<bool, string> HttpTryRespondRedirectReuse(
    const ref NativeHandle request,
    const ref string location
);

private long RouteStatus(const ref string path) {
    if (path == "/health") {
        return 200;
    }
    return 404;
}

private Html route(const ref string path) {
    if (path == "/health") {
        return Html.UnsafeRaw("<strong>healthy</strong>");
    }
    return Html.UnsafeRaw("<h2>Not found</h2>");
}

private void HandleRequest(NativeHandle request) {
    string path = HttpRequestPath(request);
    if (path == "/redirect") {
        var sent = HttpTryRespondRedirectReuse(
            request, "/health"
        );
        switch (sent) {
            case Result.Ok(reuse): {
                return;
            }
            case Result.Err(error): {
                Console.Error.WriteLine(error);
                return;
            }
        }
    }
    long status = RouteStatus(path);
    Html document = route(path);
    string body = document.ToHtmlString();
    HttpRespondHtml(request, status, body);
}

int main() {
    NativeHandle server = HttpServerOpen("127.0.0.1", 0);
    Console.WriteLine(HttpServerPort(server));

    long count = 0;
    while (count < 3) {
        NativeHandle request = HttpAccept(server);
        HandleRequest(request);
        count = count + 1;
    }
    return 0;
}
