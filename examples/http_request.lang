using Aster.Html;

private extern NativeHandle HttpServerOpen(string address, long port);
private extern NativeHandle HttpAccept(NativeHandle server);
private extern string HttpRequestMethod(NativeHandle request);
private extern string HttpRequestPath(NativeHandle request);
private extern string HttpRequestHeader(
    NativeHandle request,
    string name
);
private extern long HttpRespondHtml(
    NativeHandle request,
    long status,
    string body
);

int main() {
    NativeHandle server = HttpServerOpen("127.0.0.1", 8080);
    NativeHandle request = HttpAccept(server);

    Console.WriteLine(HttpRequestMethod(request));
    Console.WriteLine(HttpRequestPath(request));
    Console.WriteLine(HttpRequestHeader(request, "host"));

    Html document = <h2>Version B response</h2>;
    string body = document.ToHtmlString();
    HttpRespondHtml(request, 200, body);
    return 0;
}
