using Aster.Data.Sqlite;
using System.IO;

struct Entry
{
    long Id;
    string Name;
    bool Active;
    double Score;
    string? Note;
    List<byte> Payload;
}

private Entry ReadEntry(Row row)
{
    return new()
    {
        Id = row.GetInt64("Id"),
        Name = row.GetString("Name"),
        Active = row.GetBoolean("Active"),
        Score = row.GetDouble("Score"),
        Note = row.GetNullableString("Note"),
        Payload = row.GetBytes("Payload")
    };
}

private long CountEntries(Database database)
{
    Statement count = database.Prepare("SELECT COUNT(*) FROM Entries");
    if (!count.Read()) { return -1; }
    return count.CurrentRow().GetInt64(0);
}

private void InsertSimple(Database database, string name)
{
    Statement insert = database.Prepare(
        "INSERT INTO Entries (Name, Active, Score, Note, Payload) VALUES (@name, 1, 1.0, NULL, X'')"
    );
    insert.Bind("@name", name);
    insert.Execute();
}

int main()
{
    Database database = Database.Open(":memory:");
    database.Execute(
        "CREATE TABLE Entries (Id INTEGER PRIMARY KEY, Name TEXT NOT NULL, Active INTEGER NOT NULL, Score REAL NOT NULL, Note TEXT NULL, Payload BLOB NOT NULL)"
    );

    List<byte> payload = new();
    payload.Add(0);
    payload.Add(127);
    payload.Add(255);
    Statement insert = database.Prepare(
        "INSERT INTO Entries (Name, Active, Score, Note, Payload) VALUES (@name, @active, @score, @note, @payload)"
    );
    insert.Bind("@name", "Aster");
    insert.Bind("@active", true);
    insert.Bind("@score", 9.5);
    insert.BindNull("@note");
    insert.Bind("@payload", payload);
    if (insert.Execute() != 1 || database.LastInsertRowId() != 1)
        { return 1; }

    Statement query = database.Prepare(
        "SELECT Id, Name, Active, Score, Note, Payload FROM Entries WHERE Id = @id"
    );
    query.Bind("@id", 1);
    if (!query.Read()) { return 2; }
    Entry entry = ReadEntry(query.CurrentRow());
    if (entry.Id != 1 || entry.Name != "Aster" || !entry.Active ||
        entry.Score != 9.5 || entry.Note != null ||
        entry.Payload.Count != 3 || entry.Payload[2] != 255)
        { return 3; }

    query.Reset();
    query.ClearBindings();
    query.Bind("@id", 99);
    if (query.Read()) { return 4; }

    {
        Transaction rollback = database.BeginTransaction(
            TransactionMode.Immediate
        );
        InsertSimple(database, "Rolled back");
    }
    if (CountEntries(database) != 1) { return 5; }

    {
        Transaction committed = database.BeginTransaction();
        InsertSimple(database, "Committed");
        committed.Commit();
    }
    if (CountEntries(database) != 2) { return 6; }

    {
        Transaction outer = database.BeginTransaction();
        InsertSimple(database, "Outer");
        {
            Transaction inner = database.BeginTransaction();
            InsertSimple(database, "Inner rollback");
        }
        if (CountEntries(database) != 3) { return 7; }
        outer.Commit();
    }
    if (CountEntries(database) != 3) { return 8; }

    bool typedError = false;
    try
    {
        Statement bad = database.Prepare("SELECT @known");
        bad.Bind("@missing", 1);
    }
    catch (SqliteException error)
    {
        typedError = error.Message.Contains("parameter");
    }
    if (!typedError) { return 9; }

    string migrationDirectory = "sqlite_application_migrations";
    Directory.CreateDirectory(migrationDirectory);
    File.WriteAllText(
        Path.Combine(migrationDirectory, "001_CreateNotes.sql"),
        "CREATE TABLE Notes (Id INTEGER PRIMARY KEY, Title TEXT NOT NULL);"
    );
    File.WriteAllText(
        Path.Combine(migrationDirectory, "002_AddPublished.sql"),
        "ALTER TABLE Notes ADD COLUMN Published INTEGER NOT NULL DEFAULT 0;"
    );
    Database migrated = Database.Open(":memory:");
    migrated.Migrate(migrationDirectory);
    migrated.Migrate(migrationDirectory);
    Statement columns = migrated.Prepare(
        "SELECT COUNT(*) FROM pragma_table_info('Notes')"
    );
    if (!columns.Read() || columns.CurrentRow().GetInt64(0) != 3)
        { return 10; }

    File.WriteAllText(
        Path.Combine(migrationDirectory, "001_CreateNotes.sql"),
        "SELECT 1;"
    );
    bool changedMigration = false;
    try
    {
        migrated.Migrate(migrationDirectory);
    }
    catch (SqliteException error)
    {
        changedMigration = error.Message.Contains("has changed");
    }
    if (!changedMigration) { return 11; }

    return 0;
}
