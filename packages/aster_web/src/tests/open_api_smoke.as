namespace Tests.OpenApiSmoke;

using Aster.Web;
using Aster.Web.OpenApi;
using System.Text.Json;

private struct ApplicationOwner
{
    WebApplication Value;
}

~ApplicationOwner()
{
    delete self.Value;
}

private Response GetArticle(string id)
{
    return Results.Text(id);
}

private Response UpdateArticle(string id)
{
    return Results.Text(id);
}

private Response Health(const ref Request request)
{
    return Results.Text("ready");
}

private bool RejectsInvalidInfo()
{
    try
    {
        OpenApiInfo invalid = OpenApiInfo("", "1.0.0");
        return false;
    }
    catch (ArgumentException error)
    {
        if (error.Message.Length == 0) { return false; }
    }
    try
    {
        OpenApiInfo invalid = OpenApiInfo("Example", "");
        return false;
    }
    catch (ArgumentException error)
    {
        return error.Message.Length > 0;
    }
}

private bool RejectsOptionalPathParameters()
{
    WebApplication app = WebApplication.Create();
    ApplicationOwner owner = new() { Value = app };
    app.MapGet("/articles/{id?}", GetArticle);
    try
    {
        string document = GenerateOpenApi(
            app.Endpoints, OpenApiInfo("Example", "1.0.0")
        );
        return false;
    }
    catch (InvalidOperationException error)
    {
        return error.Message.Length > 0;
    }
}

int main()
{
    WebApplication app = WebApplication.Create();
    ApplicationOwner owner = new() { Value = app };
    app.MapGet("/articles/{id:int}", GetArticle)
        .WithName("GetArticle")
        .WithDescription("Gets an article")
        .WithTag("articles")
        .Produces(StatusCodes.Status200OK)
        .Produces(StatusCodes.Status404NotFound);
    app.MapPost("/articles/{id}", UpdateArticle)
        .WithName("UpdateArticle")
        .WithTag("articles")
        .Produces(StatusCodes.Status204NoContent);
    app.MapGet("/health", Health);

    string document = GenerateOpenApi(
        app.Endpoints, OpenApiInfo("Example API", "1.0.0")
    );
    JsonElement root = JsonDocument.Parse(document).RootElement;
    if (root.GetProperty("openapi").GetRawText() != "\"3.1.0\"")
    {
        return 1;
    }
    if (root.GetProperty("info").GetProperty("title").GetRawText() !=
        "\"Example API\"")
    {
        return 2;
    }
    JsonElement article = root.GetProperty("paths")
        .GetProperty("/articles/{id}");
    JsonElement get = article.GetProperty("get");
    if (get.GetProperty("operationId").GetRawText() != "\"GetArticle\"" ||
        get.GetProperty("description").GetRawText() != "\"Gets an article\"" ||
        get.GetProperty("tags").GetArrayLength() != 1 ||
        get.GetProperty("parameters").GetArrayLength() != 1 ||
        get.GetProperty("parameters")[0].GetProperty("name").GetRawText() !=
            "\"id\"" ||
        get.GetProperty("responses").GetProperty("404")
            .GetProperty("description").GetRawText() != "\"Not Found\"")
    {
        return 3;
    }
    if (article.GetProperty("post").GetProperty("operationId")
        .GetRawText() != "\"UpdateArticle\"")
    {
        return 4;
    }
    if (root.GetProperty("paths").GetProperty("/health")
        .GetProperty("get").GetProperty("responses").GetProperty("200")
        .GetProperty("description").GetRawText() != "\"OK\"")
    {
        return 5;
    }
    if (!RejectsInvalidInfo()) { return 6; }
    if (!RejectsOptionalPathParameters()) { return 7; }
    return 0;
}
