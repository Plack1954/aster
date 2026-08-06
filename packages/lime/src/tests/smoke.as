namespace Tests.Smoke;

using Lime;
using Lime.Routing;
using Lime.Forms;
using Lime.Forwarding;
using Lime.Content;
using Lime.Markdown;
using Lime.Static;
using Aster.Html;
using System.Text;

struct ApplicationOwner
{
    WebApplication Value;
}

~ApplicationOwner()
{
    delete self.Value;
}

private ApplicationOwner NewApplication()
{
    return new() { Value = WebApplication.Create() };
}

private Response home(Request request)
{
    return Results.Html(
        <section>
            <h1>Lime</h1>
            <p>Small, explicit, and written in Aster.</p>
        </section>
    );
}

private Response article(Request request)
{
    switch (request.RouteValue("slug"))
    {
        case Option.Some(slug): {
            return Results.Html(<article>{slug}</article>);
        }
        case Option.None: {
            return Results.InternalServerError(<p>missing route value</p>);
        }
    }
}

private Response create(Request request)
{
    return Results.SeeOther("/");
}

private Response search(Request request)
{
    switch (request.QueryValues())
    {
        case Result.Ok(query): {
            string page = "first";
            string sort = "default";
            switch (query.Get("page"))
            {
                case Option.Some(value): {
                    page = value;
                }
                case Option.None: {
                }
            }
            switch (query.Get("sort"))
            {
                case Option.Some(value): {
                    sort = value;
                }
                case Option.None: {
                }
            }
            if (query.Count() != 2)
            {
                return Results.BadRequest(<p>unexpected query</p>);
            }
            return Results.Html(
                <p>{page}:{sort}</p>
            );
        }
        case Result.Err(error): {
            return Results.BadRequest(<p>{error}</p>);
        }
    }
}

private Response missing(Request request)
{
    return Results.NotFound(
        <section><h1>Not found</h1></section>
    );
}

private Response broken(Request request)
{
    throw new Exception("route failed");
}

private Response HandleRouteException(Exception error)
{
    return Results.Text(503, error.Message);
}

