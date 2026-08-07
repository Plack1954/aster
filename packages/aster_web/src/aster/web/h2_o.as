namespace Aster.Web.H2O;

using Aster.Web;
using Aster.Memory;
using Aster.Html;
using System.IO;
using System.Text;

public struct H2OServerOptions
{
    string Address;
    long Port;
    long MaxRequestBodySize;
    long RequestTimeoutMilliseconds;
    long GracefulShutdownTimeoutMilliseconds;
    long MaxQueuedRequests;
    bool HandleSignals;
}

public H2OServerOptions H2OServerOptions()
{
    return new()
    {
        Address = "127.0.0.1",
        Port = 8080,
        MaxRequestBodySize = 1048576,
        RequestTimeoutMilliseconds = 30000,
        GracefulShutdownTimeoutMilliseconds = 30000,
        MaxQueuedRequests = 256,
        HandleSignals = true
    };
}

private extern Result<NativeHandle, string> H2OTryServerOpenNative(
    string address,
    long port,
    long maxBodyBytes,
    long timeoutMilliseconds,
    long gracefulShutdownTimeoutMilliseconds,
    long maxQueuedRequests,
    bool handleSignals
);

public Result<NativeHandle, string> H2OTryServerOpen(
    H2OServerOptions options
)
{
    return H2OTryServerOpenNative(
        options.Address,
        options.Port,
        options.MaxRequestBodySize,
        options.RequestTimeoutMilliseconds,
        options.GracefulShutdownTimeoutMilliseconds,
        options.MaxQueuedRequests,
        options.HandleSignals
    );
}

public extern long H2OServerPort(NativeHandle server);
public extern bool H2OStopRequested(NativeHandle server);
public extern Result<bool, string> H2OTryShutdown(
    NativeHandle server
);
public extern Result<NativeHandle, string> H2OTryAccept(
    NativeHandle server
);
public extern string H2ORequestMethod(NativeHandle request);
public extern string H2ORequestTarget(NativeHandle request);
public extern string H2ORequestAuthority(NativeHandle request);
public extern string H2ORequestHeader(
    NativeHandle request,
    string name
);
public extern string H2ORequestHeaders(NativeHandle request);
public extern string H2ORequestBody(NativeHandle request);
public extern string H2ORequestRemoteIpAddress(NativeHandle request);
public extern string H2ORequestScheme(NativeHandle request);
public extern Result<bool, string> H2OTryRespond(
    NativeHandle request,
    long status,
    string contentType,
    string headers,
    string body,
    bool head
);
public extern Result<bool, string> H2OTryRespondBytes(
    NativeHandle request,
    long status,
    string contentType,
    string headers,
    ReadOnlySpan<byte> body,
    bool head
);
public extern Result<bool, string> H2OTryRespondEmpty(
    NativeHandle request,
    long status,
    string headers
);
public extern Result<NativeHandle, string> H2OTryStreamBegin(
    NativeHandle request,
    long status,
    string contentType,
    string headers,
    bool head
);
public extern Result<bool, string> H2OTryStreamWrite(
    NativeHandle stream,
    string bytes,
    bool final
);
public extern Result<bool, string> H2OTryStreamWriteBytes(
    NativeHandle stream,
    ReadOnlySpan<byte> bytes,
    bool final
);
public extern Result<bool, string> H2ORegisterStatic(
    NativeHandle server,
    string root
);
public extern Result<bool, string> H2OTryRespondFile(
    NativeHandle request,
    long status,
    string headers,
    string path
);

public Result<bool, string> H2OBind(
    NativeHandle server,
    WebApplication app
)
{
    List<string> roots = app.StaticRoots();
    foreach (string root in roots)
    {
        bool registered = try H2ORegisterStatic(server, root);
    }
    return Result.Ok(true);
}

private string H2OAssetContentType(AssetKind kind)
{
    switch (kind)
    {
        case AssetKind.JavaScript: { return "text/javascript; charset=utf-8"; }
        case AssetKind.Json: { return "application/json; charset=utf-8"; }
        case AssetKind.ProblemJson: {
            return "application/problem+json; charset=utf-8";
        }
        case AssetKind.Xml: { return "application/xml; charset=utf-8"; }
        case AssetKind.Svg: { return "image/svg+xml"; }
        case AssetKind.Png: { return "image/png"; }
        case AssetKind.Jpeg: { return "image/jpeg"; }
        case AssetKind.Gif: { return "image/gif"; }
        case AssetKind.WebP: { return "image/webp"; }
        case AssetKind.Icon: { return "image/x-icon"; }
        case AssetKind.Woff: { return "font/woff"; }
        case AssetKind.Woff2: { return "font/woff2"; }
        case AssetKind.Ttf: { return "font/ttf"; }
        case AssetKind.Wasm: { return "application/wasm"; }
        case AssetKind.Binary: { return "application/octet-stream"; }
    }
}

