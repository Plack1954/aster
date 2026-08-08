namespace Aster.Web.CurrentHttp;

using Aster.Web;
using Aster.Memory;
using Aster.Net.Http;
using Aster.Html;
using System.IO;
using System.Text;

private string AssetContentType(AssetKind kind)
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

public Request CurrentHttpRequest(const ref NativeHandle request)
{
    return RequestNewTransport(
        HttpRequestMethod(request),
        HttpRequestPath(request),
        HttpRequestHeader(request, "host"),
        "http",
        HttpRequestRemoteIpAddress(request),
        HttpRequestHeader(request, "content-type"),
        HttpRequestHeader(request, "cookie"),
        HttpRequestBody(request),
        HttpRequestHeaders(request)
    );
}

public Result<bool, string> CurrentHttpSend(
    const ref NativeHandle request,
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
    switch (body)
    {
        case ResponseBody.Empty: {
            return HttpTryRespondEmptyHeadersReuse(
                request, (long)status, headerBlock
            );
        }
        case ResponseBody.Html(page): {
            return HttpTryRespondHtmlHeadersReuse(
                request, (long)status, headerBlock, page
            );
        }
        case ResponseBody.Text(text): {
            return HttpTryRespondHeadersReuse(
                request,
                (long)status,
                "text/plain; charset=utf-8",
                headerBlock,
                text
            );
        }
        case ResponseBody.Css(text): {
            return HttpTryRespondHeadersReuse(
                request,
                (long)status,
                "text/css; charset=utf-8",
                headerBlock,
                text
            );
        }
        case ResponseBody.Asset(asset): {
            (string bytes, AssetKind kind) = asset;
            return HttpTryRespondHeadersReuse(
                request,
                (long)status,
                AssetContentType(kind),
                headerBlock,
                bytes
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
                HttpStreamBeginHeaders(
                    request, (long)status,
                    AssetContentType(kind), headerBlock
                );
                if (HttpRequestMethod(request) != "HEAD")
                {
                    HttpStreamChunkBytes(request, destination);
                }
                HttpStreamFinish(request);
            }
            return Result.Ok(false);
        }
        case ResponseBody.Stream(streamBody): {
            (Stream stream, AssetKind kind) = streamBody;
            try
            {
                HttpStreamBeginHeaders(
                    request,
                    (long)status,
                    AssetContentType(kind),
                    headerBlock
                );
                if (HttpRequestMethod(request) != "HEAD")
                {
                    Buffer buffer = Buffer.allocate(65536);
                    unsafe
                    {
                        Span<byte> destination = BufferAsMutSlice(buffer);
                        while (true)
                        {
                            nuint count = stream.ReadInto(destination);
                            if (count == 0) { break; }
                            ReadOnlySpan<byte> chunk = ByteSliceRange(
                                destination, 0, count
                            );
                            HttpStreamChunkBytes(request, chunk);
                        }
                    }
                }
                HttpStreamFinish(request);
            }
            finally
            {
                stream.Close();
            }
            return Result.Ok(false);
        }
        case ResponseBody.File(fileBody): {
            (string path, AssetKind kind) = fileBody;
            FileStream stream = File.OpenRead(path);
            try
            {
                HttpStreamBeginHeaders(
                    request,
                    (long)status,
                    AssetContentType(kind),
                    headerBlock
                );
                if (HttpRequestMethod(request) != "HEAD")
                {
                    Buffer buffer = Buffer.allocate(65536);
                    unsafe
                    {
                        Span<byte> destination = BufferAsMutSlice(buffer);
                        while (true)
                        {
                            nuint count = stream.ReadInto(destination);
                            if (count == 0) { break; }
                            ReadOnlySpan<byte> chunk = ByteSliceRange(
                                destination, 0, count
                            );
                            HttpStreamChunkBytes(request, chunk);
                        }
                    }
                }
                HttpStreamFinish(request);
            }
            finally
            {
                stream.Close();
            }
            return Result.Ok(false);
        }
    }
}

public Result<bool, string> CurrentHttpDispatch(
    WebApplication app,
    NativeHandle request
)
{
    try
    {
        Request input = CurrentHttpRequest(request);
        Response output = app.Dispatch(input);
        return CurrentHttpSend(request, output);
    }
    catch (ArgumentException error)
    {
        return CurrentHttpSend(
            request, Results.BadRequest(<h1>Bad request</h1>)
        );
    }
}

public async Task<Result<bool, string>> CurrentHttpDispatchAsync(
    WebApplication app,
    NativeHandle request
)
{
    try
    {
        Request input = CurrentHttpRequest(request);
        Response output = await app.DispatchAsync(input);
        return CurrentHttpSend(request, output);
    }
    catch (ArgumentException error)
    {
        return CurrentHttpSend(
            request, Results.BadRequest(<h1>Bad request</h1>)
        );
    }
}
