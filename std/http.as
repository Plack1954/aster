namespace Aster.Net.Http;

using System.IO;

// Socket operations and bounded HTTP framing are registered C primitives.
// Routing, middleware, handlers, and response construction remain Aster.

public extern NativeHandle HttpServerOpenConfig(
    string address,
    long port,
    long maxHeaderBytes,
    long maxBodyBytes,
    long timeoutMs
);

public extern NativeHandle HttpServerOpenKeepAlive(
    string address,
    long port,
    long maxHeaderBytes,
    long maxBodyBytes,
    long timeoutMs,
    long maxRequestsPerConnection
);

public extern Result<NativeHandle, string> HttpTryServerOpen(
    string address,
    long port,
    long maxHeaderBytes,
    long maxBodyBytes,
    long timeoutMs,
    long maxRequestsPerConnection
);

public extern long HttpServerPort(
    NativeHandle server
);

public extern NativeHandle HttpAccept(
    NativeHandle server
);

public extern Result<NativeHandle, string> HttpTryAccept(
    NativeHandle server
);

public extern string HttpRequestMethod(
    NativeHandle request
);

public extern string HttpRequestPath(
    NativeHandle request
);

public extern string HttpRequestHeader(
    NativeHandle request,
    string name
);

// Borrowed compact name/value pairs used by transport-neutral adapters.
public extern string HttpRequestHeaders(
    NativeHandle request
);

public extern string HttpRequestBody(
    NativeHandle request
);

public extern string HttpRequestRemoteIpAddress(
    NativeHandle request
);

public MemoryStream HttpRequestBodyStream(NativeHandle request)
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
    NativeHandle request
);

public extern Result<bool, string> HttpTryRequestNext(
    NativeHandle request
);

public extern bool HttpPathMatches(
    string pattern,
    string path
);

public extern string HttpPathParam(
    string pattern,
    string path,
    string name
);

public extern Result<string, string> HttpFormValue(
    string body,
    string name
);

public extern long HttpRespondHtml(
    NativeHandle request,
    long status,
    string body
);

public extern bool HttpRespondHtmlReuse(
    NativeHandle request,
    long status,
    string body
);

public extern Result<bool, string> HttpTryRespondHtmlReuse(
    NativeHandle request,
    long status,
    string body
);

// Consumes Html directly. Its existing contiguous buffer is borrowed by the
// synchronous HTTP write for one call, then deterministically released.
public extern Result<bool, string> HttpTryRespondHtml(
    NativeHandle request,
    long status,
    Html body
);

public extern Result<bool, string> HttpTryRespondRedirectReuse(
    NativeHandle request,
    string location
);

public extern bool HttpRespondReuse(
    NativeHandle request,
    long status,
    string contentType,
    string body
);

public extern Result<bool, string> HttpTryRespondReuse(
    NativeHandle request,
    long status,
    string contentType,
    string body
);

public Result<bool, string> HttpTryRespondBytesReuse(
    NativeHandle request,
    long status,
    string contentType,
    List<byte> body
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
    NativeHandle request,
    long status,
    string contentType,
    string headers,
    string body
);

public extern Result<bool, string> HttpTryRespondEmptyHeadersReuse(
    NativeHandle request,
    long status,
    string headers
);

public extern Result<bool, string> HttpTryRespondHtmlHeadersReuse(
    NativeHandle request,
    long status,
    string headers,
    Html body
);

public extern long HttpStreamBegin(
    NativeHandle request,
    long status,
    string contentType
);

public extern long HttpStreamBeginHeaders(
    NativeHandle request,
    long status,
    string contentType,
    string headers
);

public extern long HttpStreamChunk(
    NativeHandle request,
    string data
);

public extern long HttpStreamFinish(
    NativeHandle request
);
