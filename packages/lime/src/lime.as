namespace Lime;

using Lime.Forwarding;
using Lime.Routing;
using Aster.Html;
using System.IO;
using System.Text;
using System.Text.Json;

public struct Request
{
    string method;
    string path;
    string host;
    string scheme;
    string remoteIpAddress;
    string contentType;
    string cookieHeader;
    string body;
    string target;
    string queryString;
    Option<RoutePattern> routePattern;
    string headerData;

    public string Method => method;
    public string Path => path;
    public string Host => host;
    public string Scheme => scheme;
    public string RemoteIpAddress => remoteIpAddress;
    public string ContentType => contentType;
    public string Body => body;
    public string Target => target;
    public string QueryString => queryString;
}

public struct ResponseHeader
{
    string name;
    string value;

    public string Name => name;
    public string Value => value;
}

public enum AssetKind
{
    JavaScript,
    Json,
    ProblemJson,
    Xml,
    Svg,
    Png,
    Jpeg,
    Gif,
    WebP,
    Icon,
    Woff,
    Woff2,
    Ttf,
    Wasm,
    Binary,
}

public struct AssetBody
{
    string bytes;
    AssetKind kind;
}

public struct StreamBody
{
    Stream stream;
    AssetKind kind;
}

public struct FileBody
{
    string path;
    AssetKind kind;
}

public union ResponseBody
{
    Empty,
    Html(Html),
    Text(string),
    Css(string),
    Asset(AssetBody),
    Stream(StreamBody),
    File(FileBody),
}

public struct Response
{
    int status;
    ResponseBody body;
    List<ResponseHeader> headers;

    public int StatusCode => status;
}

public struct Results
{
}

public struct StatusCodes
{
    public static int Status200OK => 200;
    public static int Status201Created => 201;
    public static int Status202Accepted => 202;
    public static int Status204NoContent => 204;
    public static int Status301MovedPermanently => 301;
    public static int Status302Found => 302;
    public static int Status303SeeOther => 303;
    public static int Status307TemporaryRedirect => 307;
    public static int Status308PermanentRedirect => 308;
    public static int Status400BadRequest => 400;
    public static int Status401Unauthorized => 401;
    public static int Status403Forbidden => 403;
    public static int Status404NotFound => 404;
    public static int Status405MethodNotAllowed => 405;
    public static int Status409Conflict => 409;
    public static int Status415UnsupportedMediaType => 415;
    public static int Status422UnprocessableEntity => 422;
    public static int Status429TooManyRequests => 429;
    public static int Status500InternalServerError => 500;
    public static int Status503ServiceUnavailable => 503;
}

public struct ProblemDetails
{
    string? Type;
    string? Title;
    int Status;
    string? Detail;
    string? Instance;

    public static ProblemDetails Create(int status, string title)
    {
        if (status < 400 || status > 599)
        {
            throw new ArgumentException(
                "problem status must be between 400 and 599"
            );
        }
        return new()
        {
            Type = null,
            Title = title,
            Status = status,
            Detail = null,
            Instance = null
        };
    }
}

public enum CookieSameSite
{
    Strict,
    Lax,
    None,
}

public struct CookieOptions
{
    string path;
    Option<string> domain;
    Option<long> maxAge;
    bool httpOnly;
    bool secure;
    CookieSameSite sameSite;
}

private void EnsureResponseBodyAllowed(int status)
{
    if ((status >= 100 && status < 200) || status == 204 || status == 304)
    {
        throw new ArgumentException(
            "the response status does not permit a body"
        );
    }
}

private Response HtmlResponse(int status, Html page)
{
    EnsureResponseBodyAllowed(status);
    List<ResponseHeader> headers = new();
    return new()
    {
        status = status,
        body = ResponseBody.Html(page),
        headers = headers
    };
}

public Response Results.Html(int status, Html page)
{
    return HtmlResponse(status, page);
}

public Response Results.Text(int status, string body)
{
    EnsureResponseBodyAllowed(status);
    List<ResponseHeader> headers = new();
    return new()
    {
        status = status,
        body = ResponseBody.Text(body),
        headers = headers
    };
}

public Response Results.Css(int status, string body)
{
    EnsureResponseBodyAllowed(status);
    List<ResponseHeader> headers = new();
    return new()
    {
        status = status,
        body = ResponseBody.Css(body),
        headers = headers
    };
}

public Response Results.Asset(
    int status,
    string bytes,
    AssetKind kind
)
{
    EnsureResponseBodyAllowed(status);
    List<ResponseHeader> headers = new();
    AssetBody asset = new()
    {
        bytes = bytes,
        kind = kind
    };
    return new()
    {
        status = status,
        body = ResponseBody.Asset(asset),
        headers = headers
    };
}

public Response Results.Json(int status, string bytes)
{
    return Results.Asset(status, bytes, AssetKind.Json);
}

private string SerializeProblemDetails(ProblemDetails problem)
{
    StringBuilder json = new();
    json.Append("{\"type\":");
    json.Append(JsonSerializer.Serialize(problem.Type));
    json.Append(",\"title\":");
    json.Append(JsonSerializer.Serialize(problem.Title));
    json.Append(",\"status\":");
    json.Append(JsonSerializer.Serialize(problem.Status));
    json.Append(",\"detail\":");
    json.Append(JsonSerializer.Serialize(problem.Detail));
    json.Append(",\"instance\":");
    json.Append(JsonSerializer.Serialize(problem.Instance));
    json.Append("}");
    return json.ToString();
}

public Response Results.Problem(ProblemDetails problem)
{
    if (problem.Status < 400 || problem.Status > 599)
    {
        throw new ArgumentException(
            "problem status must be between 400 and 599"
        );
    }
    return Results.Asset(
        problem.Status,
        SerializeProblemDetails(problem),
        AssetKind.ProblemJson
    );
}

public Response Results.Html(Html page)
{
    return HtmlResponse(200, page);
}

public Response Results.Text(string body)
{
    List<ResponseHeader> headers = new();
    return new()
    {
        status = 200,
        body = ResponseBody.Text(body),
        headers = headers
    };
}

public Response Results.Css(string body)
{
    List<ResponseHeader> headers = new();
    return new()
    {
        status = 200,
        body = ResponseBody.Css(body),
        headers = headers
    };
}

public Response Results.Asset(string bytes, AssetKind kind)
{
    List<ResponseHeader> headers = new();
    AssetBody asset = new()
    {
        bytes = bytes,
        kind = kind
    };
    return new()
    {
        status = 200,
        body = ResponseBody.Asset(asset),
        headers = headers
    };
}

public Response Results.JavaScript(string bytes)
{
    return Results.Asset(bytes, AssetKind.JavaScript);
}

public Response Results.Json(string bytes)
{
    return Results.Asset(bytes, AssetKind.Json);
}

public Response Results.Xml(string bytes)
{
    return Results.Asset(bytes, AssetKind.Xml);
}

public Response Results.Svg(string bytes)
{
    return Results.Asset(bytes, AssetKind.Svg);
}

public Response Results.Wasm(string bytes)
{
    return Results.Asset(bytes, AssetKind.Wasm);
}

public Response Results.Stream(Stream stream, AssetKind kind)
{
    List<ResponseHeader> headers = new();
    StreamBody body = new()
    {
        stream = stream,
        kind = kind
    };
    return new()
    {
        status = 200,
        body = ResponseBody.Stream(body),
        headers = headers
    };
}

public Response Results.Stream(
    int status,
    Stream stream,
    AssetKind kind
)
{
    EnsureResponseBodyAllowed(status);
    Response response = Results.Stream(stream, kind);
    response.status = status;
    return response;
}

public Response Results.File(string path, AssetKind kind)
{
    List<ResponseHeader> headers = new();
    FileBody body = new()
    {
        path = path,
        kind = kind
    };
    return new()
    {
        status = 200,
        body = ResponseBody.File(body),
        headers = headers
    };
}

private Response RedirectResponse(int status, string location)
{
    if (location.Length == 0)
    {
        throw new ArgumentException("redirect location cannot be empty");
    }
    List<ResponseHeader> headers = new();
    ResponseHeader redirect = LimeResultOrThrow(
        ResponseHeader("Location", location)
    );
    headers.Add(redirect);
    return new()
    {
        status = status,
        body = ResponseBody.Text(""),
        headers = headers
    };
}

public Response Results.Redirect(string location)
{
    return RedirectResponse(StatusCodes.Status302Found, location);
}

public Response Results.SeeOther(string location)
{
    return RedirectResponse(StatusCodes.Status303SeeOther, location);
}

public Response Results.PermanentRedirect(string location)
{
    return RedirectResponse(StatusCodes.Status301MovedPermanently, location);
}

public Response Results.TemporaryRedirectPreserveMethod(string location)
{
    return RedirectResponse(StatusCodes.Status307TemporaryRedirect, location);
}

public Response Results.PermanentRedirectPreserveMethod(string location)
{
    return RedirectResponse(StatusCodes.Status308PermanentRedirect, location);
}

public Response Results.NoContent()
{
    List<ResponseHeader> headers = new();
    return new()
    {
        status = StatusCodes.Status204NoContent,
        body = ResponseBody.Empty,
        headers = headers
    };
}

public Response Results.Created(string location, Html page)
{
    Response response = Results.Html(StatusCodes.Status201Created, page);
    response.AddHeader(LimeResultOrThrow(
        ResponseHeader("Location", location)
    ));
    return response;
}

public Response Results.BadRequest(Html page)
{
    return HtmlResponse(400, page);
}

