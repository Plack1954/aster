namespace Issues.Pages;

using Issues.Model;
using Aster.Html;

private Url IssuePath(long id, string suffix) {
    return Url.relative($"/issues/{id}{suffix}");
}

private Url IssueUrl(long id) {
    return IssuePath(id, "");
}

private Url CloseUrl(long id) {
    return IssuePath(id, "/close");
}

private Html stylesheet() {
    return <style>
        body { font-family: sans-serif; font-size: 13px; color: #222; background: #eee; margin: 0; }
        a { color: #0645ad; text-decoration: underline; }
        a:visited { color: #551a8b; }
        .page { width: 760px; margin: 18px auto; background: #fff; border: 1px solid #aaa; }
        .header { padding: 12px 16px; background: #e5e5e5; border-bottom: 1px solid #aaa; }
        .header h2 { margin: 0 0 6px; font-size: 18px; }
        .nav a { margin-right: 14px; }
        .content { padding: 14px 16px 18px; }
        .panel { border: 1px solid #bbb; margin: 0 0 14px; padding: 10px; }
        table { width: 100%; border-collapse: collapse; }
        th, td { text-align: left; padding: 6px 7px; border: 1px solid #bbb; vertical-align: top; }
        th { background: #eee; font-size: 12px; }
        label { display: block; font-weight: bold; margin-bottom: 4px; }
        textarea { width: 98%; height: 58px; font-family: sans-serif; font-size: 13px; }
        button { margin-top: 7px; font-size: 12px; padding: 2px 10px; }
        .muted { color: #666; font-size: 12px; }
        .closed { color: #666; }
    </style>;
}

private Html frame(Html content) {
    return <div class="page">
        {stylesheet()}
        <header class="header">
            <h2>Aster Issue Tracker</h2>
            <nav class="nav">
                <a href=Url.relative("/issues")>Issues</a>
                <a href=Url.relative("/issues/new")>New issue</a>
            </nav>
        </header>
        <main class="content">
            {content}
        </main>
    </div>;
}

public Html ErrorPage(string message) {
    return frame(<section>
        <h2>Application error</h2>
        <p>{message}</p>
    </section>);
}

public Html MissingPage(string path) {
    return frame(<section>
        <h2>Not found</h2>
        <p>{path}</p>
        <p><a href=Url.relative("/issues")>Return to issues</a></p>
    </section>);
}

public Html NewIssuePage() {
    return frame(<section>
        <h2>New issue</h2>
        <form
            action=Url.relative("/issues")
            method="post"
            class="panel"
        >
            <label>Title</label>
            <textarea name="title"></textarea>
            <div><button>Create issue</button></div>
        </form>
        <p class="muted">Keep the title short and specific.</p>
    </section>);
}

public Html IssueListPage(List<Issue> issues) {
    var page = <section>
        <h2>Issues</h2>
        <p><a href=Url.relative("/issues/new")>Create a new issue</a></p>
        <table class="issues">
            <thead>
                <tr>
                    <th>ID</th>
                    <th>Title</th>
                    <th>Status</th>
                </tr>
            </thead>
            <tbody>
                foreach (Issue issue in issues) {
                    var destination = IssueUrl(issue.id);
                    <tr>
                        <td>{issue.id}</td>
                        <td>
                            <a href=destination>
                                {issue.title}
                            </a>
                        </td>
                        <td>
                            if (issue.closed) {
                                <span class="closed">closed</span>
                            } else {
                                <>open</>
                            }
                        </td>
                    </tr>
                }
            </tbody>
        </table>
    </section>;
    return frame(page);
}

public Html IssueDetailPage(Issue issue) {
    var action = CloseUrl(issue.id);
    return frame(<section>
        <h2>{issue.title}</h2>
        <div class="panel">
            <p><strong>Issue: </strong>{issue.id}</p>
            <p>
                <strong>Status: </strong>
                if (issue.closed) {
                    <>closed</>
                } else {
                    <>open</>
                }
            </p>
        </div>
        if (!issue.closed) {
            <form action=action method="post" class="panel">
                <button>Close issue</button>
            </form>
        }
        <p><a href=Url.relative("/issues")>Back to issues</a></p>
    </section>);
}