private Result<bool, string> H2OSendStream(
    NativeHandle request,
    long status,
    string contentType,
    string headers,
    bool head,
    Stream stream
)
{
    try
    {
        Buffer buffer = Buffer.allocate(65536);
        NativeHandle output = try H2OTryStreamBegin(
            request, status, contentType, headers, head
        );
        if (head)
        {
            return H2OTryStreamWrite(output, "", true);
        }
        unsafe
        {
            Span<byte> destination = BufferAsMutSlice(buffer);
            while (true)
            {
                nuint count = stream.ReadInto(destination);
                if (count == 0)
                {
                    return H2OTryStreamWrite(output, "", true);
                }
                ReadOnlySpan<byte> chunk = ByteSliceRange(
                    destination, 0, count
                );
                switch (H2OTryStreamWriteBytes(output, chunk, false))
                {
                    case Result.Ok(sent): { }
                    case Result.Err(error): { return Result.Err(error); }
                }
            }
        }
    }
    finally
    {
        stream.Close();
    }
    return Result.Err("H2O response stream ended unexpectedly");
}

public Request H2ORequest(NativeHandle request)
{
    return RequestNewTransport(
        H2ORequestMethod(request),
        H2ORequestTarget(request),
        H2ORequestAuthority(request),
        H2ORequestScheme(request),
        H2ORequestRemoteIpAddress(request),
        H2ORequestHeader(request, "content-type"),
        H2ORequestHeader(request, "cookie"),
        H2ORequestBody(request),
        H2ORequestHeaders(request)
    );
}

public Result<bool, string> H2OSend(
    NativeHandle request,
    Response response
)
{
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    StringBuilder renderedHeaders = new();
    foreach (ResponseHeader header in headers)
    {
        renderedHeaders.Append(header.Name);
        renderedHeaders.Append(": ");
        renderedHeaders.Append(header.Value);
        renderedHeaders.Append("\r\n");
    }
    string headerBlock = renderedHeaders.ToString();
    bool head = H2ORequestMethod(request) == "HEAD";
    switch (body)
    {
        case ResponseBody.Empty: {
            return H2OTryRespondEmpty(
                request, (long)status, headerBlock
            );
        }
        case ResponseBody.Html(page): {
            return H2OTryRespond(
                request, (long)status, "text/html; charset=utf-8",
                headerBlock, page.ToHtmlString(), head
            );
        }
        case ResponseBody.Text(text): {
            return H2OTryRespond(
                request, (long)status, "text/plain; charset=utf-8",
                headerBlock, text, head
            );
        }
        case ResponseBody.Css(text): {
            return H2OTryRespond(
                request, (long)status, "text/css; charset=utf-8",
                headerBlock, text, head
            );
        }
        case ResponseBody.Asset(asset): {
            (string bytes, AssetKind kind) = asset;
            return H2OTryRespond(
                request, (long)status, H2OAssetContentType(kind),
                headerBlock, bytes, head
            );
        }
        case ResponseBody.Bytes(byteBody): {
            (List<byte> bytes, AssetKind kind) = byteBody;
            Buffer buffer = Buffer.allocate((long)bytes.Count);
            unsafe
            {
                Span<byte> destination = BufferAsMutSlice(buffer);
                for (nuint index = 0; index < bytes.Count; index += 1)
                {
                    ByteSliceSet(destination, index, bytes[index]);
                }
                return H2OTryRespondBytes(
                    request, (long)status, H2OAssetContentType(kind),
                    headerBlock, destination, head
                );
            }
        }
        case ResponseBody.Stream(streamBody): {
            (Stream stream, AssetKind kind) = streamBody;
            return H2OSendStream(
                request, (long)status, H2OAssetContentType(kind),
                headerBlock, head, stream
            );
        }
        case ResponseBody.File(fileBody): {
            (string path, AssetKind kind) = fileBody;
            return H2OTryRespondFile(
                request, (long)status, headerBlock, path
            );
        }
    }
}

public Result<bool, string> H2ODispatch(
    WebApplication app,
    NativeHandle request
)
{
    try
    {
        Request input = H2ORequest(request);
        Response output = app.Dispatch(input);
        return H2OSend(request, output);
    }
    catch (ArgumentException error)
    {
        return H2OSend(
            request, Results.BadRequest(<h1>Bad request</h1>)
        );
    }
}

public async Task<Result<bool, string>> H2ODispatchAsync(
    WebApplication app,
    NativeHandle request
)
{
    try
    {
        Request input = H2ORequest(request);
        Response output = await app.DispatchAsync(input);
        return H2OSend(request, output);
    }
    catch (ArgumentException error)
    {
        return H2OSend(
            request, Results.BadRequest(<h1>Bad request</h1>)
        );
    }
}

public Result<bool, string> H2OServe(
    NativeHandle server,
    WebApplication app
)
{
    while (!H2OStopRequested(server))
    {
        switch (H2OTryAccept(server))
        {
            case Result.Ok(request): {
                switch (H2ODispatch(app, request))
                {
                    case Result.Ok(sent): { }
                    case Result.Err(error): { Console.Error.WriteLine(error); }
                }
            }
            case Result.Err(error): {
                if (!H2OStopRequested(server))
                {
                    return Result.Err(error);
                }
            }
        }
    }
    return H2OTryShutdown(server);
}

public async Task<Result<bool, string>> H2OServeAsync(
    NativeHandle server,
    WebApplication app
)
{
    while (!H2OStopRequested(server))
    {
        switch (H2OTryAccept(server))
        {
            case Result.Ok(request): {
                switch (await H2ODispatchAsync(app, request))
                {
                    case Result.Ok(sent): { }
                    case Result.Err(error): {
                        Console.Error.WriteLine(error);
                    }
                }
            }
            case Result.Err(error): {
                if (!H2OStopRequested(server))
                {
                    return Result.Err(error);
                }
            }
        }
    }
    return H2OTryShutdown(server);
}
