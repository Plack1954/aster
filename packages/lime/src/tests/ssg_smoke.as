namespace Tests.SsgSmoke;

using Lime;
using Lime.Content;
using Lime.Markdown;
using Lime.Ssg;
using Lime.Static;
using Aster.Html;
using Aster.Interop;
using System.Text;

struct ApplicationOwner
{
    WebApplication Value;
}

~ApplicationOwner()
{
    delete self.Value;
}

private Response home(Request request)
{
    return Results.Html(
        <html>
            <head><title>Lime SSG</title></head>
            <body><h1>Home</h1></body>
        </html>
    );
}

private Response about(Request request)
{
    return Results.Html(<main><h1>About</h1></main>);
}

private Response ArticlePage(Request request)
{
    switch (request.RouteValue("slug"))
    {
        case Option.Some(slug): {
            return Results.Html(<article><h1>{slug}</h1></article>);
        }
        case Option.None: {
            return Results.InternalError(<p>missing route value</p>);
        }
    }
}

private Response robots(Request request)
{
    return Results.Text("User-agent: *\nAllow: /\n");
}

private Response missing(Request request)
{
    return Results.NotFound(<h1>Not found</h1>);
}

private List<string> ArticlePages()
{
    List<string> paths = new();
    paths.Add("/articles/aster/");
    return paths;
}

private Result<int, string> build()
{
    string outputRoot = try NativeProcessArg(0);
    List<ContentDocument> documents = LoadContentDirectory(
        "packages/lime/test_content", ".md"
    );
    StringBuilder discovered = new();
    int documentIndex = 0;
    foreach (ContentDocument document in documents)
    {
        discovered.Append(document.path);
        discovered.Append("\n");
        if (documentIndex == 0)
        {
            string title = document.Required("title");
            List<string> tags = document.Strings("tags");
            if (title != "First" || tags.Count != 2 ||
                !document.body.StartsWith("First **body**."))
            {
                return Result.Err("content document parsed incorrectly");
            }
        }
        documentIndex += 1;
    }
    if (discovered.ToString() !=
        "packages/lime/test_content/a.md\npackages/lime/test_content/b.md\n")
    {
        return Result.Err(
            "content discovery must filter and sort files"
        );
    }
    string markdown = Markdown(
        "Hello **Aster**.\n\n1. One\n2. Two"
    ).ToHtmlString();
    if (markdown !=
        "<p>Hello <strong>Aster</strong>.</p><ol><li>One</li><li>Two</li></ol>")
    {
        return Result.Err("Markdown rendered incorrectly");
    }
    ApplicationOwner appOwner = new()
    {
        Value = WebApplication.Create()
    };
    WebApplication app = appOwner.Value;
    app.MapFallback(missing);
    app.MapGet("/", home);
    app.MapGet("/about/", about);
    app.MapGet("/articles/{slug}/", ArticlePage, ArticlePages);
    app.MapGet("/robots.txt", robots);
    app.Static("/assets/", "packages/lime/test_assets");

    SiteBuild built = try SiteBuild(app, outputRoot);
    if (built.files != 7)
    {
        return Result.Err("expected seven generated files");
    }
    return Result.Ok(0);
}

int main()
{
    switch (build())
    {
        case Result.Ok(status): { return status; }
        case Result.Err(error): {
            Console.WriteLine(error);
            return 1;
        }
    }
}
