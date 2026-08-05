namespace Aster.Web.Router;

using Aster.Net.Http;

public delegate Html Handler(string path);

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
    List<Route> routes = output.routes;
    routes.Add(new() {
        path = path,
        handler = handler,
    });
    output.routes = routes;
    return output;
}

public Html RouterDispatch(
    Router router,
    string path
) {
    nuint length = router.routes.Count;
    for (nuint index = 0; index < length; index++) {
        Route route = router.routes[index];
        if (HttpPathMatches(route.path, path)) {
            Handler handler = route.handler;
            return handler(path);
        }
    }
    Handler fallback = router.fallback;
    return fallback(path);
}
