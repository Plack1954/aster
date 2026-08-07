namespace Lime;

using Lime.Forwarding;
using Lime.Routing;
using Aster.Memory;
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

public struct ByteBody
{
    List<byte> bytes;
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
    Bytes(ByteBody),
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
    public static int Status413PayloadTooLarge => 413;
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

private Response EmptyResponse(int status)
{
    if (status < 100 || status > 599)
    {
        throw new ArgumentException(
            "response status must be between 100 and 599"
        );
    }
    List<ResponseHeader> headers = new();
    return new()
    {
        status = status,
        body = ResponseBody.Empty,
        headers = headers
    };
}

private Response EmptyResponseWithLocation(int status, string location)
{
    if (location.Length == 0)
    {
        throw new ArgumentException("response location cannot be empty");
    }
    Response response = EmptyResponse(status);
    response.AddHeader(LimeResultOrThrow(
        ResponseHeader("Location", location)
    ));
    return response;
}

public Response Results.StatusCode(int status)
{
    return EmptyResponse(status);
}

public Response Results.Ok()
{
    return EmptyResponse(StatusCodes.Status200OK);
}

public Response Results.Accepted()
{
    return EmptyResponse(StatusCodes.Status202Accepted);
}

public Response Results.Accepted(string location)
{
    return EmptyResponseWithLocation(
        StatusCodes.Status202Accepted, location
    );
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

public Response Results.Bytes(
    int status,
    List<byte> bytes,
    AssetKind kind
)
{
    EnsureResponseBodyAllowed(status);
    List<ResponseHeader> headers = new();
    ByteBody byteBody = new()
    {
        bytes = bytes,
        kind = kind
    };
    return new()
    {
        status = status,
        body = ResponseBody.Bytes(byteBody),
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

public Response Results.Bytes(List<byte> bytes, AssetKind kind)
{
    return Results.Bytes(StatusCodes.Status200OK, bytes, kind);
}

public Response Results.Bytes(List<byte> bytes)
{
    return Results.Bytes(bytes, AssetKind.Binary);
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
    return EmptyResponseWithLocation(status, location);
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
    return EmptyResponse(StatusCodes.Status204NoContent);
}

public Response Results.Created(string location)
{
    return EmptyResponseWithLocation(
        StatusCodes.Status201Created, location
    );
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

public Response Results.BadRequest()
{
    return EmptyResponse(StatusCodes.Status400BadRequest);
}

public Response Results.NotFound(Html page)
{
    return HtmlResponse(404, page);
}

public Response Results.NotFound()
{
    return EmptyResponse(StatusCodes.Status404NotFound);
}

public Response Results.Unauthorized(Html page)
{
    return HtmlResponse(StatusCodes.Status401Unauthorized, page);
}

public Response Results.Unauthorized()
{
    return EmptyResponse(StatusCodes.Status401Unauthorized);
}

public Response Results.Forbid(Html page)
{
    return HtmlResponse(StatusCodes.Status403Forbidden, page);
}

public Response Results.Forbid()
{
    return EmptyResponse(StatusCodes.Status403Forbidden);
}

public Response Results.Conflict(Html page)
{
    return HtmlResponse(StatusCodes.Status409Conflict, page);
}

public Response Results.Conflict()
{
    return EmptyResponse(StatusCodes.Status409Conflict);
}

public Response Results.UnprocessableEntity()
{
    return EmptyResponse(StatusCodes.Status422UnprocessableEntity);
}

public Response Results.TooManyRequests()
{
    return EmptyResponse(StatusCodes.Status429TooManyRequests);
}

public Response Results.MethodNotAllowed(Html page)
{
    return HtmlResponse(405, page);
}

public Response Results.InternalServerError(Html page)
{
    return HtmlResponse(StatusCodes.Status500InternalServerError, page);
}

public Response Results.InternalServerError()
{
    return EmptyResponse(StatusCodes.Status500InternalServerError);
}

public Response Results.ServiceUnavailable()
{
    return EmptyResponse(StatusCodes.Status503ServiceUnavailable);
}

public delegate Response Handler(Request request);
public delegate Task<Response> AsyncHandler(Request request);

public delegate Response StringRouteHandler(string value);
public delegate Response IntRouteHandler(int value);
public delegate Response LongRouteHandler(long value);
public delegate Response BoolRouteHandler(bool value);
public delegate Response RequestStringRouteHandler(Request request, string value);
public delegate Response RequestIntRouteHandler(Request request, int value);
public delegate Response RequestLongRouteHandler(Request request, long value);
public delegate Response RequestBoolRouteHandler(Request request, bool value);

public delegate Task<Response> AsyncStringRouteHandler(string value);
public delegate Task<Response> AsyncIntRouteHandler(int value);
public delegate Task<Response> AsyncLongRouteHandler(long value);
public delegate Task<Response> AsyncBoolRouteHandler(bool value);
public delegate Task<Response> AsyncRequestStringRouteHandler(Request request, string value);
public delegate Task<Response> AsyncRequestIntRouteHandler(Request request, int value);
public delegate Task<Response> AsyncRequestLongRouteHandler(Request request, long value);
public delegate Task<Response> AsyncRequestBoolRouteHandler(Request request, bool value);

public struct RouteBinding
{
    string Name;
    string Value;
}

public struct QueryBinding
{
    string Name;
    string Value;
}

public struct HeaderBinding
{
    string Name;
    string Value;
}

public struct JsonBody
{
    string Value;
}

public RouteBinding FromRoute(string name)
{
    return new() { Name = name, Value = "" };
}

public QueryBinding FromQuery(string name)
{
    return new() { Name = name, Value = "" };
}

public HeaderBinding FromHeader(string name)
{
    return new() { Name = name, Value = "" };
}

public JsonBody FromJsonBody()
{
    return new() { Value = "" };
}

public T JsonBody.Deserialize<T>(JsonBody self)
{
    T value = JsonSerializer.Deserialize(self.Value);
    return value;
}

public delegate Response RouteRouteBindingHandler(
    RouteBinding first, RouteBinding second
);
public delegate Response RouteQueryBindingHandler(
    RouteBinding route, QueryBinding query
);
public delegate Response RouteHeaderBindingHandler(
    RouteBinding route, HeaderBinding header
);
public delegate Response RouteJsonBindingHandler(
    RouteBinding route, JsonBody body
);
public delegate Task<Response> AsyncRouteRouteBindingHandler(
    RouteBinding first, RouteBinding second
);
public delegate Task<Response> AsyncRouteQueryBindingHandler(
    RouteBinding route, QueryBinding query
);
public delegate Task<Response> AsyncRouteHeaderBindingHandler(
    RouteBinding route, HeaderBinding header
);
public delegate Task<Response> AsyncRouteJsonBindingHandler(
    RouteBinding route, JsonBody body
);

struct RouteRouteBindingEndpoint
{
    string first;
    string second;
    RouteRouteBindingHandler handler;
}
struct RouteQueryBindingEndpoint
{
    string route;
    string query;
    RouteQueryBindingHandler handler;
}
struct RouteHeaderBindingEndpoint
{
    string route;
    string header;
    RouteHeaderBindingHandler handler;
}
struct RouteJsonBindingEndpoint
{
    string route;
    RouteJsonBindingHandler handler;
}
struct AsyncRouteRouteBindingEndpoint
{
    string first;
    string second;
    AsyncRouteRouteBindingHandler handler;
}
struct AsyncRouteQueryBindingEndpoint
{
    string route;
    string query;
    AsyncRouteQueryBindingHandler handler;
}
struct AsyncRouteHeaderBindingEndpoint
{
    string route;
    string header;
    AsyncRouteHeaderBindingHandler handler;
}
struct AsyncRouteJsonBindingEndpoint
{
    string route;
    AsyncRouteJsonBindingHandler handler;
}

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
union RouteHandler
{
    Sync(Handler),
    Async(AsyncHandler),
    String(StringRouteHandler),
    Int(IntRouteHandler),
    Long(LongRouteHandler),
    Bool(BoolRouteHandler),
    RequestString(RequestStringRouteHandler),
    RequestInt(RequestIntRouteHandler),
    RequestLong(RequestLongRouteHandler),
    RequestBool(RequestBoolRouteHandler),
    AsyncString(AsyncStringRouteHandler),
    AsyncInt(AsyncIntRouteHandler),
    AsyncLong(AsyncLongRouteHandler),
    AsyncBool(AsyncBoolRouteHandler),
    AsyncRequestString(AsyncRequestStringRouteHandler),
    AsyncRequestInt(AsyncRequestIntRouteHandler),
    AsyncRequestLong(AsyncRequestLongRouteHandler),
    AsyncRequestBool(AsyncRequestBoolRouteHandler),
    BoundRouteRoute(RouteRouteBindingEndpoint),
    BoundRouteQuery(RouteQueryBindingEndpoint),
    BoundRouteHeader(RouteHeaderBindingEndpoint),
    BoundRouteJson(RouteJsonBindingEndpoint),
    AsyncBoundRouteRoute(AsyncRouteRouteBindingEndpoint),
    AsyncBoundRouteQuery(AsyncRouteQueryBindingEndpoint),
    AsyncBoundRouteHeader(AsyncRouteHeaderBindingEndpoint),
    AsyncBoundRouteJson(AsyncRouteJsonBindingEndpoint),
}
struct UrlValue
{
    string name;
    string value;
}

public struct UrlValues
{
    List<UrlValue> values;
}

private class Route
{
    public List<string> methods;
    public RoutePattern pattern;
    public RouteHandler handler;
    public EndpointMetadata metadata;

    public Route(
        RoutePattern routePattern,
        List<string> routeMethods,
        RouteHandler routeHandler,
        EndpointMetadata endpointMetadata
    )
    {
        pattern = routePattern;
        methods = routeMethods;
        handler = routeHandler;
        metadata = endpointMetadata;
    }

    ~Route()
    {
        delete metadata;
    }
}

private List<Route> InsertRouteByPrecedence(
    List<Route> routes,
    Route route
)
{
    nuint index = 0;
    while (index < routes.Count)
    {
        Route current = routes[index];
        if (route.pattern.ComparePrecedence(current.pattern) < 0)
        {
            break;
        }
        index += 1;
    }
    routes.Insert(index, route);
    return routes;
}

private Option<Route> MatchOrderedRoutes(
    List<Route> routes,
    string path
)
{
    foreach (Route route in routes)
    {
        if (route.pattern.IsMatch(path))
        {
            return Option.Some(route);
        }
    }
    return Option.None;
}

private string FirstPathSegment(string path)
{
    if (path.Length <= 1) { return ""; }
    nuint end = 1;
    while (end < path.Length && path[end] != 47)
    {
        end += 1;
    }
    return StringSlice(path, 1, end);
}

private class RouteLiteralBucket
{
    private string Literal;
    private List<Route> Routes;

    public RouteLiteralBucket(string literal)
    {
        Literal = literal;
        Routes = new();
    }

    public bool Handles(string literal)
    {
        return AsciiEqualIgnoringCase(Literal, literal);
    }

    public void Add(Route route)
    {
        Routes = InsertRouteByPrecedence(Routes, route);
    }

    public Option<Route> Match(string path)
    {
        return MatchOrderedRoutes(Routes, path);
    }
}

private class RouteMethodBucket
{
    private string Method;
    private List<Route> DynamicRoutes;
    private List<RouteLiteralBucket> LiteralBuckets;

    public RouteMethodBucket(string method)
    {
        Method = method;
        DynamicRoutes = new();
        LiteralBuckets = new();
    }

    public bool Handles(string method)
    {
        return Method == method;
    }

    public void Add(Route route)
    {
        switch (route.pattern.FirstLiteralSegment)
        {
            case Option.Some(literal): {
                foreach (RouteLiteralBucket bucket in LiteralBuckets)
                {
                    if (bucket.Handles(literal))
                    {
                        bucket.Add(route);
                        return;
                    }
                }
                RouteLiteralBucket bucket = new RouteLiteralBucket(literal);
                bucket.Add(route);
                List<RouteLiteralBucket> buckets = LiteralBuckets;
                buckets.Add(bucket);
                LiteralBuckets = buckets;
            }
            case Option.None: {
                DynamicRoutes = InsertRouteByPrecedence(
                    DynamicRoutes, route
                );
            }
        }
    }

    public Option<Route> Match(string path)
    {
        string literal = FirstPathSegment(path);
        foreach (RouteLiteralBucket bucket in LiteralBuckets)
        {
            if (bucket.Handles(literal))
            {
                switch (bucket.Match(path))
                {
                    case Option.Some(route): { return Option.Some(route); }
                    case Option.None: {
                        return MatchOrderedRoutes(DynamicRoutes, path);
                    }
                }
            }
        }
        return MatchOrderedRoutes(DynamicRoutes, path);
    }

    ~RouteMethodBucket()
    {
        foreach (RouteLiteralBucket bucket in LiteralBuckets)
        {
            delete bucket;
        }
    }
}

private class RouteTable
{
    private List<RouteMethodBucket> Buckets;

    public RouteTable()
    {
        Buckets = new();
    }

    public void Add(Route route)
    {
        foreach (string method in route.methods)
        {
            bool found = false;
            foreach (RouteMethodBucket bucket in Buckets)
            {
                if (bucket.Handles(method))
                {
                    bucket.Add(route);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                RouteMethodBucket bucket = new RouteMethodBucket(method);
                bucket.Add(route);
                List<RouteMethodBucket> buckets = Buckets;
                buckets.Add(bucket);
                Buckets = buckets;
            }
        }
    }

    public Option<Route> Match(string method, string path)
    {
        foreach (RouteMethodBucket bucket in Buckets)
        {
            if (bucket.Handles(method))
            {
                return bucket.Match(path);
            }
        }
        return Option.None;
    }

    ~RouteTable()
    {
        foreach (RouteMethodBucket bucket in Buckets)
        {
            delete bucket;
        }
    }
}

private class EndpointMetadata
{
    public Option<string> Name;
    public Option<string> Description;
    public List<string> Tags;
    public List<int> ProducedStatuses;

    public EndpointMetadata()
    {
        Name = Option.None;
        Description = Option.None;
        Tags = new();
        ProducedStatuses = new();
    }
}

private class RouteGroupState
{
    public string Prefix;
    public RouteGroupState Parent;
    public Option<string> Description;
    public List<string> Tags;
    public List<EndpointMetadata> Endpoints;

    public RouteGroupState(string prefix, RouteGroupState parent)
    {
        Prefix = prefix;
        Parent = parent;
        Description = Option.None;
        Tags = new();
        Endpoints = new();
    }
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

public struct LinkGenerator
{
    WebApplication Application;
}

public struct EndpointDataSource
{
    WebApplication Application;

    public nuint Count
    {
        get
        {
            if (Application == null)
            {
                throw new InvalidOperationException(
                    "endpoint data source is invalid"
                );
            }
            WebApplication application = Application;
            return application.routes.Count;
        }
    }

    public RouteEndpoint GetEndpoint(nuint index)
    {
        if (Application == null)
        {
            throw new InvalidOperationException(
                "endpoint data source is invalid"
            );
        }
        WebApplication application = Application;
        if (index >= application.routes.Count)
        {
            throw new ArgumentException("endpoint index is out of range");
        }
        return new() { Endpoint = application.routes[index] };
    }
}

public struct RouteEndpoint
{
    Route Endpoint;

    public string Pattern
    {
        get { return this.Validate().pattern.RawText; }
    }

    public Option<string> Name
    {
        get
        {
            return this.Validate().metadata.Name;
        }
    }

    public Option<string> Description
    {
        get
        {
            return this.Validate().metadata.Description;
        }
    }

    public nuint MethodCount
    {
        get
        {
            Route endpoint = this.Validate();
            return endpoint.methods.Count;
        }
    }

    public string GetMethod(nuint index)
    {
        Route endpoint = this.Validate();
        if (index >= endpoint.methods.Count)
        {
            throw new ArgumentException("endpoint method index is out of range");
        }
        return endpoint.methods[index];
    }

    public nuint TagCount
    {
        get
        {
            EndpointMetadata metadata = this.Validate().metadata;
            return metadata.Tags.Count;
        }
    }

    public string GetTag(nuint index)
    {
        EndpointMetadata metadata = this.Validate().metadata;
        if (index >= metadata.Tags.Count)
        {
            throw new ArgumentException("endpoint tag index is out of range");
        }
        return metadata.Tags[index];
    }

    public nuint ProducedStatusCount
    {
        get
        {
            EndpointMetadata metadata = this.Validate().metadata;
            return metadata.ProducedStatuses.Count;
        }
    }

    public nuint ParameterCount
    {
        get { return this.Validate().pattern.ParameterCount; }
    }

    public string GetParameterName(nuint index)
    {
        return this.Validate().pattern.GetParameterName(index);
    }

    public string OpenApiPath
    {
        get { return this.Validate().pattern.ToOpenApiPath(); }
    }

    public int GetProducedStatus(nuint index)
    {
        EndpointMetadata metadata = this.Validate().metadata;
        if (index >= metadata.ProducedStatuses.Count)
        {
            throw new ArgumentException(
                "endpoint produced-status index is out of range"
            );
        }
        return metadata.ProducedStatuses[index];
    }

    private readonly Route Validate()
    {
        if (Endpoint == null)
        {
            throw new InvalidOperationException("route endpoint is invalid");
        }
        return Endpoint;
    }
}

public class WebApplication
{
    public List<Route> routes;
    public RouteTable routeTable;
    public List<RouteGroupState> routeGroups;
    public List<BuildPage> pages;
    public List<RequestFilter> filters;
    public List<HtmlMiddleware> htmlMiddleware;
    public List<StaticDirectory> staticDirectories;
    public RouteHandler fallback;
    public ExceptionHandler exceptionHandler;
    public Option<ForwardedHeadersOptions> forwardedHeaders;
    public long maxRequestBodySize;

    public LinkGenerator Links => new() { Application = this };
    public EndpointDataSource Endpoints => new() { Application = this };

    private WebApplication()
    {
        routes = new();
        routeTable = new RouteTable();
        routeGroups = new();
        pages = new();
        filters = new();
        htmlMiddleware = new();
        staticDirectories = new();
        fallback = RouteHandler.Sync(DefaultNotFound);
        exceptionHandler = DefaultExceptionResponse;
        forwardedHeaders = Option.None;
        maxRequestBodySize = 1048576;
    }

    public static WebApplication Create()
    {
        return new WebApplication();
    }

    public void SetMaxRequestBodySize(long bytes)
    {
        if (bytes < 0)
        {
            throw new ArgumentException(
                "maximum request body size cannot be negative"
            );
        }
        maxRequestBodySize = bytes;
    }

    ~WebApplication()
    {
        delete routeTable;
        foreach (RouteGroupState group in routeGroups)
        {
            delete group;
        }
        foreach (Route route in routes)
        {
            delete route;
        }
    }
}

public struct EndpointBuilder
{
    WebApplication Application;
    EndpointMetadata Metadata;
}

public struct RouteGroup
{
    WebApplication Application;
    RouteGroupState State;

    public string Prefix
    {
        get
        {
            if (State == null)
            {
                throw new InvalidOperationException("route group is invalid");
            }
            RouteGroupState state = State;
            return state.Prefix;
        }
    }
}

public Result<string, string> LinkGenerator.GetPathByName(
    LinkGenerator self,
    string endpointName,
    RouteValues values
)
{
    if (self.Application == null)
    {
        return Result.Err("link generator is invalid");
    }
    if (endpointName.Length == 0)
    {
        return Result.Err("endpoint name cannot be empty");
    }
    WebApplication application = self.Application;
    foreach (Route route in application.routes)
    {
        switch (route.metadata.Name)
        {
            case Option.Some(name): {
                if (name == endpointName)
                {
                    return route.pattern.GetPath(values);
                }
            }
            case Option.None: { }
        }
    }
    return Result.Err($"endpoint '{endpointName}' was not found");
}

public Result<string, string> LinkGenerator.GetPathByName(
    LinkGenerator self,
    string endpointName
)
{
    return self.GetPathByName(endpointName, RouteValues.Create());
}

private Response DefaultExceptionResponse(Exception error)
{
    Console.Error.WriteLine(error.Message);
    return Results.InternalServerError(<h1>Internal server error</h1>);
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

// This view borrows the Request's owned buffered body and does not extend its
// lifetime.
public ReadOnlySpan<byte> Request.BodyBytes(Request self)
{
    return StringAsByteSlice(self.body);
}

public nuint Request.BodyLength(Request self)
{
    return self.body.Length;
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

public void WebApplication.MapFallback(WebApplication self, Handler handler)
{
    self.fallback = RouteHandler.Sync(handler);
}

public void WebApplication.MapFallback(WebApplication self, AsyncHandler handler)
{
    self.fallback = RouteHandler.Async(handler);
}

public void WebApplication.UseForwardedHeaders(
    WebApplication self,
    ForwardedHeadersOptions options
)
{
    ValidateForwardedHeadersOptions(options);
    Option<ForwardedHeadersOptions> configured = Option.Some(options);
    self.forwardedHeaders = configured;
}

public void WebApplication.OnException(
    WebApplication self,
    ExceptionHandler handler
)
{
    self.exceptionHandler = handler;
}

public void WebApplication.MountStatic(
    WebApplication self,
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

public List<string> WebApplication.StaticRoots(WebApplication self)
{
    List<string> roots = new();
    foreach (StaticDirectory directory in self.staticDirectories)
    {
        roots.Add(directory.root);
    }
    return roots;
}

public void WebApplication.UseFilter(WebApplication self, RequestFilter filter)
{
    self.filters.Add(filter);
}

public void WebApplication.AfterHtml(WebApplication self, HtmlMiddleware middleware)
{
    self.htmlMiddleware.Add(middleware);
}

private EndpointBuilder WebApplication.MapEndpoint(
    WebApplication self,
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
    EndpointMetadata metadata = new EndpointMetadata();
    Route route = new Route(pattern, methods, handler, metadata);
    self.routes.Add(route);
    self.routeTable.Add(route);
    return new()
    {
        Application = self,
        Metadata = metadata
    };
}

private EndpointBuilder WebApplication.MapMethod(
    WebApplication self,
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
    return self.MapEndpoint(
        pattern, methods, RouteHandler.Sync(handler)
    );
}

private EndpointBuilder WebApplication.MapMethodAsync(
    WebApplication self,
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
    return self.MapEndpoint(
        pattern, methods, RouteHandler.Async(handler)
    );
}

private EndpointBuilder WebApplication.MapTypedMethod(
    WebApplication self,
    string method,
    string path,
    RouteHandler handler
)
{
    if (!HttpMethodValid(method))
    {
        throw new ArgumentException(
            "HTTP method must be a non-empty uppercase token"
        );
    }
    RoutePattern pattern = ParseRoutePattern(path);
    if (pattern.ParameterCount != 1)
    {
        throw new ArgumentException(
            "typed route handler requires exactly one route parameter"
        );
    }
    List<string> methods = new();
    methods.Add(method);
    return self.MapEndpoint(pattern, methods, handler);
}

private EndpointBuilder WebApplication.MapExplicitBindingMethod(
    WebApplication self,
    string method,
    string path,
    RouteHandler handler,
    nuint routeParameterCount,
    string firstRouteName,
    Option<string> secondRouteName
)
{
    if (!HttpMethodValid(method))
    {
        throw new ArgumentException(
            "HTTP method must be a non-empty uppercase token"
        );
    }
    RoutePattern pattern = ParseRoutePattern(path);
    if (pattern.ParameterCount != routeParameterCount)
    {
        throw new ArgumentException(
            "explicit route bindings must name every route parameter exactly once"
        );
    }
    if (!pattern.HasParameter(firstRouteName))
    {
        throw new ArgumentException(
            "route binding name does not occur in the route pattern"
        );
    }
    switch (secondRouteName)
    {
        case Option.Some(name): {
            if (!pattern.HasParameter(name))
            {
                throw new ArgumentException(
                    "route binding name does not occur in the route pattern"
                );
            }
        }
        case Option.None: { }
    }
    List<string> methods = new();
    methods.Add(method);
    return self.MapEndpoint(pattern, methods, handler);
}

private void ValidateBindingName(string name)
{
    if (name.Length == 0)
    {
        throw new ArgumentException("binding source name cannot be empty");
    }
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path,
    RouteBinding first, RouteBinding second,
    RouteRouteBindingHandler handler
)
{
    ValidateBindingName(first.Name);
    ValidateBindingName(second.Name);
    if (first.Name == second.Name)
    {
        throw new ArgumentException("route binding name is duplicated");
    }
    RouteRouteBindingEndpoint endpoint = new()
    {
        first = first.Name, second = second.Name, handler = handler
    };
    return self.MapExplicitBindingMethod(
        "GET", path, RouteHandler.BoundRouteRoute(endpoint), 2,
        first.Name, Option.Some(second.Name)
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path,
    RouteBinding route, QueryBinding query,
    RouteQueryBindingHandler handler
)
{
    ValidateBindingName(route.Name);
    ValidateBindingName(query.Name);
    RouteQueryBindingEndpoint endpoint = new()
    {
        route = route.Name, query = query.Name, handler = handler
    };
    return self.MapExplicitBindingMethod(
        "GET", path, RouteHandler.BoundRouteQuery(endpoint), 1,
        route.Name, Option.None
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path,
    RouteBinding route, HeaderBinding header,
    RouteHeaderBindingHandler handler
)
{
    ValidateBindingName(route.Name);
    ValidateBindingName(header.Name);
    RouteHeaderBindingEndpoint endpoint = new()
    {
        route = route.Name, header = header.Name, handler = handler
    };
    return self.MapExplicitBindingMethod(
        "GET", path, RouteHandler.BoundRouteHeader(endpoint), 1,
        route.Name, Option.None
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path,
    RouteBinding route, JsonBody body,
    RouteJsonBindingHandler handler
)
{
    ValidateBindingName(route.Name);
    RouteJsonBindingEndpoint endpoint = new()
    {
        route = route.Name, handler = handler
    };
    return self.MapExplicitBindingMethod(
        "POST", path, RouteHandler.BoundRouteJson(endpoint), 1,
        route.Name, Option.None
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path,
    RouteBinding first, RouteBinding second,
    AsyncRouteRouteBindingHandler handler
)
{
    ValidateBindingName(first.Name);
    ValidateBindingName(second.Name);
    if (first.Name == second.Name)
    {
        throw new ArgumentException("route binding name is duplicated");
    }
    AsyncRouteRouteBindingEndpoint endpoint = new()
    {
        first = first.Name, second = second.Name, handler = handler
    };
    return self.MapExplicitBindingMethod(
        "GET", path, RouteHandler.AsyncBoundRouteRoute(endpoint), 2,
        first.Name, Option.Some(second.Name)
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path,
    RouteBinding route, QueryBinding query,
    AsyncRouteQueryBindingHandler handler
)
{
    ValidateBindingName(route.Name);
    ValidateBindingName(query.Name);
    AsyncRouteQueryBindingEndpoint endpoint = new()
    {
        route = route.Name, query = query.Name, handler = handler
    };
    return self.MapExplicitBindingMethod(
        "GET", path, RouteHandler.AsyncBoundRouteQuery(endpoint), 1,
        route.Name, Option.None
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path,
    RouteBinding route, HeaderBinding header,
    AsyncRouteHeaderBindingHandler handler
)
{
    ValidateBindingName(route.Name);
    ValidateBindingName(header.Name);
    AsyncRouteHeaderBindingEndpoint endpoint = new()
    {
        route = route.Name, header = header.Name, handler = handler
    };
    return self.MapExplicitBindingMethod(
        "GET", path, RouteHandler.AsyncBoundRouteHeader(endpoint), 1,
        route.Name, Option.None
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path,
    RouteBinding route, JsonBody body,
    AsyncRouteJsonBindingHandler handler
)
{
    ValidateBindingName(route.Name);
    AsyncRouteJsonBindingEndpoint endpoint = new()
    {
        route = route.Name, handler = handler
    };
    return self.MapExplicitBindingMethod(
        "POST", path, RouteHandler.AsyncBoundRouteJson(endpoint), 1,
        route.Name, Option.None
    );
}

private void ValidateHttpMethods(List<string> methods)
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
}

private EndpointBuilder WebApplication.MapTypedMethods(
    WebApplication self,
    string path,
    List<string> methods,
    RouteHandler handler
)
{
    ValidateHttpMethods(methods);
    RoutePattern pattern = ParseRoutePattern(path);
    if (pattern.ParameterCount != 1)
    {
        throw new ArgumentException(
            "typed route handler requires exactly one route parameter"
        );
    }
    return self.MapEndpoint(pattern, methods, handler);
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    Handler handler
)
{
    ValidateHttpMethods(methods);
    RoutePattern pattern = ParseRoutePattern(path);
    return self.MapEndpoint(
        pattern, methods, RouteHandler.Sync(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    AsyncHandler handler
)
{
    ValidateHttpMethods(methods);
    RoutePattern pattern = ParseRoutePattern(path);
    return self.MapEndpoint(
        pattern, methods, RouteHandler.Async(handler)
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self,
    string path,
    Handler handler
)
{
    EndpointBuilder endpoint = self.MapMethod("GET", path, handler);
    if (BuildPagePathValid(path) && !RouteHasParameter(path))
    {
        self.pages.Add(new() { path = path });
    }
    return endpoint;
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self,
    string path,
    AsyncHandler handler
)
{
    EndpointBuilder endpoint = self.MapMethodAsync("GET", path, handler);
    if (BuildPagePathValid(path) && !RouteHasParameter(path))
    {
        self.pages.Add(new() { path = path });
    }
    return endpoint;
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, StringRouteHandler handler
)
{
    return self.MapTypedMethod("GET", path, RouteHandler.String(handler));
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, IntRouteHandler handler
)
{
    return self.MapTypedMethod("GET", path, RouteHandler.Int(handler));
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, LongRouteHandler handler
)
{
    return self.MapTypedMethod("GET", path, RouteHandler.Long(handler));
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, BoolRouteHandler handler
)
{
    return self.MapTypedMethod("GET", path, RouteHandler.Bool(handler));
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, RequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "GET", path, RouteHandler.RequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, RequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "GET", path, RouteHandler.RequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, RequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "GET", path, RouteHandler.RequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, RequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "GET", path, RouteHandler.RequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, AsyncStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "GET", path, RouteHandler.AsyncString(handler)
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, AsyncIntRouteHandler handler
)
{
    return self.MapTypedMethod("GET", path, RouteHandler.AsyncInt(handler));
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, AsyncLongRouteHandler handler
)
{
    return self.MapTypedMethod("GET", path, RouteHandler.AsyncLong(handler));
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, AsyncBoolRouteHandler handler
)
{
    return self.MapTypedMethod("GET", path, RouteHandler.AsyncBool(handler));
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path,
    AsyncRequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "GET", path, RouteHandler.AsyncRequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, AsyncRequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "GET", path, RouteHandler.AsyncRequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, AsyncRequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "GET", path, RouteHandler.AsyncRequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self, string path, AsyncRequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "GET", path, RouteHandler.AsyncRequestBool(handler)
    );
}

public Result<bool, string> WebApplication.AddBuildPage(WebApplication self, string path)
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
        self.pages.Add(new() { path = path });
        return Result.Ok(true);
    }
    return Result.Err("build page does not match a parameterized GET route");
}

public Result<EndpointBuilder, string> WebApplication.TryMapGetFrom(
    WebApplication self,
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
        self.pages.Add(new() { path = path });
    }
    EndpointBuilder endpoint = self.MapMethod("GET", pattern, handler);
    return Result.Ok(endpoint);
}

public EndpointBuilder WebApplication.MapGet(
    WebApplication self,
    string pattern,
    Handler handler,
    BuildSource source
)
{
    return LimeResultOrThrow(
        self.TryMapGetFrom(pattern, handler, source)
    );
}

public EndpointBuilder WebApplication.MapPost(WebApplication self, string path, Handler handler)
{
    return self.MapMethod("POST", path, handler);
}

public EndpointBuilder WebApplication.MapPost(WebApplication self, string path, AsyncHandler handler)
{
    return self.MapMethodAsync("POST", path, handler);
}

public EndpointBuilder WebApplication.MapPut(WebApplication self, string path, Handler handler)
{
    return self.MapMethod("PUT", path, handler);
}

public EndpointBuilder WebApplication.MapPut(WebApplication self, string path, AsyncHandler handler)
{
    return self.MapMethodAsync("PUT", path, handler);
}

public EndpointBuilder WebApplication.MapPatch(WebApplication self, string path, Handler handler)
{
    return self.MapMethod("PATCH", path, handler);
}

public EndpointBuilder WebApplication.MapPatch(WebApplication self, string path, AsyncHandler handler)
{
    return self.MapMethodAsync("PATCH", path, handler);
}

public EndpointBuilder WebApplication.MapDelete(WebApplication self, string path, Handler handler)
{
    return self.MapMethod("DELETE", path, handler);
}

public EndpointBuilder WebApplication.MapDelete(WebApplication self, string path, AsyncHandler handler)
{
    return self.MapMethodAsync("DELETE", path, handler);
}

public EndpointBuilder WebApplication.MapHead(WebApplication self, string path, Handler handler)
{
    return self.MapMethod("HEAD", path, handler);
}

public EndpointBuilder WebApplication.MapHead(WebApplication self, string path, AsyncHandler handler)
{
    return self.MapMethodAsync("HEAD", path, handler);
}

// BEGIN GENERATED TYPED ROUTE OVERLOADS: WEBAPPLICATION
public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, StringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.String(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, IntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.Int(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, LongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.Long(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, BoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.Bool(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, RequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.RequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, RequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.RequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, RequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.RequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, RequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.RequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, AsyncStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.AsyncString(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, AsyncIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.AsyncInt(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, AsyncLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.AsyncLong(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, AsyncBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.AsyncBool(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, AsyncRequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.AsyncRequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, AsyncRequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.AsyncRequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, AsyncRequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.AsyncRequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapPost(
    WebApplication self, string path, AsyncRequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "POST", path, RouteHandler.AsyncRequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, StringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.String(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, IntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.Int(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, LongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.Long(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, BoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.Bool(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, RequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.RequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, RequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.RequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, RequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.RequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, RequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.RequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, AsyncStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.AsyncString(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, AsyncIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.AsyncInt(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, AsyncLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.AsyncLong(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, AsyncBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.AsyncBool(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, AsyncRequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.AsyncRequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, AsyncRequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.AsyncRequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, AsyncRequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.AsyncRequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapPut(
    WebApplication self, string path, AsyncRequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PUT", path, RouteHandler.AsyncRequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, StringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.String(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, IntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.Int(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, LongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.Long(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, BoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.Bool(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, RequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.RequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, RequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.RequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, RequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.RequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, RequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.RequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, AsyncStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.AsyncString(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, AsyncIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.AsyncInt(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, AsyncLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.AsyncLong(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, AsyncBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.AsyncBool(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, AsyncRequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.AsyncRequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, AsyncRequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.AsyncRequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, AsyncRequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.AsyncRequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapPatch(
    WebApplication self, string path, AsyncRequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "PATCH", path, RouteHandler.AsyncRequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, StringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.String(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, IntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.Int(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, LongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.Long(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, BoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.Bool(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, RequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.RequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, RequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.RequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, RequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.RequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, RequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.RequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, AsyncStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.AsyncString(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, AsyncIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.AsyncInt(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, AsyncLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.AsyncLong(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, AsyncBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.AsyncBool(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, AsyncRequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.AsyncRequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, AsyncRequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.AsyncRequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, AsyncRequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.AsyncRequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapDelete(
    WebApplication self, string path, AsyncRequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "DELETE", path, RouteHandler.AsyncRequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, StringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.String(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, IntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.Int(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, LongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.Long(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, BoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.Bool(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, RequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.RequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, RequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.RequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, RequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.RequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, RequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.RequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, AsyncStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.AsyncString(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, AsyncIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.AsyncInt(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, AsyncLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.AsyncLong(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, AsyncBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.AsyncBool(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, AsyncRequestStringRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.AsyncRequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, AsyncRequestIntRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.AsyncRequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, AsyncRequestLongRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.AsyncRequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapHead(
    WebApplication self, string path, AsyncRequestBoolRouteHandler handler
)
{
    return self.MapTypedMethod(
        "HEAD", path, RouteHandler.AsyncRequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    StringRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.String(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    IntRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.Int(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    LongRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.Long(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    BoolRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.Bool(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    RequestStringRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.RequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    RequestIntRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.RequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    RequestLongRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.RequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    RequestBoolRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.RequestBool(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    AsyncStringRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.AsyncString(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    AsyncIntRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.AsyncInt(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    AsyncLongRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.AsyncLong(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    AsyncBoolRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.AsyncBool(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    AsyncRequestStringRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.AsyncRequestString(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    AsyncRequestIntRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.AsyncRequestInt(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    AsyncRequestLongRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.AsyncRequestLong(handler)
    );
}

public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    AsyncRequestBoolRouteHandler handler
)
{
    return self.MapTypedMethods(
        path, methods, RouteHandler.AsyncRequestBool(handler)
    );
}
// END GENERATED TYPED ROUTE OVERLOADS: WEBAPPLICATION

private void ValidateEndpointBuilder(EndpointBuilder self)
{
    if (self.Application == null || self.Metadata == null)
    {
        throw new InvalidOperationException("endpoint builder is invalid");
    }
}

public EndpointBuilder EndpointBuilder.WithName(
    EndpointBuilder self,
    string name
)
{
    if (name.Length == 0)
    {
        throw new ArgumentException("endpoint name cannot be empty");
    }
    ValidateEndpointBuilder(self);
    WebApplication application = self.Application;
    foreach (Route route in application.routes)
    {
        if (route.metadata == self.Metadata) { continue; }
        switch (route.metadata.Name)
        {
            case Option.Some(existing): {
                if (existing == name)
                {
                    throw new ArgumentException("endpoint name is duplicated");
                }
            }
            case Option.None: { }
        }
    }
    Option<string> configured = Option.Some(name);
    EndpointMetadata metadata = self.Metadata;
    metadata.Name = configured;
    return self;
}

public EndpointBuilder EndpointBuilder.WithDescription(
    EndpointBuilder self,
    string description
)
{
    ValidateEndpointBuilder(self);
    Option<string> configured = Option.Some(description);
    EndpointMetadata metadata = self.Metadata;
    metadata.Description = configured;
    return self;
}

private void AddMetadataTag(EndpointMetadata metadata, string tag)
{
    foreach (string existing in metadata.Tags)
    {
        if (existing == tag) { return; }
    }
    metadata.Tags.Add(tag);
}

public EndpointBuilder EndpointBuilder.WithTag(
    EndpointBuilder self,
    string tag
)
{
    if (tag.Length == 0)
    {
        throw new ArgumentException("endpoint tag cannot be empty");
    }
    ValidateEndpointBuilder(self);
    EndpointMetadata metadata = self.Metadata;
    AddMetadataTag(metadata, tag);
    return self;
}

public EndpointBuilder EndpointBuilder.Produces(
    EndpointBuilder self,
    int statusCode
)
{
    if (statusCode < 100 || statusCode > 599)
    {
        throw new ArgumentException(
            "produced status must be between 100 and 599"
        );
    }
    ValidateEndpointBuilder(self);
    EndpointMetadata metadata = self.Metadata;
    foreach (int existing in metadata.ProducedStatuses)
    {
        if (existing == statusCode) { return self; }
    }
    metadata.ProducedStatuses.Add(statusCode);
    return self;
}

private RouteGroupState ValidateRouteGroup(RouteGroup self)
{
    if (self.Application == null || self.State == null)
    {
        throw new InvalidOperationException("route group is invalid");
    }
    return self.State;
}

private void ApplyRouteGroupState(
    RouteGroupState state,
    EndpointMetadata endpoint
)
{
    if (state.Parent != null)
    {
        ApplyRouteGroupState(state.Parent, endpoint);
    }
    switch (state.Description)
    {
        case Option.Some(description): {
            Option<string> configured = Option.Some(description);
            endpoint.Description = configured;
        }
        case Option.None: { }
    }
    foreach (string tag in state.Tags)
    {
        AddMetadataTag(endpoint, tag);
    }
    List<EndpointMetadata> endpoints = state.Endpoints;
    endpoints.Add(endpoint);
    state.Endpoints = endpoints;
}

private EndpointBuilder RouteGroup.TrackEndpoint(
    RouteGroup self,
    EndpointBuilder endpoint
)
{
    RouteGroupState state = ValidateRouteGroup(self);
    ValidateEndpointBuilder(endpoint);
    if (endpoint.Application != self.Application)
    {
        throw new InvalidOperationException(
            "route group cannot track another application's endpoint"
        );
    }
    ApplyRouteGroupState(state, endpoint.Metadata);
    return endpoint;
}

public RouteGroup RouteGroup.WithDescription(
    RouteGroup self,
    string description
)
{
    RouteGroupState state = ValidateRouteGroup(self);
    Option<string> configured = Option.Some(description);
    state.Description = configured;
    foreach (EndpointMetadata endpoint in state.Endpoints)
    {
        endpoint.Description = configured;
    }
    return self;
}

public RouteGroup RouteGroup.WithTag(RouteGroup self, string tag)
{
    if (tag.Length == 0)
    {
        throw new ArgumentException("endpoint tag cannot be empty");
    }
    RouteGroupState state = ValidateRouteGroup(self);
    foreach (string existing in state.Tags)
    {
        if (existing == tag) { return self; }
    }
    List<string> tags = state.Tags;
    tags.Add(tag);
    state.Tags = tags;
    foreach (EndpointMetadata endpoint in state.Endpoints)
    {
        AddMetadataTag(endpoint, tag);
    }
    return self;
}

private string GroupPattern(string prefix, string pattern)
{
    if (pattern.Length == 0 || pattern[0] != 47)
    {
        throw new ArgumentException("group route must begin with `/`");
    }
    if (prefix == "/") { return pattern; }
    if (pattern == "/") { return string.Concat(prefix, "/"); }
    return string.Concat(prefix, pattern);
}

public RouteGroup WebApplication.MapGroup(
    WebApplication self,
    string prefix
)
{
    if (prefix.Length == 0 || prefix[0] != 47)
    {
        throw new ArgumentException("route group prefix must begin with `/`");
    }
    if (prefix.Length > 1 && prefix[prefix.Length - 1] == 47)
    {
        throw new ArgumentException("route group prefix must not end with `/`");
    }
    RoutePattern ignored = ParseRoutePattern(prefix);
    RouteGroupState state = new RouteGroupState(prefix, null);
    List<RouteGroupState> groups = self.routeGroups;
    groups.Add(state);
    self.routeGroups = groups;
    return new()
    {
        Application = self,
        State = state
    };
}

public RouteGroup RouteGroup.MapGroup(RouteGroup self, string prefix)
{
    RouteGroupState parent = ValidateRouteGroup(self);
    string combined = GroupPattern(self.Prefix, prefix);
    RoutePattern ignored = ParseRoutePattern(combined);
    RouteGroupState state = new RouteGroupState(combined, parent);
    WebApplication application = self.Application;
    List<RouteGroupState> groups = application.routeGroups;
    groups.Add(state);
    application.routeGroups = groups;
    return new() { Application = application, State = state };
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self,
    string pattern,
    Handler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self,
    string pattern,
    AsyncHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, StringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, IntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, LongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, BoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, RequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, RequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, RequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, RequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, AsyncStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, AsyncIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, AsyncLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, AsyncBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern,
    AsyncRequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, AsyncRequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, AsyncRequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapGet(
    RouteGroup self, string pattern, AsyncRequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapGet(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(RouteGroup self, string pattern, Handler handler)
{
    return self.TrackEndpoint(
        self.Application.MapPost(GroupPattern(self.Prefix, pattern), handler)
    );
}

public EndpointBuilder RouteGroup.MapPost(RouteGroup self, string pattern, AsyncHandler handler)
{
    return self.TrackEndpoint(
        self.Application.MapPost(GroupPattern(self.Prefix, pattern), handler)
    );
}

public EndpointBuilder RouteGroup.MapPut(RouteGroup self, string pattern, Handler handler)
{
    return self.TrackEndpoint(
        self.Application.MapPut(GroupPattern(self.Prefix, pattern), handler)
    );
}

public EndpointBuilder RouteGroup.MapPut(RouteGroup self, string pattern, AsyncHandler handler)
{
    return self.TrackEndpoint(
        self.Application.MapPut(GroupPattern(self.Prefix, pattern), handler)
    );
}

public EndpointBuilder RouteGroup.MapPatch(RouteGroup self, string pattern, Handler handler)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(GroupPattern(self.Prefix, pattern), handler)
    );
}

public EndpointBuilder RouteGroup.MapPatch(RouteGroup self, string pattern, AsyncHandler handler)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(GroupPattern(self.Prefix, pattern), handler)
    );
}

public EndpointBuilder RouteGroup.MapDelete(RouteGroup self, string pattern, Handler handler)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(GroupPattern(self.Prefix, pattern), handler)
    );
}

public EndpointBuilder RouteGroup.MapDelete(RouteGroup self, string pattern, AsyncHandler handler)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(GroupPattern(self.Prefix, pattern), handler)
    );
}

public EndpointBuilder RouteGroup.MapHead(RouteGroup self, string pattern, Handler handler)
{
    return self.TrackEndpoint(
        self.Application.MapHead(GroupPattern(self.Prefix, pattern), handler)
    );
}

public EndpointBuilder RouteGroup.MapHead(RouteGroup self, string pattern, AsyncHandler handler)
{
    return self.TrackEndpoint(
        self.Application.MapHead(GroupPattern(self.Prefix, pattern), handler)
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    Handler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    AsyncHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

// BEGIN GENERATED TYPED ROUTE OVERLOADS: ROUTEGROUP
public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, StringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, IntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, LongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, BoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, RequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, RequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, RequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, RequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, AsyncStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, AsyncIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, AsyncLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, AsyncBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, AsyncRequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, AsyncRequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, AsyncRequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPost(
    RouteGroup self, string pattern, AsyncRequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPost(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, StringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, IntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, LongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, BoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, RequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, RequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, RequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, RequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, AsyncStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, AsyncIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, AsyncLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, AsyncBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, AsyncRequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, AsyncRequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, AsyncRequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPut(
    RouteGroup self, string pattern, AsyncRequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPut(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, StringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, IntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, LongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, BoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, RequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, RequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, RequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, RequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, AsyncStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, AsyncIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, AsyncLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, AsyncBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, AsyncRequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, AsyncRequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, AsyncRequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapPatch(
    RouteGroup self, string pattern, AsyncRequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapPatch(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, StringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, IntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, LongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, BoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, RequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, RequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, RequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, RequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, AsyncStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, AsyncIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, AsyncLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, AsyncBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, AsyncRequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, AsyncRequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, AsyncRequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapDelete(
    RouteGroup self, string pattern, AsyncRequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapDelete(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, StringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, IntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, LongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, BoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, RequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, RequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, RequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, RequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, AsyncStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, AsyncIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, AsyncLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, AsyncBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, AsyncRequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, AsyncRequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, AsyncRequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapHead(
    RouteGroup self, string pattern, AsyncRequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapHead(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    StringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    IntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    LongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    BoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    RequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    RequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    RequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    RequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    AsyncStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    AsyncIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    AsyncLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    AsyncBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    AsyncRequestStringRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    AsyncRequestIntRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    AsyncRequestLongRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}

public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    AsyncRequestBoolRouteHandler handler
)
{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}
// END GENERATED TYPED ROUTE OVERLOADS: ROUTEGROUP

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
        case ResponseBody.Bytes(byteBody): {
            return new()
            {
                status = status,
                body = ResponseBody.Bytes(byteBody),
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

private Response DispatchAppUnchecked(WebApplication self, Request request)
{
    if ((long)request.BodyLength() > self.maxRequestBodySize)
    {
        return Results.Text(
            StatusCodes.Status413PayloadTooLarge,
            "Request body exceeds the configured limit."
        );
    }
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
        self.routeTable,
        self.staticDirectories,
        self.htmlMiddleware,
        self.fallback,
        request
    );
}

public Response WebApplication.Dispatch(WebApplication self, Request request)
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
    WebApplication self,
    Request request
)
{
    if ((long)request.BodyLength() > self.maxRequestBodySize)
    {
        return Results.Text(
            StatusCodes.Status413PayloadTooLarge,
            "Request body exceeds the configured limit."
        );
    }
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
        self.routeTable,
        self.staticDirectories,
        self.htmlMiddleware,
        self.fallback,
        request
    );
}

public async Task<Response> WebApplication.DispatchAsync(WebApplication self, Request request)
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

private Response DispatchAppFallbackUnchecked(WebApplication self, Request request)
{
    if ((long)request.BodyLength() > self.maxRequestBodySize)
    {
        return Results.Text(
            StatusCodes.Status413PayloadTooLarge,
            "Request body exceeds the configured limit."
        );
    }
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

public Response WebApplication.DispatchFallback(WebApplication self, Request request)
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

public async Task<Response> WebApplication.DispatchFallbackAsync(
    WebApplication self,
    Request request
)
{
    try
    {
        if ((long)request.BodyLength() > self.maxRequestBodySize)
        {
            return Results.Text(
                StatusCodes.Status413PayloadTooLarge,
                "Request body exceeds the configured limit."
            );
        }
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

private Option<string> SingleRouteValue(Request request)
{
    switch (request.routePattern)
    {
        case Option.Some(pattern): {
            return pattern.SingleParameter(request.path);
        }
        case Option.None: { return Option.None; }
    }
}

private Response RouteBindingBadRequest()
{
    return Results.Text(
        StatusCodes.Status400BadRequest,
        "A route parameter could not be bound to the handler parameter."
    );
}

private Option<RouteBinding> BindRoute(Request request, string name)
{
    switch (request.RouteValue(name))
    {
        case Option.Some(value): {
            return Option.Some(new() { Name = name, Value = value });
        }
        case Option.None: { return Option.None; }
    }
}

private Option<QueryBinding> BindQuery(Request request, string name)
{
    switch (request.Query(name))
    {
        case Result.Ok(value): {
            switch (value)
            {
                case Option.Some(found): {
                    return Option.Some(new() { Name = name, Value = found });
                }
                case Option.None: { return Option.None; }
            }
        }
        case Result.Err(error): { return Option.None; }
    }
}

private Option<HeaderBinding> BindHeader(Request request, string name)
{
    switch (request.Header(name))
    {
        case Option.Some(value): {
            return Option.Some(new() { Name = name, Value = value });
        }
        case Option.None: { return Option.None; }
    }
}

private Option<JsonBody> BindJsonBody(Request request)
{
    switch (request.Json())
    {
        case Result.Ok(value): {
            return Option.Some(new() { Value = value });
        }
        case Result.Err(error): { return Option.None; }
    }
}

private Option<bool> ParseRouteBool(string value)
{
    if (AsciiEqualIgnoringCase(value, "true"))
    {
        return Option.Some(true);
    }
    if (AsciiEqualIgnoringCase(value, "false"))
    {
        return Option.Some(false);
    }
    return Option.None;
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
        case RouteHandler.String(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): { return handler(value); }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.Int(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    int parsed = 0;
                    if (int.TryParse(value, out parsed)) { return handler(parsed); }
                    return RouteBindingBadRequest();
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.Long(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    long parsed = 0;
                    if (long.TryParse(value, out parsed)) { return handler(parsed); }
                    return RouteBindingBadRequest();
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.Bool(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    switch (ParseRouteBool(value))
                    {
                        case Option.Some(parsed): { return handler(parsed); }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.RequestString(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): { return handler(request, value); }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.RequestInt(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    int parsed = 0;
                    if (int.TryParse(value, out parsed)) {
                        return handler(request, parsed);
                    }
                    return RouteBindingBadRequest();
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.RequestLong(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    long parsed = 0;
                    if (long.TryParse(value, out parsed)) {
                        return handler(request, parsed);
                    }
                    return RouteBindingBadRequest();
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.RequestBool(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    switch (ParseRouteBool(value))
                    {
                        case Option.Some(parsed): {
                            return handler(request, parsed);
                        }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.BoundRouteRoute(endpoint): {
            switch (BindRoute(request, endpoint.first))
            {
                case Option.Some(first): {
                    switch (BindRoute(request, endpoint.second))
                    {
                        case Option.Some(second): {
                            RouteRouteBindingHandler invoke = endpoint.handler;
                            return invoke(first, second);
                        }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.BoundRouteQuery(endpoint): {
            switch (BindRoute(request, endpoint.route))
            {
                case Option.Some(route): {
                    switch (BindQuery(request, endpoint.query))
                    {
                        case Option.Some(query): {
                            RouteQueryBindingHandler invoke = endpoint.handler;
                            return invoke(route, query);
                        }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.BoundRouteHeader(endpoint): {
            switch (BindRoute(request, endpoint.route))
            {
                case Option.Some(route): {
                    switch (BindHeader(request, endpoint.header))
                    {
                        case Option.Some(header): {
                            RouteHeaderBindingHandler invoke = endpoint.handler;
                            return invoke(route, header);
                        }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.BoundRouteJson(endpoint): {
            switch (BindRoute(request, endpoint.route))
            {
                case Option.Some(route): {
                    switch (BindJsonBody(request))
                    {
                        case Option.Some(body): {
                            RouteJsonBindingHandler invoke = endpoint.handler;
                            return invoke(route, body);
                        }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncString(handler): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
        case RouteHandler.AsyncInt(handler): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
        case RouteHandler.AsyncLong(handler): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
        case RouteHandler.AsyncBool(handler): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
        case RouteHandler.AsyncRequestString(handler): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
        case RouteHandler.AsyncRequestInt(handler): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
        case RouteHandler.AsyncRequestLong(handler): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
        case RouteHandler.AsyncRequestBool(handler): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
        case RouteHandler.AsyncBoundRouteRoute(endpoint): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
        case RouteHandler.AsyncBoundRouteQuery(endpoint): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
        case RouteHandler.AsyncBoundRouteHeader(endpoint): {
            throw new InvalidOperationException(
                "async endpoint requires DispatchAsync"
            );
        }
        case RouteHandler.AsyncBoundRouteJson(endpoint): {
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
        case RouteHandler.String(handler): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.Int(handler): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.Long(handler): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.Bool(handler): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.RequestString(handler): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.RequestInt(handler): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.RequestLong(handler): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.RequestBool(handler): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.BoundRouteRoute(endpoint): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.BoundRouteQuery(endpoint): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.BoundRouteHeader(endpoint): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.BoundRouteJson(endpoint): {
            return InvokeRouteHandler(routeHandler, request);
        }
        case RouteHandler.AsyncString(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): { return await handler(value); }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncInt(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    int parsed = 0;
                    if (int.TryParse(value, out parsed)) {
                        return await handler(parsed);
                    }
                    return RouteBindingBadRequest();
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncLong(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    long parsed = 0;
                    if (long.TryParse(value, out parsed)) {
                        return await handler(parsed);
                    }
                    return RouteBindingBadRequest();
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncBool(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    switch (ParseRouteBool(value))
                    {
                        case Option.Some(parsed): {
                            return await handler(parsed);
                        }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncRequestString(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    return await handler(request, value);
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncRequestInt(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    int parsed = 0;
                    if (int.TryParse(value, out parsed)) {
                        return await handler(request, parsed);
                    }
                    return RouteBindingBadRequest();
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncRequestLong(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    long parsed = 0;
                    if (long.TryParse(value, out parsed)) {
                        return await handler(request, parsed);
                    }
                    return RouteBindingBadRequest();
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncRequestBool(handler): {
            switch (SingleRouteValue(request))
            {
                case Option.Some(value): {
                    switch (ParseRouteBool(value))
                    {
                        case Option.Some(parsed): {
                            return await handler(request, parsed);
                        }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncBoundRouteRoute(endpoint): {
            switch (BindRoute(request, endpoint.first))
            {
                case Option.Some(first): {
                    switch (BindRoute(request, endpoint.second))
                    {
                        case Option.Some(second): {
                            AsyncRouteRouteBindingHandler invoke = endpoint.handler;
                            return await invoke(first, second);
                        }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncBoundRouteQuery(endpoint): {
            switch (BindRoute(request, endpoint.route))
            {
                case Option.Some(route): {
                    switch (BindQuery(request, endpoint.query))
                    {
                        case Option.Some(query): {
                            AsyncRouteQueryBindingHandler invoke = endpoint.handler;
                            return await invoke(route, query);
                        }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncBoundRouteHeader(endpoint): {
            switch (BindRoute(request, endpoint.route))
            {
                case Option.Some(route): {
                    switch (BindHeader(request, endpoint.header))
                    {
                        case Option.Some(header): {
                            AsyncRouteHeaderBindingHandler invoke = endpoint.handler;
                            return await invoke(route, header);
                        }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
        case RouteHandler.AsyncBoundRouteJson(endpoint): {
            switch (BindRoute(request, endpoint.route))
            {
                case Option.Some(route): {
                    switch (BindJsonBody(request))
                    {
                        case Option.Some(body): {
                            AsyncRouteJsonBindingHandler invoke = endpoint.handler;
                            return await invoke(route, body);
                        }
                        case Option.None: { return RouteBindingBadRequest(); }
                    }
                }
                case Option.None: { return RouteBindingBadRequest(); }
            }
        }
    }
}

private Response DispatchRoutes(
    List<Route> routes,
    RouteTable routeTable,
    List<StaticDirectory> staticDirectories,
    List<HtmlMiddleware> middleware,
    RouteHandler fallback,
    Request request
)
{
    switch (routeTable.Match(request.method, request.path))
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
        switch (routeTable.Match("GET", request.path))
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
    RouteTable routeTable,
    List<StaticDirectory> staticDirectories,
    List<HtmlMiddleware> middleware,
    RouteHandler fallback,
    Request request
)
{
    switch (routeTable.Match(request.method, request.path))
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
        switch (routeTable.Match("GET", request.path))
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
