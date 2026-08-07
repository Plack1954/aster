namespace Tests.RoutingPatterns;

using Aster.Web.Routing;

private RoutePattern Parse(string value)
{
    switch (RoutePattern.TryParse(value))
    {
        case Result.Ok(pattern): { return pattern; }
        case Result.Err(error): { throw new Exception(error); }
    }
}

private bool Rejects(string value)
{
    switch (RoutePattern.TryParse(value))
    {
        case Result.Ok(pattern): { return false; }
        case Result.Err(error): { return true; }
    }
}

private bool ParameterEquals(
    RoutePattern pattern,
    string path,
    string name,
    string expected
)
{
    switch (pattern.Parameter(path, name))
    {
        case Option.Some(value): { return value == expected; }
        case Option.None: { return false; }
    }
}

private bool ParameterAbsent(
    RoutePattern pattern,
    string path,
    string name
)
{
    switch (pattern.Parameter(path, name))
    {
        case Option.Some(value): { return false; }
        case Option.None: { return true; }
    }
}

private bool PathEquals(
    RoutePattern pattern,
    RouteValues values,
    string expected
)
{
    switch (pattern.GetPath(values))
    {
        case Result.Ok(path): { return path == expected; }
        case Result.Err(error): { return false; }
    }
}

private bool PathRejected(RoutePattern pattern, RouteValues values)
{
    switch (pattern.GetPath(values))
    {
        case Result.Ok(path): { return false; }
        case Result.Err(error): { return true; }
    }
}

private bool ConstraintsWork()
{
    RoutePattern minimum = Parse("/orders/{id:min(10)}");
    RoutePattern maximum = Parse("/orders/{id:max(20)}");
    RoutePattern range = Parse("/orders/{id:range(10,20)}");
    RoutePattern exactLength = Parse("/codes/{value:length(4)}");
    RoutePattern minimumLength = Parse("/names/{value:minlength(3)}");
    RoutePattern maximumLength = Parse("/tags/{value:maxlength(5)}");
    RoutePattern required = Parse("/required/{value:required}");
    RoutePattern chainedNumber = Parse(
        "/bounded/{id:int:min(10):max(20)}"
    );
    RoutePattern chainedText = Parse(
        "/initials/{value:alpha:minlength(3):maxlength(5)}"
    );
    if (!minimum.IsMatch("/orders/10") ||
        minimum.IsMatch("/orders/9") ||
        minimum.IsMatch("/orders/ten") ||
        !maximum.IsMatch("/orders/20") ||
        maximum.IsMatch("/orders/21") ||
        !range.IsMatch("/orders/10") ||
        !range.IsMatch("/orders/20") ||
        range.IsMatch("/orders/9") ||
        range.IsMatch("/orders/21") ||
        !exactLength.IsMatch("/codes/AB12") ||
        exactLength.IsMatch("/codes/ABC") ||
        !minimumLength.IsMatch("/names/Ada") ||
        minimumLength.IsMatch("/names/Al") ||
        !maximumLength.IsMatch("/tags/pear") ||
        maximumLength.IsMatch("/tags/asterx") ||
        !required.IsMatch("/required/value") ||
        required.IsMatch("/required/") ||
        !chainedNumber.IsMatch("/bounded/10") ||
        !chainedNumber.IsMatch("/bounded/20") ||
        chainedNumber.IsMatch("/bounded/9") ||
        chainedNumber.IsMatch("/bounded/21") ||
        chainedNumber.IsMatch("/bounded/ten") ||
        !chainedText.IsMatch("/initials/Ada") ||
        !chainedText.IsMatch("/initials/Grace") ||
        chainedText.IsMatch("/initials/Al") ||
        chainedText.IsMatch("/initials/GraceH") ||
        chainedText.IsMatch("/initials/Ada7"))
    {
        return false;
    }

    RouteValues validRange = RouteValues.From("id", "15");
    RouteValues invalidRange = RouteValues.From("id", "21");
    RouteValues validChained = RouteValues.From("id", "12");
    RouteValues invalidChained = RouteValues.From("id", "25");
    RoutePattern requiredCatchAll = Parse("/files/{*path:required}");
    RouteValues noValues = RouteValues.Create();
    RouteValues emptyPath = RouteValues.From("path", "");
    return PathEquals(range, validRange, "/orders/15") &&
        PathRejected(range, invalidRange) &&
        PathEquals(chainedNumber, validChained, "/bounded/12") &&
        PathRejected(chainedNumber, invalidChained) &&
        !requiredCatchAll.IsMatch("/files") &&
        PathRejected(requiredCatchAll, noValues) &&
        PathRejected(requiredCatchAll, emptyPath) &&
        !Parse("/bounded/{id:int:max(0)}").ConflictsWith(
            Parse("/bounded/{id:int:min(1)}")
        ) &&
        Parse("/bounded/{id:int:max(0)}").ConflictsWith(
            Parse("/bounded/{id:int:min(0)}")
        ) &&
        Rejects("/users/{id:int::min(1)}") &&
        Rejects("/users/{id:int:alpha}") &&
        Rejects("/users/{id:min(10):max(9)}") &&
        Rejects("/names/{value:minlength(5):maxlength(4)}") &&
        Rejects("/many/{value:required:required:required:required:required:required:required:required:required}");
}

