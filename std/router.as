namespace Aster.Web.Router;

using Aster.Net.Http;

public delegate Html Handler(const ref string path);

public struct Route {
    string path;
    Handler handler;
}

public struct Router {
    List<Route> routes;
    Handler fallback;
}

public Router RouterNew(Handler fallback) {
    List<Route> routes = new();
    return new() {
        routes = routes,
        fallback = fallback,
    };
}

public Router RouterAdd(
    Router router,
    string path,
    Handler handler
) {
    Router output = router;
    output.routes.Add(new() {
        path = path,
        handler = handler,
    });
    return output;
}

public Html RouterDispatch(
    const ref Router router,
    const ref string path
) {
    foreach (Route route in router.routes) {
        if (HttpPathMatches(route.path, path)) {
            Handler handler = route.handler;
            return handler(path);
        }
    }
    Handler fallback = router.fallback;
    return fallback(path);
}