public Response Results.NotFound(Html page)
{
    return HtmlResponse(404, page);
}

public Response Results.Unauthorized(Html page)
{
    return HtmlResponse(StatusCodes.Status401Unauthorized, page);
}

public Response Results.Forbid(Html page)
{
    return HtmlResponse(StatusCodes.Status403Forbidden, page);
}

public Response Results.Conflict(Html page)
{
    return HtmlResponse(StatusCodes.Status409Conflict, page);
}

public Response Results.MethodNotAllowed(Html page)
{
    return HtmlResponse(405, page);
}

public Response Results.InternalError(Html page)
{
    return HtmlResponse(500, page);
}

public delegate Response Handler(Request request);
public delegate Task<Response> AsyncHandler(Request request);

public delegate List<string> BuildSource();

public delegate Option<Response> StaticResolver(
    string urlPrefix,
    string root,
    StaticFileOptions options,
    Request request
);

public union FilterResult
{
    Continue,
    Respond(Response),
}

public delegate FilterResult RequestFilter(Request request);
public delegate Html HtmlMiddleware(
    Request request,
    Html page
);
public delegate Response ExceptionHandler(Exception error);
public delegate Response StatefulHandler<State>(State state, Request request);
public delegate Task<Response> AsyncStatefulHandler<State>(
    State state,
    Request request
);

union RouteHandler
{
    Sync(Handler),
    Async(AsyncHandler),
}
public delegate List<string> StatefulBuildSource<State>(State state);
public delegate FilterResult StatefulRequestFilter<State>(
    State state,
    Request request
);
public delegate Html StatefulHtmlMiddleware<State>(
    State state,
    Request request,
    Html page
);

struct UrlValue
{
    string name;
    string value;
}

public struct UrlValues
{
    List<UrlValue> values;
}

struct Route
{
    List<string> methods;
    RoutePattern pattern;
    RouteHandler handler;
}

public struct BuildPage
{
    string path;
}

public struct StaticDirectory
{
    string urlPrefix;
    string root;
    StaticFileOptions options;
    StaticResolver resolver;
}

public struct StaticFileOptions
{
    long MaxAgeSeconds;
    bool Immutable;
}

public StaticFileOptions StaticFileOptions()
{
    return new()
    {
        MaxAgeSeconds = 0,
        Immutable = false
    };
}

public struct App
{
    List<Route> routes;
    List<BuildPage> pages;
    List<RequestFilter> filters;
    List<HtmlMiddleware> htmlMiddleware;
    List<StaticDirectory> staticDirectories;
    RouteHandler fallback;
    ExceptionHandler exceptionHandler;
    Option<ForwardedHeadersOptions> forwardedHeaders;
}

struct StatefulRoute<State>
{
    List<string> methods;
    RoutePattern pattern;
    StatefulHandler<State> handler;
}

public struct StatefulApp<State>
{
    State state;
    List<StatefulRoute<State>> routes;
    List<BuildPage> pages;
    List<StatefulRequestFilter<State>> filters;
    List<StatefulHtmlMiddleware<State>> htmlMiddleware;
    List<StaticDirectory> staticDirectories;
    StatefulHandler<State> fallback;
    ExceptionHandler exceptionHandler;
    Option<ForwardedHeadersOptions> forwardedHeaders;
}

private Response DefaultExceptionResponse(Exception error)
{
    Console.Error.WriteLine(error.Message);
    return Results.InternalError(<h1>Internal server error</h1>);
}

private Response DefaultNotFound(Request request)
{
    return Results.NotFound(<h1>Not found</h1>);
}

private T LimeResultOrThrow<T>(Result<T, string> result)
{
    switch (result)
    {
        case Result.Ok(value): { return value; }
        case Result.Err(error): { throw new Exception(error); }
    }
}

private RoutePattern ParseRoutePattern(string pattern)
{
    switch (RoutePattern.TryParse(pattern))
    {
        case Result.Ok(parsed): { return parsed; }
        case Result.Err(error): { throw new ArgumentException(error); }
    }
}

private bool RouteHasParameter(string path)
{
    return ParseRoutePattern(path).HasParameters;
}

private bool BuildPagePathValid(string path)
{
    nuint length = path.Length;
    if (length == 0 || StringByteAt(path, 0) != 47)
    {
        return false;
    }
    nuint segmentStart = 1;
    for (nuint index = 1; index <= length; index++)
    {
        bool boundary = index == length ||
            StringByteAt(path, index) == 47;
        if (!boundary)
        {
            byte current = StringByteAt(path, index);
            if (current == 0 || current == 35 || current == 63 ||
                current == 58 || current == 92)
            {
                return false;
            }
            continue;
        }
        nuint segmentLength = index - segmentStart;
        if (segmentLength == 0)
        {
            if (length != 1 && index != length) { return false; }
        }
        else if ((segmentLength == 1 &&
               StringByteAt(path, segmentStart) == 46) ||
              (segmentLength == 2 &&
               StringByteAt(path, segmentStart) == 46 &&
               StringByteAt(path, segmentStart + 1) == 46))
        {
            return false;
        }
        segmentStart = index + 1;
    }
    return true;
}

private bool BuildPagesContains(List<BuildPage> pages, string path)
{
    foreach (BuildPage page in pages)
    {
        if (page.path == path) { return true; }
    }
    return false;
}

private void BuildPagesAdd(ref List<BuildPage> pages, string path)
{
    BuildPage page = new() { path = path };
    pages.Add(page);
}

private bool BuildPageMatchesRoutes(
    List<Route> routes,
    string path
)
{
    foreach (Route route in routes)
    {
        if (MethodContains(route.methods, "GET") &&
            route.pattern.HasParameters &&
            route.pattern.IsMatch(path))
        {
            return true;
        }
    }
    return false;
}

private bool BuildPageMatchesStatefulRoutes<State>(
    List<StatefulRoute<State>> routes,
    string path
)
{
    foreach (StatefulRoute<State> route in routes)
    {
        if (MethodContains(route.methods, "GET") &&
            route.pattern.HasParameters &&
            route.pattern.IsMatch(path))
        {
            return true;
        }
    }
    return false;
}

private bool RouteMatches(string pattern, string path)
{
    return ParseRoutePattern(pattern).IsMatch(path);
}

private int RequestPathHex(byte value)
{
    if (value >= 48 && value <= 57) { return (int)value - 48; }
    if (value >= 65 && value <= 70) { return (int)value - 65 + 10; }
    if (value >= 97 && value <= 102) { return (int)value - 97 + 10; }
    return -1;
}

private bool TryRequestPathByte(
    string value,
    nuint index,
    out byte decoded
)
{
    decoded = 0;
    if (index + 2 >= value.Length || value[index] != 37)
    {
        return false;
    }
    int high = RequestPathHex(value[index + 1]);
    int low = RequestPathHex(value[index + 2]);
    if (high < 0 || low < 0)
    {
        return false;
    }
    decoded = (byte)(high * 16 + low);
    return true;
}

private int RequestPathUtf8Length(byte first)
{
    if (first >= 194 && first <= 223) { return 2; }
    if (first >= 224 && first <= 239) { return 3; }
    if (first >= 240 && first <= 244) { return 4; }
    return 0;
}

// Returns the number of encoded source bytes consumed. Zero means the escape
// was malformed or was not valid UTF-8 and must remain literal.
private Result<nuint, string> AppendRequestPathEscape(
    ref StringBuilder output,
    string value,
    nuint index
)
{
    byte first = 0;
    if (!TryRequestPathByte(value, index, out first))
    {
        return Result.Ok((nuint)0);
    }
    if (first == 0)
    {
        return Result.Err("request path contains a null character");
    }
    if (first == 47)
    {
        output.Append(StringSlice(value, index, index + 3));
        return Result.Ok((nuint)3);
    }
    if (first <= 127)
    {
        output.AppendByte(first);
        return Result.Ok((nuint)3);
    }

    int sequenceLength = RequestPathUtf8Length(first);
    if (sequenceLength == 0)
    {
        return Result.Ok((nuint)0);
    }
    List<byte> bytes = new();
    bytes.Add(first);
    for (int offset = 1; offset < sequenceLength; offset += 1)
    {
        byte next = 0;
        nuint source = index + (nuint)(offset * 3);
        if (!TryRequestPathByte(value, source, out next) ||
            (next & 192) != 128)
        {
            return Result.Ok((nuint)0);
        }
        bytes.Add(next);
    }
    try
    {
        string decoded = Encoding.UTF8().GetString(bytes);
        output.Append(decoded);
    }
    catch (FormatException error)
    {
        return Result.Ok((nuint)0);
    }
    return Result.Ok((nuint)(sequenceLength * 3));
}

private Result<string, string> DecodeRequestPath(string value)
{
    StringBuilder output = new();
    nuint index = 0;
    while (index < value.Length)
    {
        byte current = value[index];
        if (current == 0)
        {
            return Result.Err("request path contains a null character");
        }
        if (current != 37)
        {
            output.AppendByte(current);
            index += 1;
            continue;
        }
        switch (AppendRequestPathEscape(output, value, index))
        {
            case Result.Err(error): { return Result.Err(error); }
            case Result.Ok(consumed): {
                if (consumed == 0)
                {
                    output.Append("%");
                    index += 1;
                }
                else
                {
                    index += consumed;
                }
            }
        }
    }
    string decoded = output.ToString();
    try
    {
        decoded.ToCharArray();
    }
    catch (FormatException error)
    {
        return Result.Err("request path is not valid UTF-8");
    }
    return Result.Ok(decoded);
}

