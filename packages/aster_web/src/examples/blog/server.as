namespace Examples.Blog.Server;

using Examples.Blog.App;
using Aster.Web;
using Aster.Web.CurrentHttp;
using Aster.Net.Http;

private int serve(NativeHandle server, WebApplication app)
{
    Console.WriteLine($"http://127.0.0.1:{HttpServerPort(server)}");
    while (true)
    {
        switch (HttpTryAccept(server))
        {
            case Result.Ok(request): {
                switch (CurrentHttpDispatch(app, request))
                {
                    case Result.Ok(reuse): {
                    }
                    case Result.Err(error): {
                        Console.Error.WriteLine(error);
                        return 1;
                    }
                }
            }
            case Result.Err(error): {
                Console.Error.WriteLine(error);
                return 1;
            }
        }
    }
    return 0;
}

int main()
{
    switch (CreateApp())
    {
        case Result.Ok(application): {
            (WebApplication app, Blog state) = application;
            switch (HttpTryServerOpen(
                "127.0.0.1", 0, 8192, 65536, 5000, 128
            ))
            {
                case Result.Ok(server): { return serve(server, app); }
                case Result.Err(error): {
                    Console.Error.WriteLine(error);
                    return 1;
                }
            }
        }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
