using Aster.Html;
using Aster.Net.Http;

private Html page(string path) {
    <main>
        <h1>Aster versus Rust</h1>
        <p>Path: {path}</p>
    </main>
}

private int serve(NativeHandle server) {
    while (true) {
        Result<NativeHandle, string> accepted =
            HttpTryAccept(server);
        switch (accepted) {
            case Result.Ok(request): {
                string path = HttpRequestPath(request);
                String body = page(path).ToHtmlString();
                Result<bool, string> sent =
                    HttpTryRespondHtmlReuse(
                        request, 200, body
                    );
                switch (sent) {
                    case Result.Ok(reuse): {}
                    case Result.Err(error): {}
                }
            }
            case Result.Err(error): {}
        }
    }
    return 0;
}

int main() {
    Result<NativeHandle, string> opened =
        HttpTryServerOpen(
            "127.0.0.1",
            18380,
            16384,
            1024,
            5000,
            1,
        );
    switch (opened) {
        case Result.Ok(server): {
            return serve(server);
        }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
