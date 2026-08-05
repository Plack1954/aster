namespace Tests.AsyncResponse;

using Lime;
using Aster.Html;

struct ApplicationOwner
{
    WebApplication Value;
}

~ApplicationOwner()
{
    delete self.Value;
}

private extern Task Task.Delay(int milliseconds);

private async Task<Response> LoadResponseAsync()
{
    await Task.Delay(1);
    return Results.Html(
        <article><h1>Async Lime response</h1></article>
    );
}

private async Task<Response> HandleAsync(Request request)
{
    await Task.Delay(1);
    switch (request.RouteValue("id"))
    {
        case Option.Some(id): { return Results.Text(id); }
        case Option.None: {
            return Results.InternalError(<h1>Missing route value</h1>);
        }
    }
}

private Response HandleSync(Request request)
{
    return Results.Text("sync");
}

private Response Missing(Request request)
{
    return Results.NotFound(<h1>Missing</h1>);
}

private async Task<Response> HandleMethodAsync(Request request)
{
    await Task.Delay(1);
    return Results.Text(request.Method);
}

private async Task<Response> FailAsync(Request request)
{
    await Task.Delay(1);
    throw new Exception("async endpoint failed");
}

private async Task<Response> AsyncMissing(Request request)
{
    await Task.Delay(1);
    return Results.Text("async fallback");
}

private Response HandleException(Exception error)
{
    return Results.Text(error.Message);
}

private bool ResponseTextEquals(
    Response response,
    int expectedStatus,
    string expectedText
)
{
    (int status, ResponseBody body,
        List<ResponseHeader> headers) = response;
    if (status != expectedStatus) { return false; }
    switch (body)
    {
        case ResponseBody.Text(text): { return text == expectedText; }
        case ResponseBody.Empty: { return false; }
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private async Task<bool> VerifyEndpointDispatchAsync()
{
    WebApplication app = WebApplication.Create();
    ApplicationOwner appOwner = new() { Value = app };
    app.MapFallback(Missing);
    app.OnException(HandleException);
    app.MapGet("/async/{id:int}", HandleAsync);
    app.MapGet("/sync", HandleSync);
    List<string> methods = new();
    methods.Add("POST");
    methods.Add("PUT");
    app.MapMethods("/methods", methods, HandleMethodAsync);
    methods.Add("DELETE");
    app.MapGet("/failure", FailAsync);

    Response handled = await app.DispatchAsync(RequestNew(
        "GET", "/async/42", "", "", "", ""
    ));
    Response syncHandled = await app.DispatchAsync(RequestNew(
        "GET", "/sync", "", "", "", ""
    ));
    Response methodHandled = await app.DispatchAsync(RequestNew(
        "PUT", "/methods", "", "", "", ""
    ));
    Response methodRejected = await app.DispatchAsync(RequestNew(
        "DELETE", "/methods", "", "", "", ""
    ));
    Response failed = await app.DispatchAsync(RequestNew(
        "GET", "/failure", "", "", "", ""
    ));
    WebApplication defaultApp = WebApplication.Create();
    ApplicationOwner defaultOwner = new() { Value = defaultApp };
    Response defaultMissing = await defaultApp.DispatchAsync(RequestNew(
        "GET", "/missing", "", "", "", ""
    ));
    WebApplication fallbackApp = WebApplication.Create();
    ApplicationOwner fallbackOwner = new() { Value = fallbackApp };
    fallbackApp.MapFallback(AsyncMissing);
    Response asyncMissing = await fallbackApp.DispatchAsync(RequestNew(
        "GET", "/missing", "", "", "", ""
    ));

    return ResponseTextEquals(handled, 200, "42") &&
        ResponseTextEquals(syncHandled, 200, "sync") &&
        ResponseTextEquals(methodHandled, 200, "PUT") &&
        methodRejected.StatusCode == StatusCodes.Status405MethodNotAllowed &&
        ResponseTextEquals(failed, 200, "async endpoint failed") &&
        defaultMissing.StatusCode == StatusCodes.Status404NotFound &&
        ResponseTextEquals(asyncMissing, 200, "async fallback");
}

async Task<int> main()
{
    if (!await VerifyEndpointDispatchAsync()) { return 12; }

    Task<Response> pending = LoadResponseAsync();
    Response first = await pending;
    Response second = await pending;

    (int firstStatus, ResponseBody firstBody,
        List<ResponseHeader> firstHeaders) = first;
    (int secondStatus, ResponseBody secondBody,
        List<ResponseHeader> secondHeaders) = second;
    Console.WriteLine(firstStatus);
    Console.WriteLine(secondStatus);
    switch (firstBody)
    {
        case ResponseBody.Empty: { return 11; }
        case ResponseBody.Html(page): { Console.WriteLine(page.ToHtmlString()); }
        case ResponseBody.Text(text): { return 1; }
        case ResponseBody.Css(text): { return 2; }
        case ResponseBody.Asset(asset): { return 3; }
        case ResponseBody.Stream(stream): { return 4; }
        case ResponseBody.File(file): { return 5; }
    }
    switch (secondBody)
    {
        case ResponseBody.Empty: { return 11; }
        case ResponseBody.Html(page): { Console.WriteLine(page.ToHtmlString()); }
        case ResponseBody.Text(text): { return 6; }
        case ResponseBody.Css(text): { return 7; }
        case ResponseBody.Asset(asset): { return 8; }
        case ResponseBody.Stream(stream): { return 9; }
        case ResponseBody.File(file): { return 10; }
    }
    return 0;
}
