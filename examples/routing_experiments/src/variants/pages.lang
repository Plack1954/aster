namespace Variants.Pages;

using Aster.Web.HttpApp;
using Pages.Generated;
using Site.View;

int main() {
    Router router = RouterNew(missing);
    AddPages(router);
    PrintResponse(RouterDispatch(router, request("/")));
    PrintResponse(RouterDispatch(router, request("/about/")));
    PrintResponse(RouterDispatch(router, request("/missing/")));
    return 0;
}
