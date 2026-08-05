namespace Tests.BrowserHttpServer;

using Lime;
using Lime.Browser;
using Lime.CurrentHttp;
using Aster.Interop;
using Aster.Html;
using Aster.Net.Http;
using System.Text;
using Tests.BrowserApp;

struct BrowserState
{
    BrowserAssets assets;
}

private Response home(BrowserState state, Request request)
{
    return Response.Ok(BrowserPage(state.assets.loader()));
}

private Response BrowserAsset(BrowserState state, Request request)
{
    switch (state.assets.serve(request))
    {
        case Result.Ok(response): { return response; }
        case Result.Err(error): {
            return Response.NotFound(<p>{error}</p>);
        }
    }
}

private Response FormFallback(BrowserState state, Request request)
{
    switch (request.FormValues())
    {
        case Result.Ok(values): {
            return Response.Ok(<p>Saved without WebAssembly.</p>);
        }
        case Result.Err(error): {
            return Response.BadRequest(<p>{error}</p>);
        }
    }
}

private Response missing(BrowserState state, Request request)
{
    return Response.NotFound(<h1>Missing</h1>);
}

private int serve(NativeHandle server, BrowserAssets assets)
{
    BrowserState state = new() { assets = assets };
    StatefulApp<BrowserState> app = StatefulAppNew(state, missing);
    app.Get("/", home);
    app.Get("/browser/:name", BrowserAsset);
    app.post("/contact", FormFallback);
    app.post("/todo", FormFallback);
    app.post("/message", FormFallback);

    Console.WriteLine(HttpServerPort(server));
    for (int count = 0; count < 5; count++)
    {
        switch (HttpTryAccept(server))
        {
            case Result.Err(error): {
                Console.Error.WriteLine(error);
                return 1;
            }
            case Result.Ok(request): {
                switch (CurrentHttpDispatchStateful(app, request))
                {
                    case Result.Ok(reuse): {
                    }
                    case Result.Err(error): {
                        Console.Error.WriteLine(error);
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

int main()
{
    string assetDirectory = "";
    switch (NativeProcessEnvironment("ASTER_BROWSER_ASSET_DIR"))
    {
        case Result.Ok(value): { assetDirectory = value; }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
    switch (BrowserAssets(
        assetDirectory, "/browser", "browser_http_server"
    ))
    {
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
        case Result.Ok(assets): {
            switch (HttpTryServerOpen(
                "127.0.0.1", 0, 8192, 4096, 1000, 1
            ))
            {
                case Result.Ok(server): {
                    return serve(server, assets);
                }
                case Result.Err(error): {
                    Console.Error.WriteLine(error);
                    return 1;
                }
            }
        }
    }
}
