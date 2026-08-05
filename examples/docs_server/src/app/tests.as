namespace App.Tests;

using Docs.Assets;
using Docs.Config;
using Docs.Site;
using AppHttp = Aster.Web.HttpApp;
using Aster.Web.Middleware;
using HtmlRouter = Aster.Web.Router;

int main() {
    Result<ServerConfig, string> parsed = ParseConfig(
        "address=127.0.0.1\nport=8080\nmax_header_bytes=4096\nmax_body_bytes=8192\ntimeout_ms=2500\n"
    );
    switch (parsed) {
        case Result.Ok(config): {
            Console.WriteLine(config.port);
            Console.WriteLine(config.timeoutMs);
        }
        case Result.Err(error): {
            Console.WriteLine(error);
            return 1;
        }
    }

    Result<ServerConfig, string> invalid =
        ParseConfig("unknown=1\n");
    switch (invalid) {
        case Result.Ok(config): {
            Console.WriteLine("invalid configuration accepted");
            return 1;
        }
        case Result.Err(error): {
            Console.WriteLine("invalid configuration rejected");
        }
    }

    Result<long, string> markdown = CountMarkdown("docs");
    switch (markdown) {
        case Result.Ok(count): {
            if (count < 1) {
                Console.WriteLine("documentation traversal found no files");
                return 1;
            }
            Console.WriteLine("documentation traversal passed");
        }
        case Result.Err(error): {
            Console.WriteLine(error);
            return 1;
        }
    }

    Result<string, string> asset =
        LoadAsset("examples/docs_server/assets/site.css");
    switch (asset) {
        case Result.Ok(contents): {
            Console.WriteLine("static asset test passed");
        }
        case Result.Err(error): {
            Console.WriteLine(error);
            return 1;
        }
    }

    AppHttp.Router appRouter = ApplicationRouter();
    AppHttp.Request request = new() {
        method = "GET",
        path = "/guide",
        host = "test.local",
        body = "",
    };
    AppHttp.Response response =
        AppHttp.RouterDispatch(appRouter, request);
    switch (response) {
        case AppHttp.Response.Ok(page): {
            string body = page.ToHtmlString();
            Console.WriteLine("typed HTTP handler passed");
        }
        case AppHttp.Response.Redirect(location): {
            Console.WriteLine("typed HTTP handler unexpectedly redirected");
            return 1;
        }
        case AppHttp.Response.NotFound(page): {
            Console.WriteLine("typed HTTP handler returned 404");
            return 1;
        }
        case AppHttp.Response.InternalError(page): {
            Console.WriteLine("typed HTTP handler returned 500");
            return 1;
        }
    }

    HtmlRouter.Router router = SiteRouter();
    MiddlewareChain middleware = SiteMiddleware();
    Html page = RenderPath(router, middleware, "/missing");
    string rendered = page.ToHtmlString();
    Console.WriteLine(rendered);
    Console.WriteLine("docs smoke passed");
    return 0;
}