private bool ExceptionBoundaryWorks()
{
    ApplicationOwner appOwner = NewApplication();
    WebApplication app = appOwner.Value;
    app.MapFallback(missing);
    app.OnException(HandleRouteException);
    app.MapGet("/broken", broken);
    Response response = app.Dispatch(request("GET", "/broken"));
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != 503) { return false; }
    switch (body)
    {
        case ResponseBody.Text(text): { return text == "route failed"; }
        case ResponseBody.Empty: { return false; }
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private bool ContentLibrariesWork()
{
    switch (TryLoadContentDirectory("packages/lime/test_content", ".md"))
    {
        case Result.Err(error): { return false; }
        case Result.Ok(documents): {
            if (documents.Count != 2) { return false; }
            int index = 0;
            foreach (ContentDocument document in documents)
            {
                if (index == 0)
                {
                    switch (document.TryRequired("title"))
                    {
                        case Result.Ok(title): {
                            if (title != "First") { return false; }
                        }
                        case Result.Err(error): { return false; }
                    }
                    switch (document.TryStrings("tags"))
                    {
                        case Result.Ok(tags): {
                            if (tags.Count != 2) { return false; }
                        }
                        case Result.Err(error): { return false; }
                    }
                }
                index += 1;
            }
        }
    }
    if (Markdown("Hello **Aster**.").ToHtmlString() !=
        "<p>Hello <strong>Aster</strong>.</p>")
    {
        return false;
    }
    return Markdown("### Gratitude & **Reflection**").ToHtmlString() ==
        "<h3>Gratitude &amp; <strong>Reflection</strong></h3>";
}

class SiteState
{
    private string Title;

    public SiteState(string title)
    {
        Title = title;
    }

    public Response Home(Request request)
    {
        return Results.Html(<h1>{Title}</h1>);
    }

    public Response Missing(Request request)
    {
        return Results.NotFound(<p>{Title}:missing</p>);
    }

    public Response Article(string slug)
    {
        return Results.Text($"{Title}:{slug}");
    }
}

struct SiteStateOwner
{
    SiteState Value;
}

~SiteStateOwner()
{
    delete self.Value;
}

private void RegisterDynamicRoute(
    WebApplication app,
    Handler handler
)
{
    string path = "/dynamic";
    app.MapGet(path, handler);
}

private Request request(string method, string path)
{
    return RequestNew(method, path, "example.test", "", "", "");
}

private bool RejectsMalformedValues()
{
    switch (UrlValuesParse("first=ready&broken=%GG"))
    {
        case Result.Ok(values): {
            return false;
        }
        case Result.Err(error): {
            return true;
        }
    }
}

private bool RejectsUnsafeResponseMetadata()
{
    try
    {
        Response invalidBody = Results.Text(204, "not allowed");
        return false;
    }
    catch (ArgumentException error)
    {
        if (error.Message.Length == 0) { return false; }
    }
    switch (ResponseHeader("Content-Length", "4"))
    {
        case Result.Ok(header): { return false; }
        case Result.Err(error): {
        }
    }
    switch (ResponseHeader("X-Test", "safe\r\nInjected: yes"))
    {
        case Result.Ok(header): { return false; }
        case Result.Err(error): {
        }
    }
    switch (ResponseCookie("session", "bad;value"))
    {
        case Result.Ok(cookie): { return false; }
        case Result.Err(error): { return true; }
    }
}

private bool HttpPrimitivesWork()
{
    Request withHeaders = RequestNewWithHeaders(
        "GET", "/", "example.test", "", "", "",
        "X-Test\0one\0x-test\0two\0"
    );
    switch (withHeaders.Header("X-TEST"))
    {
        case Option.Some(value): {
            if (value != "one") { return false; }
        }
        case Option.None: { return false; }
    }

    Request jsonRequest = RequestNew(
        "POST", "/", "example.test",
        "Application/Json; charset=utf-8", "", "{\"ok\":true}"
    );
    switch (jsonRequest.Json())
    {
        case Result.Ok(body): {
            if (body != "{\"ok\":true}") { return false; }
        }
        case Result.Err(error): { return false; }
    }

    CookieOptions cookieSettings = CookieOptions();
    cookieSettings.secure = false;
    cookieSettings.sameSite = CookieSameSite.None;
    switch (ResponseCookieWith("session", "value", cookieSettings))
    {
        case Result.Ok(cookie): { return false; }
        case Result.Err(error): {
        }
    }

    Response created = Results.Json(
        201, "{\"created\":true}"
    );
    (int status, ResponseBody body,
    List<ResponseHeader> headers) = created;
    if (status != 201) { return false; }
    switch (body)
    {
        case ResponseBody.Asset(asset): {
            switch (asset.kind)
            {
                case AssetKind.Json: { return true; }
                case AssetKind.ProblemJson: { return false; }
                case AssetKind.JavaScript: { return false; }
                case AssetKind.Xml: { return false; }
                case AssetKind.Svg: { return false; }
                case AssetKind.Png: { return false; }
                case AssetKind.Jpeg: { return false; }
                case AssetKind.Gif: { return false; }
                case AssetKind.WebP: { return false; }
                case AssetKind.Icon: { return false; }
                case AssetKind.Woff: { return false; }
                case AssetKind.Woff2: { return false; }
                case AssetKind.Ttf: { return false; }
                case AssetKind.Wasm: { return false; }
                case AssetKind.Binary: { return false; }
            }
        }
        case ResponseBody.Empty: { return false; }
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Text(text): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private bool RequestPathNormalizationWorks()
{
    Request decoded = RequestNew(
        "GET",
        "/a/%2E%2E/users/Ada%20Lovelace/%E2%82%AC?view=full%20page",
        "example.test", "", "", ""
    );
    if (decoded.Path != "/users/Ada Lovelace/€" ||
        decoded.Target !=
            "/a/%2E%2E/users/Ada%20Lovelace/%E2%82%AC?view=full%20page" ||
        decoded.QueryString != "view=full%20page")
    {
        return false;
    }

    Request reserved = RequestNew(
        "GET", "/files/a%2Fb+plus/%FF/%2", "example.test", "", "", ""
    );
    if (reserved.Path != "/files/a%2Fb+plus/%FF/%2")
    {
        return false;
    }

    try
    {
        Request invalid = RequestNew(
            "GET", "/invalid/%00", "example.test", "", "", ""
        );
        return false;
    }
    catch (ArgumentException error)
    {
        return error.Message.Length > 0;
    }
}

private bool ProblemResultsWork()
{
    ProblemDetails problem = ProblemDetails.Create(409, "Conflict");
    problem.Type = "https://lime.test/problems/conflict";
    problem.Detail = "The article already exists.";
    problem.Instance = "/articles/aster";
    Response response = Results.Problem(problem);
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != 409) { return false; }
    switch (body)
    {
        case ResponseBody.Asset(asset): {
            (string bytes, AssetKind kind) = asset;
            return kind == AssetKind.ProblemJson && bytes ==
                "{\"type\":\"https://lime.test/problems/conflict\",\"title\":\"Conflict\",\"status\":409,\"detail\":\"The article already exists.\",\"instance\":\"/articles/aster\"}";
        }
        case ResponseBody.Empty: { return false; }
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Text(text): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private Response Origin(Request request)
{
    return Results.Text(
        $"{request.Scheme}|{request.Host}|{request.RemoteIpAddress}"
    );
}

private bool ForwardedHeadersAreTrustedExplicitly()
{
    ApplicationOwner appOwner = NewApplication();
    WebApplication app = appOwner.Value;
    app.MapFallback(missing);
    app.MapGet("/origin", Origin);
    ForwardedHeadersOptions options = ForwardedHeadersOptions();
    options.ForwardedHeaders = ForwardedHeaders.All;
    options.KnownProxies.Add("127.0.0.1");
    app.UseForwardedHeaders(options);

    string forwarded = "X-Forwarded-For\0198.51.100.20\0X-Forwarded-Host\0aster.example\0X-Forwarded-Proto\0https\0";
    Request trusted = RequestNewTransport(
        "GET", "/origin", "internal.test", "http", "127.0.0.1",
        "", "", "", forwarded
    );
    Response trustedResponse = app.Dispatch(trusted);
    (int trustedStatus, ResponseBody trustedBody,
    List<ResponseHeader> trustedHeaders) = trustedResponse;
    if (trustedStatus != 200) { return false; }
    switch (trustedBody)
    {
        case ResponseBody.Text(text): {
            if (text != "https|aster.example|198.51.100.20")
            {
                return false;
            }
        }
        case ResponseBody.Empty: { return false; }
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }

    Request untrusted = RequestNewTransport(
        "GET", "/origin", "internal.test", "http", "203.0.113.9",
        "", "", "", forwarded
    );
    Response untrustedResponse = app.Dispatch(untrusted);
    (int untrustedStatus, ResponseBody untrustedBody,
    List<ResponseHeader> untrustedHeaders) = untrustedResponse;
    if (untrustedStatus != 200) { return false; }
    switch (untrustedBody)
    {
        case ResponseBody.Text(text): {
            return text == "http|internal.test|203.0.113.9";
        }
        case ResponseBody.Empty: { return false; }
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private bool StaticAssetsWork()
{
    switch (StaticFile("packages/lime/testdata", "/../README.md"))
    {
        case Result.Ok(response): { return false; }
        case Result.Err(error): {
        }
    }
    switch (StaticFile("packages/lime/testdata", "/mark.svg"))
    {
        case Result.Err(error): { return false; }
        case Result.Ok(response): {
            (int status, ResponseBody body,
           List<ResponseHeader> headers) = response;
            if (status != 200)
            {
                return false;
            }
            switch (body)
            {
                case ResponseBody.Asset(asset): {
                    (string bytes, AssetKind kind) = asset;
                    if (bytes.Length == 0)
                    {
                        return false;
                    }
                    switch (kind)
                    {
                        case AssetKind.Svg: { return true; }
                        case AssetKind.JavaScript: { return false; }
                        case AssetKind.Json: { return false; }
                        case AssetKind.ProblemJson: { return false; }
                        case AssetKind.Xml: { return false; }
                        case AssetKind.Png: { return false; }
                        case AssetKind.Jpeg: { return false; }
                        case AssetKind.Gif: { return false; }
                        case AssetKind.WebP: { return false; }
                        case AssetKind.Icon: { return false; }
                        case AssetKind.Woff: { return false; }
                        case AssetKind.Woff2: { return false; }
                        case AssetKind.Ttf: { return false; }
                        case AssetKind.Wasm: { return false; }
                        case AssetKind.Binary: { return false; }
                    }
                }
                case ResponseBody.Empty: { return false; }
                case ResponseBody.Html(page): { return false; }
                case ResponseBody.Text(text): { return false; }
                case ResponseBody.Css(text): { return false; }
                case ResponseBody.Stream(stream): { return false; }
                case ResponseBody.File(file): { return false; }
            }
        }
    }
}

private bool StaticCssWorks()
{
    switch (StaticFile("packages/lime/testdata", "/site.css"))
    {
        case Result.Err(error): { return false; }
        case Result.Ok(response): {
            (int status, ResponseBody body,
             List<ResponseHeader> headers) = response;
            if (status != 200) { return false; }
            switch (body)
            {
                case ResponseBody.Css(text): {
                    return text == "body { color: black; }\n";
                }
                case ResponseBody.Empty: { return false; }
                case ResponseBody.Html(page): { return false; }
                case ResponseBody.Text(text): { return false; }
                case ResponseBody.Asset(asset): { return false; }
                case ResponseBody.Stream(stream): { return false; }
                case ResponseBody.File(file): { return false; }
            }
        }
    }
}

private bool StaticDirectoriesWork()
{
    ApplicationOwner appOwner = NewApplication();
    WebApplication app = appOwner.Value;
    app.MapFallback(missing);
    switch (app.TryStatic("/assets/", "packages/lime/testdata"))
    {
        case Result.Err(error): { return false; }
        case Result.Ok(registered): {
        }
    }
    Response response = app.Dispatch(request("GET", "/assets/site.css"));
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != 200) { return false; }
    switch (body)
    {
        case ResponseBody.Css(text): {
            return text == "body { color: black; }\n";
        }
        case ResponseBody.Empty: { return false; }
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Text(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private bool StaticCachePolicyWorks()
{
    StaticFileOptions options = StaticFileOptions();
    options.MaxAgeSeconds = 60;
    options.Immutable = true;
    ApplicationOwner appOwner = NewApplication();
    WebApplication app = appOwner.Value;
    app.MapFallback(missing);
    app.Static("/assets/", "packages/lime/testdata", options);
    Response response = app.Dispatch(request("GET", "/assets/site.css"));
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != 200 || headers.Count != 1) { return false; }
    foreach (ResponseHeader header in headers)
    {
        if (header.Name != "Cache-Control" ||
            header.Value != "public, max-age=60, immutable")
        {
            return false;
        }
    }

    StaticFileOptions invalid = StaticFileOptions();
    invalid.Immutable = true;
    switch (app.TryStatic("/bad/", "packages/lime/testdata", invalid))
    {
        case Result.Ok(registered): { return false; }
        case Result.Err(error): { return true; }
    }
}

private bool MultipartFormsWork()
{
    StringBuilder uploadBody = new();
    uploadBody.Append("--lime-boundary\r\n");
    uploadBody.Append(
        "Content-Disposition: form-data; name=\"title\"\r\n\r\n"
    );
    uploadBody.Append("Aster & Lime\r\n");
    uploadBody.Append("--lime-boundary\r\n");
    uploadBody.Append(
        "Content-Disposition: form-data; name=\"image\"; "
    );
    uploadBody.Append("filename=\"mark.svg\"\r\n");
    uploadBody.Append("Content-Type: image/svg+xml\r\n\r\n");
    uploadBody.Append("<svg>binary-safe</svg>\r\n");
    uploadBody.Append("--lime-boundary--\r\n");
    string body = uploadBody.ToString();
    Request upload = RequestNew(
        "POST",
        "/upload",
        "example.test",
        "multipart/form-data; boundary=lime-boundary",
        "",
        body
    );
    FormCollection form = upload.ReadForm();
    switch (form.Get("title"))
    {
        case Option.Some(title): {
            if (title != "Aster & Lime") { return false; }
        }
        case Option.None: { return false; }
    }
    switch (form.GetFile("image"))
    {
        case Option.Some(file): {
            return file.FileName == "mark.svg" &&
                file.ContentType == "image/svg+xml" &&
                file.Length == 22;
        }
        case Option.None: { return false; }
    }
}

private bool PrintHtmlResponse(Response response, int expectedStatus)
{
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != expectedStatus)
    {
        return false;
    }
    switch (body)
    {
        case ResponseBody.Empty: { return false; }
        case ResponseBody.Html(page): {
            Console.WriteLine(page.ToHtmlString());
            return true;
        }
        case ResponseBody.Text(text): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private bool PrintRedirect(Response response)
{
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != 303 || headers.Count != 1)
    {
        return false;
    }
    foreach (ResponseHeader header in headers)
    {
        if (header.Name != "Location")
        {
            return false;
        }
        Console.WriteLine(header.Value);
    }
    switch (body)
    {
        case ResponseBody.Empty: { return true; }
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Text(text): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private bool BoundServiceRoutes()
{
    SiteStateOwner stateOwner = new()
    {
        Value = new SiteState("Stateful Lime")
    };
    SiteState state = stateOwner.Value;
    ApplicationOwner appOwner = NewApplication();
    WebApplication app = appOwner.Value;
    Handler missing = state.Missing;
    Handler home = state.Home;
    StringRouteHandler article = state.Article;
    app.MapFallback(missing);
    app.MapGet("/", home);
    app.MapGet("/articles/{slug}", article);
    RegisterDynamicRoute(app, home);

    if (!PrintHtmlResponse(
            app.Dispatch(request("GET", "/")), 200))
    {
        return false;
    }
    Response dynamic = app.Dispatch(request("GET", "/dynamic"));
    (int dynamicStatus, ResponseBody dynamicBody,
    List<ResponseHeader> dynamicHeaders) = dynamic;
    if (dynamicStatus != 200)
    {
        return false;
    }
    switch (dynamicBody)
    {
        case ResponseBody.Empty: { return false; }
        case ResponseBody.Html(page): {
        }
        case ResponseBody.Text(text): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
    if (!TextResponseEquals(
            app.Dispatch(request("GET", "/articles/aster")),
            200, "Stateful Lime:aster"
        ))
    {
        return false;
    }
    return PrintHtmlResponse(
        app.Dispatch(request("GET", "/missing")), 404
    );
}

private Response ParameterRoute(Request request)
{
    return Results.Text("parameter");
}

private Response ConstrainedRoute(Request request)
{
    return Results.Text("constrained");
}

private Response LiteralRoute(Request request)
{
    return Results.Text("literal");
}

private Response HeadRoute(Request request)
{
    return Results.Text("head");
}

private Response TypedString(string slug)
{
    return Results.Text($"string:{slug}");
}

private Response TypedInt(int id)
{
    return Results.Text($"int:{id}");
}

private Response TypedLong(long id)
{
    return Results.Text($"long:{id}");
}

private Response TypedBool(bool enabled)
{
    return Results.Text(enabled ? "bool:true" : "bool:false");
}

private Response TypedRequestInt(Request request, int id)
{
    return Results.Text($"{request.Method}:{id}");
}

private bool TextResponseEquals(
    Response response,
    int expectedStatus,
    string expectedBody
)
{
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != expectedStatus)
    {
        return false;
    }
    switch (body)
    {
        case ResponseBody.Text(text): { return text == expectedBody; }
        case ResponseBody.Empty: { return false; }
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private bool AllowEquals(Response response, string expected)
{
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    foreach (ResponseHeader header in headers)
    {
        if (header.Name == "Allow")
        {
            return header.Value == expected;
        }
    }
    return false;
}

private bool EmptyResponseEquals(Response response, int expectedStatus)
{
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != expectedStatus) { return false; }
    switch (body)
    {
        case ResponseBody.Empty: { return true; }
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Text(text): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private bool LocationEquals(Response response, string expected)
{
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    foreach (ResponseHeader header in headers)
    {
        if (header.Name == "Location")
        {
            return header.Value == expected;
        }
    }
    return false;
}

private bool ConventionalResultsWork()
{
    if (!EmptyResponseEquals(Results.Ok(), StatusCodes.Status200OK) ||
        !EmptyResponseEquals(
            Results.Accepted(), StatusCodes.Status202Accepted
        ) ||
        !EmptyResponseEquals(
            Results.BadRequest(), StatusCodes.Status400BadRequest
        ) ||
        !EmptyResponseEquals(
            Results.Unauthorized(), StatusCodes.Status401Unauthorized
        ) ||
        !EmptyResponseEquals(
            Results.Forbid(), StatusCodes.Status403Forbidden
        ) ||
        !EmptyResponseEquals(
            Results.NotFound(), StatusCodes.Status404NotFound
        ) ||
        !EmptyResponseEquals(
            Results.Conflict(), StatusCodes.Status409Conflict
        ) ||
        !EmptyResponseEquals(
            Results.UnprocessableEntity(),
            StatusCodes.Status422UnprocessableEntity
        ) ||
        !EmptyResponseEquals(
            Results.TooManyRequests(),
            StatusCodes.Status429TooManyRequests
        ) ||
        !EmptyResponseEquals(
            Results.InternalServerError(),
            StatusCodes.Status500InternalServerError
        ) ||
        !EmptyResponseEquals(
            Results.ServiceUnavailable(),
            StatusCodes.Status503ServiceUnavailable
        ) ||
        !EmptyResponseEquals(Results.StatusCode(418), 418))
    {
        return false;
    }

    Response accepted = Results.Accepted("/jobs/42");
    Response created = Results.Created("/articles/aster");
    Response redirect = Results.Redirect("/new-location");
    if (!LocationEquals(accepted, "/jobs/42") ||
        !LocationEquals(created, "/articles/aster") ||
        !LocationEquals(redirect, "/new-location") ||
        !EmptyResponseEquals(redirect, StatusCodes.Status302Found))
    {
        return false;
    }

    try
    {
        Response invalid = Results.StatusCode(99);
        return false;
    }
    catch (ArgumentException error)
    {
        if (error.Message.Length == 0) { return false; }
    }
    try
    {
        Response invalid = Results.Accepted("");
        return false;
    }
    catch (ArgumentException error)
    {
        return error.Message.Length > 0;
    }
}

private bool StructuralRoutingWorks()
{
    ApplicationOwner appOwner = NewApplication();
    WebApplication app = appOwner.Value;
    app.MapFallback(missing);
    app.MapGet("/precedence/{value}", ParameterRoute);
    app.MapGet("/precedence/{value:int}", ConstrainedRoute);
    app.MapGet("/precedence/current", LiteralRoute);
    app.MapHead("/precedence/current", HeadRoute);
    List<string> multiMethods = new();
    multiMethods.Add("POST");
    multiMethods.Add("PUT");
    app.MapMethods("/multi", multiMethods, ParameterRoute);
    multiMethods.Add("DELETE");

    if (!TextResponseEquals(
            app.Dispatch(request("GET", "/precedence/current")),
            200,
            "literal"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("GET", "/precedence/42")),
            200,
            "constrained"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("GET", "/precedence/aster")),
            200,
            "parameter"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("HEAD", "/precedence/current")),
            200,
            "head"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("POST", "/multi")), 200, "parameter"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("PUT", "/multi")), 200, "parameter"
        ))
    {
        return false;
    }

    Response options = app.Dispatch(request(
        "OPTIONS", "/precedence/42"
    ));
    Response invalidMethod = app.Dispatch(request(
        "DELETE", "/precedence/42"
    ));
    Response multiInvalidMethod = app.Dispatch(request(
        "DELETE", "/multi"
    ));
    (int optionsStatus, ResponseBody optionsBody,
    List<ResponseHeader> optionsHeaders) = options;
    (int invalidStatus, ResponseBody invalidBody,
    List<ResponseHeader> invalidHeaders) = invalidMethod;
    return EmptyResponseEquals(options, 204) && invalidStatus == 405 &&
        AllowEquals(options, "GET, HEAD, OPTIONS") &&
        AllowEquals(invalidMethod, "GET, HEAD, OPTIONS") &&
        multiInvalidMethod.StatusCode == 405 &&
        AllowEquals(multiInvalidMethod, "POST, PUT, OPTIONS");
}

private bool RegistrationValidationWorks()
{
    bool malformedRejected = false;
    try
    {
        ApplicationOwner malformedOwner = NewApplication();
        WebApplication malformed = malformedOwner.Value;
        malformed.MapGet("users/{id}", ParameterRoute);
    }
    catch (ArgumentException error)
    {
        malformedRejected = true;
    }

    bool conflictRejected = false;
    try
    {
        ApplicationOwner conflictOwner = NewApplication();
        WebApplication conflict = conflictOwner.Value;
        conflict.MapGet("/users/{id}", ParameterRoute);
        conflict.MapGet("/users/{name}", LiteralRoute);
    }
    catch (ArgumentException error)
    {
        conflictRejected = true;
    }

    try
    {
        ApplicationOwner disjointOwner = NewApplication();
        WebApplication disjoint = disjointOwner.Value;
        disjoint.MapGet("/users/{id:int}", ConstrainedRoute);
        disjoint.MapGet("/users/{name:alpha}", ParameterRoute);
    }
    catch (ArgumentException error)
    {
        return false;
    }

    bool invalidMethodRejected = false;
    try
    {
        ApplicationOwner invalidMethodOwner = NewApplication();
        WebApplication invalidMethod = invalidMethodOwner.Value;
        List<string> methods = new();
        methods.Add("get");
        invalidMethod.MapMethods("/methods", methods, LiteralRoute);
    }
    catch (ArgumentException error)
    {
        invalidMethodRejected = true;
    }

    bool atomicConflictRejected = false;
    bool registrationStayedAtomic = false;
    ApplicationOwner atomicOwner = NewApplication();
    WebApplication atomic = atomicOwner.Value;
    try
    {
        atomic.MapGet("/atomic", LiteralRoute);
        List<string> methods = new();
        methods.Add("POST");
        methods.Add("GET");
        atomic.MapMethods("/atomic", methods, ParameterRoute);
    }
    catch (ArgumentException error)
    {
        atomicConflictRejected = true;
        registrationStayedAtomic =
            atomic.Dispatch(request("POST", "/atomic")).StatusCode == 405;
    }
    return malformedRejected && conflictRejected &&
        invalidMethodRejected && atomicConflictRejected &&
        registrationStayedAtomic;
}

private bool GroupsAndBuildersWork()
{
    ApplicationOwner appOwner = NewApplication();
    WebApplication app = appOwner.Value;
    RouteGroup api = app.MapGroup("/api");
    RouteGroup articles = api.MapGroup("/articles");
    EndpointBuilder endpoint = articles.MapGet("/{value}", TypedString);
    endpoint.WithName("GetArticle")
        .WithDescription("Gets one article")
        .WithTag("articles")
        .Produces(StatusCodes.Status200OK)
        .Produces(StatusCodes.Status404NotFound);

    if (!TextResponseEquals(
            app.Dispatch(request("GET", "/api/articles/aster")),
            200,
            "string:aster"
        ))
    {
        return false;
    }

    try
    {
        EndpointBuilder duplicate = api.MapGet("/other", LiteralRoute);
        duplicate.WithName("GetArticle");
        return false;
    }
    catch (ArgumentException error)
    {
        return error.Message.Length > 0;
    }
}

private bool TypedRouteBindingWorks()
{
    ApplicationOwner appOwner = NewApplication();
    WebApplication app = appOwner.Value;
    app.MapGet("/articles/{slug}", TypedString);
    app.MapGet("/users/{id:int}", TypedInt);
    app.MapGet("/orders/{id:long}", TypedLong);
    app.MapGet("/flags/{enabled:bool}", TypedBool);
    app.MapGet("/request/{id:int}", TypedRequestInt);
    app.MapGet("/unconstrained/{id}", TypedInt);
    app.MapPost("/posts/{slug}", TypedString);
    app.MapPut("/users/{id:int}", TypedInt);
    app.MapPatch("/orders/{id:long}", TypedLong);
    app.MapDelete("/flags/{enabled:bool}", TypedBool);
    app.MapHead("/request/{id:int}", TypedRequestInt);
    List<string> customMethods = new();
    customMethods.Add("CONNECT");
    app.MapMethods("/custom/{slug}", customMethods, TypedString);
    RouteGroup group = app.MapGroup("/group");
    group.MapPost("/{slug}", TypedString);

    if (!TextResponseEquals(
            app.Dispatch(request("GET", "/articles/aster")),
            200, "string:aster"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("GET", "/users/42")), 200, "int:42"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("GET", "/orders/2147483648")),
            200, "long:2147483648"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("GET", "/flags/TRUE")),
            200, "bool:true"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("GET", "/request/7")), 200, "GET:7"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("POST", "/posts/aster")),
            200, "string:aster"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("PUT", "/users/43")), 200, "int:43"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("PATCH", "/orders/2147483649")),
            200, "long:2147483649"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("DELETE", "/flags/false")),
            200, "bool:false"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("HEAD", "/request/8")), 200, "HEAD:8"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("CONNECT", "/custom/aster")),
            200, "string:aster"
        ) ||
        !TextResponseEquals(
            app.Dispatch(request("POST", "/group/aster")),
            200, "string:aster"
        ) ||
        app.Dispatch(request(
            "GET", "/unconstrained/not-an-integer"
        )).StatusCode != StatusCodes.Status400BadRequest)
    {
        return false;
    }

    try
    {
        app.MapGet("/missing", TypedInt);
        return false;
    }
    catch (ArgumentException error)
    {
        if (error.Message.Length == 0) { return false; }
    }

    try
    {
        app.MapGet("/too-many/{first}/{second}", TypedInt);
        return false;
    }
    catch (ArgumentException error)
    {
        return error.Message.Length > 0;
    }
}

