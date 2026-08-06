namespace System.Net.Http;

using Aster.Memory;

private extern Result<NativeHandle, string> NativeHttpClientSend(
    string method,
    string requestUri,
    string headers,
    ReadOnlySpan<byte> body,
    long timeoutMilliseconds,
    long maximumResponseBodyBytes,
    bool followRedirects
);

private extern long NativeHttpClientResponseStatus(NativeHandle response);
private extern string NativeHttpClientResponseHeaders(NativeHandle response);
private extern string NativeHttpClientResponseUrl(NativeHandle response);
private extern long NativeHttpClientResponseBodyLength(NativeHandle response);
private extern Result<nuint, string> NativeHttpClientResponseCopyBody(
    NativeHandle response,
    Span<byte> destination
);

public struct HttpContent
{
    Buffer Data;

    public HttpContent(ReadOnlySpan<byte> bytes)
    {
        Data = Buffer.allocate((long)ByteSliceLen(bytes));
        unsafe
        {
            Span<byte> destination = BufferAsMutSlice(Data);
            ByteSliceCopyTo(bytes, destination);
        }
    }
}

public nuint HttpContent.Length(const ref HttpContent self)
{
    unsafe { return ByteSliceLen(BufferAsSlice(self.Data)); }
}

// The returned view borrows Content.Data and must not outlive the content.
public ReadOnlySpan<byte> HttpContent.ReadAsBytes(const ref HttpContent self)
{
    unsafe { return BufferAsSlice(self.Data); }
}

public string HttpContent.ReadAsString(const ref HttpContent self)
{
    unsafe
    {
        ReadOnlySpan<byte> bytes = BufferAsSlice(self.Data);
        switch (ByteSliceToString(bytes, 0, ByteSliceLen(bytes)))
        {
            case Result.Ok(value): { return value; }
            case Result.Err(error): { throw new IOException(error); }
        }
    }
}

public struct HttpResponseMessage
{
    int StatusCode;
    string Headers;
    string RequestUri;
    HttpContent Content;
}

public bool HttpResponseMessage.IsSuccessStatusCode(
    const ref HttpResponseMessage self
)
{
    return self.StatusCode >= 200 && self.StatusCode <= 299;
}

public void HttpResponseMessage.EnsureSuccessStatusCode(
    const ref HttpResponseMessage self
)
{
    if (!self.IsSuccessStatusCode())
    {
        throw new IOException(
            $"HTTP request failed with status {self.StatusCode}"
        );
    }
}

public struct HttpClient
{
    long TimeoutMilliseconds;
    long MaximumResponseBodyBytes;
    bool FollowRedirects;

    public HttpClient()
    {
        TimeoutMilliseconds = 100000;
        MaximumResponseBodyBytes = 16777216;
        FollowRedirects = true;
    }
}

private NativeHandle HttpResultOrThrow(
    Result<NativeHandle, string> result
)
{
    switch (result)
    {
        case Result.Ok(response): { return response; }
        case Result.Err(error): { throw new IOException(error); }
    }
}

private nuint HttpCountOrThrow(Result<nuint, string> result)
{
    switch (result)
    {
        case Result.Ok(count): { return count; }
        case Result.Err(error): { throw new IOException(error); }
    }
}

public HttpResponseMessage HttpClient.Send(
    HttpClient self,
    string method,
    string requestUri,
    string headers,
    ReadOnlySpan<byte> body
)
{
    if (method.Length == 0) { throw new ArgumentException("method is empty"); }
    if (requestUri.Length == 0)
    {
        throw new ArgumentException("requestUri is empty");
    }
    if (self.TimeoutMilliseconds < 0)
    {
        throw new ArgumentException("TimeoutMilliseconds cannot be negative");
    }
    if (self.MaximumResponseBodyBytes < 0)
    {
        throw new ArgumentException(
            "MaximumResponseBodyBytes cannot be negative"
        );
    }

    NativeHandle nativeResponse = HttpResultOrThrow(
        NativeHttpClientSend(
            method,
            requestUri,
            headers,
            body,
            self.TimeoutMilliseconds,
            self.MaximumResponseBodyBytes,
            self.FollowRedirects
        )
    );
    long bodyLength = NativeHttpClientResponseBodyLength(nativeResponse);
    Buffer data = Buffer.allocate(bodyLength);
    unsafe
    {
        Span<byte> destination = BufferAsMutSlice(data);
        nuint copied = HttpCountOrThrow(
            NativeHttpClientResponseCopyBody(nativeResponse, destination)
        );
        if (copied != (nuint)bodyLength)
        {
            throw new IOException("HTTP response body copy was incomplete");
        }
    }
    return new()
    {
        StatusCode = (int)NativeHttpClientResponseStatus(nativeResponse),
        Headers = NativeHttpClientResponseHeaders(nativeResponse),
        RequestUri = NativeHttpClientResponseUrl(nativeResponse),
        Content = new() { Data = data }
    };
}

private HttpResponseMessage HttpClient.SendWithoutBody(
    HttpClient self,
    string method,
    string requestUri,
    string headers
)
{
    Buffer empty = Buffer.allocate(0);
    unsafe
    {
        return self.Send(method, requestUri, headers, BufferAsSlice(empty));
    }
}

public HttpResponseMessage HttpClient.Get(HttpClient self, string requestUri)
{
    return self.SendWithoutBody("GET", requestUri, "");
}

public HttpResponseMessage HttpClient.Delete(
    HttpClient self,
    string requestUri
)
{
    return self.SendWithoutBody("DELETE", requestUri, "");
}

public HttpResponseMessage HttpClient.Post(
    HttpClient self,
    string requestUri,
    string contentType,
    ReadOnlySpan<byte> body
)
{
    return self.Send(
        "POST", requestUri, $"Content-Type: {contentType}\r\n", body
    );
}

public HttpResponseMessage HttpClient.Post(
    HttpClient self,
    string requestUri,
    string contentType,
    string body
)
{
    return self.Post(requestUri, contentType, StringAsByteSlice(body));
}
