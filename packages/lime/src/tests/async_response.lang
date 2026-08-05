namespace Tests.AsyncResponse;

using Lime;
using Aster.Html;

private extern Task Task.Delay(int milliseconds);

private async Task<Response> LoadResponseAsync()
{
    await Task.Delay(1);
    return Response.Ok(
        <article><h1>Async Lime response</h1></article>
    );
}

async Task<int> main()
{
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
        case ResponseBody.Html(page): { Console.WriteLine(page.ToHtmlString()); }
        case ResponseBody.Text(text): { return 1; }
        case ResponseBody.Css(text): { return 2; }
        case ResponseBody.Asset(asset): { return 3; }
        case ResponseBody.Stream(stream): { return 4; }
        case ResponseBody.File(file): { return 5; }
    }
    switch (secondBody)
    {
        case ResponseBody.Html(page): { Console.WriteLine(page.ToHtmlString()); }
        case ResponseBody.Text(text): { return 6; }
        case ResponseBody.Css(text): { return 7; }
        case ResponseBody.Asset(asset): { return 8; }
        case ResponseBody.Stream(stream): { return 9; }
        case ResponseBody.File(file): { return 10; }
    }
    return 0;
}