private bool EndpointMetadataWorks()
{
    ApplicationOwner appOwner = NewApplication();
    WebApplication app = appOwner.Value;
    app.MapGet("/articles/{slug}", TypedString)
        .WithName("GetArticle")
        .WithDescription("Gets one article")
        .WithTag("articles")
        .Produces(StatusCodes.Status200OK)
        .Produces(StatusCodes.Status404NotFound);
    List<string> methods = new();
    methods.Add("POST");
    methods.Add("PUT");
    app.MapMethods("/articles", methods, LiteralRoute)
        .WithName("WriteArticle");

    EndpointDataSource endpoints = app.Endpoints;
    if (endpoints.Count != 2) { return false; }
    RouteEndpoint article = endpoints.GetEndpoint(0);
    if (article.Pattern != "/articles/{slug}" ||
        article.MethodCount != 1 || article.GetMethod(0) != "GET" ||
        article.TagCount != 1 || article.GetTag(0) != "articles" ||
        article.ProducedStatusCount != 2 ||
        article.GetProducedStatus(0) != StatusCodes.Status200OK ||
        article.GetProducedStatus(1) != StatusCodes.Status404NotFound)
    {
        return false;
    }
    switch (article.Name)
    {
        case Option.Some(name): {
            if (name != "GetArticle") { return false; }
        }
        case Option.None: { return false; }
    }
    switch (article.Description)
    {
        case Option.Some(description): {
            if (description != "Gets one article") { return false; }
        }
        case Option.None: { return false; }
    }

    RouteEndpoint write = endpoints.GetEndpoint(1);
    if (write.Pattern != "/articles" || write.MethodCount != 2 ||
        write.GetMethod(0) != "POST" || write.GetMethod(1) != "PUT")
    {
        return false;
    }

    try
    {
        RouteEndpoint missing = endpoints.GetEndpoint(2);
        return false;
    }
    catch (ArgumentException error)
    {
        if (error.Message.Length == 0) { return false; }
    }
    try
    {
        string missing = article.GetTag(1);
        return false;
    }
    catch (ArgumentException error)
    {
        if (error.Message.Length == 0) { return false; }
    }
    return true;
}

