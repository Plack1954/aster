namespace System.Net.Http;

using Aster.Memory;

private extern Result<NativeHandle, string> NativeHttpClientSend(
    const ref string method,
    const ref string requestUri,
    const ref string headers,
    ReadOnlySpan<byte> body,
    long timeoutMilliseconds,
    long maximumResponseBodyBytes,
    bool followRedirects
);
private extern Result<NativeHandle, string> NativeHttpClientStart(
    const ref string method,
    const ref string requestUri,
    const ref string headers,
    ReadOnlySpan<byte> body,
    long timeoutMilliseconds,
    long maximumResponseBodyBytes,
    bool followRedirects
);
private extern long NativeHttpClientPoll(const ref NativeHandle request);
private extern void NativeHttpClientCancel(const ref NativeHandle request);
private extern Result<NativeHandle, string> NativeHttpClientTakeResponse(
    const ref NativeHandle request
);
private extern Result<NativeHandle, string> NativeHttpClientStartStream(
    const ref string method,
    const ref string requestUri,
    const ref string headers,
    ReadOnlySpan<byte> body,
    long timeoutMilliseconds,
    long maximumResponseBodyBytes,
    bool followRedirects
);
private extern long NativeHttpClientStreamPoll(
    const ref NativeHandle stream);
private extern Result<nuint, string> NativeHttpClientStreamRead(
    const ref NativeHandle stream,
    Span<byte> destination
);
private extern bool NativeHttpClientStreamFinished(
    const ref NativeHandle stream);
private extern long NativeHttpClientStreamStatus(
    const ref NativeHandle stream);
private extern string NativeHttpClientStreamHeaders(
    const ref NativeHandle stream);
private extern string NativeHttpClientStreamUrl(
    const ref NativeHandle stream);
private extern void NativeHttpClientStreamClose(
    const ref NativeHandle stream);
private extern Result<NativeHandle, string> NativeHttpClientStartUpload(
    const ref string method,
    const ref string requestUri,
    const ref string headers,
    long contentLength,
    long timeoutMilliseconds,
    long maximumResponseBodyBytes,
    bool followRedirects
);
private extern Result<nuint, string> NativeHttpClientUploadWrite(
    const ref NativeHandle upload,
    ReadOnlySpan<byte> source
);
private extern Result<Unit, string> NativeHttpClientUploadComplete(
    const ref NativeHandle upload
);
private extern Task Task.Delay(int milliseconds);

private extern long NativeHttpClientResponseStatus(
    const ref NativeHandle response);
private extern string NativeHttpClientResponseHeaders(
    const ref NativeHandle response);
private extern string NativeHttpClientResponseUrl(
    const ref NativeHandle response);
private extern long NativeHttpClientResponseBodyLength(
    const ref NativeHandle response);
