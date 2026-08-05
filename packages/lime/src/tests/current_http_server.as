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
    switch (request.query("ref"))
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
            return Response.BadRequest(<p>{error}</p>);
        }
    }
    Response response = Response.Ok(
        <article>{request.param("slug")}:{source}</article>
    );
    switch (ResponseHeader("X-Lime", "Aster"))
    {
        case Result.Ok(header): {
            response.AddHeader(header);
        }
        case Result.Err(error): {
            return Response.InternalError(<p>{error}</p>);
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
                return Response.BadRequest(<p>Missing title</p>);
            }
        }
        switch (form.Get("kind"))
        {
            case Option.Some(value): { kind = value; }
            case Option.None: {
                return Response.BadRequest(<p>Missing kind</p>);
            }
        }
        return Response.Ok(<p>{title}:{kind}</p>);
    }
    catch (Exception error)
    {
        return Response.BadRequest(<p>{error.Message}</p>);
    }
}

private Response missing(ServerState state, Request request)
{
    return Response.NotFound(<h1>Missing</h1>);
}

private Response robots(ServerState state, Request request)
{
    return Response.Text(
        "User-agent: *\nDisallow: /private\n"
    );
}

private Response stylesheet(ServerState state, Request request)
{
    return Response.Css(
        "body { color: #e45b20; }\n"
    );
}

private Response feed(ServerState state, Request request)
{
    return Response.Xml("<rss version=\"2.0\"></rss>");
}

private Response mark(ServerState state, Request request)
{
    switch (StaticFile("packages/lime/testdata", request.path))
    {
        case Result.Ok(response): { return response; }
        case Result.Err(error): {
            return Response.NotFound(<p>{error}</p>);
        }
    }
}

private Response MethodResponse(ServerState state, Request request)
{
    return Response.Text(request.method);
}

private Response CookieValue(ServerState state, Request request)
{
    switch (request.cookie("theme"))
    {
        case Option.Some(value): {
            Response response = Response.Text(value);
            switch (ResponseCookie("theme", value))
            {
                case Result.Ok(cookie): {
                    response.AddHeader(cookie);
                    return response;
                }
                case Result.Err(error): {
                    return Response.InternalError(<p>{error}</p>);
                }
            }
        }
        case Option.None: {
            return Response.BadRequest(<p>Missing cookie</p>);
        }
    }
}

private Response HeaderValue(ServerState state, Request request)
{
    switch (request.header("x-aster-test"))
    {
        case Option.Some(value): {
            return Response.Text(value);
        }
        case Option.None: {
            return Response.BadRequest(<p>Missing header</p>);
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
    Response response = Response.Text("configured");
    switch (ResponseCookieWith("theme", "aster", options))
    {
        case Result.Ok(cookie): {
            response.AddHeader(cookie);
            return response;
        }
        case Result.Err(error): {
            return Response.InternalError(<p>{error}</p>);
        }
    }
}

private Response DeletedCookie(ServerState state, Request request)
{
    Response response = Response.Text("deleted");
    switch (ResponseDeleteCookie("theme", CookieOptions()))
    {
        case Result.Ok(cookie): {
            response.AddHeader(cookie);
            return response;
        }
        case Result.Err(error): {
            return Response.InternalError(<p>{error}</p>);
        }
    }
}

private Response JsonEcho(ServerState state, Request request)
{
    switch (request.json())
    {
        case Result.Ok(body): {
            return Response.JsonStatus(201, body);
        }
        case Result.Err(error): {
            return Response.BadRequest(<p>{error}</p>);
        }
    }
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
    Response response = Response.Stream(stream, AssetKind.Binary);
    switch (ResponseHeader("X-Lime-Stream", "yes"))
    {
        case Result.Ok(header): { response.AddHeader(header); }
        case Result.Err(error): {
            return Response.InternalError(<p>{error}</p>);
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
        request.path == "/filtered")
    {
        return FilterResult.Respond(
            Response.NotFound(<h1>Filtered</h1>)
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
        (request.path == "/filtered" || request.path == "/missing"))
    {
        return <main data-path=request.path>{page}</main>;
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
    app.filter(RejectFiltered);
    app.AfterHtml(FrameErrors);
    app.Get("/articles/:slug", article);
    app.put("/articles/:slug", MethodResponse);
    app.patch("/articles/:slug", MethodResponse);
    app.delete("/articles/:slug", MethodResponse);
    app.Get("/robots.txt", robots);
    app.Get("/assets/site.css", stylesheet);
    app.Get("/feed.xml", feed);
    app.Get("/mark.svg", mark);
    app.Get("/cookie", CookieValue);
    app.Get("/header", HeaderValue);
    app.Get("/cookie-options", ConfiguredCookie);
    app.Get("/cookie-delete", DeletedCookie);
    app.Get("/head-priority", robots);
    app.head("/head-priority", stylesheet);
    app.post("/submit", submit);
    app.post("/json", JsonEcho);
    app.Get("/stream", StreamedAsset);

    Console.WriteLine(HttpServerPort(server));
    for (int count = 0; count < 22; count++)
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