private bool LinkGenerationWorks()
{
    ApplicationOwner appOwner = NewApplication();
    WebApplication app = appOwner.Value;
    app.MapGet("/", home).WithName("Home");
    app.MapGet("/articles/{slug}", TypedString)
        .WithName("GetArticle");
    app.MapGet("/users/{id:int}", TypedInt)
        .WithName("GetUser");

    LinkGenerator links = app.Links;
    RouteValues article = RouteValues.From("slug", "Aster & C#");
    article.Add("page", "two words");
    switch (links.GetPathByName("GetArticle", article))
    {
        case Result.Ok(path): {
            if (path != "/articles/Aster%20%26%20C%23?page=two%20words")
            {
                return false;
            }
        }
        case Result.Err(error): { return false; }
    }

    switch (links.GetPathByName("Home"))
    {
        case Result.Ok(path): {
            if (path != "/") { return false; }
        }
        case Result.Err(error): { return false; }
    }

    switch (links.GetPathByName("GetArticle"))
    {
        case Result.Ok(path): { return false; }
        case Result.Err(error): {
            if (error.Length == 0) { return false; }
        }
    }
    switch (links.GetPathByName(
        "GetUser", RouteValues.From("id", "not-an-integer")
    ))
    {
        case Result.Ok(path): { return false; }
        case Result.Err(error): {
            if (error.Length == 0) { return false; }
        }
    }
    switch (links.GetPathByName("Missing"))
    {
        case Result.Ok(path): { return false; }
        case Result.Err(error): { return error.Length > 0; }
    }
}