private string RemoveRequestPathDotSegments(string path)
{
    if (path == "/") { return path; }
    List<string> segments = new();
    nuint start = 1;
    while (start <= path.Length)
    {
        nuint end = start;
        while (end < path.Length && path[end] != 47) { end += 1; }
        string segment = StringSlice(path, start, end);
        if (segment == "..")
        {
            if (segments.Count > 0)
            {
                segments.RemoveAt(segments.Count - 1);
            }
        }
        else if (segment != ".")
        {
            segments.Add(segment);
        }
        if (end == path.Length) { break; }
        start = end + 1;
    }

    bool closingSlash = path.EndsWith("/") || path.EndsWith("/.") ||
        path.EndsWith("/..");
    StringBuilder output = new();
    output.Append("/");
    for (nuint index = 0; index < segments.Count; index += 1)
    {
        if (index > 0) { output.Append("/"); }
        output.Append(segments[index]);
    }
    string normalized = output.ToString();
    if (closingSlash && !normalized.EndsWith("/"))
    {
        output.Append("/");
        normalized = output.ToString();
    }
    return normalized;
}

private Result<string, string> NormalizeRequestPath(string rawPath)
{
    if (rawPath == "*") { return Result.Ok(rawPath); }
    if (rawPath.Length == 0 || rawPath[0] != 47)
    {
        return Result.Err("request path must begin with '/'");
    }
    switch (DecodeRequestPath(rawPath))
    {
        case Result.Err(error): { return Result.Err(error); }
        case Result.Ok(decoded): {
            return Result.Ok(RemoveRequestPathDotSegments(decoded));
        }
    }
}

public Request RequestNewWithHeaders(
    string method,
    string target,
    string host,
    string contentType,
    string cookieHeader,
    string body,
    string headerData
)
{
    return RequestNewTransport(
        method, target, host, "http", "", contentType,
        cookieHeader, body, headerData
    );
}

public Request RequestNewTransport(
    string method,
    string target,
    string host,
    string scheme,
    string remoteIpAddress,
    string contentType,
    string cookieHeader,
    string body,
    string headerData
)
{
    string rawPath = target;
    string queryString = "";
    switch (StringFindByte(target, 63))
    {
        case Option.Some(separator): {
            rawPath = StringSlice(target, 0, separator);
            queryString = StringSlice(
                target, separator + 1, target.Length
            );
        }
        case Option.None: {
        }
    }

    string path = "";
    switch (NormalizeRequestPath(rawPath))
    {
        case Result.Err(error): { throw new ArgumentException(error); }
        case Result.Ok(normalized): { path = normalized; }
    }

    return new()
    {
        method = method,
        path = path,
        host = host,
        scheme = scheme,
        remoteIpAddress = remoteIpAddress,
        contentType = contentType,
        cookieHeader = cookieHeader,
        body = body,
        target = target,
        queryString = queryString,
        routePattern = Option.None,
        headerData = headerData
    };
}

public Request RequestNew(
    string method,
    string target,
    string host,
    string contentType,
    string cookieHeader,
    string body
)
{
    return RequestNewWithHeaders(
        method,
        target,
        host,
        contentType,
        cookieHeader,
        body,
        ""
    );
}

public Option<string> Request.Header(Request self, string name)
{
    nuint length = self.headerData.Length;
    nuint cursor = 0;
    while (cursor < length)
    {
        nuint nameStart = cursor;
        while (cursor < length &&
            StringByteAt(self.headerData, cursor) != 0)
        {
            cursor += 1;
        }
        if (cursor >= length)
        {
            return Option.None;
        }
        string headerName = StringSlice(
            self.headerData, nameStart, cursor
        );
        cursor += 1;

        nuint valueStart = cursor;
        while (cursor < length &&
            StringByteAt(self.headerData, cursor) != 0)
        {
            cursor += 1;
        }
        if (cursor >= length)
        {
            return Option.None;
        }
        if (AsciiEqualIgnoringCase(headerName, name))
        {
            return Option.Some(StringSlice(
                self.headerData, valueStart, cursor
            ));
        }
        cursor += 1;
    }
    return Option.None;
}

private Request ApplyConfiguredForwardedHeaders(
    Option<ForwardedHeadersOptions> configured,
    Request request
)
{
    switch (configured)
    {
        case Option.Some(options): {
            ForwardedOrigin origin = new()
            {
                Host = request.host,
                Scheme = request.scheme,
                RemoteIpAddress = request.remoteIpAddress
            };
            origin = ApplyForwardedHeaders(
                origin,
                request.Header("X-Forwarded-For"),
                request.Header("X-Forwarded-Host"),
                request.Header("X-Forwarded-Proto"),
                options
            );
            request.host = origin.Host;
            request.scheme = origin.Scheme;
            request.remoteIpAddress = origin.RemoteIpAddress;
            return request;
        }
        case Option.None: {
            return request;
        }
    }
}

public Option<string> Request.RouteValue(Request self, string name)
{
    switch (self.routePattern)
    {
        case Option.Some(pattern): {
            return pattern.Parameter(self.path, name);
        }
        case Option.None: { return Option.None; }
    }
}

private void SelectRoute(ref Request request, RoutePattern pattern)
{
    Option<RoutePattern> selected = Option.Some(pattern);
    request.routePattern = selected;
}

// Query names and values are borrowed raw target bytes. Percent decoding is a
// separate operation so lookup itself stays allocation-free.
public Option<string> Request.QueryRaw(Request self, string name)
{
    nuint length = self.queryString.Length;
    nuint pairStart = 0;

    while (pairStart < length)
    {
        nuint pairEnd = pairStart;
        while (pairEnd < length &&
            StringByteAt(self.queryString, pairEnd) != 38)
        {
            pairEnd += 1;
        }

        nuint separator = pairStart;
        while (separator < pairEnd &&
            StringByteAt(self.queryString, separator) != 61)
        {
            separator += 1;
        }

        if (StringSlice(self.queryString, pairStart, separator) == name)
        {
            if (separator < pairEnd)
            {
                return Option.Some(StringSlice(
                    self.queryString, separator + 1, pairEnd
                ));
            }
            return Option.Some("");
        }

        pairStart = pairEnd + 1;
    }

    return Option.None;
}

public Option<string> Request.Cookie(Request self, string name)
{
    if (name.Length == 0)
    {
        return Option.None;
    }
    nuint length = self.cookieHeader.Length;
    nuint pairStart = 0;
    while (pairStart < length)
    {
        while (pairStart < length &&
            (StringByteAt(self.cookieHeader, pairStart) == 32 ||
                StringByteAt(self.cookieHeader, pairStart) == 9 ||
                StringByteAt(self.cookieHeader, pairStart) == 59))
        {
            pairStart += 1;
        }
        if (pairStart >= length)
        {
            break;
        }

        nuint pairEnd = pairStart;
        while (pairEnd < length &&
            StringByteAt(self.cookieHeader, pairEnd) != 59)
        {
            pairEnd += 1;
        }
        nuint trimmedEnd = pairEnd;
        while (trimmedEnd > pairStart &&
            (StringByteAt(self.cookieHeader, trimmedEnd - 1) == 32 ||
                StringByteAt(self.cookieHeader, trimmedEnd - 1) == 9))
        {
            trimmedEnd -= 1;
        }

        nuint separator = pairStart;
        while (separator < trimmedEnd &&
            StringByteAt(self.cookieHeader, separator) != 61)
        {
            separator += 1;
        }
        if (separator < trimmedEnd)
        {
            nuint nameEnd = separator;
            while (nameEnd > pairStart &&
                (StringByteAt(self.cookieHeader, nameEnd - 1) == 32 ||
                    StringByteAt(self.cookieHeader, nameEnd - 1) == 9))
            {
                nameEnd -= 1;
            }
            if (StringSlice(
                self.cookieHeader,
                pairStart,
                nameEnd
            ) == name)
            {
                nuint valueStart = separator + 1;
                while (valueStart < trimmedEnd &&
                    (StringByteAt(self.cookieHeader, valueStart) == 32 ||
                        StringByteAt(self.cookieHeader, valueStart) == 9))
                {
                    valueStart += 1;
                }
                if (trimmedEnd > valueStart + 1 &&
                    StringByteAt(self.cookieHeader, valueStart) == 34 &&
                    StringByteAt(self.cookieHeader, trimmedEnd - 1) == 34)
                {
                    valueStart += 1;
                    trimmedEnd -= 1;
                }
                return Option.Some(StringSlice(
                    self.cookieHeader,
                    valueStart,
                    trimmedEnd
                ));
            }
        }
        pairStart = pairEnd + 1;
    }
    return Option.None;
}

private int HexValue(byte value)
{
    if (value >= 48 && value <= 57)
    {
        return (int)value - 48;
    }
    if (value >= 65 && value <= 70)
    {
        return (int)value - 65 + 10;
    }
    if (value >= 97 && value <= 102)
    {
        return (int)value - 97 + 10;
    }
    return -1;
}

public Result<string, string> UrlDecode(string value)
{
    StringBuilder output = new();
    nuint length = value.Length;
    nuint index = 0;
    while (index < length)
    {
        byte current = StringByteAt(value, index);
        if (current == 43)
        {
            output.AppendByte(32);
        }
        else if (current == 37)
        {
            if (index + 2 >= length)
            {
                return Result.Err("invalid percent escape");
            }
            int high = HexValue(StringByteAt(value, index + 1));
            int low = HexValue(StringByteAt(value, index + 2));
            if (high < 0 || low < 0)
            {
                return Result.Err("invalid percent escape");
            }
            output.AppendByte((byte)(high * 16 + low));
            index += 2;
        }
        else
        {
            output.AppendByte(current);
        }
        index += 1;
    }
    return Result.Ok(output.ToString());
}

