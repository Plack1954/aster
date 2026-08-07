namespace Tests.AsyncSsgSmoke;

using Lime;
using Lime.Ssg;
using Aster.Html;
using Aster.Interop;

private struct ApplicationOwner
{
    WebApplication Value;
}

~ApplicationOwner()
{
    delete self.Value;
}

private extern Task Task.Delay(int milliseconds);

private async Task<Response> HomeAsync(Request request)
{
    await Task.Delay(1);
    return Results.Html(<main><h1>Async static page</h1></main>);
}

private Response Missing(Request request)
{
    return Results.NotFound(<h1>Not found</h1>);
}

async Task<int> main()
{
    string outputRoot = "";
    switch (NativeProcessArg(0))
    {
        case Result.Ok(value): { outputRoot = value; }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    ApplicationOwner appOwner = new()
    {
        Value = WebApplication.Create()
    };
    WebApplication app = appOwner.Value;
    app.MapFallback(Missing);
    app.MapGet("/", HomeAsync);
    switch (await SiteBuildAsync(app, outputRoot))
    {
        case Result.Ok(build): {
            return build.files == 2 ? 0 : 2;
        }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 3;
        }
    }
}
