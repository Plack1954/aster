namespace Tests.CurrentHttpServer;

using Lime;
using Lime.CurrentHttp;
using Lime.Forms;
using Lime.Static;
using Aster.Html;
using Aster.Net.Http;
using System.IO;
using System.Text;

struct ServerState
{
    string name;
}

private Response article(ServerState state, Request request)
{
    string source = "";
    switch (request.Query("ref"))
    {
        case Result.Ok(found): {
            switch (found)
            {
                case Option.Some(value): {
                    source = value;
                }
                case Option.None: {
                }
            }
        }
        case Result.Err(error): {
            return Results.BadRequest(<p>{error}</p>);
        }
    }
    string slug = "";
    switch (request.RouteValue("slug"))
    {
        case Option.Some(value): { slug = value; }
        case Option.None: {
            return Results.InternalError(<p>missing route value</p>);
        }
    }
    Response response = Results.Html(<article>{slug}:{source}</article>);
    switch (ResponseHeader("X-Lime", "Aster"))
    {
        case Result.Ok(header): {
            response.AddHeader(header);
        }
        case Result.Err(error): {
            return Results.InternalError(<p>{error}</p>);
        }
    }
    return response;
}

private Response submit(ServerState state, Request request)
{
    try
    {
        FormCollection form = request.ReadForm();
        string title = "";
        string kind = "";
        switch (form.Get("title"))
        {
            case Option.Some(value): { title = value; }
            case Option.None: {
                return Results.BadRequest(<p>Missing title</p>);
            }
        }
        switch (form.Get("kind"))
        {
            case Option.Some(value): { kind = value; }
            case Option.None: {
                return Results.BadRequest(<p>Missing kind</p>);
            }
        }
        return Results.Html(<p>{title}:{kind}</p>);
    }
    catch (Exception error)
    {
        return Results.BadRequest(<p>{error.Message}</p>);
    }
}

private Response missing(ServerState state, Request request)
{
    return Results.NotFound(<h1>Missing</h1>);
}

private Response robots(ServerState state, Request request)
{
    return Results.Text(
        "User-agent: *\nDisallow: /private\n"
    );
}

private Response stylesheet(ServerState state, Request request)
{
    return Results.Css(
        "body { color: #e45b20; }\n"
    );
}

private Response feed(ServerState state, Request request)
{
    return Results.Xml("<rss version=\"2.0\"></rss>");
}

private Response mark(ServerState state, Request request)
{
    switch (StaticFile("packages/lime/testdata", request.Path))
    {
        case Result.Ok(response): { return response; }
        case Result.Err(error): {
            return Results.NotFound(<p>{error}</p>);
        }
    }
}

private Response MethodResponse(ServerState state, Request request)
{
    return Results.Text(request.Method);
}

private Response CookieValue(ServerState state, Request request)
{
    switch (request.Cookie("theme"))
    {
        case Option.Some(value): {
            Response response = Results.Text(value);
            switch (ResponseCookie("theme", value))
            {
                case Result.Ok(cookie): {
                    response.AddHeader(cookie);
                    return response;
                }
                case Result.Err(error): {
                    return Results.InternalError(<p>{error}</p>);
                }
            }
        }
        case Option.None: {
            return Results.BadRequest(<p>Missing cookie</p>);
        }
    }
}

private Response HeaderValue(ServerState state, Request request)
{
    switch (request.Header("x-aster-test"))
    {
        case Option.Some(value): {
            return Results.Text(value);
        }
        case Option.None: {
            return Results.BadRequest(<p>Missing header</p>);
        }
    }
}

private Response ConfiguredCookie(ServerState state, Request request)
{
    Option<string> domain = Option.Some("lime.test");
    Option<long> maxAge = Option.Some(3600);
    CookieOptions options = new()
    {
        path = "/account",
        domain = domain,
        maxAge = maxAge,
        httpOnly = false,
        secure = true,
        sameSite = CookieSameSite.Strict
    };
    Response response = Results.Text("configured");
    switch (ResponseCookieWith("theme", "aster", options))
    {
        case Result.Ok(cookie): {
            response.AddHeader(cookie);
            return response;
        }
        case Result.Err(error): {
            return Results.InternalError(<p>{error}</p>);
        }
    }
}

