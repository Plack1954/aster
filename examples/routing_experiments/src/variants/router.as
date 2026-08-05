namespace Variants.Router;

using Aster.Web.HttpApp;
using Site.View;

private Router application() {
    Router router = RouterNew(missing);
    RouterGetMut(router, "/", home);
    RouterGetMut(router, "/about/", about);
    RouterGetMut(router, "/article/:slug/", article);
    return router;
}

int main() {
    Router router = application();
    PrintResponse(RouterDispatch(router, request("/")));
    PrintResponse(RouterDispatch(router, request("/about/")));
    PrintResponse(RouterDispatch(router, request("/article/hello-world/")));
    PrintResponse(RouterDispatch(router, request("/missing/")));
    return 0;
}
