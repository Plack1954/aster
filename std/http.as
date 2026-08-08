namespace Aster.Net.Http;

using System.IO;

// Socket operations and bounded HTTP framing are registered C primitives.
// Routing, middleware, handlers, and response construction remain Aster.

public extern NativeHandle HttpServerOpenConfig(
    const ref string address,
    long port,
    long maxHeaderBytes,
    long maxBodyBytes,
    long timeoutMs
);

public extern NativeHandle HttpServerOpenKeepAlive(
    const ref string address,
    long port,
    long maxHeaderBytes,
    long maxBodyBytes,
    long timeoutMs,
    long maxRequestsPerConnection
);

public extern Result<NativeHandle, string> HttpTryServerOpen(
    const ref string address,
    long port,
    long maxHeaderBytes,
    long maxBodyBytes,
    long timeoutMs,
    long maxRequestsPerConnection
);

public extern long HttpServerPort(
    const ref NativeHandle server
);

public extern NativeHandle HttpAccept(
    const ref NativeHandle server
);

public extern Result<NativeHandle, string> HttpTryAccept(
    const ref NativeHandle server
);

public extern string HttpRequestMethod(
    const ref NativeHandle request
);

public extern string HttpRequestPath(
    const ref NativeHandle request
);

public extern string HttpRequestHeader(
    const ref NativeHandle request,
    const ref string name
);

// Borrowed compact name/value pairs used by transport-neutral adapters.
public extern string HttpRequestHeaders(
    const ref NativeHandle request
);

public extern string HttpRequestBody(
    const ref NativeHandle request
);

public extern string HttpRequestRemoteIpAddress(
    const ref NativeHandle request
);

public MemoryStream HttpRequestBodyStream(const ref NativeHandle request)
{
    string body = HttpRequestBody(request);
    List<byte> bytes = new();
    for (nuint index = 0; index < body.Length; index += 1)
    {
        bytes.Add(body[index]);
    }
    return MemoryStream.Create(bytes);
}

public extern bool HttpRequestNext(
    const ref NativeHandle request
);

public extern Result<bool, string> HttpTryRequestNext(
    const ref NativeHandle request
);

public extern bool HttpPathMatches(
    const ref string pattern,
    const ref string path
);

public extern string HttpPathParam(
    const ref string pattern,
    const ref string path,
    const ref string name
);

public extern Result<string, string> HttpFormValue(
    const ref string body,
    const ref string name
);

public extern long HttpRespondHtml(
    const ref NativeHandle request,
    long status,
    const ref string body
);

public extern bool HttpRespondHtmlReuse(
    const ref NativeHandle request,
    long status,
    const ref string body
);

public extern Result<bool, string> HttpTryRespondHtmlReuse(
    const ref NativeHandle request,
    long status,
    const ref string body
);

// Consumes Html directly. Its existing contiguous buffer is borrowed by the
// synchronous HTTP write for one call, then deterministically released.
public extern Result<bool, string> HttpTryRespondHtml(
    const ref NativeHandle request,
    long status,
    Html body
);

public extern Result<bool, string> HttpTryRespondRedirectReuse(
    const ref NativeHandle request,
    const ref string location
);

public extern bool HttpRespondReuse(
    const ref NativeHandle request,
    long status,
    const ref string contentType,
    const ref string body
);

public extern Result<bool, string> HttpTryRespondReuse(
    const ref NativeHandle request,
    long status,
    const ref string contentType,
    const ref string body
);

public Result<bool, string> HttpTryRespondBytesReuse(
    const ref NativeHandle request,
    long status,
    const ref string contentType,
    const ref List<byte> body
)
{
    StringBuilder bytes = new();
    foreach (byte value in body) { bytes.AppendByte(value); }
    return HttpTryRespondReuse(
        request,
        status,
        contentType,
        bytes.ToString()
    );
}

// `headers` is a validated sequence of complete `Name: value\r\n` lines.
// Framework adapters construct it; application code should use typed headers.
public extern Result<bool, string> HttpTryRespondHeadersReuse(
    const ref NativeHandle request,
    long status,
    const ref string contentType,
    const ref string headers,
    const ref string body
);

public extern Result<bool, string> HttpTryRespondEmptyHeadersReuse(
    const ref NativeHandle request,
    long status,
    const ref string headers
);

public extern Result<bool, string> HttpTryRespondHtmlHeadersReuse(
    const ref NativeHandle request,
    long status,
    const ref string headers,
    Html body
);

public extern long HttpStreamBegin(
    const ref NativeHandle request,
    long status,
    const ref string contentType
);

public extern long HttpStreamBeginHeaders(
    const ref NativeHandle request,
    long status,
    const ref string contentType,
    const ref string headers
);

public extern long HttpStreamChunk(
    const ref NativeHandle request,
    const ref string data
);

public extern long HttpStreamChunkBytes(
    const ref NativeHandle request,
    ReadOnlySpan<byte> data
);

public extern long HttpStreamFinish(
    const ref NativeHandle request
);