public Result<Option<string>, string> FormValue(
    string encoded,
    string name
)
{
    nuint length = encoded.Length;
    nuint pairStart = 0;
    while (pairStart < length)
    {
        nuint pairEnd = pairStart;
        while (pairEnd < length &&
            StringByteAt(encoded, pairEnd) != 38)
        {
            pairEnd += 1;
        }

        nuint separator = pairStart;
        while (separator < pairEnd &&
            StringByteAt(encoded, separator) != 61)
        {
            separator += 1;
        }

        Result<string, string> decodedName = UrlDecode(
            StringSlice(encoded, pairStart, separator)
        );
        switch (decodedName)
        {
            case Result.Ok(fieldName): {
                if (fieldName == name)
                {
                    nuint valueStart = pairEnd;
                    if (separator < pairEnd)
                    {
                        valueStart = separator + 1;
                    }
                    switch (UrlDecode(
                        StringSlice(encoded, valueStart, pairEnd)
                    ))
                    {
                        case Result.Ok(decoded): {
                            return Result.Ok(Option.Some(decoded));
                        }
                        case Result.Err(error): {
                            return Result.Err(error);
                        }
                    }
                }
            }
            case Result.Err(error): {
                return Result.Err(error);
            }
        }

        pairStart = pairEnd + 1;
    }
    return Result.Ok(Option.None);
}

public Result<UrlValues, string> UrlValuesParse(string encoded)
{
    List<UrlValue> values = new();
    nuint length = encoded.Length;
    nuint pairStart = 0;
    while (pairStart < length)
    {
        nuint pairEnd = pairStart;
        while (pairEnd < length &&
            StringByteAt(encoded, pairEnd) != 38)
        {
            pairEnd += 1;
        }

        if (pairEnd > pairStart)
        {
            nuint separator = pairStart;
            while (separator < pairEnd &&
                StringByteAt(encoded, separator) != 61)
            {
                separator += 1;
            }

            nuint valueStart = pairEnd;
            if (separator < pairEnd)
            {
                valueStart = separator + 1;
            }

            switch (UrlDecode(
                StringSlice(encoded, pairStart, separator)
            ))
            {
                case Result.Ok(name): {
                    switch (UrlDecode(
                        StringSlice(encoded, valueStart, pairEnd)
                    ))
                    {
                        case Result.Ok(value): {
                            UrlValue entry = new()
                            {
                                name = name,
                                value = value
                            };
                            values.Add(entry);
                        }
                        case Result.Err(error): {
                            return Result.Err(error);
                        }
                    }
                }
                case Result.Err(error): {
                    return Result.Err(error);
                }
            }
        }

        pairStart = pairEnd + 1;
    }

    return Result.Ok(new()
    {
        values = values
    });
}

private Option<string> UrlValuesGet(
    List<UrlValue> values,
    string name
)
{
    foreach (UrlValue value in values)
    {
        if (value.name == name)
        {
            return Option.Some(value.value);
        }
    }
    return Option.None;
}

public Option<string> UrlValues.Get(UrlValues self, string name)
{
    return UrlValuesGet(self.values, name);
}

public nuint UrlValues.Count(UrlValues self)
{
    return self.values.Count;
}

private byte AsciiLower(byte value)
{
    if (value >= 65 && value <= 90)
    {
        return value + 32;
    }
    return value;
}

private bool AsciiEqualIgnoringCase(string left, string right)
{
    if (left.Length != right.Length)
    {
        return false;
    }
    for (nuint index = 0; index < left.Length; index++)
    {
        if (AsciiLower(StringByteAt(left, index)) !=
            AsciiLower(StringByteAt(right, index)))
        {
            return false;
        }
    }
    return true;
}

private bool HeaderNameByte(byte value)
{
    return (value >= 65 && value <= 90) ||
        (value >= 97 && value <= 122) ||
        (value >= 48 && value <= 57) ||
        value == 33 || value == 35 || value == 36 || value == 37 ||
        value == 38 || value == 39 || value == 42 || value == 43 ||
        value == 45 || value == 46 || value == 94 || value == 95 ||
        value == 96 || value == 124 || value == 126;
}

private bool ResponseHeaderNameValid(string name)
{
    if (name.Length == 0)
    {
        return false;
    }
    for (nuint index = 0; index < name.Length; index++)
    {
        if (!HeaderNameByte(StringByteAt(name, index)))
        {
            return false;
        }
    }
    return !AsciiEqualIgnoringCase(name, "Content-Length") &&
        !AsciiEqualIgnoringCase(name, "Content-Type") &&
        !AsciiEqualIgnoringCase(name, "Connection") &&
        !AsciiEqualIgnoringCase(name, "Transfer-Encoding") &&
        !AsciiEqualIgnoringCase(name, "X-Content-Type-Options");
}

private bool ResponseHeaderValueValid(string value)
{
    for (nuint index = 0; index < value.Length; index++)
    {
        byte current = StringByteAt(value, index);
        if ((current < 32 && current != 9) || current == 127)
        {
            return false;
        }
    }
    return true;
}

public Result<ResponseHeader, string> ResponseHeader(
    string name,
    string value
)
{
    if (!ResponseHeaderNameValid(name))
    {
        return Result.Err("invalid or transport-owned response header name");
    }
    if (!ResponseHeaderValueValid(value))
    {
        return Result.Err("response header value contains control bytes");
    }
    return Result.Ok(new()
    {
        name = name,
        value = value
    });
}

private bool CookieValueValid(string value)
{
    for (nuint index = 0; index < value.Length; index++)
    {
        byte current = StringByteAt(value, index);
        if (current < 33 || current > 126 || current == 34 ||
            current == 44 || current == 59 || current == 92)
        {
            return false;
        }
    }
    return true;
}

public Result<ResponseHeader, string> ResponseCookie(
    string name,
    string value
)
{
    return ResponseCookieWith(name, value, CookieOptions());
}

public CookieOptions CookieOptions()
{
    return new()
    {
        path = "/",
        domain = Option.None,
        maxAge = Option.None,
        httpOnly = true,
        secure = true,
        sameSite = CookieSameSite.Lax
    };
}

private bool CookieScopeValid(string value)
{
    if (value.Length == 0)
    {
        return false;
    }
    for (nuint index = 0; index < value.Length; index++)
    {
        byte current = StringByteAt(value, index);
        if (current < 33 || current > 126 || current == 59)
        {
            return false;
        }
    }
    return true;
}

public Result<ResponseHeader, string> ResponseCookieWith(
    string name,
    string value,
    CookieOptions options
)
{
    if (name.Length == 0)
    {
        return Result.Err("invalid cookie name");
    }
    for (nuint index = 0; index < name.Length; index++)
    {
        if (!HeaderNameByte(StringByteAt(name, index)))
        {
            return Result.Err("invalid cookie name");
        }
    }
    if (!CookieValueValid(value))
    {
        return Result.Err("invalid cookie value");
    }
    if (!CookieScopeValid(options.path) ||
        StringByteAt(options.path, 0) != 47)
    {
        return Result.Err("cookie path must begin with /");
    }
    if (!options.secure)
    {
        switch (options.sameSite)
        {
            case CookieSameSite.None: {
                return Result.Err("SameSite=None cookies must be Secure");
            }
            case CookieSameSite.Strict: {
            }
            case CookieSameSite.Lax: {
            }
        }
    }
    StringBuilder output = new();
    output.Append(name);
    output.Append("=");
    output.Append(value);
    output.Append("; Path=");
    output.Append(options.path);
    switch (options.domain)
    {
        case Option.Some(domain): {
            if (!CookieScopeValid(domain))
            {
                return Result.Err("invalid cookie domain");
            }
            output.Append("; Domain=");
            output.Append(domain);
        }
        case Option.None: {
        }
    }
    switch (options.maxAge)
    {
        case Option.Some(maxAge): {
            string rendered = maxAge.ToString();
            output.Append("; Max-Age=");
            output.Append(rendered);
        }
        case Option.None: {
        }
    }
    if (options.httpOnly) { output.Append("; HttpOnly"); }
    if (options.secure) { output.Append("; Secure"); }
    switch (options.sameSite)
    {
        case CookieSameSite.Strict: { output.Append("; SameSite=Strict"); }
        case CookieSameSite.Lax: { output.Append("; SameSite=Lax"); }
        case CookieSameSite.None: { output.Append("; SameSite=None"); }
    }
    return Result.Ok(new()
    {
        name = "Set-Cookie",
        value = output.ToString()
    });
}

public Result<ResponseHeader, string> ResponseDeleteCookie(
    string name,
    CookieOptions options
)
{
    Option<long> expired = Option.Some(0);
    options.maxAge = expired;
    return ResponseCookieWith(name, "", options);
}

public void Response.AddHeader(ref Response self, ResponseHeader header)
{
    self.headers.Add(header);
}

private bool MediaTypeIs(string value, string expected)
{
    nuint length = value.Length;
    nuint expectedLength = expected.Length;
    nuint index = 0;
    while (index < length &&
         (StringByteAt(value, index) == 32 ||
            StringByteAt(value, index) == 9))
    {
        index += 1;
    }

    if (expectedLength > length - index)
    {
        return false;
    }
    for (nuint expectedIndex = 0;
        expectedIndex < expectedLength;
        expectedIndex++)
    {
        if (AsciiLower(StringByteAt(value, index + expectedIndex)) !=
            AsciiLower(StringByteAt(expected, expectedIndex)))
        {
            return false;
        }
    }

    index += expectedLength;
    while (index < length &&
         (StringByteAt(value, index) == 32 ||
            StringByteAt(value, index) == 9))
    {
        index += 1;
    }
    return index == length || StringByteAt(value, index) == 59;
}

