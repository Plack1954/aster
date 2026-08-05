namespace Lime.CurrentHttp;

using Lime;
using Aster.Net.Http;
using System.IO;
using System.Text;

private string AssetContentType(AssetKind kind)
{
    switch (kind)
    {
        case AssetKind.JavaScript: { return "text/javascript; charset=utf-8"; }
        case AssetKind.Json: { return "application/json; charset=utf-8"; }
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

public Request CurrentHttpRequest(NativeHandle request)
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
    NativeHandle request,
    Response response
)
{
    (int status, ResponseBody body, List<ResponseHeader> headers) = response;
    StringBuilder renderedHeaders = new();
    foreach (ResponseHeader header in headers)
    {
        renderedHeaders.Append(header.name);
        renderedHeaders.Append(": ");
        renderedHeaders.Append(header.value);
        renderedHeaders.Append("\r\n");
    }
    string headerBlock = renderedHeaders.ToString();
    switch (body)
    {
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
        case ResponseBody.Stream(streamBody): {
            (Stream stream, AssetKind kind) = streamBody;
            HttpStreamBeginHeaders(
                request,
                (long)status,
                AssetContentType(kind),
                headerBlock
            );
            bool reading = true;
            while (reading)
            {
                List<byte> bytes = stream.Read(65536);
                if (bytes.Count == 0)
                {
                    reading = false;
                }
                else
                {
                    StringBuilder chunk = new();
                    foreach (byte value in bytes)
                    {
                        chunk.AppendByte(value);
                    }
                    HttpStreamChunk(request, chunk.ToString());
                }
            }
            stream.Close();
            HttpStreamFinish(request);
            return Result.Ok(false);
        }
        case ResponseBody.File(fileBody): {
            (string path, AssetKind kind) = fileBody;
            FileStream stream = File.OpenRead(path);
            HttpStreamBeginHeaders(
                request,
                (long)status,
                AssetContentType(kind),
                headerBlock
            );
            try
            {
                while (true)
                {
                    List<byte> bytes = stream.Read(65536);
                    if (bytes.Count == 0) { break; }
                    StringBuilder chunk = new();
                    foreach (byte value in bytes)
                    {
                        chunk.AppendByte(value);
                    }
                    HttpStreamChunk(request, chunk.ToString());
                }
            }
            finally
            {
                stream.Close();
            }
            HttpStreamFinish(request);
            return Result.Ok(false);
        }
    }
}

public Result<bool, string> CurrentHttpDispatch(
    App app,
    NativeHandle request
)
{
    Request input = CurrentHttpRequest(request);
    Response output = app.dispatch(input);
    return CurrentHttpSend(request, output);
}

public Result<bool, string> CurrentHttpDispatchStateful<State>(
    StatefulApp<State> app,
    NativeHandle request
)
{
    Request input = CurrentHttpRequest(request);
    Response output = app.dispatch(input);
    return CurrentHttpSend(request, output);
}
