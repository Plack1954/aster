using Aster.Html;
using Aster.Net.Http;
using Aster.Web.Middleware;
using Aster.Core;
using Aster.Core;
using Aster.Core;
using Aster.Web.Router;
using System.Text;
using System.Collections.Generic;

private Html home(string path) {
    return <section><h2>Home</h2></section>;
}

private Html health(string path) {
    string id = HttpPathParam("/users/:id", path, "id");
    return <strong>{id}</strong>;
}

private Html missing(string path) {
    return <p>{path}</p>;
}

private Html frame(Html page) {
    return <section>{page}</section>;
}

private Html emphasize(Html page) {
    return <strong>{page}</strong>;
}

int main() {
    Pair<long, long> numbers = pair(20, 22);
    Console.WriteLine(numbers.first + numbers.second);

    Option<long> some = Option.Some(1);
    Console.WriteLine(OptionIsSome(some));
    Option<long> none = Option.None;
    Console.WriteLine(OptionIsNone(none));

    Result<long, string> ok = Result.Ok(1);
    Console.WriteLine(ResultIsOk(ok));
    Result<long, string> error = Result.Err("error");
    Console.WriteLine(ResultIsErr(error));

    List<long> values = new();
    values.Add(1);
    List<long> extended = values;
    extended.Add(2);
    Console.WriteLine(extended.Count);

    Console.WriteLine(
        HttpPathParam(
            "/users/:id",
            "/admins/42",
            "id",
        ) == ""
    );

    Router router = RouterNew(missing);
    router = RouterAdd(router, "/", home);
    router = RouterAdd(router, "/users/:id", health);
    Html page = RouterDispatch(router, "/users/42?full=true");
    string rendered = page.ToHtmlString();
    Console.WriteLine(rendered);

    string joined = string.Concat("Aster ", "libraries");
    Console.WriteLine(joined);

    MiddlewareChain chain =
        MiddlewareChain(frame, emphasize);
    Html content = <p>content</p>;
    Html wrapped = MiddlewareApply(chain, content);
    string wrappedText = wrapped.ToHtmlString();
    Console.WriteLine(wrappedText);
    return 0;
}