private bool IsUrlencodedForm(string contentType)
{
    return MediaTypeIs(
        contentType,
        "application/x-www-form-urlencoded"
    );
}

public Result<string, string> Request.Json(Request self)
{
    if (!MediaTypeIs(self.contentType, "application/json"))
    {
        return Result.Err("expected application/json");
    }
    return Result.Ok(self.body);
}

public Result<Option<string>, string> Request.Query(
    Request self,
    string name
)
{
    return FormValue(self.queryString, name);
}

public Result<Option<string>, string> Request.Form(
    Request self,
    string name
)
{
    if (!IsUrlencodedForm(self.contentType))
    {
        return Result.Err(
            "expected application/x-www-form-urlencoded"
        );
    }
    return FormValue(self.body, name);
}

public Result<UrlValues, string> Request.QueryValues(Request self)
{
    return UrlValuesParse(self.queryString);
}

public Result<UrlValues, string> Request.FormValues(Request self)
{
    if (!IsUrlencodedForm(self.contentType))
    {
        return Result.Err(
            "expected application/x-www-form-urlencoded"
        );
    }
    return UrlValuesParse(self.body);
}

public App AppNew()
{
    List<Route> routes = new();
    List<BuildPage> pages = new();
    List<RequestFilter> filters = new();
    List<HtmlMiddleware> htmlMiddleware = new();
    List<StaticDirectory> staticDirectories = new();
    return new()
    {
        routes = routes,
        pages = pages,
        filters = filters,
        htmlMiddleware = htmlMiddleware,
        staticDirectories = staticDirectories,
        fallback = RouteHandler.Sync(DefaultNotFound),
        exceptionHandler = DefaultExceptionResponse,
        forwardedHeaders = Option.None
    };
}

public void App.MapFallback(ref App self, Handler handler)
{
    self.fallback = RouteHandler.Sync(handler);
}

public void App.MapFallback(ref App self, AsyncHandler handler)
{
    self.fallback = RouteHandler.Async(handler);
}

public void App.UseForwardedHeaders(
    ref App self,
    ForwardedHeadersOptions options
)
{
    ValidateForwardedHeadersOptions(options);
    Option<ForwardedHeadersOptions> configured = Option.Some(options);
    self.forwardedHeaders = configured;
}

public void App.OnException(
    ref App self,
    ExceptionHandler handler
)
{
    self.exceptionHandler = handler;
}

public void App.MountStatic(
    ref App self,
    string urlPrefix,
    string root,
    StaticFileOptions options,
    StaticResolver resolver
)
{
    self.staticDirectories.Add(new()
    {
        urlPrefix = urlPrefix,
        root = root,
        options = options,
        resolver = resolver
    });
}

public List<string> App.StaticRoots(App self)
{
    List<string> roots = new();
    foreach (StaticDirectory directory in self.staticDirectories)
    {
        roots.Add(directory.root);
    }
    return roots;
}

public void App.UseFilter(ref App self, RequestFilter filter)
{
    self.filters.Add(filter);
}

public void App.AfterHtml(ref App self, HtmlMiddleware middleware)
{
    self.htmlMiddleware.Add(middleware);
}

private void App.MapEndpoint(
    ref App self,
    RoutePattern pattern,
    List<string> methods,
    RouteHandler handler
)
{
    foreach (Route existing in self.routes)
    {
        if (MethodsOverlap(existing.methods, methods) &&
            existing.pattern.ConflictsWith(pattern))
        {
            throw new ArgumentException(
                "route conflicts with an existing endpoint"
            );
        }
    }
    self.routes.Add(new()
    {
        methods = methods,
        pattern = pattern,
        handler = handler
    });
}

private void App.MapMethod(
    ref App self,
    string method,
    string path,
    Handler handler
)
{
    if (!HttpMethodValid(method))
    {
        throw new ArgumentException(
            "HTTP method must be a non-empty uppercase token"
        );
    }
    RoutePattern pattern = ParseRoutePattern(path);
    List<string> methods = new();
    methods.Add(method);
    self.MapEndpoint(pattern, methods, RouteHandler.Sync(handler));
}

private void App.MapMethodAsync(
    ref App self,
    string method,
    string path,
    AsyncHandler handler
)
{
    if (!HttpMethodValid(method))
    {
        throw new ArgumentException(
            "HTTP method must be a non-empty uppercase token"
        );
    }
    RoutePattern pattern = ParseRoutePattern(path);
    List<string> methods = new();
    methods.Add(method);
    self.MapEndpoint(pattern, methods, RouteHandler.Async(handler));
}

public void App.MapMethods(
    ref App self,
    string path,
    List<string> methods,
    Handler handler
)
{
    if (methods.Count == 0)
    {
        throw new ArgumentException("endpoint requires an HTTP method");
    }
    for (nuint index = 0; index < methods.Count; index += 1)
    {
        string method = methods[index];
        if (method.Length == 0)
        {
            throw new ArgumentException("HTTP method cannot be empty");
        }
        if (!HttpMethodValid(method))
        {
            throw new ArgumentException(
                "HTTP method must be an uppercase token"
            );
        }
        for (nuint other = 0; other < index; other += 1)
        {
            if (methods[other] == method)
            {
                throw new ArgumentException("HTTP method is duplicated");
            }
        }
    }
    RoutePattern pattern = ParseRoutePattern(path);
    self.MapEndpoint(pattern, methods, RouteHandler.Sync(handler));
}

public void App.MapMethods(
    ref App self,
    string path,
    List<string> methods,
    AsyncHandler handler
)
{
    if (methods.Count == 0)
    {
        throw new ArgumentException("endpoint requires an HTTP method");
    }
    for (nuint index = 0; index < methods.Count; index += 1)
    {
        string method = methods[index];
        if (method.Length == 0)
        {
            throw new ArgumentException("HTTP method cannot be empty");
        }
        if (!HttpMethodValid(method))
        {
            throw new ArgumentException(
                "HTTP method must be an uppercase token"
            );
        }
        for (nuint other = 0; other < index; other += 1)
        {
            if (methods[other] == method)
            {
                throw new ArgumentException("HTTP method is duplicated");
            }
        }
    }
    RoutePattern pattern = ParseRoutePattern(path);
    self.MapEndpoint(pattern, methods, RouteHandler.Async(handler));
}

public void App.MapGet(ref App self, string path, Handler handler)
{
    self.MapMethod("GET", path, handler);
    if (BuildPagePathValid(path) && !RouteHasParameter(path))
    {
        BuildPagesAdd(self.pages, path);
    }
}

public void App.MapGet(ref App self, string path, AsyncHandler handler)
{
    self.MapMethodAsync("GET", path, handler);
    if (BuildPagePathValid(path) && !RouteHasParameter(path))
    {
        BuildPagesAdd(self.pages, path);
    }
}

public Result<bool, string> App.AddBuildPage(ref App self, string path)
{
    if (!BuildPagePathValid(path) || RouteHasParameter(path))
    {
        return Result.Err("build page requires one safe concrete URL path");
    }
    if (BuildPagesContains(self.pages, path))
    {
        return Result.Err("duplicate build page path");
    }
    if (BuildPageMatchesRoutes(self.routes, path))
    {
        BuildPagesAdd(self.pages, path);
        return Result.Ok(true);
    }
    return Result.Err("build page does not match a parameterized GET route");
}

public Result<bool, string> App.TryMapGetFrom(
    ref App self,
    string pattern,
    Handler handler,
    BuildSource source
)
{
    if (!RouteHasParameter(pattern))
    {
        return Result.Err(
            "parameterized build source requires a parameterized GET route"
        );
    }
    BuildSource buildSource = source;
    List<string> paths = buildSource();
    foreach (string path in paths)
    {
        if (!BuildPagePathValid(path) || RouteHasParameter(path))
        {
            return Result.Err(
                "build source returned an unsafe or parameterized URL path"
            );
        }
        if (!RouteMatches(pattern, path))
        {
            return Result.Err(
                "build source URL does not match its parameterized GET route"
            );
        }
        if (BuildPagesContains(self.pages, path))
        {
            return Result.Err("duplicate build page path");
        }
        BuildPagesAdd(self.pages, path);
    }
    self.MapMethod("GET", pattern, handler);
    return Result.Ok(true);
}

public void App.MapGet(
    ref App self,
    string pattern,
    Handler handler,
    BuildSource source
)
{
    bool ignored = LimeResultOrThrow(
        self.TryMapGetFrom(pattern, handler, source)
    );
}

public void App.MapPost(ref App self, string path, Handler handler)
{
    self.MapMethod("POST", path, handler);
}

public void App.MapPost(ref App self, string path, AsyncHandler handler)
{
    self.MapMethodAsync("POST", path, handler);
}

public void App.MapPut(ref App self, string path, Handler handler)
{
    self.MapMethod("PUT", path, handler);
}

public void App.MapPut(ref App self, string path, AsyncHandler handler)
{
    self.MapMethodAsync("PUT", path, handler);
}

public void App.MapPatch(ref App self, string path, Handler handler)
{
    self.MapMethod("PATCH", path, handler);
}

