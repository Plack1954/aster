namespace Aster.Web.Middleware;

public delegate Html Middleware(Html page);

public struct MiddlewareChain {
    Middleware first;
    Middleware second;
}

public MiddlewareChain MiddlewareChain(
    Middleware first,
    Middleware second
) {
    return new MiddlewareChain {
        first = first,
        second = second,
    };
}

public Html MiddlewareApply(
    MiddlewareChain chain,
    Html page
) {
    Middleware first = chain.first;
    Html afterFirst = first(page);
    Middleware second = chain.second;
    return second(afterFirst);
}