int main()
{
    if (!MultipartFormsWork()) { return 1; }
    if (!ExceptionBoundaryWorks())
    {
        return 1;
    }
    if (!ContentLibrariesWork())
    {
        return 1;
    }
    if (!RejectsMalformedValues())
    {
        return 1;
    }
    if (!RejectsUnsafeResponseMetadata())
    {
        return 1;
    }
    if (!HttpPrimitivesWork())
    {
        return 1;
    }
    if (!RequestPathNormalizationWorks())
    {
        return 1;
    }
    if (!ProblemResultsWork())
    {
        return 1;
    }
    if (!ForwardedHeadersAreTrustedExplicitly())
    {
        return 1;
    }
    if (!StaticAssetsWork())
    {
        return 1;
    }
    if (!StaticCssWorks())
    {
        return 1;
    }
    if (!StaticDirectoriesWork())
    {
        return 1;
    }
    if (!StaticCachePolicyWorks())
    {
        return 1;
    }
    if (!BoundServiceRoutes())
    {
        return 1;
    }
    if (!StructuralRoutingWorks())
    {
        return 1;
    }
    if (!ConventionalResultsWork())
    {
        return 1;
    }
    if (!RegistrationValidationWorks())
    {
        return 1;
    }
    if (!GroupsAndBuildersWork())
    {
        return 1;
    }
    if (!TypedRouteBindingWorks())
    {
        return 1;
    }
    if (!LinkGenerationWorks())
    {
        return 1;
    }
    if (!EndpointMetadataWorks())
    {
        return 1;
    }

    ApplicationOwner appOwner = NewApplication();

    WebApplication app = appOwner.Value;
    app.MapFallback(missing);
    app.MapGet("/", home);
    app.MapGet("/articles/{slug}", article);
    app.MapGet("/search", search);
    app.MapPost("/articles", create);

    if (!PrintHtmlResponse(app.Dispatch(request("GET", "/")), 200) ||
        !PrintHtmlResponse(app.Dispatch(
            request("GET", "/articles/first-post")
        ), 200) ||
        !PrintHtmlResponse(app.Dispatch(
            request("GET", "/search?page=two+words%21&sort=new")
        ), 200) ||
        !PrintHtmlResponse(app.Dispatch(
            request("DELETE", "/articles")
        ), 405) ||
        !PrintRedirect(app.Dispatch(request("POST", "/articles"))))
    {
        return 1;
    }
    return 0;
}