public void App.MapPatch(ref App self, string path, AsyncHandler handler)
{
    self.MapMethodAsync("PATCH", path, handler);
}

public void App.MapDelete(ref App self, string path, Handler handler)
{
    self.MapMethod("DELETE", path, handler);
}

public void App.MapDelete(ref App self, string path, AsyncHandler handler)
{
    self.MapMethodAsync("DELETE", path, handler);
}

public void App.MapHead(ref App self, string path, Handler handler)
{
    self.MapMethod("HEAD", path, handler);
}

public void App.MapHead(ref App self, string path, AsyncHandler handler)
{
    self.MapMethodAsync("HEAD", path, handler);
}

private Html ApplyHtml(
    List<HtmlMiddleware> middleware,
    Request request,
    Html page
)
{
    foreach (HtmlMiddleware transform in middleware)
    {
        page = transform(request, page);
    }
    return page;
}

private Response ApplyHtmlMiddleware(
    List<HtmlMiddleware> middleware,
    Request request,
    Response response
)
{
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    switch (body)
    {
        case ResponseBody.Empty: {
            return new()
            {
                status = status,
                body = ResponseBody.Empty,
                headers = headers
            };
        }
        case ResponseBody.Html(page): {
            return new()
            {
                status = status,
                body = ResponseBody.Html(
                    ApplyHtml(middleware, request, page)
                ),
                headers = headers
            };
        }
        case ResponseBody.Text(text): {
            return new()
            {
                status = status,
                body = ResponseBody.Text(text),
                headers = headers
            };
        }
        case ResponseBody.Css(text): {
            return new()
            {
                status = status,
                body = ResponseBody.Css(text),
                headers = headers
            };
        }
        case ResponseBody.Asset(asset): {
            return new()
            {
                status = status,
                body = ResponseBody.Asset(asset),
                headers = headers
            };
        }
        case ResponseBody.Stream(stream): {
            return new()
            {
                status = status,
                body = ResponseBody.Stream(stream),
                headers = headers
            };
        }
        case ResponseBody.File(file): {
            return new()
            {
                status = status,
                body = ResponseBody.File(file),
                headers = headers
            };
        }
    }
}

private FilterResult ApplyRequestFilters(
    List<RequestFilter> filters,
    Request request
)
{
    foreach (RequestFilter filter in filters)
    {
        switch (filter(request))
        {
            case FilterResult.Continue: {
            }
            case FilterResult.Respond(response): {
                return FilterResult.Respond(response);
            }
        }
    }
    return FilterResult.Continue;
}

private Response DispatchAppUnchecked(App self, Request request)
{
    request = ApplyConfiguredForwardedHeaders(self.forwardedHeaders, request);
    switch (ApplyRequestFilters(self.filters, request))
    {
        case FilterResult.Continue: {
        }
        case FilterResult.Respond(response): {
            return ApplyHtmlMiddleware(
                self.htmlMiddleware,
                request,
                response
            );
        }
    }

    return DispatchRoutes(
        self.routes,
        self.staticDirectories,
        self.htmlMiddleware,
        self.fallback,
        request
    );
}

public Response App.Dispatch(App self, Request request)
{
    try
    {
        return DispatchAppUnchecked(self, request);
    }
    catch (Exception error)
    {
        ExceptionHandler handler = self.exceptionHandler;
        return handler(error);
    }
}

private async Task<Response> DispatchAppUncheckedAsync(
    App self,
    Request request
)
{
    request = ApplyConfiguredForwardedHeaders(self.forwardedHeaders, request);
    switch (ApplyRequestFilters(self.filters, request))
    {
        case FilterResult.Continue: {
        }
        case FilterResult.Respond(response): {
            return ApplyHtmlMiddleware(
                self.htmlMiddleware,
                request,
                response
            );
        }
    }

    return await DispatchRoutesAsync(
        self.routes,
        self.staticDirectories,
        self.htmlMiddleware,
        self.fallback,
        request
    );
}

public async Task<Response> App.DispatchAsync(App self, Request request)
{
    try
    {
        return await DispatchAppUncheckedAsync(self, request);
    }
    catch (Exception error)
    {
        ExceptionHandler handler = self.exceptionHandler;
        return handler(error);
    }
}

private Response DispatchAppFallbackUnchecked(App self, Request request)
{
    request = ApplyConfiguredForwardedHeaders(self.forwardedHeaders, request);
    switch (ApplyRequestFilters(self.filters, request))
    {
        case FilterResult.Continue: {
        }
        case FilterResult.Respond(response): {
            return ApplyHtmlMiddleware(
                self.htmlMiddleware,
                request,
                response
            );
        }
    }
    return ApplyHtmlMiddleware(
        self.htmlMiddleware,
        request,
        InvokeRouteHandler(self.fallback, request)
    );
}

public Response App.DispatchFallback(App self, Request request)
{
    try
    {
        return DispatchAppFallbackUnchecked(self, request);
    }
    catch (Exception error)
    {
        ExceptionHandler handler = self.exceptionHandler;
        return handler(error);
    }
}

public async Task<Response> App.DispatchFallbackAsync(
    App self,
    Request request
)
{
    try
    {
        request = ApplyConfiguredForwardedHeaders(
            self.forwardedHeaders, request
        );
        switch (ApplyRequestFilters(self.filters, request))
        {
            case FilterResult.Continue: { }
            case FilterResult.Respond(response): {
                return ApplyHtmlMiddleware(
                    self.htmlMiddleware, request, response
                );
            }
        }
        Response response = await InvokeRouteHandlerAsync(
            self.fallback, request
        );
        return ApplyHtmlMiddleware(
            self.htmlMiddleware, request, response
        );
    }
    catch (Exception error)
    {
        ExceptionHandler handler = self.exceptionHandler;
        return handler(error);
    }
}

private bool MethodContains(List<string> methods, string method)
{
    foreach (string existing in methods)
    {
        if (existing == method)
        {
            return true;
        }
    }
    return false;
}

private bool MethodsOverlap(List<string> left, List<string> right)
{
    foreach (string method in left)
    {
        if (MethodContains(right, method)) { return true; }
    }
    return false;
}

private bool HttpMethodValid(string method)
{
    if (method.Length == 0)
    {
        return false;
    }
    for (nuint index = 0; index < method.Length; index += 1)
    {
        byte current = method[index];
        bool upper = current >= 65 && current <= 90;
        bool digit = current >= 48 && current <= 57;
        bool symbol = current == 33 || current == 35 || current == 36 ||
            current == 37 || current == 38 || current == 39 ||
            current == 42 || current == 43 || current == 45 ||
            current == 46 || current == 94 || current == 95 ||
            current == 96 || current == 124 || current == 126;
        if (!upper && !digit && !symbol)
        {
            return false;
        }
    }
    return true;
}

private void AddAllowedMethod(ref List<string> methods, string method)
{
    if (!MethodContains(methods, method))
    {
        methods.Add(method);
    }
}

private Response WithAllowedMethods(Response response, List<string> methods)
{
    StringBuilder value = new();
    for (nuint index = 0; index < methods.Count; index += 1)
    {
        if (index > 0)
        {
            value.Append(", ");
        }
        value.Append(methods[index]);
    }
    ResponseHeader allow = LimeResultOrThrow(
        ResponseHeader("Allow", value.ToString())
    );
    response.AddHeader(allow);
    return response;
}

private Option<Route> BestRoute(
    List<Route> routes,
    string method,
    string path
)
{
    Option<Route> selected = Option.None;
    foreach (Route route in routes)
    {
        if (!MethodContains(route.methods, method) ||
            !route.pattern.IsMatch(path))
        {
            continue;
        }
        switch (selected)
        {
            case Option.None: { selected = Option.Some(route); }
            case Option.Some(current): {
                int precedence = route.pattern.ComparePrecedence(
                    current.pattern
                );
                if (precedence < 0)
                {
                    selected = Option.Some(route);
                }
                else if (precedence == 0)
                {
                    throw new InvalidOperationException(
                        "request matched multiple endpoints with equal precedence"
                    );
                }
            }
        }
    }
    return selected;
}

private Response InvokeRouteHandler(
    RouteHandler routeHandler,
    Request request
)
{
    switch (routeHandler)
    {
        case RouteHandler.Sync(handler): {
            Handler invoke = handler;
            return invoke(request);
        }
        case RouteHandler.Async(handler): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
    }
}

private async Task<Response> InvokeRouteHandlerAsync(
    RouteHandler routeHandler,
    Request request
)
{
    switch (routeHandler)
    {
        case RouteHandler.Sync(handler): {
            Handler invoke = handler;
            return invoke(request);
        }
        case RouteHandler.Async(handler): {
            AsyncHandler invoke = handler;
            return await invoke(request);
        }
    }
}

