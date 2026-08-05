namespace Tests.H2OHttpServer;

using Lime;
using Lime.Forms;
using Lime.Forwarding;
using Lime.H2O;
using Lime.Sessions;
using Lime.Static;
using Aster.Html;
using System.IO;

class TestState
{
    private SessionStore Sessions;

    public TestState()
    {
        Sessions = SessionStore.Create();
    }

    public Response SessionValue(Request request)
    {
        SessionStore sessions = Sessions;
        Session session = sessions.Open(request);
        string value = "";
        if (session.IsNew())
        {
            session.SetString("user", "brandon");
            value = "created";
        }
        else
        {
            switch (session.GetString("user"))
            {
                case Option.Some(found): { value = found; }
                case Option.None: { value = "missing"; }
            }
        }
        Response response = Results.Text(value);
        session.Commit(ref response);
        return response;
    }
}

struct ServerLifetime
{
    WebApplication Application;
    TestState State;
}

~ServerLifetime()
{
    delete self.Application;
    delete self.State;
}

private Response Hello(Request request)
{
    string source = "";
    switch (request.Query("from"))
    {
        case Result.Ok(found): {
            switch (found)
            {
                case Option.Some(value): { source = value; }
                case Option.None: { }
            }
        }
        case Result.Err(error): {
            return Results.BadRequest(<p>{error}</p>);
        }
    }
    string name = "";
    switch (request.RouteValue("name"))
    {
        case Option.Some(value): { name = value; }
        case Option.None: {
            return Results.InternalError(<p>missing route value</p>);
        }
    }
    Response response = Results.Html(<p>{name}:{source}</p>);
    switch (ResponseHeader("X-Lime-Adapter", "h2o"))
    {
        case Result.Ok(header): { response.AddHeader(header); }
        case Result.Err(error): {
            return Results.InternalError(<p>{error}</p>);
        }
    }
    return response;
}

private Response Form(Request request)
{
    try
    {
        FormCollection form = request.ReadForm();
        switch (form.Get("title"))
        {
            case Option.Some(title): {
                return Results.Html(<p>{title}</p>);
            }
            case Option.None: {
                return Results.BadRequest(<p>Missing title</p>);
            }
        }
    }
    catch (Exception error)
    {
        return Results.BadRequest(<p>{error.Message}</p>);
    }
}

private Response Upload(Request request)
{
    FormCollection form = request.ReadForm();
    switch (form.Get("title"))
    {
        case Option.Some(title): {
            switch (form.GetFile("image"))
            {
                case Option.Some(file): {
                    return Results.Text($"{title}:{file.FileName}:{file.ContentType}:{file.Length}");
                }
                case Option.None: {
                    return Results.BadRequest(<p>Missing image</p>);
                }
            }
        }
        case Option.None: { return Results.BadRequest(<p>Missing title</p>); }
    }
}

private Response Cookie(Request request)
{
    Response response = Results.Text("cookie");
    switch (ResponseCookie("theme", "aster"))
    {
        case Result.Ok(header): { response.AddHeader(header); }
        case Result.Err(error): { throw new InvalidOperationException(error); }
    }
    return response;
}

private Response Redirect(Request request)
{
    return Results.SeeOther("/hello/Aster?from=redirect");
}

private Response Explode(Request request)
{
    throw new InvalidOperationException("intentional H2O failure");
}

private Response Origin(Request request)
{
    return Results.Text(
        $"{request.Scheme}|{request.Host}|{request.RemoteIpAddress}"
    );
}

private Response HandleException(Exception error)
{
    Response response = Results.Text(500, $"caught:{error.Message}");
    switch (ResponseHeader("X-Lime-Exception", "handled"))
    {
        case Result.Ok(header): { response.AddHeader(header); }
        case Result.Err(headerError): { }
    }
    return response;
}

private Response Stylesheet(Request request)
{
    return Results.Css("body { color: aster; }\n");
}

private Response Binary(Request request)
{
    MemoryStream stream = MemoryStream.Create();
    List<byte> bytes = new();
    bytes.Add(79);
    bytes.Add(114);
    bytes.Add(97);
    bytes.Add(110);
    bytes.Add(103);
    bytes.Add(101);
    stream.Write(bytes);
    stream.Seek(0, SeekOrigin.Begin);
    return Results.Stream(stream, AssetKind.Binary);
}

private Response LargeBinary(Request request)
{
    MemoryStream stream = MemoryStream.Create();
    List<byte> bytes = new();
    for (int index = 0; index < 70000; index++)
    {
        bytes.Add(97);
    }
    bytes.Add(115);
    bytes.Add(116);
    bytes.Add(114);
    bytes.Add(101);
    bytes.Add(97);
    bytes.Add(109);
    bytes.Add(45);
    bytes.Add(116);
    bytes.Add(97);
    bytes.Add(105);
    bytes.Add(108);
    stream.Write(bytes);
    stream.Seek(0, SeekOrigin.Begin);
    return Results.Stream(stream, AssetKind.Binary);
}

private Response DisconnectBinary(Request request)
{
    return Results.Stream(File.OpenRead("/dev/zero"), AssetKind.Binary);
}

private Response Missing(Request request)
{
    return Results.NotFound(<h1>Missing</h1>);
}

private int Serve(NativeHandle server)
{
    TestState state = new TestState();
    WebApplication app = WebApplication.Create();
    ServerLifetime lifetime = new()
    {
        Application = app,
        State = state
    };
    Handler sessionValue = state.SessionValue;
    app.MapFallback(Missing);
    ForwardedHeadersOptions forwarded = ForwardedHeadersOptions();
    forwarded.ForwardedHeaders = ForwardedHeaders.All;
    forwarded.KnownProxies.Add("127.0.0.1");
    forwarded.KnownProxies.Add("::1");
    app.UseForwardedHeaders(forwarded);
    app.OnException(HandleException);
    app.MapGet("/hello/{name}", Hello);
    app.MapPost("/form", Form);
    app.MapPost("/upload", Upload);
    app.MapGet("/cookie", Cookie);
    app.MapGet("/session", sessionValue);
    app.MapGet("/redirect", Redirect);
    app.MapGet("/explode", Explode);
    app.MapGet("/origin", Origin);
    app.MapGet("/head", Stylesheet);
    app.MapGet("/binary", Binary);
    app.MapGet("/large", LargeBinary);
    app.MapGet("/disconnect", DisconnectBinary);
    StaticFileOptions staticOptions = StaticFileOptions();
    staticOptions.MaxAgeSeconds = 3600;
    app.Static(
        "/files/", "packages/lime/test_assets", staticOptions
    );

    switch (H2OBind(server, app))
    {
        case Result.Ok(bound): { }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    Console.WriteLine(H2OServerPort(server));
    switch (H2OServe(server, app))
    {
        case Result.Ok(stopped): { return 0; }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}

int main()
{
    H2OServerOptions options = H2OServerOptions();
    options.Port = 0;
    options.MaxRequestBodySize = 8192;
    options.RequestTimeoutMilliseconds = 200;
    options.GracefulShutdownTimeoutMilliseconds = 1000;
    switch (H2OTryServerOpen(options))
    {
        case Result.Ok(server): { return Serve(server); }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
