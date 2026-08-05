using Aster.Html;
using Aster.Net.Http;
using System.Text;

private Html Page(string path)
{
    return <main><h1>Aster versus C#</h1><p>Path: {path}</p></main>;
}

private int Serve(NativeHandle server)
{
    while (true)
    {
        switch (HttpTryAccept(server))
        {
            case Result.Ok(request): {
                string path = HttpRequestPath(request);
                string body = Page(path).ToHtmlString();
                switch (HttpTryRespondHtmlReuse(request, 200, body))
                {
                    case Result.Ok(reuse): {}
                    case Result.Err(error): {}
                }
            }
            case Result.Err(error): {}
        }
    }
    return 0;
}

int main()
{
    switch (HttpTryServerOpen("127.0.0.1", 18480, 16384, 1024, 5000, 1))
    {
        case Result.Ok(server): { return Serve(server); }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