private Response DispatchRoutes(
    List<Route> routes,
    List<StaticDirectory> staticDirectories,
    List<HtmlMiddleware> middleware,
    RouteHandler fallback,
    Request request
)
{
    switch (BestRoute(routes, request.method, request.path))
    {
        case Option.Some(route): {
                SelectRoute(request, route.pattern);
                return ApplyHtmlMiddleware(
                    middleware,
                    request,
                    InvokeRouteHandler(route.handler, request)
                );
        }
        case Option.None: { }
    }

    if (request.method == "HEAD")
    {
        switch (BestRoute(routes, "GET", request.path))
        {
            case Option.Some(route): {
                SelectRoute(request, route.pattern);
                return ApplyHtmlMiddleware(
                    middleware,
                    request,
                    InvokeRouteHandler(route.handler, request)
                );
            }
            case Option.None: { }
        }
    }

    List<string> allowedMethods = new();
    foreach (Route route in routes)
    {
        if (route.pattern.IsMatch(request.path))
        {
            foreach (string method in route.methods)
            {
                AddAllowedMethod(allowedMethods, method);
                if (method == "GET")
                {
                    AddAllowedMethod(allowedMethods, "HEAD");
                }
            }
        }
    }
    if (allowedMethods.Count > 0)
    {
        AddAllowedMethod(allowedMethods, "OPTIONS");
        if (request.method == "OPTIONS")
        {
            return ApplyHtmlMiddleware(
                middleware,
                request,
                WithAllowedMethods(
                    Results.NoContent(), allowedMethods
                )
            );
        }
        return ApplyHtmlMiddleware(
            middleware,
            request,
            WithAllowedMethods(
                Results.MethodNotAllowed(
                    <h1>Method not allowed</h1>
                ),
                allowedMethods
            )
        );
    }


    if (request.method == "GET" || request.method == "HEAD")
    {
        foreach (StaticDirectory directory in staticDirectories)
        {
            if (!request.path.StartsWith(directory.urlPrefix))
            {
                continue;
            }
            StaticResolver resolver = directory.resolver;
            switch (resolver(
                directory.urlPrefix, directory.root,
                directory.options, request
            ))
            {
                case Option.Some(response): {
                    return ApplyHtmlMiddleware(
                        middleware, request, response
                    );
                }
                case Option.None: {
                }
            }
        }
    }

    return ApplyHtmlMiddleware(
        middleware,
        request,
        InvokeRouteHandler(fallback, request)
    );
}

private async Task<Response> DispatchRoutesAsync(
    List<Route> routes,
    List<StaticDirectory> staticDirectories,
    List<HtmlMiddleware> middleware,
    RouteHandler fallback,
    Request request
)
{
    switch (BestRoute(routes, request.method, request.path))
    {
        case Option.Some(route): {
            SelectRoute(request, route.pattern);
            Response response = await InvokeRouteHandlerAsync(
                route.handler, request
            );
            return ApplyHtmlMiddleware(middleware, request, response);
        }
        case Option.None: { }
    }

    if (request.method == "HEAD")
    {
        switch (BestRoute(routes, "GET", request.path))
        {
            case Option.Some(route): {
                SelectRoute(request, route.pattern);
                Response response = await InvokeRouteHandlerAsync(
                    route.handler, request
                );
                return ApplyHtmlMiddleware(middleware, request, response);
            }
            case Option.None: { }
        }
    }

    List<string> allowedMethods = new();
    foreach (Route route in routes)
    {
        if (route.pattern.IsMatch(request.path))
        {
            foreach (string method in route.methods)
            {
                AddAllowedMethod(allowedMethods, method);
                if (method == "GET")
                {
                    AddAllowedMethod(allowedMethods, "HEAD");
                }
            }
        }
    }
    if (allowedMethods.Count > 0)
    {
        AddAllowedMethod(allowedMethods, "OPTIONS");
        if (request.method == "OPTIONS")
        {
            return ApplyHtmlMiddleware(
                middleware,
                request,
                WithAllowedMethods(Results.NoContent(), allowedMethods)
            );
        }
        return ApplyHtmlMiddleware(
            middleware,
            request,
            WithAllowedMethods(
                Results.MethodNotAllowed(<h1>Method not allowed</h1>),
                allowedMethods
            )
        );
    }

    if (request.method == "GET" || request.method == "HEAD")
    {
        foreach (StaticDirectory directory in staticDirectories)
        {
            if (!request.path.StartsWith(directory.urlPrefix))
            {
                continue;
            }
            StaticResolver resolver = directory.resolver;
            switch (resolver(
                directory.urlPrefix,
                directory.root,
                directory.options,
                request
            ))
            {
                case Option.Some(response): {
                    return ApplyHtmlMiddleware(
                        middleware, request, response
                    );
                }
                case Option.None: { }
            }
        }
    }

    Response response = await InvokeRouteHandlerAsync(fallback, request);
    return ApplyHtmlMiddleware(middleware, request, response);
}

public StatefulApp<State> StatefulAppNew<State>(
    State state,
    StatefulHandler<State> fallback
)
{
    List<StatefulRoute<State>> routes = new();
    List<BuildPage> pages = new();
    List<StatefulRequestFilter<State>> filters = new();
    List<StatefulHtmlMiddleware<State>> htmlMiddleware = new();
    List<StaticDirectory> staticDirectories = new();
    return new()
    {
        state = state,
        routes = routes,
        pages = pages,
        filters = filters,
        htmlMiddleware = htmlMiddleware,
        staticDirectories = staticDirectories,
        fallback = fallback,
        exceptionHandler = DefaultExceptionResponse,
        forwardedHeaders = Option.None
    };
}

public void StatefulApp.UseForwardedHeaders<State>(
    ref StatefulApp<State> self,
    ForwardedHeadersOptions options
)
{
    ValidateForwardedHeadersOptions(options);
    Option<ForwardedHeadersOptions> configured = Option.Some(options);
    self.forwardedHeaders = configured;
}

public void StatefulApp.OnException<State>(
    ref StatefulApp<State> self,
    ExceptionHandler handler
)
{
    self.exceptionHandler = handler;
}

public void StatefulApp.MountStatic<State>(
    ref StatefulApp<State> self,
    string urlPrefix,
    string root,
    StaticFileOptions options,
    StaticResolver resolver
)
{
    self.staticDirectories.Add(new()
    {
        urlPrefix = urlPrefix,
        root = root,
        options = options,
        resolver = resolver
    });
}

public List<string> StatefulApp.StaticRoots<State>(
    StatefulApp<State> self
)
{
    List<string> roots = new();
    foreach (StaticDirectory directory in self.staticDirectories)
    {
        roots.Add(directory.root);
    }
    return roots;
}

public void StatefulApp.UseFilter<State>(
    ref StatefulApp<State> self,
    StatefulRequestFilter<State> filter
)
{
    self.filters.Add(filter);
}

public void StatefulApp.AfterHtml<State>(
    ref StatefulApp<State> self,
    StatefulHtmlMiddleware<State> middleware
)
{
    self.htmlMiddleware.Add(middleware);
}

private void StatefulApp.MapEndpoint<State>(
    ref StatefulApp<State> self,
    RoutePattern pattern,
    List<string> methods,
    StatefulHandler<State> handler
)
{
    foreach (StatefulRoute<State> existing in self.routes)
    {
        if (MethodsOverlap(existing.methods, methods) &&
            existing.pattern.ConflictsWith(pattern))
        {
            throw new ArgumentException(
                "route conflicts with an existing endpoint"
            );
        }
    }
    StatefulRoute<State> route = new()
    {
        methods = methods,
        pattern = pattern,
        handler = handler
    };
    self.routes.Add(route);
}

private void StatefulApp.MapMethod<State>(
    ref StatefulApp<State> self,
    string method,
    string path,
    StatefulHandler<State> handler
)
{
    if (!HttpMethodValid(method))
    {
        throw new ArgumentException(
            "HTTP method must be a non-empty uppercase token"
        );
    }
    RoutePattern pattern = ParseRoutePattern(path);
    List<string> methods = new();
    methods.Add(method);
    self.MapEndpoint(pattern, methods, handler);
}

public void StatefulApp.MapMethods<State>(
    ref StatefulApp<State> self,
    string path,
    List<string> methods,
    StatefulHandler<State> handler
)
{
    if (methods.Count == 0)
    {
        throw new ArgumentException("endpoint requires an HTTP method");
    }
    for (nuint index = 0; index < methods.Count; index += 1)
    {
        string method = methods[index];
        if (method.Length == 0)
        {
            throw new ArgumentException("HTTP method cannot be empty");
        }
        if (!HttpMethodValid(method))
        {
            throw new ArgumentException(
                "HTTP method must be an uppercase token"
            );
        }
        for (nuint other = 0; other < index; other += 1)
        {
            if (methods[other] == method)
            {
                throw new ArgumentException("HTTP method is duplicated");
            }
        }
    }
    RoutePattern pattern = ParseRoutePattern(path);
    self.MapEndpoint(pattern, methods, handler);
}

public void StatefulApp.MapGet<State>(
    ref StatefulApp<State> self,
    string path,
    StatefulHandler<State> handler
)
{
    self.MapMethod("GET", path, handler);
    if (BuildPagePathValid(path) && !RouteHasParameter(path))
    {
        BuildPagesAdd(self.pages, path);
    }
}

public Result<bool, string> StatefulApp.AddBuildPage<State>(
    ref StatefulApp<State> self,
    string path
)
{
    if (!BuildPagePathValid(path) || RouteHasParameter(path))
    {
        return Result.Err("build page requires one safe concrete URL path");
    }
    if (BuildPagesContains(self.pages, path))
    {
        return Result.Err("duplicate build page path");
    }
    if (BuildPageMatchesStatefulRoutes(self.routes, path))
    {
        BuildPagesAdd(self.pages, path);
        return Result.Ok(true);
    }
    return Result.Err("build page does not match a parameterized GET route");
}

