namespace App.Server;

using Docs.Assets;
using Docs.Config;
using Docs.Site;
using Aster.Net.Http;
using Aster.Web.HttpApp;

public int ServeOpen(
    NativeHandle server,
    long maxConnections
) {
    Console.WriteLine(HttpServerPort(server));

    Router router = ApplicationRouter();
    bool running = true;
    long connectionCount = 0;
    while (running) {
        Result<NativeHandle, string> accepted =
            HttpTryAccept(server);
        switch (accepted) {
            case Result.Ok(request): {
                bool active = true;
                while (active) {
                    string path = HttpRequestPath(request);
                    if (path == "/assets/site.css") {
                        Result<string, string> loaded = LoadAsset(
                            "examples/docs_server/assets/site.css"
                        );
                        switch (loaded) {
                            case Result.Ok(asset): {
                                Result<bool, string> sent =
                                    HttpTryRespondReuse(
                                        request,
                                        200,
                                        "text/css; charset=utf-8",
                                        asset,
                                    );
                                switch (sent) {
                                    case Result.Ok(reuse): {
                                        active = reuse;
                                    }
                                    case Result.Err(error): {
                                        Console.Error.WriteLine(error);
                                        active = false;
                                    }
                                }
                            }
                            case Result.Err(error): {
                                string message =
                                    error;
                                Result<bool, string> sent =
                                    HttpTryRespondReuse(
                                        request,
                                        500,
                                        "text/plain; charset=utf-8",
                                        message,
                                    );
                                switch (sent) {
                                    case Result.Ok(reuse): {
                                        active = reuse;
                                    }
                                    case Result.Err(sendError): {
                                        Console.Error.WriteLine(sendError);
                                        active = false;
                                    }
                                }
                            }
                        }
                    } else {
                        Request input = new() {
                            method = HttpRequestMethod(request),
                            path = path,
                            host = HttpRequestHeader(request, "host"),
                            body = HttpRequestBody(request),
                        };
                        Response response =
                            RouterDispatch(router, input);
                        var sent = ResponseSend(
                            request, response
                        );
                        switch (sent) {
                            case Result.Ok(reuse): {
                                active = reuse;
                            }
                            case Result.Err(error): {
                                Console.Error.WriteLine(error);
                                active = false;
                            }
                        }
                    }
                    if (active) {
                        Result<bool, string> next =
                            HttpTryRequestNext(request);
                        switch (next) {
                            case Result.Ok(available): {
                                active = available;
                            }
                            case Result.Err(error): {
                                Console.Error.WriteLine(error);
                                active = false;
                            }
                        }
                    }
                }
            }
            case Result.Err(error): {
                Console.Error.WriteLine(error);
            }
        }
        connectionCount = connectionCount + 1;
        if (maxConnections > 0) {
            if (connectionCount >= maxConnections) {
                running = false;
            }
        }
    }
    return 0;
}

int main() {
    Result<ServerConfig, string> loaded =
        LoadConfig("examples/docs_server/docs-server.conf");
    switch (loaded) {
        case Result.Ok(config): {
            Result<NativeHandle, string> opened =
                HttpTryServerOpen(
                    config.address,
                    config.port,
                    config.maxHeaderBytes,
                    config.maxBodyBytes,
                    config.timeoutMs,
                    100,
                );
            switch (opened) {
                case Result.Ok(server): {
                    return ServeOpen(server, 0);
                }
                case Result.Err(error): {
                    Console.Error.WriteLine(error);
                    return 1;
                }
            }
        }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