private extern Result<nuint, string> NativeHttpClientResponseCopyBody(
    const ref NativeHandle response,
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

// An owned native response stream. It is a class so async reads can retain a
// zero-cost reference to the same stream state. Close destroys the owner.
public class HttpResponseStream
{
    public NativeHandle Handle;
    public int StatusCode;
    public string Headers;
    public string RequestUri;

    public HttpResponseStream(
        NativeHandle handle,
        int statusCode,
        string headers,
        string requestUri
    )
    {
        Handle = handle;
        StatusCode = statusCode;
        Headers = headers;
        RequestUri = requestUri;
    }

    ~HttpResponseStream()
    {
        NativeHttpClientStreamClose(Handle);
    }
}

public bool HttpResponseStream.IsSuccessStatusCode(
    const ref HttpResponseStream self
)
{
    return self.StatusCode >= 200 && self.StatusCode <= 299;
}

public async Task<nuint> HttpResponseStream.ReadAsync(
    HttpResponseStream self,
    Span<byte> destination,
    CancellationToken cancellationToken
)
{
    if (cancellationToken.IsCancellationRequested)
    {
        NativeHttpClientCancel(self.Handle);
        cancellationToken.ThrowIfCancellationRequested();
    }
    if (ByteSliceLen(destination) == 0) { return 0; }
    while (true)
    {
        nuint count = HttpCountOrThrow(
            NativeHttpClientStreamRead(self.Handle, destination)
        );
        if (count != 0 || NativeHttpClientStreamFinished(self.Handle))
        {
            return count;
        }
        if (cancellationToken.IsCancellationRequested)
        {
            NativeHttpClientCancel(self.Handle);
            cancellationToken.ThrowIfCancellationRequested();
        }
        long ready = NativeHttpClientStreamPoll(self.Handle);
        if (ready == 0) { await Task.Delay(1); }
    }
    return 0;
}

public async Task<nuint> HttpResponseStream.ReadAsync(
    HttpResponseStream self,
    Span<byte> destination
)
{
    return await self.ReadAsync(destination, CancellationToken.None);
}

public void HttpResponseStream.Close(HttpResponseStream self)
{
    delete self;
}

// A fixed-length streaming request body. Bytes are copied only into a bounded
// native queue; CompleteAsync finishes the transfer and returns its response.
public class HttpUploadStream
{
    public NativeHandle Handle;

    public HttpUploadStream(NativeHandle handle)
    {
        Handle = handle;
    }

    ~HttpUploadStream()
    {
        NativeHttpClientStreamClose(Handle);
    }
}

public async Task HttpUploadStream.WriteAsync(
    HttpUploadStream self,
    ReadOnlySpan<byte> source,
    CancellationToken cancellationToken
)
{
    if (cancellationToken.IsCancellationRequested)
    {
        NativeHttpClientCancel(self.Handle);
        cancellationToken.ThrowIfCancellationRequested();
    }
    nuint offset = 0;
    nuint length = ByteSliceLen(source);
    while (offset < length)
    {
        ReadOnlySpan<byte> remaining = ByteSliceRange(
            source, offset, length - offset
        );
        nuint copied = HttpCountOrThrow(
            NativeHttpClientUploadWrite(self.Handle, remaining)
        );
        offset += copied;
        if (offset == length) { break; }
        if (cancellationToken.IsCancellationRequested)
        {
            NativeHttpClientCancel(self.Handle);
            cancellationToken.ThrowIfCancellationRequested();
        }
        NativeHttpClientPoll(self.Handle);
        await Task.Delay(1);
    }
}

public async Task HttpUploadStream.WriteAsync(
    HttpUploadStream self,
    ReadOnlySpan<byte> source
)
{
    await self.WriteAsync(source, CancellationToken.None);
}

public async Task<HttpResponseMessage> HttpUploadStream.CompleteAsync(
    HttpUploadStream self,
    CancellationToken cancellationToken
)
{
    if (cancellationToken.IsCancellationRequested)
    {
        NativeHttpClientCancel(self.Handle);
        cancellationToken.ThrowIfCancellationRequested();
    }
    Unit ignored = HttpUnitOrThrow(
        NativeHttpClientUploadComplete(self.Handle)
    );
    while (NativeHttpClientPoll(self.Handle) == 0)
    {
        if (cancellationToken.IsCancellationRequested)
        {
            NativeHttpClientCancel(self.Handle);
            cancellationToken.ThrowIfCancellationRequested();
        }
        await Task.Delay(1);
    }
    return MaterializeHttpResponse(
        HttpResultOrThrow(NativeHttpClientTakeResponse(self.Handle))
    );
}

public async Task<HttpResponseMessage> HttpUploadStream.CompleteAsync(
    HttpUploadStream self
)
{
    return await self.CompleteAsync(CancellationToken.None);
}

public void HttpUploadStream.Close(HttpUploadStream self)
{
    delete self;
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

private Unit HttpUnitOrThrow(Result<Unit, string> result)
{
    switch (result)
    {
        case Result.Ok(value): { return value; }
        case Result.Err(error): { throw new IOException(error); }
    }
}

private void ValidateHttpRequest(
    HttpClient client,
    const ref string method,
    const ref string requestUri
)
{
    if (method.Length == 0) { throw new ArgumentException("method is empty"); }
    if (requestUri.Length == 0)
    {
        throw new ArgumentException("requestUri is empty");
    }
    if (client.TimeoutMilliseconds < 0)
    {
        throw new ArgumentException("TimeoutMilliseconds cannot be negative");
    }
    if (client.MaximumResponseBodyBytes < 0)
    {
        throw new ArgumentException(
            "MaximumResponseBodyBytes cannot be negative"
        );
    }
}

private HttpResponseMessage MaterializeHttpResponse(
    NativeHandle nativeResponse
)
{
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

private NativeHandle StartEmptyHttpStream(
    HttpClient client,
    const ref string requestUri
)
{
    Buffer empty = Buffer.allocate(0);
    unsafe
    {
        return HttpResultOrThrow(
            NativeHttpClientStartStream(
                "GET",
                requestUri,
                "",
                BufferAsSlice(empty),
                client.TimeoutMilliseconds,
                client.MaximumResponseBodyBytes,
                client.FollowRedirects
            )
        );
    }
}

public HttpUploadStream HttpClient.StartUpload(
    HttpClient self,
    string method,
    string requestUri,
    string headers,
    long contentLength
)
{
    ValidateHttpRequest(self, method, requestUri);
    if (contentLength < 0)
    {
        throw new ArgumentException("contentLength cannot be negative");
    }
    return new HttpUploadStream(
        HttpResultOrThrow(
            NativeHttpClientStartUpload(
                method,
                requestUri,
                headers,
                contentLength,
                self.TimeoutMilliseconds,
                self.MaximumResponseBodyBytes,
                self.FollowRedirects
            )
        )
    );
}

public HttpResponseMessage HttpClient.Send(
    HttpClient self,
    string method,
    string requestUri,
    string headers,
    ReadOnlySpan<byte> body
)
{
    ValidateHttpRequest(self, method, requestUri);

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
    return MaterializeHttpResponse(nativeResponse);
}

public async Task<HttpResponseMessage> HttpClient.SendAsync(
    HttpClient self,
    string method,
    string requestUri,
    string headers,
    ReadOnlySpan<byte> body,
    CancellationToken cancellationToken
)
{
    ValidateHttpRequest(self, method, requestUri);
    cancellationToken.ThrowIfCancellationRequested();
    NativeHandle request = HttpResultOrThrow(
        NativeHttpClientStart(
            method,
            requestUri,
            headers,
            body,
            self.TimeoutMilliseconds,
            self.MaximumResponseBodyBytes,
            self.FollowRedirects
        )
    );
    while (NativeHttpClientPoll(request) == 0)
    {
        if (cancellationToken.IsCancellationRequested)
        {
            NativeHttpClientCancel(request);
            cancellationToken.ThrowIfCancellationRequested();
        }
        await Task.Delay(1);
    }
    return MaterializeHttpResponse(
        HttpResultOrThrow(NativeHttpClientTakeResponse(request))
    );
}

public async Task<HttpResponseMessage> HttpClient.SendAsync(
    HttpClient self,
    string method,
    string requestUri,
    string headers,
    ReadOnlySpan<byte> body
)
{
    return await self.SendAsync(
        method, requestUri, headers, body, CancellationToken.None
    );
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

public async Task<HttpResponseMessage> HttpClient.GetAsync(
    HttpClient self,
    string requestUri,
    CancellationToken cancellationToken
)
{
    Buffer empty = Buffer.allocate(0);
    unsafe
    {
        return await self.SendAsync(
            "GET", requestUri, "", BufferAsSlice(empty), cancellationToken
        );
    }
}

public async Task<HttpResponseStream> HttpClient.GetStreamAsync(
    HttpClient self,
    string requestUri,
    CancellationToken cancellationToken
)
{
    ValidateHttpRequest(self, "GET", requestUri);
    cancellationToken.ThrowIfCancellationRequested();
    NativeHandle stream = StartEmptyHttpStream(self, requestUri);
    while (NativeHttpClientStreamPoll(stream) == 0)
    {
        if (cancellationToken.IsCancellationRequested)
        {
            NativeHttpClientCancel(stream);
            cancellationToken.ThrowIfCancellationRequested();
        }
        await Task.Delay(1);
    }
    long statusCode = NativeHttpClientStreamStatus(stream);
    string responseHeaders = NativeHttpClientStreamHeaders(stream);
    string responseUrl = NativeHttpClientStreamUrl(stream);
    return new HttpResponseStream(
        stream,
        (int)statusCode,
        responseHeaders,
        responseUrl
    );
}

public async Task<HttpResponseStream> HttpClient.GetStreamAsync(
    HttpClient self,
    string requestUri
)
{
    return await self.GetStreamAsync(requestUri, CancellationToken.None);
}

public async Task<HttpResponseMessage> HttpClient.GetAsync(
    HttpClient self,
    string requestUri
)
{
    return await self.GetAsync(requestUri, CancellationToken.None);
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