public Result<bool, string> StatefulApp.TryMapGetFrom<State>(
    ref StatefulApp<State> self,
    string pattern,
    StatefulHandler<State> handler,
    StatefulBuildSource<State> source
)
{
    if (!RouteHasParameter(pattern))
    {
        return Result.Err(
            "parameterized build source requires a parameterized GET route"
        );
    }
    StatefulBuildSource<State> buildSource = source;
    List<string> paths = buildSource(self.state);
    foreach (string path in paths)
    {
        if (!BuildPagePathValid(path) || RouteHasParameter(path))
        {
            return Result.Err(
                "build source returned an unsafe or parameterized URL path"
            );
        }
        if (!RouteMatches(pattern, path))
        {
            return Result.Err(
                "build source URL does not match its parameterized GET route"
            );
        }
        if (BuildPagesContains(self.pages, path))
        {
            return Result.Err("duplicate build page path");
        }
        BuildPagesAdd(self.pages, path);
    }
    self.MapMethod("GET", pattern, handler);
    return Result.Ok(true);
}

public void StatefulApp.MapGet<State>(
    ref StatefulApp<State> self,
    string pattern,
    StatefulHandler<State> handler,
    StatefulBuildSource<State> source
)
{
    bool ignored = LimeResultOrThrow(
        self.TryMapGetFrom(pattern, handler, source)
    );
}

public void StatefulApp.MapPost<State>(
    ref StatefulApp<State> self,
    string path,
    StatefulHandler<State> handler
)
{
    self.MapMethod("POST", path, handler);
}

public void StatefulApp.MapPut<State>(
    ref StatefulApp<State> self,
    string path,
    StatefulHandler<State> handler
)
{
    self.MapMethod("PUT", path, handler);
}

public void StatefulApp.MapPatch<State>(
    ref StatefulApp<State> self,
    string path,
    StatefulHandler<State> handler
)
{
    self.MapMethod("PATCH", path, handler);
}

public void StatefulApp.MapDelete<State>(
    ref StatefulApp<State> self,
    string path,
    StatefulHandler<State> handler
)
{
    self.MapMethod("DELETE", path, handler);
}

public void StatefulApp.MapHead<State>(
    ref StatefulApp<State> self,
    string path,
    StatefulHandler<State> handler
)
{
    self.MapMethod("HEAD", path, handler);
}

private Response DispatchStatefulAppUnchecked<State>(
    StatefulApp<State> self,
    Request request
)
{
    request = ApplyConfiguredForwardedHeaders(self.forwardedHeaders, request);
    switch (ApplyStatefulRequestFilters(
        self.state,
        self.filters,
        request
    ))
    {
        case FilterResult.Continue: {
        }
        case FilterResult.Respond(response): {
            return ApplyStatefulHtmlMiddleware(
                self.state,
                self.htmlMiddleware,
                request,
                response
            );
        }
    }

    return DispatchStatefulRoutes(
        self.state,
        self.routes,
        self.staticDirectories,
        self.htmlMiddleware,
        self.fallback,
        request
    );
}

public Response StatefulApp.Dispatch<State>(
    StatefulApp<State> self,
    Request request
)
{
    try
    {
        return DispatchStatefulAppUnchecked(self, request);
    }
    catch (Exception error)
    {
        ExceptionHandler handler = self.exceptionHandler;
        return handler(error);
    }
}

private Response DispatchStatefulAppFallbackUnchecked<State>(
    StatefulApp<State> self,
    Request request
)
{
    request = ApplyConfiguredForwardedHeaders(self.forwardedHeaders, request);
    switch (ApplyStatefulRequestFilters(
        self.state, self.filters, request
    ))
    {
        case FilterResult.Continue: {
        }
        case FilterResult.Respond(response): {
            return ApplyStatefulHtmlMiddleware(
                self.state,
                self.htmlMiddleware,
                request,
                response
            );
        }
    }
    StatefulHandler<State> fallback = self.fallback;
    return ApplyStatefulHtmlMiddleware(
        self.state,
        self.htmlMiddleware,
        request,
        fallback(self.state, request)
    );
}

public Response StatefulApp.DispatchFallback<State>(
    StatefulApp<State> self,
    Request request
)
{
    try
    {
        return DispatchStatefulAppFallbackUnchecked(self, request);
    }
    catch (Exception error)
    {
        ExceptionHandler handler = self.exceptionHandler;
        return handler(error);
    }
}

private Option<StatefulRoute<State>> BestStatefulRoute<State>(
    List<StatefulRoute<State>> routes,
    string method,
    string path
)
{
    Option<StatefulRoute<State>> selected = Option.None;
    foreach (StatefulRoute<State> route in routes)
    {
        if (!MethodContains(route.methods, method) ||
            !route.pattern.IsMatch(path))
        {
            continue;
        }
        switch (selected)
        {
            case Option.None: { selected = Option.Some(route); }
            case Option.Some(current): {
                int precedence = route.pattern.ComparePrecedence(
                    current.pattern
                );
                if (precedence < 0)
                {
                    selected = Option.Some(route);
                }
                else if (precedence == 0)
                {
                    throw new InvalidOperationException(
                        "request matched multiple endpoints with equal precedence"
                    );
                }
            }
        }
    }
    return selected;
}

private Response DispatchStatefulRoutes<State>(
    State state,
    List<StatefulRoute<State>> routes,
    List<StaticDirectory> staticDirectories,
    List<StatefulHtmlMiddleware<State>> middleware,
    StatefulHandler<State> fallback,
    Request request
)
{
    switch (BestStatefulRoute(routes, request.method, request.path))
    {
        case Option.Some(route): {
                SelectRoute(request, route.pattern);
                StatefulHandler<State> handler = route.handler;
                return ApplyStatefulHtmlMiddleware(
                    state,
                    middleware,
                    request,
                    handler(state, request)
                );
        }
        case Option.None: { }
    }

    if (request.method == "HEAD")
    {
        switch (BestStatefulRoute(routes, "GET", request.path))
        {
            case Option.Some(route): {
                SelectRoute(request, route.pattern);
                StatefulHandler<State> handler = route.handler;
                return ApplyStatefulHtmlMiddleware(
                    state,
                    middleware,
                    request,
                    handler(state, request)
                );
            }
            case Option.None: { }
        }
    }

    List<string> allowedMethods = new();
    foreach (StatefulRoute<State> route in routes)
    {
        if (route.pattern.IsMatch(request.path))
        {
            foreach (string method in route.methods)
            {
                AddAllowedMethod(allowedMethods, method);
                if (method == "GET")
                {
                    AddAllowedMethod(allowedMethods, "HEAD");
                }
            }
        }
    }
    if (allowedMethods.Count > 0)
    {
        AddAllowedMethod(allowedMethods, "OPTIONS");
        if (request.method == "OPTIONS")
        {
            return ApplyStatefulHtmlMiddleware(
                state,
                middleware,
                request,
                WithAllowedMethods(
                    Results.NoContent(), allowedMethods
                )
            );
        }
        return ApplyStatefulHtmlMiddleware(
            state,
            middleware,
            request,
            WithAllowedMethods(
                Results.MethodNotAllowed(
                    <h1>Method not allowed</h1>
                ),
                allowedMethods
            )
        );
    }


    if (request.method == "GET" || request.method == "HEAD")
    {
        foreach (StaticDirectory directory in staticDirectories)
        {
            if (!request.path.StartsWith(directory.urlPrefix))
            {
                continue;
            }
            StaticResolver resolver = directory.resolver;
            switch (resolver(
                directory.urlPrefix, directory.root,
                directory.options, request
            ))
            {
                case Option.Some(response): {
                    return ApplyStatefulHtmlMiddleware(
                        state, middleware, request, response
                    );
                }
                case Option.None: {
                }
            }
        }
    }

    return ApplyStatefulHtmlMiddleware(
        state,
        middleware,
        request,
        fallback(state, request)
    );
}

private Html ApplyStatefulHtml<State>(
    State state,
    List<StatefulHtmlMiddleware<State>> middleware,
    Request request,
    Html page
)
{
    foreach (StatefulHtmlMiddleware<State> transform in middleware)
    {
        page = transform(state, request, page);
    }
    return page;
}

private Response ApplyStatefulHtmlMiddleware<State>(
    State state,
    List<StatefulHtmlMiddleware<State>> middleware,
    Request request,
    Response response
)
{
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    switch (body)
    {
        case ResponseBody.Empty: {
            return new()
            {
                status = status,
                body = ResponseBody.Empty,
                headers = headers
            };
        }
        case ResponseBody.Html(page): {
            return new()
            {
                status = status,
                body = ResponseBody.Html(ApplyStatefulHtml(
                    state, middleware, request, page
                )),
                headers = headers
            };
        }
        case ResponseBody.Text(text): {
            return new()
            {
                status = status,
                body = ResponseBody.Text(text),
                headers = headers
            };
        }
        case ResponseBody.Css(text): {
            return new()
            {
                status = status,
                body = ResponseBody.Css(text),
                headers = headers
            };
        }
        case ResponseBody.Asset(asset): {
            return new()
            {
                status = status,
                body = ResponseBody.Asset(asset),
                headers = headers
            };
        }
        case ResponseBody.Stream(stream): {
            return new()
            {
                status = status,
                body = ResponseBody.Stream(stream),
                headers = headers
            };
        }
        case ResponseBody.File(file): {
            return new()
            {
                status = status,
                body = ResponseBody.File(file),
                headers = headers
            };
        }
    }
}

private FilterResult ApplyStatefulRequestFilters<State>(
    State state,
    List<StatefulRequestFilter<State>> filters,
    Request request
)
{
    foreach (StatefulRequestFilter<State> filter in filters)
    {
        switch (filter(state, request))
        {
            case FilterResult.Continue: {
            }
            case FilterResult.Respond(response): {
                return FilterResult.Respond(response);
            }
        }
    }
    return FilterResult.Continue;
}
