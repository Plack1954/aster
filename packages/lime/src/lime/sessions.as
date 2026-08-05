namespace Lime.Sessions;

using Lime;
using Aster.Data.Sqlite;

public struct SessionOptions
{
    string cookieName;
    long idleTimeoutSeconds;
    CookieOptions cookie;
}

public struct SessionStore
{
    Database database;
    SessionOptions options;
}

public struct Session
{
    Database database;
    SessionOptions options;
    string id;
    bool isNew;
}

public SessionOptions SessionOptions()
{
    CookieOptions cookie = CookieOptions();
    return new()
    {
        cookieName = "lime.session",
        idleTimeoutSeconds = 1200,
        cookie = cookie
    };
}

public SessionStore SessionStore.Create()
{
    return SessionStore.Create(":memory:", SessionOptions());
}

public SessionStore SessionStore.Create(string path)
{
    return SessionStore.Create(path, SessionOptions());
}

public SessionStore SessionStore.Create(
    string path,
    SessionOptions options
)
{
    if (options.cookieName.Length == 0)
    {
        throw new ArgumentException("Session cookie name cannot be empty.");
    }
    if (options.idleTimeoutSeconds <= 0)
    {
        throw new ArgumentException(
            "Session idle timeout must be positive."
        );
    }
    Database database = Database.Open(path);
    database.Execute("PRAGMA foreign_keys = ON");
    database.Execute(
        "CREATE TABLE IF NOT EXISTS LimeSessions (Id TEXT PRIMARY KEY, ExpiresAt INTEGER NOT NULL)"
    );
    database.Execute(
        "CREATE TABLE IF NOT EXISTS LimeSessionValues (SessionId TEXT NOT NULL, Key TEXT NOT NULL, Value TEXT NOT NULL, PRIMARY KEY (SessionId, Key), FOREIGN KEY (SessionId) REFERENCES LimeSessions(Id) ON DELETE CASCADE)"
    );
    database.Execute(
        "CREATE INDEX IF NOT EXISTS IX_LimeSessions_ExpiresAt ON LimeSessions(ExpiresAt)"
    );
    return new() { database = database, options = options };
}

private string CreateSessionId(Database database)
{
    Statement random = database.Prepare(
        "SELECT lower(hex(randomblob(32)))"
    );
    if (!random.Read())
    {
        throw new InvalidOperationException(
            "SQLite did not generate a session identifier."
        );
    }
    return random.CurrentRow().GetString(0);
}

private bool SessionExists(Database database, string id)
{
    Statement query = database.Prepare(
        "SELECT 1 FROM LimeSessions WHERE Id = @id AND ExpiresAt > unixepoch()"
    );
    query.Bind("@id", id);
    return query.Read();
}

private void RefreshSession(
    Database database,
    string id,
    long idleTimeoutSeconds
)
{
    Statement update = database.Prepare(
        "UPDATE LimeSessions SET ExpiresAt = unixepoch() + @timeout WHERE Id = @id"
    );
    update.Bind("@timeout", idleTimeoutSeconds);
    update.Bind("@id", id);
    update.Execute();
}

public Session SessionStore.Open(
    SessionStore self,
    Request request
)
{
    self.database.Execute(
        "DELETE FROM LimeSessions WHERE ExpiresAt <= unixepoch()"
    );
    string id = "";
    bool isNew = true;
    switch (request.cookie(self.options.cookieName))
    {
        case Option.Some(candidate): {
            if (candidate.Length == 64 &&
                SessionExists(self.database, candidate))
            {
                id = candidate;
                isNew = false;
            }
        }
        case Option.None: {}
    }
    if (isNew)
    {
        id = CreateSessionId(self.database);
        Statement insert = self.database.Prepare(
            "INSERT INTO LimeSessions (Id, ExpiresAt) VALUES (@id, unixepoch() + @timeout)"
        );
        insert.Bind("@id", id);
        insert.Bind("@timeout", self.options.idleTimeoutSeconds);
        insert.Execute();
    }
    else
    {
        RefreshSession(
            self.database, id, self.options.idleTimeoutSeconds
        );
    }
    return new()
    {
        database = self.database,
        options = self.options,
        id = id,
        isNew = isNew
    };
}

public string Session.Id(Session self) { return self.id; }
public bool Session.IsNew(Session self) { return self.isNew; }

public Option<string> Session.GetString(Session self, string key)
{
    Statement query = self.database.Prepare(
        "SELECT Value FROM LimeSessionValues WHERE SessionId = @session AND Key = @key"
    );
    query.Bind("@session", self.id);
    query.Bind("@key", key);
    if (query.Read())
    {
        return Option.Some(query.CurrentRow().GetString(0));
    }
    return Option.None;
}

public void Session.SetString(
    Session self,
    string key,
    string value
)
{
    if (key.Length == 0)
    {
        throw new ArgumentException("Session key cannot be empty.");
    }
    Statement command = self.database.Prepare(
        "INSERT INTO LimeSessionValues (SessionId, Key, Value) VALUES (@session, @key, @value) ON CONFLICT(SessionId, Key) DO UPDATE SET Value = excluded.Value"
    );
    command.Bind("@session", self.id);
    command.Bind("@key", key);
    command.Bind("@value", value);
    command.Execute();
    RefreshSession(
        self.database, self.id, self.options.idleTimeoutSeconds
    );
}

public void Session.Remove(Session self, string key)
{
    Statement command = self.database.Prepare(
        "DELETE FROM LimeSessionValues WHERE SessionId = @session AND Key = @key"
    );
    command.Bind("@session", self.id);
    command.Bind("@key", key);
    command.Execute();
}

public void Session.Clear(Session self)
{
    Statement command = self.database.Prepare(
        "DELETE FROM LimeSessionValues WHERE SessionId = @session"
    );
    command.Bind("@session", self.id);
    command.Execute();
}

public void Session.Commit(Session self, ref Response response)
{
    if (!self.isNew) { return; }
    CookieOptions cookie = self.options.cookie;
    Option<long> maxAge = Option.Some(self.options.idleTimeoutSeconds);
    cookie.maxAge = maxAge;
    switch (ResponseCookieWith(
        self.options.cookieName, self.id, cookie
    ))
    {
        case Result.Ok(header): { response.AddHeader(header); }
        case Result.Err(error): { throw new InvalidOperationException(error); }
    }
}
