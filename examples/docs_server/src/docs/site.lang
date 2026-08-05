namespace Docs.Site;

using Docs.Pages;
using Aster.Html;
using HttpApp = Aster.Web.HttpApp;
using Aster.Web.Middleware;
using HtmlRouter = Aster.Web.Router;

private Html frame(Html page) {
    return <div>
        {page}
    </div>;
}

private Html identity(Html page) {
    return page;
}

public HtmlRouter.Router SiteRouter() {
    HtmlRouter.Router router = HtmlRouter.RouterNew(missing);
    router = HtmlRouter.RouterAdd(router, "/", home);
    router = HtmlRouter.RouterAdd(router, "/guide", guide);
    return router;
}

public MiddlewareChain SiteMiddleware() {
    return MiddlewareChain(frame, identity);
}

public Html RenderPath(
    HtmlRouter.Router router,
    MiddlewareChain middleware,
    string path
) {
    Html page = HtmlRouter.RouterDispatch(router, path);
    return MiddlewareApply(middleware, page);
}

public long StatusFor(string path) {
    if (path == "/") {
        return 200;
    }
    if (path == "/guide") {
        return 200;
    }
    return 404;
}

private HttpApp.Response HomeHandler(HttpApp.Request request) {
    Html page = home(request.path);
    Html framed =
        MiddlewareApply(SiteMiddleware(), page);
    return HttpApp.Response.Ok(framed);
}

private HttpApp.Response GuideHandler(HttpApp.Request request) {
    Html page = guide(request.path);
    Html framed =
        MiddlewareApply(SiteMiddleware(), page);
    return HttpApp.Response.Ok(framed);
}

private HttpApp.Response MissingHandler(HttpApp.Request request) {
    Html page = missing(request.path);
    Html framed =
        MiddlewareApply(SiteMiddleware(), page);
    return HttpApp.Response.NotFound(framed);
}

public HttpApp.Router ApplicationRouter() {
    HttpApp.Router router = HttpApp.RouterNew(MissingHandler);
    router = HttpApp.RouterGet(router, "/", HomeHandler);
    router = HttpApp.RouterGet(router, "/guide", GuideHandler);
    return router;
}
