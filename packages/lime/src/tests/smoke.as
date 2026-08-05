namespace Tests.Smoke;

using Lime;
using Lime.Forms;
using Lime.Forwarding;
using Lime.Content;
using Lime.Markdown;
using Lime.Static;
using Aster.Html;
using System.Text;

private Response home(Request request)
{
    return Response.Ok(
        <section>
            <h1>Lime</h1>
            <p>Small, explicit, and written in Aster.</p>
        </section>
    );
}

private Response article(Request request)
{
    string slug = request.param("slug");
    return Response.Ok(<article>{slug}</article>);
}

private Response create(Request request)
{
    return Response.Redirect("/");
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
                return Response.BadRequest(<p>unexpected query</p>);
            }
            return Response.Ok(
                <p>{page}:{sort}</p>
            );
        }
        case Result.Err(error): {
            return Response.BadRequest(<p>{error}</p>);
        }
    }
}

private Response missing(Request request)
{
    return Response.NotFound(
        <section><h1>Not found</h1></section>
    );
}

private Response broken(Request request)
{
    throw new Exception("route failed");
}

private Response HandleRouteException(Exception error)
{
    return Response.Plain(503, error.Message);
}

private bool ExceptionBoundaryWorks()
{
    App app = AppNew(missing);
    app.OnException(HandleRouteException);
    app.Get("/broken", broken);
    Response response = app.dispatch(request("GET", "/broken"));
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != 503) { return false; }
    switch (body)
    {
        case ResponseBody.Text(text): { return text == "route failed"; }
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

struct SiteState
{
    string title;
}

private Response StateHome(SiteState state, Request request)
{
    return Response.Ok(<h1>{state.title}</h1>);
}

private Response StateMissing(SiteState state, Request request)
{
    return Response.NotFound(
        <p>{state.title}:missing</p>
    );
}

private void RegisterDynamicRoute(ref StatefulApp<SiteState> app)
{
    string path = "/dynamic";
    app.Get(path, StateHome);
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
    switch (withHeaders.header("X-TEST"))
    {
        case Option.Some(value): {
            if (value != "two") { return false; }
        }
        case Option.None: { return false; }
    }

    Request jsonRequest = RequestNew(
        "POST", "/", "example.test",
        "Application/Json; charset=utf-8", "", "{\"ok\":true}"
    );
    switch (jsonRequest.json())
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

    Response created = Response.JsonStatus(
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
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Text(text): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private Response Origin(Request request)
{
    return Response.Text(
        $"{request.Scheme()}|{request.Host()}|{request.RemoteIpAddress()}"
    );
}

private bool ForwardedHeadersAreTrustedExplicitly()
{
    App app = AppNew(missing);
    app.Get("/origin", Origin);
    ForwardedHeadersOptions options = ForwardedHeadersOptions();
    options.ForwardedHeaders = ForwardedHeaders.All;
    options.KnownProxies.Add("127.0.0.1");
    app.UseForwardedHeaders(options);

    string forwarded = "X-Forwarded-For\0198.51.100.20\0X-Forwarded-Host\0aster.example\0X-Forwarded-Proto\0https\0";
    Request trusted = RequestNewTransport(
        "GET", "/origin", "internal.test", "http", "127.0.0.1",
        "", "", "", forwarded
    );
    Response trustedResponse = app.dispatch(trusted);
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
    Response untrustedResponse = app.dispatch(untrusted);
    (int untrustedStatus, ResponseBody untrustedBody,
    List<ResponseHeader> untrustedHeaders) = untrustedResponse;
    if (untrustedStatus != 200) { return false; }
    switch (untrustedBody)
    {
        case ResponseBody.Text(text): {
            return text == "http|internal.test|203.0.113.9";
        }
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
    App app = AppNew(missing);
    switch (app.TryStatic("/assets/", "packages/lime/testdata"))
    {
        case Result.Err(error): { return false; }
        case Result.Ok(registered): {
        }
    }
    Response response = app.dispatch(request("GET", "/assets/site.css"));
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != 200) { return false; }
    switch (body)
    {
        case ResponseBody.Css(text): {
            return text == "body { color: black; }\n";
        }
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
    App app = AppNew(missing);
    app.Static("/assets/", "packages/lime/testdata", options);
    Response response = app.dispatch(request("GET", "/assets/site.css"));
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    if (status != 200 || headers.Count != 1) { return false; }
    foreach (ResponseHeader header in headers)
    {
        if (header.name != "Cache-Control" ||
            header.value != "public, max-age=60, immutable")
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
            return file.FileName() == "mark.svg" &&
                file.ContentType() == "image/svg+xml" &&
                file.Length() == 22;
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
        if (header.name != "Location")
        {
            return false;
        }
        Console.WriteLine(header.value);
    }
    switch (body)
    {
        case ResponseBody.Text(text): { return true; }
        case ResponseBody.Html(page): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
}

private bool StatefulRoutes()
{
    SiteState state = new()
    {
        title = "Stateful Lime"
    };
    StatefulApp<SiteState> app = StatefulAppNew(
        state,
        StateMissing
    );
    app.Get("/", StateHome);
    RegisterDynamicRoute(app);

    if (!PrintHtmlResponse(
            app.dispatch(request("GET", "/")), 200))
    {
        return false;
    }
    Response dynamic = app.dispatch(request("GET", "/dynamic"));
    (int dynamicStatus, ResponseBody dynamicBody,
    List<ResponseHeader> dynamicHeaders) = dynamic;
    if (dynamicStatus != 200)
    {
        return false;
    }
    switch (dynamicBody)
    {
        case ResponseBody.Html(page): {
        }
        case ResponseBody.Text(text): { return false; }
        case ResponseBody.Css(text): { return false; }
        case ResponseBody.Asset(asset): { return false; }
        case ResponseBody.Stream(stream): { return false; }
        case ResponseBody.File(file): { return false; }
    }
    return PrintHtmlResponse(
        app.dispatch(request("GET", "/missing")), 404
    );
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
    if (!StatefulRoutes())
    {
        return 1;
    }

    App app = AppNew(missing);
    app.Get("/", home);
    app.Get("/articles/:slug", article);
    app.Get("/search", search);
    app.post("/articles", create);

    if (!PrintHtmlResponse(app.dispatch(request("GET", "/")), 200) ||
        !PrintHtmlResponse(app.dispatch(
            request("GET", "/articles/first-post")
        ), 200) ||
        !PrintHtmlResponse(app.dispatch(
            request("GET", "/search?page=two+words%21&sort=new")
        ), 200) ||
        !PrintHtmlResponse(app.dispatch(
            request("DELETE", "/articles")
        ), 405) ||
        !PrintRedirect(app.dispatch(request("POST", "/articles"))))
    {
        return 1;
    }
    return 0;
}
