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

int main()
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
    return 0;
}
