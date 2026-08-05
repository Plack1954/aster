namespace App.Routes;

using Issues.Model;
using Issues.Pages;
using Aster.Net.Http;
using Aster.Web.HttpApp;
using Aster.Data.Sqlite;
using System.Text;

private string DatabasePath() {
    return "examples/issue_tracker/issues.db";
}

private Response DatabaseError(SqliteError error) {
    return Response.InternalError(
        ErrorPage(error)
    );
}

private Response HtmlResponse(
    Result<Html, SqliteError> rendered
) {
    switch (rendered) {
        case Result.Ok(document): {
            return Response.Ok(document);
        }
        case Result.Err(error): {
            return DatabaseError(error);
        }
    }
}

private Response OptionalHtmlResponse(
    Result<Option<Html>, SqliteError> rendered,
    string missingPath
) {
    switch (rendered) {
        case Result.Ok(page): {
            switch (page) {
                case Option.Some(document): {
                    return Response.Ok(document);
                }
                case Option.None: {
                    return Response.NotFound(
                        MissingPage(missingPath)
                    );
                }
            }
        }
        case Result.Err(error): {
            return DatabaseError(error);
        }
    }
}

private Result<Html, SqliteError> RenderIssueList() {
    var database = try SqliteOpen(DatabasePath());
    var issues = try ListIssues(database);
    return Result.Ok(IssueListPage(issues));
}

private Result<Option<Html>, SqliteError> RenderIssue(string id) {
    var database = try SqliteOpen(DatabasePath());
    var found = try FindIssue(database, id);
    switch (found) {
        case Option.Some(issue): {
            return Result.Ok(Option.Some(
                IssueDetailPage(issue)
            ));
        }
        case Option.None: {
            return Result.Ok(Option.None);
        }
    }
}

private Result<long, SqliteError> InsertIssue(string title) {
    var database = try SqliteOpen(DatabasePath());
    try EnsureSchema(database);
    return CreateIssue(database, title);
}

private Result<bool, SqliteError> FinishIssue(string id) {
    var database = try SqliteOpen(DatabasePath());
    return CloseIssue(database, id);
}

private Response ListHandler(Request _request) {
    return HtmlResponse(RenderIssueList());
}

private Response NewHandler(Request _request) {
    return Response.Ok(NewIssuePage());
}

private Response ShowHandler(Request request) {
    string id =
        HttpPathParam("/issues/:id", request.path, "id");
    return OptionalHtmlResponse(
        RenderIssue(id),
        request.path,
    );
}

private Response CreateHandler(Request request) {
    Result<string, string> decoded =
        HttpFormValue(request.body, "title");
    switch (decoded) {
        case Result.Ok(title): {
            var created = InsertIssue(title);
            switch (created) {
                case Result.Ok(id): {
                    return Response.Redirect(
                        "/issues"
                    );
                }
                case Result.Err(error): {
                    return DatabaseError(error);
                }
            }
        }
        case Result.Err(error): {
            return Response.InternalError(ErrorPage(error));
        }
    }
}

private Response CloseHandler(Request request) {
    string id = HttpPathParam(
        "/issues/:id/close", request.path, "id"
    );
    var closed = FinishIssue(id);
    switch (closed) {
        case Result.Ok(found): {
            if (found) {
                return Response.Redirect(
                    "/issues"
                );
            }
            return Response.NotFound(
                MissingPage(request.path)
            );
        }
        case Result.Err(error): {
            return DatabaseError(error);
        }
    }
}

private Response MissingHandler(Request request) {
    return Response.NotFound(MissingPage(request.path));
}

public Router ApplicationRouter() {
    var router = RouterNew(MissingHandler);
    RouterGetMut(router, "/issues/new", NewHandler);
    RouterGetMut(router, "/issues/:id", ShowHandler);
    RouterGetMut(router, "/issues", ListHandler);
    RouterPostMut(router, "/issues", CreateHandler);
    RouterPostMut(
        router,
        "/issues/:id/close",
        CloseHandler,
    );
    return router;
}
