namespace Aster.Web.Sessions;

using Aster.Web;
using Aster.Data.Sqlite;
using System.Security.Cryptography;

public struct SessionOptions
{
    string cookieName;
    long idleTimeoutSeconds;
    long cleanupInterval;
    CookieOptions cookie;
}

public struct SessionStore
{
    Database database;
    SessionOptions options;
    long opensSinceSweep;
}

public struct Session
{
    Database database;
    SessionOptions options;
    string id;
    bool isNew;
    bool cookieDirty;
    bool destroyed;
}

public SessionOptions SessionOptions()
{
    CookieOptions cookie = CookieOptions();
    return new()
    {
        cookieName = "aster.session",
        idleTimeoutSeconds = 1200,
        cleanupInterval = 256,
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
    if (options.cleanupInterval <= 0)
    {
        throw new ArgumentException(
            "Session cleanup interval must be positive."
        );
    }
    Database database = Database.Open(path);
    database.Execute("PRAGMA foreign_keys = ON");
    database.Execute(
        "CREATE TABLE IF NOT EXISTS AsterWebSessions (Id TEXT PRIMARY KEY, ExpiresAt INTEGER NOT NULL)"
    );
    database.Execute(
        "CREATE TABLE IF NOT EXISTS AsterWebSessionValues (SessionId TEXT NOT NULL, Key TEXT NOT NULL, Value TEXT NOT NULL, PRIMARY KEY (SessionId, Key), FOREIGN KEY (SessionId) REFERENCES AsterWebSessions(Id) ON DELETE CASCADE)"
    );
    database.Execute(
        "CREATE INDEX IF NOT EXISTS IX_AsterWebSessions_ExpiresAt ON AsterWebSessions(ExpiresAt)"
    );
    return new()
    {
        database = database,
        options = options,
        opensSinceSweep = 0
    };
}

private string CreateSessionId()
{
    return RandomNumberGenerator.GetHexString(32);
}

private bool SessionExists(Database database, string id)
{
    Statement query = database.Prepare(
        "SELECT 1 FROM AsterWebSessions WHERE Id = @id AND ExpiresAt > unixepoch()"
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
        "UPDATE AsterWebSessions SET ExpiresAt = unixepoch() + @timeout WHERE Id = @id"
    );
    update.Bind("@timeout", idleTimeoutSeconds);
    update.Bind("@id", id);
    update.Execute();
}

public Session SessionStore.Open(
    ref SessionStore self,
    Request request
)
{
    self.opensSinceSweep += 1;
    if (self.opensSinceSweep >= self.options.cleanupInterval)
    {
        self.SweepExpired();
    }
    string id = "";
    bool isNew = true;
    switch (request.Cookie(self.options.cookieName))
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
        id = CreateSessionId();
        Statement insert = self.database.Prepare(
            "INSERT INTO AsterWebSessions (Id, ExpiresAt) VALUES (@id, unixepoch() + @timeout)"
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
        isNew = isNew,
        cookieDirty = isNew,
        destroyed = false
    };
}

public long SessionStore.SweepExpired(ref SessionStore self)
{
    self.database.Execute(
        "DELETE FROM AsterWebSessions WHERE ExpiresAt <= unixepoch()"
    );
    self.opensSinceSweep = 0;
    return self.database.Changes();
}

public string Session.Id(Session self) { return self.id; }
public bool Session.IsNew(Session self) { return self.isNew; }

private void EnsureSessionActive(Session self)
{
    if (self.destroyed)
    {
        throw new InvalidOperationException("The session was destroyed.");
    }
}

public Option<string> Session.GetString(Session self, string key)
{
    EnsureSessionActive(self);
    Statement query = self.database.Prepare(
        "SELECT Value FROM AsterWebSessionValues WHERE SessionId = @session AND Key = @key"
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
    EnsureSessionActive(self);
    if (key.Length == 0)
    {
        throw new ArgumentException("Session key cannot be empty.");
    }
    Statement command = self.database.Prepare(
        "INSERT INTO AsterWebSessionValues (SessionId, Key, Value) VALUES (@session, @key, @value) ON CONFLICT(SessionId, Key) DO UPDATE SET Value = excluded.Value"
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
    EnsureSessionActive(self);
    Statement command = self.database.Prepare(
        "DELETE FROM AsterWebSessionValues WHERE SessionId = @session AND Key = @key"
    );
    command.Bind("@session", self.id);
    command.Bind("@key", key);
    command.Execute();
}

public void Session.Clear(Session self)
{
    EnsureSessionActive(self);
    Statement command = self.database.Prepare(
        "DELETE FROM AsterWebSessionValues WHERE SessionId = @session"
    );
    command.Bind("@session", self.id);
    command.Execute();
}

public void Session.Rotate(ref Session self)
{
    EnsureSessionActive(self);
    string previous = self.id;
    string replacement = CreateSessionId();
    Transaction transaction = self.database.BeginTransaction(
        TransactionMode.Immediate
    );
    Statement insert = self.database.Prepare(
        "INSERT INTO AsterWebSessions (Id, ExpiresAt) VALUES (@id, unixepoch() + @timeout)"
    );
    insert.Bind("@id", replacement);
    insert.Bind("@timeout", self.options.idleTimeoutSeconds);
    insert.Execute();
    Statement moveValues = self.database.Prepare(
        "UPDATE AsterWebSessionValues SET SessionId = @replacement WHERE SessionId = @previous"
    );
    moveValues.Bind("@replacement", replacement);
    moveValues.Bind("@previous", previous);
    moveValues.Execute();
    Statement removePrevious = self.database.Prepare(
        "DELETE FROM AsterWebSessions WHERE Id = @previous"
    );
    removePrevious.Bind("@previous", previous);
    removePrevious.Execute();
    transaction.Commit();
    self.id = replacement;
    self.isNew = false;
    self.cookieDirty = true;
}

public void Session.Destroy(ref Session self)
{
    if (self.destroyed) { return; }
    Statement command = self.database.Prepare(
        "DELETE FROM AsterWebSessions WHERE Id = @session"
    );
    command.Bind("@session", self.id);
    command.Execute();
    self.destroyed = true;
    self.isNew = false;
    self.cookieDirty = true;
}

public void Session.Commit(Session self, ref Response response)
{
    if (!self.cookieDirty) { return; }
    CookieOptions cookie = self.options.cookie;
    if (self.destroyed)
    {
        switch (ResponseDeleteCookie(self.options.cookieName, cookie))
        {
            case Result.Ok(header): { response.AddHeader(header); }
            case Result.Err(error): {
                throw new InvalidOperationException(error);
            }
        }
        return;
    }
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
