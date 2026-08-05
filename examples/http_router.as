using Aster.Net.Http;
using Aster.Html;
using Aster.Web.HttpApp;

private Response home(Request request) {
    return Response.Ok(<h2>home</h2>);
}

private Response ShowUser(Request request) {
    string id =
        HttpPathParam("/users/:id", request.path, "id");
    return Response.Ok(<strong>{id}</strong>);
}

private Response CreateUser(Request request) {
    return Response.Ok(<p>{request.body}</p>);
}

private Response missing(Request request) {
    return Response.NotFound(<p>{request.path}</p>);
}

private string render(Response response) {
    switch (response) {
        case Response.Ok(page): {
            return page.ToHtmlString();
        }
        case Response.Redirect(location): {
            return location;
        }
        case Response.NotFound(page): {
            return page.ToHtmlString();
        }
        case Response.InternalError(page): {
            return page.ToHtmlString();
        }
    }
}

int main() {
    Router router = RouterNew(missing);
    router = RouterGet(router, "/", home);
    router = RouterGet(router, "/users/:id", ShowUser);
    router = RouterPost(router, "/users", CreateUser);

    Console.WriteLine(render(RouterDispatch(router, new Request {
        method = "GET",
        path = "/",
        host = "localhost",
        body = "",
    })));
    Console.WriteLine(render(RouterDispatch(router, new Request {
        method = "GET",
        path = "/users/42?full=true",
        host = "localhost",
        body = "",
    })));
    Console.WriteLine(render(RouterDispatch(router, new Request {
        method = "POST",
        path = "/users",
        host = "localhost",
        body = "Aster",
    })));
    Console.WriteLine(render(RouterDispatch(router, new Request {
        method = "DELETE",
        path = "/users",
        host = "localhost",
        body = "",
    })));
    return 0;
}
