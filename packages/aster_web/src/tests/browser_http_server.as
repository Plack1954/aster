namespace Tests.BrowserHttpServer;

using Aster.Web;
using Aster.Web.Browser;
using Aster.Web.CurrentHttp;
using Aster.Interop;
using Aster.Html;
using Aster.Net.Http;
using System.Text;
using Tests.BrowserApp;

private class BrowserState
{
    public BrowserAssets assets;

    public BrowserState(BrowserAssets value)
    {
        assets = value;
    }

    public Response Home(Request request)
    {
        BrowserAssets current = assets;
        return Results.Html(BrowserPage(current.loader()));
    }

    public Response BrowserAsset(Request request)
    {
        BrowserAssets current = assets;
        switch (current.serve(request))
        {
            case Result.Ok(response): { return response; }
            case Result.Err(error): {
                return Results.NotFound(<p>{error}</p>);
            }
        }
    }

    public Response FormFallback(Request request)
    {
        switch (request.FormValues())
        {
            case Result.Ok(values): {
                return Results.Html(<p>Saved without WebAssembly.</p>);
            }
            case Result.Err(error): {
                return Results.BadRequest(<p>{error}</p>);
            }
        }
    }
}

private struct BrowserLifetime
{
    WebApplication Application;
    BrowserState State;
}

~BrowserLifetime()
{
    delete self.Application;
    delete self.State;
}

private Response missing(Request request)
{
    return Results.NotFound(<h1>Missing</h1>);
}

private int serve(NativeHandle server, BrowserAssets assets)
{
    BrowserState state = new BrowserState(assets);
    WebApplication app = WebApplication.Create();
    BrowserLifetime lifetime = new()
    {
        Application = app,
        State = state
    };
    Handler home = state.Home;
    Handler asset = state.BrowserAsset;
    Handler formFallback = state.FormFallback;
    app.MapFallback(missing);
    app.MapGet("/", home);
    RouteGroup browser = app.MapGroup("/browser");
    browser.MapGet("/{name}", asset);
    app.MapPost("/contact", formFallback);
    app.MapPost("/todo", formFallback);
    app.MapPost("/message", formFallback);

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
                switch (CurrentHttpDispatch(app, request))
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
        assetDirectory, "/browser", "Aster.Web.BrowserHttpServer"
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
