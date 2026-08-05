namespace App.Render;

using Docs.Assets;
using Docs.Config;
using Docs.Site;
using Aster.Web.Middleware;
using Aster.Web.Router;

int main() {
    Router router = SiteRouter();
    MiddlewareChain middleware = SiteMiddleware();

    Html home = RenderPath(router, middleware, "/");
    string homeText = home.ToHtmlString();
    Console.WriteLine(homeText);

    Html guide = RenderPath(router, middleware, "/guide");
    string guideText = guide.ToHtmlString();
    Console.WriteLine(guideText);

    Result<ServerConfig, string> loaded =
        LoadConfig("examples/docs_server/docs-server.conf");
    switch (loaded) {
        case Result.Ok(config): {
            Console.WriteLine(config.timeoutMs);
            Console.WriteLine("configuration parsed");
        }
        case Result.Err(error): {
            Console.WriteLine(error);
        }
    }

    Result<long, string> traversed = CountMarkdown("docs");
    switch (traversed) {
        case Result.Ok(count): {
            Console.WriteLine(count);
            Console.WriteLine("documentation directory traversed");
        }
        case Result.Err(error): {
            Console.WriteLine(error);
        }
    }

    Result<string, string> asset =
        LoadAsset("examples/docs_server/assets/site.css");
    switch (asset) {
        case Result.Ok(contents): {
            Console.WriteLine("static asset loaded");
        }
        case Result.Err(error): {
            Console.WriteLine(error);
        }
    }
    return 0;
}
