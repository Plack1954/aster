namespace App.Render;

using Issues.Model;
using Issues.Pages;
using Aster.Data.Sqlite;

private int run() {
    Database database = Database.Open(":memory:");
    IssueRepository issues = IssueRepository.Create(database);
    issues.EnsureSchema();
    long first = issues.CreateIssue(
        "Search results lose their sort order"
    );
    long second = issues.CreateIssue(
        "Add an export command"
    );
    List<Issue> all = issues.ListIssues();
    Html page = IssueListPage(all);
    Console.WriteLine(page.ToHtmlString());
    return 0;
}

int main() {
    return run();
}