private Response DeletedCookie(ServerState state, Request request)
{
    Response response = Results.Text("deleted");
    switch (ResponseDeleteCookie("theme", CookieOptions()))
    {
        case Result.Ok(cookie): {
            response.AddHeader(cookie);
            return response;
        }
        case Result.Err(error): {
            return Results.InternalError(<p>{error}</p>);
        }
    }
}

private Response JsonEcho(ServerState state, Request request)
{
    switch (request.Json())
    {
        case Result.Ok(body): {
            return Results.Json(201, body);
        }
        case Result.Err(error): {
            return Results.BadRequest(<p>{error}</p>);
        }
    }
}

private Response Problem(ServerState state, Request request)
{
    ProblemDetails problem = ProblemDetails.Create(409, "Conflict");
    problem.Detail = "The article already exists.";
    problem.Instance = request.Path;
    return Results.Problem(problem);
}

private Response StreamedAsset(ServerState state, Request request)
{
    MemoryStream stream = MemoryStream.Create();
    List<byte> first = new();
    first.Add(65);
    first.Add(115);
    first.Add(116);
    first.Add(101);
    first.Add(114);
    stream.Write(first);
    stream.Seek(0, SeekOrigin.Begin);
    Response response = Results.Stream(stream, AssetKind.Binary);
    switch (ResponseHeader("X-Lime-Stream", "yes"))
    {
        case Result.Ok(header): { response.AddHeader(header); }
        case Result.Err(error): {
            return Results.InternalError(<p>{error}</p>);
        }
    }
    return response;
}

private FilterResult RejectFiltered(
    ServerState state,
    Request request
)
{
    if (state.name == "Lime integration" &&
        request.Path == "/filtered")
    {
        return FilterResult.Respond(
            Results.NotFound(<h1>Filtered</h1>)
        );
    }
    return FilterResult.Continue;
}

private Html FrameErrors(
    ServerState state,
    Request request,
    Html page
)
{
    if (state.name == "Lime integration" &&
        (request.Path == "/filtered" || request.Path == "/missing"))
    {
        return <main data-path=request.Path>{page}</main>;
    }
    return page;
}

private int serve(NativeHandle server)
{
    ServerState state = new()
    {
        name = "Lime integration"
    };
    StatefulApp<ServerState> app = StatefulAppNew(state, missing);
    app.UseFilter(RejectFiltered);
    app.AfterHtml(FrameErrors);
    app.MapGet("/articles/{slug}", article);
    app.MapPut("/articles/{slug}", MethodResponse);
    app.MapPatch("/articles/{slug}", MethodResponse);
    app.MapDelete("/articles/{slug}", MethodResponse);
    app.MapGet("/robots.txt", robots);
    app.MapGet("/assets/site.css", stylesheet);
    app.MapGet("/feed.xml", feed);
    app.MapGet("/mark.svg", mark);
    app.MapGet("/cookie", CookieValue);
    app.MapGet("/header", HeaderValue);
    app.MapGet("/cookie-options", ConfiguredCookie);
    app.MapGet("/cookie-delete", DeletedCookie);
    app.MapGet("/head-priority", robots);
    app.MapHead("/head-priority", stylesheet);
    app.MapPost("/submit", submit);
    app.MapPost("/json", JsonEcho);
    app.MapGet("/problem", Problem);
    app.MapGet("/stream", StreamedAsset);

    Console.WriteLine(HttpServerPort(server));
    for (int count = 0; count < 26; count++)
    {
        switch (HttpTryAccept(server))
        {
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
            case Result.Err(error): {
                Console.Error.WriteLine(error);
                return 1;
            }
        }
    }
    return 0;
}

int main()
{
    switch (HttpTryServerOpen(
        "127.0.0.1",
        0,
        8192,
        4096,
        1000,
        1
    ))
    {
        case Result.Ok(server): {
            return serve(server);
        }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
