# SQLite

`Aster.Data.Sqlite` is a direct, application-oriented wrapper around the real
SQLite C library. It is not ADO.NET, a provider-neutral database abstraction,
or an ORM. SQL remains SQLite SQL and the public concepts remain databases,
prepared statements, rows, transactions, parameters, and columns.

SQLite is linked optionally and exposed through registered native calls shared
by the VM and generated-C backend. `Database`, `Statement`, and `Transaction`
are distinct Aster types over cleanup-managed opaque handles. Database and
statement handles call `sqlite3_close_v2` and `sqlite3_finalize`. An active
transaction rolls back when its last transaction handle leaves scope.

## Commands and parameters

```aster
Database database = Database.Open("site.db");

Statement insert = database.Prepare(
    "INSERT INTO Posts (Title, Published) VALUES (@title, @published)"
);

insert.Bind("@title", post.Title);
insert.Bind("@published", post.Published);
long changed = insert.Execute();
```

`Bind` supports positional or SQLite-named parameters and overloads for
integers, Booleans, floating-point values, strings, and byte lists.
`BindNull` is explicit. The adapter delegates named-parameter resolution to
`sqlite3_bind_parameter_index`; it does not parse SQL.

Statements can be reused with `Reset` and `ClearBindings`.

## Queries and mapping

```aster
private Post ReadPost(Row row)
{
    return new()
    {
        Id = row.GetInt64("Id"),
        Title = row.GetString("Title"),
        Published = row.GetBoolean("Published")
    };
}

Statement query = database.Prepare(
    "SELECT Id, Title, Published FROM Posts ORDER BY Id"
);

List<Post> posts = new();
while (query.Read())
{
    posts.Add(ReadPost(query.CurrentRow()));
}
```

Row mapping is ordinary Aster code. There is no reflection, field annotation,
change tracker, or generated entity. Column getters accept either a zero-based
index or an exact result-column name. Index access avoids name lookup when it
matters. Nullable getters distinguish SQL `NULL` from a value.

Supported result values include 64- and 32-bit integers, Booleans, doubles,
UTF-8 strings, nulls, and copied blobs. `ColumnType` exposes SQLite's runtime
storage class where dynamic inspection is genuinely needed.

## Transactions

```aster
{
    Transaction transaction = database.BeginTransaction(
        TransactionMode.Immediate
    );

    SavePost(database, post);
    SaveTags(database, post.Tags);

    transaction.Commit();
}
```

Leaving the scope without `Commit` or `Rollback` rolls back. Nested calls use
SQLite savepoints, so reusable repository functions can establish a local
transaction without breaking an existing outer transaction. Modes map to
SQLite's deferred, immediate, and exclusive transaction modes.

## Migrations

Migrations are ordered ordinary `.sql` files:

```text
Migrations/
    001_CreatePosts.sql
    002_AddPostSlug.sql
```

```aster
database.Migrate("Migrations");
```

The runner sorts filenames ordinally, creates `AsterMigrations`, applies each
new file transactionally, and records its complete SQL text. Changing a
migration that has already been applied is rejected. There is no parallel
schema DSL and no automatic model inspection.

## Repositories

A repository is an ordinary Aster struct containing a `Database` and ordinary
methods containing SQL. It does not inherit from framework infrastructure:

```aster
struct PostRepository
{
    Database Database;
}

public Post? PostRepository.Find(PostRepository self, long id)
{
    Statement query = self.Database.Prepare(
        "SELECT Id, Title FROM Posts WHERE Id = @id"
    );
    query.Bind("@id", id);
    if (!query.Read()) { return null; }
    return ReadPost(query.CurrentRow());
}
```

The older `SqliteOpen`, `SqlitePrepare`, `SqliteBind*`, and related
`Result<T, SqliteError>` functions remain available when database failure is an
expected value. The main object API throws `SqliteException`, allowing ordinary
application code to propagate failures to its request or process boundary.

Lime may add a substantially higher-level productivity API later. That layer
must build on this package and preserve visible SQL rather than moving ORM or
query-language magic into Aster's core SQLite interface.
