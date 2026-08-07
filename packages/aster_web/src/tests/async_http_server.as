namespace Tests.AsyncHttpServer;

using Aster.Web;
using Aster.Web.CurrentHttp;
using Aster.Net.Http;

private struct ApplicationOwner
{
    WebApplication Value;
}

~ApplicationOwner()
{
    delete self.Value;
}

private extern Task Task.Delay(int milliseconds);

private async Task<Response> AsyncValue(Request request)
{
    await Task.Delay(1);
    switch (request.RouteValue("id"))
    {
        case Option.Some(id): { return Results.Text($"async:{id}"); }
        case Option.None: { return Results.Text(500, "missing route value"); }
    }
}

private async Task<int> ServeAsync(NativeHandle server, WebApplication app)
{
    Console.WriteLine(HttpServerPort(server));
    switch (HttpTryAccept(server))
    {
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 2;
        }
        case Result.Ok(request): {
            switch (await CurrentHttpDispatchAsync(app, request))
            {
                case Result.Ok(reuse): { return 0; }
                case Result.Err(error): {
                    Console.Error.WriteLine(error);
                    return 3;
                }
            }
        }
    }
}

async Task<int> main()
{
    WebApplication app = WebApplication.Create();
    ApplicationOwner appOwner = new() { Value = app };
    app.MapGet("/async/{id:int}", AsyncValue);
    switch (HttpTryServerOpen(
        "127.0.0.1", 0, 8192, 4096, 1000, 1
    ))
    {
        case Result.Ok(server): {
            return await ServeAsync(server, app);
        }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
