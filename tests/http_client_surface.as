using Aster.Interop;
using Aster.Memory;
using System.Net.Http;
using System.Text;

private string TestOrigin()
{
    switch (NativeProcessEnvironment("ASTER_HTTP_TEST_ORIGIN"))
    {
        case Result.Ok(value): { return value; }
        case Result.Err(error): { throw new IOException(error); }
    }
}

async Task<int> main()
{
    string origin = TestOrigin();
    HttpClient client = new HttpClient();
    client.TimeoutMilliseconds = 5000;
    client.MaximumResponseBodyBytes = 1024;

    HttpResponseMessage response = client.Get($"{origin}/hello");
    if (response.StatusCode != 200 || !response.IsSuccessStatusCode() ||
        response.Content.Length() != 11 ||
        !response.Headers.Contains("X-Fixture: yes") ||
        response.RequestUri != $"{origin}/hello")
    {
        return 1;
    }
    response.EnsureSuccessStatusCode();
    ReadOnlySpan<byte> bytes = response.Content.ReadAsBytes();
    if (ByteSliceAt(bytes, 0) != 104 || ByteSliceAt(bytes, 5) != 0 ||
        ByteSliceAt(bytes, 10) != 114)
    {
        return 2;
    }

    HttpResponseMessage posted = client.Post(
        $"{origin}/echo", "text/plain", "posted"
    );
    if (posted.StatusCode != 201 || posted.Content.ReadAsString() != "posted")
    {
        return 3;
    }

    HttpResponseMessage redirected = client.Get($"{origin}/redirect");
    if (redirected.StatusCode != 200 ||
        redirected.RequestUri != $"{origin}/hello")
    {
        return 4;
    }

    client.MaximumResponseBodyBytes = 4;
    bool limited = false;
    try
    {
        HttpResponseMessage ignored = client.Get($"{origin}/large");
    }
    catch (IOException error)
    {
        limited = error.Message.Contains("body limit");
    }
    if (!limited) { return 5; }

    Task<HttpResponseMessage> first = client.GetAsync($"{origin}/slow");
    Task<HttpResponseMessage> second = client.GetAsync($"{origin}/slow");
    HttpResponseMessage firstResponse = await first;
    HttpResponseMessage secondResponse = await second;
    if (firstResponse.Content.ReadAsString() != "slow" ||
        secondResponse.Content.ReadAsString() != "slow")
    {
        return 6;
    }

    CancellationTokenSource source = new();
    CancellationToken token = source.Token;
    Task<HttpResponseMessage> canceled = client.GetAsync(
        $"{origin}/slow", token
    );
    source.Cancel();
    bool observedCancellation = false;
    try
    {
        HttpResponseMessage ignoredCancellation = await canceled;
    }
    catch (OperationCanceledException error)
    {
        observedCancellation = error.Message.Contains("canceled");
    }
    if (!observedCancellation) { return 7; }

    client.MaximumResponseBodyBytes = 300000;
    HttpUploadStream upload = client.StartUpload(
        "POST",
        $"{origin}/echo",
        "Content-Type: text/plain\r\n",
        100000
    );
    Buffer uploadChunk = Buffer.allocate(70000);
    unsafe
    {
        Span<byte> uploadBytes = BufferAsMutSlice(uploadChunk);
        ByteSliceFill(uploadBytes, 65);
        await upload.WriteAsync(uploadBytes);
        await upload.WriteAsync(ByteSliceRange(uploadBytes, 0, 30000));
    }
    HttpResponseMessage uploaded = await upload.CompleteAsync();
    upload.Close();
    if (uploaded.StatusCode != 201 || uploaded.Content.Length() != 100000)
    {
        return 8;
    }
    unsafe
    {
        ReadOnlySpan<byte> uploadedBytes = uploaded.Content.ReadAsBytes();
        if (ByteSliceAt(uploadedBytes, 0) != 65 ||
            ByteSliceAt(uploadedBytes, 99999) != 65)
        {
            return 9;
        }
    }

    HttpUploadStream shortUpload = client.StartUpload(
        "POST", $"{origin}/echo", "Content-Type: text/plain\r\n", 2
    );
    bool rejectedShortUpload = false;
    unsafe
    {
        Span<byte> uploadBytes = BufferAsMutSlice(uploadChunk);
        await shortUpload.WriteAsync(ByteSliceRange(uploadBytes, 0, 1));
    }
    try
    {
        HttpResponseMessage ignoredShortUpload =
            await shortUpload.CompleteAsync();
    }
    catch (IOException error)
    {
        rejectedShortUpload = error.Message.Contains("content length");
    }
    shortUpload.Close();
    if (!rejectedShortUpload) { return 10; }

    HttpResponseStream stream = await client.GetStreamAsync(
        $"{origin}/stream"
    );
    if (stream.StatusCode != 200 || !stream.IsSuccessStatusCode() ||
        !stream.Headers.Contains("X-Stream: yes") ||
        stream.RequestUri != $"{origin}/stream")
    {
        return 11;
    }
    Buffer chunk = Buffer.allocate(4096);
    nuint total = 0;
    byte firstByte = 255;
    byte lastByte = 255;
    while (true)
    {
        bool finished = false;
        unsafe
        {
            Span<byte> destination = BufferAsMutSlice(chunk);
            nuint count = await stream.ReadAsync(destination);
            if (count == 0) { finished = true; }
            else
            {
                if (total == 0) { firstByte = ByteSliceAt(destination, 0); }
                lastByte = ByteSliceAt(destination, count - 1);
                total += count;
            }
        }
        if (finished) { break; }
    }
    stream.Close();
    if (total != 200000 || firstByte != 0 || lastByte != 203)
    {
        return 12;
    }

    client.MaximumResponseBodyBytes = 4;
    HttpResponseStream limitedStream = await client.GetStreamAsync(
        $"{origin}/large"
    );
    bool streamLimited = false;
    try
    {
        unsafe
        {
            Span<byte> destination = BufferAsMutSlice(chunk);
            nuint ignoredLimitedRead = await limitedStream.ReadAsync(
                destination
            );
        }
    }
    catch (IOException error)
    {
        streamLimited = error.Message.Contains("body limit");
    }
    limitedStream.Close();
    if (!streamLimited) { return 13; }

    client.MaximumResponseBodyBytes = 3000000;
    HttpResponseStream abandoned = await client.GetStreamAsync(
        $"{origin}/endless"
    );
    CancellationTokenSource readSource = new();
    CancellationToken readToken = readSource.Token;
    readSource.Cancel();
    bool readCanceled = false;
    try
    {
        unsafe
        {
            Span<byte> destination = BufferAsMutSlice(chunk);
            nuint ignoredRead = await abandoned.ReadAsync(
                destination, readToken
            );
        }
    }
    catch (OperationCanceledException error)
    {
        readCanceled = error.Message.Contains("canceled");
    }
    abandoned.Close();
    if (!readCanceled) { return 14; }
    return 0;
}
