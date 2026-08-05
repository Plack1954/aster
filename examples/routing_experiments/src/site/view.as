namespace Site.View;

using Aster.Html;
using Aster.Net.Http;
using Aster.Web.HttpApp;

private Html layout(string title, Html content) {
    return <>
        <header class="site-header">
            <a href=Url.relative("/")>Nook</a>
            <nav>
                <a href=Url.relative("/about/")>About</a>
            </nav>
        </header>
        <main>
            <h1>{title}</h1>
            {content}
        </main>
        <footer>Nook in Aster</footer>
    </>;
}

public Response home(Request request) {
    return Response.Ok(layout(
        "Latest articles",
        <section class="post-list">
            <article><a href=Url.relative("/article/hello-world/")>Hello world</a></article>
            <article><a href=Url.relative("/article/homemade-presents/")>Homemade presents</a></article>
        </section>
    ));
}

public Response about(Request request) {
    return Response.Ok(layout(
        "About",
        <p>A small publication-shaped website.</p>
    ));
}

public Response article(Request request) {
    string slug = HttpPathParam(
        "/article/:slug/", request.path, "slug"
    );
    return Response.Ok(layout(
        "Article",
        <article><p>Requested article: {slug}</p></article>
    ));
}

public Response missing(Request request) {
    return Response.NotFound(layout(
        "Page not found",
        <p>No page exists at {request.path}</p>
    ));
}

public Request request(string path) {
    return new Request {
        method = "GET",
        path = path,
        host = "localhost",
        body = "",
    };
}

public void PrintResponse(Response response) {
    switch (response) {
        case Response.Ok(page): { Console.WriteLine(page.ToHtmlString()); }
        case Response.Redirect(location): { Console.WriteLine(location); }
        case Response.NotFound(page): { Console.WriteLine(page.ToHtmlString()); }
        case Response.InternalError(page): { Console.WriteLine(page.ToHtmlString()); }
    }
}