int main()
{
    RoutePattern root = Parse("/");
    if (!root.IsMatch("/") || root.IsMatch("") || root.IsMatch("//"))
    {
        return 1;
    }

    RoutePattern literal = Parse("/articles/archive/");
    if (!literal.IsMatch("/articles/archive/") ||
        !literal.IsMatch("/ARTICLES/Archive/") ||
        !literal.IsMatch("/articles/archive") ||
        literal.IsMatch("/articles/other/"))
    {
        return 2;
    }

    RoutePattern article = Parse("/articles/{slug}");
    if (!article.HasParameters || article.SegmentCount != 2 ||
        !article.IsMatch("/articles/aster") ||
        !article.IsMatch("/articles/aster/") ||
        article.IsMatch("/articles/") ||
        !ParameterEquals(article, "/articles/aster", "SLUG", "aster"))
    {
        return 3;
    }

    RoutePattern integer = Parse("/users/{id:int}");
    if (!integer.IsMatch("/users/42") ||
        integer.IsMatch("/users/four") ||
        !ParameterEquals(integer, "/users/42", "id", "42"))
    {
        return 4;
    }

    RoutePattern boolean = Parse("/flags/{enabled:bool}");
    if (!boolean.IsMatch("/flags/true") ||
        !boolean.IsMatch("/flags/false") ||
        !boolean.IsMatch("/flags/TRUE") ||
        boolean.IsMatch("/flags/yes"))
    {
        return 5;
    }

    RoutePattern alpha = Parse("/authors/{name:alpha}");
    if (!alpha.IsMatch("/authors/Ada") ||
        alpha.IsMatch("/authors/Ada7"))
    {
        return 6;
    }

    if (!ConstraintsWork())
    {
        return 12;
    }

    RoutePattern optional = Parse("/search/{term?}");
    if (!optional.IsMatch("/search") ||
        !optional.IsMatch("/search/") ||
        !optional.IsMatch("/search/aster") ||
        optional.IsMatch("/search/aster/more"))
    {
        return 7;
    }

    RoutePattern files = Parse("/files/{*path}");
    RoutePattern preservedFiles = Parse("/files/{**path}");
    if (!files.IsMatch("/files") ||
        !files.IsMatch("/files/") ||
        !files.IsMatch("/files/a/b/c.txt") ||
        !ParameterAbsent(files, "/files", "path") ||
        !ParameterEquals(files, "/files/a/b/c.txt", "path", "a/b/c.txt") ||
        !preservedFiles.IsMatch("/files") ||
        !preservedFiles.IsMatch("/files/a/b/c.txt"))
    {
        return 8;
    }

    RoutePattern literalUser = Parse("/users/current");
    RoutePattern constrainedUser = Parse("/users/{id:int}");
    RoutePattern parameterUser = Parse("/users/{name}");
    RoutePattern catchAllUser = Parse("/users/{*path}");
    if (literalUser.ComparePrecedence(constrainedUser) >= 0 ||
        constrainedUser.ComparePrecedence(parameterUser) >= 0 ||
        parameterUser.ComparePrecedence(catchAllUser) >= 0 ||
        !Parse("/users/{id}").ConflictsWith(
            Parse("/users/{name}")
        ) ||
        !Parse("/users/{id:int}").ConflictsWith(
            Parse("/users/{value:long}")
        ) ||
        Parse("/users/{id:int}").ConflictsWith(
            Parse("/users/{name:alpha}")
        ) ||
        Parse("/users/current").ConflictsWith(
            Parse("/users/archive")
        ) ||
        !Parse("/Users/Current").ConflictsWith(
            Parse("/users/current")
        ) ||
        !Parse("/users/current/").ConflictsWith(
            Parse("/users/current")
        ) ||
        Parse("/orders/{id:range(1,5)}").ConflictsWith(
            Parse("/orders/{id:range(6,10)}")
        ) ||
        !Parse("/orders/{id:range(1,5)}").ConflictsWith(
            Parse("/orders/{id:range(5,10)}")
        ))
    {
        return 9;
    }

    RouteValues articleValues = RouteValues.From("slug", "hello world");
    articleValues.Add("view", "full page");
    RouteValues invalidInteger = RouteValues.From("id", "four");
    RouteValues fileValues = RouteValues.From("path", "a b/c#");
    RouteValues noValues = RouteValues.Create();
    RouteValues caseInsensitiveValues = RouteValues.From("SLUG", "Ada");
    if (!PathEquals(
            article,
            articleValues,
            "/articles/hello%20world?view=full%20page"
        ) ||
        !PathEquals(optional, noValues, "/search") ||
        !PathEquals(article, caseInsensitiveValues, "/articles/Ada") ||
        !PathEquals(files, fileValues, "/files/a%20b%2Fc%23") ||
        !PathEquals(
            preservedFiles, fileValues, "/files/a%20b/c%23"
        ) ||
        !PathEquals(files, noValues, "/files") ||
        !PathRejected(article, noValues) ||
        !PathRejected(integer, invalidInteger))
    {
        return 10;
    }

    if (!Rejects("articles") ||
        !Rejects("/articles//archive") ||
        !Rejects("/articles/{") ||
        !Rejects("/articles/{9slug}") ||
        !Rejects("/articles/{slug}/{slug}") ||
        !Rejects("/articles/{Slug}/{slug}") ||
        !Rejects("/articles/{slug?}/edit") ||
        !Rejects("/files/{*path}/edit") ||
        !Rejects("/users/{id:guid}") ||
        !Rejects("/orders/{id:min(ten)}") ||
        !Rejects("/orders/{id:range(20,10)}") ||
        !Rejects("/orders/{id:range(1,2,3)}") ||
        !Rejects("/codes/{value:length(-1)}"))
    {
        return 11;
    }
    return 0;
}
