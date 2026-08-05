namespace Issues.Model;

using Aster.Data.Sqlite;

public struct Issue
{
    long id;
    string title;
    bool closed;
}

public struct IssueRepository
{
    Database Database;
}

private Issue ReadIssue(Row row)
{
    return new()
    {
        id = row.GetInt64("id"),
        title = row.GetString("title"),
        closed = row.GetBoolean("closed")
    };
}

public IssueRepository IssueRepository.Create(Database database)
{
    return new() { Database = database };
}

public void IssueRepository.EnsureSchema(IssueRepository self)
{
    self.Database.Execute(
        "CREATE TABLE IF NOT EXISTS issues (id INTEGER PRIMARY KEY, title TEXT NOT NULL, closed INTEGER NOT NULL DEFAULT 0)"
    );
}

public long IssueRepository.CreateIssue(
    IssueRepository self,
    string title
)
{
    Statement insert = self.Database.Prepare(
        "INSERT INTO issues (title, closed) VALUES (@title, 0)"
    );
    insert.Bind("@title", title);
    insert.Execute();
    return self.Database.LastInsertRowId();
}

public bool IssueRepository.CloseIssue(
    IssueRepository self,
    string id
)
{
    Statement update = self.Database.Prepare(
        "UPDATE issues SET closed = 1 WHERE id = @id"
    );
    update.Bind("@id", id);
    return update.Execute() > 0;
}

public List<Issue> IssueRepository.ListIssues(IssueRepository self)
{
    self.EnsureSchema();
    Statement query = self.Database.Prepare(
        "SELECT id, title, closed FROM issues ORDER BY id DESC"
    );
    List<Issue> issues = new();
    while (query.Read())
    {
        issues.Add(ReadIssue(query.CurrentRow()));
    }
    return issues;
}

public Issue? IssueRepository.FindIssue(
    IssueRepository self,
    string id
)
{
    self.EnsureSchema();
    Statement query = self.Database.Prepare(
        "SELECT id, title, closed FROM issues WHERE id = @id"
    );
    query.Bind("@id", id);
    if (!query.Read()) { return null; }
    return ReadIssue(query.CurrentRow());
}

public long IssueRepository.CountIssues(IssueRepository self)
{
    Statement query = self.Database.Prepare(
        "SELECT COUNT(*) FROM issues"
    );
    if (!query.Read())
    {
        throw new SqliteException("count query returned no row");
    }
    return query.CurrentRow().GetInt64(0);
}

// Temporary compatibility boundary for the older Result-shaped issue-tracker
// routes. New code uses IssueRepository directly and lets SqliteException
// propagate to the application boundary.
public Result<Unit, SqliteError> EnsureSchema(Database database)
{
    try
    {
        Unit completed = IssueRepository.Create(database).EnsureSchema();
        return Result.Ok(completed);
    }
    catch (SqliteException error) { return Result.Err(error.Message); }
}

public Result<long, SqliteError> CreateIssue(
    Database database,
    string title
)
{
    try
    {
        return Result.Ok(
            IssueRepository.Create(database).CreateIssue(title)
        );
    }
    catch (SqliteException error) { return Result.Err(error.Message); }
}

public Result<bool, SqliteError> CloseIssue(
    Database database,
    string id
)
{
    try
    {
        return Result.Ok(
            IssueRepository.Create(database).CloseIssue(id)
        );
    }
    catch (SqliteException error) { return Result.Err(error.Message); }
}

public Result<List<Issue>, SqliteError> ListIssues(Database database)
{
    try
    {
        return Result.Ok(
            IssueRepository.Create(database).ListIssues()
        );
    }
    catch (SqliteException error) { return Result.Err(error.Message); }
}

public Result<Option<Issue>, SqliteError> FindIssue(
    Database database,
    string id
)
{
    try
    {
        Issue? found = IssueRepository.Create(database).FindIssue(id);
        switch (found)
        {
            case Option.Some(issue):
            {
                return Result.Ok(Option.Some(issue));
            }
            case Option.None: { return Result.Ok(Option.None); }
        }
    }
    catch (SqliteException error) { return Result.Err(error.Message); }
}

public Result<long, SqliteError> CountIssues(Database database)
{
    try
    {
        return Result.Ok(
            IssueRepository.Create(database).CountIssues()
        );
    }
    catch (SqliteException error) { return Result.Err(error.Message); }
}
