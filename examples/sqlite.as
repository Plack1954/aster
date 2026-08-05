using Aster.Data.Sqlite;
using System.Text;

private Result<int, SqliteError> run() {
    Database database = try SqliteOpen(":memory:");
    try SqliteExecute(
        database,
        "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL)"
    );

    Statement insert = try SqlitePrepare(
        database,
        "INSERT INTO users (id, name) VALUES (?1, ?2)"
    );
    try SqliteBindI64(insert, 1, 42);
    try SqliteBindText(insert, 2, "Ada");
    Step inserted = try SqliteStep(insert);
    switch (inserted) {
        case Step.Done: {}
        case Step.Row: {
            return Result.Err(
                "insert unexpectedly returned a row"
            );
        }
    }

    Statement query = try SqlitePrepare(
        database,
        "SELECT id, name FROM users WHERE id = ?1"
    );
    try SqliteBindI64(query, 1, 42);
    Step stepped = try SqliteStep(query);
    switch (stepped) {
        case Step.Row: {
            long id = try SqliteColumnI64(query, 0);
            string name = try SqliteColumnText(query, 1);
            Console.WriteLine(id);
            Console.WriteLine(name);
        }
        case Step.Done: {
            return Result.Err("user was not found");
        }
    }
    return Result.Ok(0);
}

int main() {
    Result<int, SqliteError> result = run();
    switch (result) {
        case Result.Ok(status): {
            return status;
        }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
    return 1;
}
