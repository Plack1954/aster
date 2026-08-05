using Aster.Html;

private extern NativeHandle HttpServerOpen(string address, long port);
private extern long HttpServerPort(NativeHandle server);
private extern long HttpServeOnce(
    NativeHandle server,
    string body
);

private Html page() {
    <section>
        <h2>Aster HTTP server</h2>
        <p>This response was rendered by the bytecode VM.</p>
    </section>
}

int main() {
    NativeHandle server = HttpServerOpen("127.0.0.1", 8080);
    Console.WriteLine(HttpServerPort(server));

    Html document = page();
    string body = document.ToHtmlString();

    // Blocks until one GET or HEAD request has been served.
    Console.WriteLine(HttpServeOnce(server, body));
    return 0;
}
