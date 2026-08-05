#include "lang/lang.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(LANG_HAVE_SQLITE3)
#include <sqlite3.h>
#endif

typedef enum SqliteHandleKind {
    SQLITE_HANDLE_DATABASE = 1,
    SQLITE_HANDLE_STATEMENT = 2,
    SQLITE_HANDLE_TRANSACTION = 3
} SqliteHandleKind;

typedef struct SqliteHandle {
    uint32_t magic;
    SqliteHandleKind kind;
#if defined(LANG_HAVE_SQLITE3)
    union {
        sqlite3 *database;
        sqlite3_stmt *statement;
        struct {
            sqlite3 *database;
            bool active;
            bool nested;
            char savepoint[64];
        } transaction;
    } as;
#endif
} SqliteHandle;

#define SQLITE_HANDLE_MAGIC UINT32_C(0x4f53514c)

static LangNativeResult native_failure(const char *message) {
    return lang_native_result_error(message);
}

static LangNativeResult result_error(LangVM *vm, const char *message) {
    LangValue string;
    LangStringView view = {
        message != NULL ? message : "SQLite error",
        message != NULL ? strlen(message) : strlen("SQLite error")
    };
    if (!lang_string_value(vm, view, &string))
        return native_failure("could not allocate SQLite error");
    LangValue result;
    if (!lang_result_err_value(vm, string, &result)) {
        lang_value_drop(vm, &string);
        return native_failure("could not construct SQLite error Result");
    }
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult result_value(LangVM *vm, LangValue value) {
    LangValue result;
    if (!lang_result_ok_value(vm, value, &result)) {
        lang_value_drop(vm, &value);
        return native_failure("could not construct SQLite Result");
    }
    return (LangNativeResult){true, result, NULL};
}

#if defined(LANG_HAVE_SQLITE3)

static void sqlite_handle_drop(void *opaque) {
    SqliteHandle *handle = opaque;
    if (handle == NULL || handle->magic != SQLITE_HANDLE_MAGIC) return;
    if (handle->kind == SQLITE_HANDLE_DATABASE)
        (void)sqlite3_close_v2(handle->as.database);
    else if (handle->kind == SQLITE_HANDLE_STATEMENT)
        (void)sqlite3_finalize(handle->as.statement);
    else if (handle->kind == SQLITE_HANDLE_TRANSACTION &&
             handle->as.transaction.active) {
        char sql[192];
        if (handle->as.transaction.nested)
            (void)snprintf(
                sql, sizeof(sql),
                "ROLLBACK TO SAVEPOINT %s; RELEASE SAVEPOINT %s",
                handle->as.transaction.savepoint,
                handle->as.transaction.savepoint);
        else
            (void)snprintf(sql, sizeof(sql), "ROLLBACK");
        (void)sqlite3_exec(
            handle->as.transaction.database, sql, NULL, NULL, NULL);
        handle->as.transaction.active = false;
    }
    handle->magic = 0U;
    free(handle);
}

static SqliteHandle *require_handle(
    const LangValue *value, SqliteHandleKind kind) {
    SqliteHandle *handle = lang_native_handle_data(value);
    if (handle == NULL || handle->magic != SQLITE_HANDLE_MAGIC ||
        handle->kind != kind)
        return NULL;
    return handle;
}

static bool integer_arg(const LangValue *value, int64_t *output) {
    if (value->tag == LANG_VALUE_I64) {
        *output = value->as.i64;
        return true;
    }
    if (value->tag == LANG_VALUE_U64 &&
        value->as.u64 <= (uint64_t)INT64_MAX) {
        *output = (int64_t)value->as.u64;
        return true;
    }
    return false;
}

static LangNativeResult sqlite_open_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (arg_count != 1U ||
        !lang_value_string_view(&args[0], &path))
        return native_failure("sqlite_open expects one path string");
    if (path.length > (size_t)INT_MAX ||
        memchr(path.data, '\0', path.length) != NULL)
        return result_error(vm, "invalid SQLite path");
    char *path_c = malloc(path.length + 1U);
    if (path_c == NULL)
        return native_failure("out of memory opening SQLite database");
    memcpy(path_c, path.data, path.length);
    path_c[path.length] = '\0';
    sqlite3 *database = NULL;
    int status = sqlite3_open_v2(
        path_c, &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    free(path_c);
    if (status != SQLITE_OK) {
        const char *message =
            database != NULL ? sqlite3_errmsg(database)
                             : "could not open SQLite database";
        LangNativeResult error = result_error(vm, message);
        if (database != NULL) (void)sqlite3_close(database);
        return error;
    }
    SqliteHandle *handle = calloc(1U, sizeof(*handle));
    if (handle == NULL) {
        (void)sqlite3_close(database);
        return native_failure("out of memory wrapping SQLite database");
    }
    handle->magic = SQLITE_HANDLE_MAGIC;
    handle->kind = SQLITE_HANDLE_DATABASE;
    handle->as.database = database;
    LangValue value;
    if (!lang_native_handle_value(
            vm, handle, sqlite_handle_drop, &value)) {
        sqlite_handle_drop(handle);
        return native_failure("could not wrap SQLite database");
    }
    return result_value(vm, value);
}

static LangNativeResult sqlite_execute_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *database =
        arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_DATABASE) : NULL;
    LangStringView sql;
    if (arg_count != 2U || database == NULL ||
        !lang_value_string_view(&args[1], &sql))
        return native_failure("sqlite_execute expects `(Database, string)`");
    if (sql.length > (size_t)INT_MAX)
        return result_error(vm, "SQLite statement is too large");
    if (memchr(sql.data, '\0', sql.length) != NULL)
        return result_error(vm, "SQLite statement contains a null byte");
    char *sql_c = malloc(sql.length + 1U);
    if (sql_c == NULL)
        return native_failure("out of memory executing SQLite statement");
    memcpy(sql_c, sql.data, sql.length);
    sql_c[sql.length] = '\0';
    char *message = NULL;
    int status = sqlite3_exec(
        database->as.database, sql_c, NULL, NULL, &message);
    free(sql_c);
    if (status != SQLITE_OK) {
        LangNativeResult error = result_error(
            vm, message != NULL ? message
                                : sqlite3_errmsg(database->as.database));
        sqlite3_free(message);
        return error;
    }
    return result_value(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult sqlite_prepare_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *database =
        arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_DATABASE) : NULL;
    LangStringView sql;
    if (arg_count != 2U || database == NULL ||
        !lang_value_string_view(&args[1], &sql))
        return native_failure("sqlite_prepare expects `(Database, string)`");
    if (sql.length > (size_t)INT_MAX)
        return result_error(vm, "SQLite statement is too large");
    sqlite3_stmt *statement = NULL;
    int status = sqlite3_prepare_v2(
        database->as.database, sql.data, (int)sql.length,
        &statement, NULL);
    if (status != SQLITE_OK)
        return result_error(vm, sqlite3_errmsg(database->as.database));
    SqliteHandle *handle = calloc(1U, sizeof(*handle));
    if (handle == NULL) {
        (void)sqlite3_finalize(statement);
        return native_failure("out of memory wrapping SQLite statement");
    }
    handle->magic = SQLITE_HANDLE_MAGIC;
    handle->kind = SQLITE_HANDLE_STATEMENT;
    handle->as.statement = statement;
    LangValue value;
    if (!lang_native_handle_value(
            vm, handle, sqlite_handle_drop, &value)) {
        sqlite_handle_drop(handle);
        return native_failure("could not wrap SQLite statement");
    }
    return result_value(vm, value);
}

static LangNativeResult sqlite_bind_i64_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement =
        arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    int64_t value;
    if (arg_count != 3U || statement == NULL ||
        !integer_arg(&args[1], &index) ||
        !integer_arg(&args[2], &value) ||
        index < 1 || index > INT_MAX)
        return native_failure(
            "sqlite_bind_i64 expects `(Statement, positive i64, i64)`");
    int status = sqlite3_bind_int64(
        statement->as.statement, (int)index, value);
    if (status != SQLITE_OK)
        return result_error(
            vm, sqlite3_errmsg(
                sqlite3_db_handle(statement->as.statement)));
    return result_value(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult sqlite_bind_text_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement =
        arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    LangStringView value;
    if (arg_count != 3U || statement == NULL ||
        !integer_arg(&args[1], &index) ||
        index < 1 || index > INT_MAX ||
        !lang_value_string_view(&args[2], &value))
        return native_failure(
            "sqlite_bind_text expects `(Statement, positive i64, string)`");
    if (value.length > (size_t)INT_MAX)
        return result_error(vm, "SQLite text parameter is too large");
    int status = sqlite3_bind_text(
        statement->as.statement, (int)index,
        value.data, (int)value.length, SQLITE_TRANSIENT);
    if (status != SQLITE_OK)
        return result_error(
            vm, sqlite3_errmsg(
                sqlite3_db_handle(statement->as.statement)));
    return result_value(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult sqlite_bind_f64_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    if (arg_count != 3U || statement == NULL ||
        !integer_arg(&args[1], &index) || index < 1 || index > INT_MAX ||
        args[2].tag != LANG_VALUE_F64)
        return native_failure(
            "sqlite_bind_f64 expects `(Statement, positive i64, f64)`");
    int status = sqlite3_bind_double(
        statement->as.statement, (int)index, args[2].as.f64);
    if (status != SQLITE_OK)
        return result_error(vm, sqlite3_errmsg(
            sqlite3_db_handle(statement->as.statement)));
    return result_value(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult sqlite_bind_null_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    if (arg_count != 2U || statement == NULL ||
        !integer_arg(&args[1], &index) || index < 1 || index > INT_MAX)
        return native_failure(
            "sqlite_bind_null expects `(Statement, positive i64)`");
    int status = sqlite3_bind_null(statement->as.statement, (int)index);
    if (status != SQLITE_OK)
        return result_error(vm, sqlite3_errmsg(
            sqlite3_db_handle(statement->as.statement)));
    return result_value(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult sqlite_bind_blob_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    if (arg_count != 3U || statement == NULL ||
        !integer_arg(&args[1], &index) || index < 1 || index > INT_MAX ||
        args[2].tag != LANG_VALUE_BYTE_SLICE ||
        args[2].as.bytes.length > (size_t)INT_MAX)
        return native_failure(
            "sqlite_bind_blob expects `(Statement, positive i64, Slice<byte>)`");
    int status = sqlite3_bind_blob(
        statement->as.statement, (int)index,
        args[2].as.bytes.data, (int)args[2].as.bytes.length,
        SQLITE_TRANSIENT);
    if (status != SQLITE_OK)
        return result_error(vm, sqlite3_errmsg(
            sqlite3_db_handle(statement->as.statement)));
    return result_value(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult sqlite_parameter_index_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    LangStringView name;
    if (arg_count != 2U || statement == NULL ||
        !lang_value_string_view(&args[1], &name) ||
        name.length > (size_t)INT_MAX ||
        memchr(name.data, '\0', name.length) != NULL)
        return native_failure(
            "sqlite_parameter_index expects `(Statement, string)`");
    char *copy = malloc(name.length + 1U);
    if (copy == NULL) return native_failure("out of memory reading parameter");
    memcpy(copy, name.data, name.length);
    copy[name.length] = '\0';
    int index = sqlite3_bind_parameter_index(statement->as.statement, copy);
    free(copy);
    if (index == 0) return result_error(vm, "SQLite parameter was not found");
    return result_value(vm, (LangValue){
        .tag=LANG_VALUE_I64, .as.i64=(int64_t)index
    });
}

static LangNativeResult sqlite_step_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement =
        arg_count == 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    if (statement == NULL)
        return native_failure("sqlite_step expects one Statement");
    int status = sqlite3_step(statement->as.statement);
    if (status == SQLITE_ROW || status == SQLITE_DONE)
        return result_value(vm, (LangValue){
            .tag=LANG_VALUE_BOOL,
            .as.boolean=status == SQLITE_ROW
        });
    return result_error(
        vm, sqlite3_errmsg(
            sqlite3_db_handle(statement->as.statement)));
}

static LangNativeResult sqlite_column_i64_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement =
        arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    if (arg_count != 2U || statement == NULL ||
        !integer_arg(&args[1], &index) ||
        index < 0 || index >= sqlite3_column_count(statement->as.statement))
        return native_failure(
            "sqlite_column_i64 expects a valid Statement column");
    if (sqlite3_column_type(
            statement->as.statement, (int)index) != SQLITE_INTEGER)
        return result_error(vm, "SQLite column is not an integer");
    sqlite3_int64 value =
        sqlite3_column_int64(statement->as.statement, (int)index);
    return result_value(vm, (LangValue){
        .tag=LANG_VALUE_I64, .as.i64=(int64_t)value
    });
}

static LangNativeResult sqlite_column_text_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement =
        arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    if (arg_count != 2U || statement == NULL ||
        !integer_arg(&args[1], &index) ||
        index < 0 || index >= sqlite3_column_count(statement->as.statement))
        return native_failure(
            "sqlite_column_text expects a valid Statement column");
    if (sqlite3_column_type(
            statement->as.statement, (int)index) != SQLITE_TEXT)
        return result_error(vm, "SQLite column is not text");
    const unsigned char *text =
        sqlite3_column_text(statement->as.statement, (int)index);
    int length =
        sqlite3_column_bytes(statement->as.statement, (int)index);
    if (text == NULL)
        return result_error(vm, "could not read SQLite text column");
    LangValue value;
    LangStringView view = {
        text != NULL ? (const char *)text : "",
        length > 0 ? (size_t)length : 0U
    };
    if (!lang_string_value(vm, view, &value))
        return native_failure("could not copy SQLite text column");
    return result_value(vm, value);
}

static LangNativeResult sqlite_column_f64_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    if (arg_count != 2U || statement == NULL ||
        !integer_arg(&args[1], &index) || index < 0 ||
        index >= sqlite3_column_count(statement->as.statement))
        return native_failure(
            "sqlite_column_f64 expects a valid Statement column");
    int type = sqlite3_column_type(statement->as.statement, (int)index);
    if (type != SQLITE_FLOAT && type != SQLITE_INTEGER)
        return result_error(vm, "SQLite column is not numeric");
    return result_value(vm, (LangValue){
        .tag=LANG_VALUE_F64,
        .as.f64=sqlite3_column_double(statement->as.statement, (int)index)
    });
}

static LangNativeResult sqlite_column_type_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    if (arg_count != 2U || statement == NULL ||
        !integer_arg(&args[1], &index) || index < 0 ||
        index >= sqlite3_column_count(statement->as.statement))
        return native_failure(
            "sqlite_column_type expects a valid Statement column");
    return result_value(vm, (LangValue){
        .tag=LANG_VALUE_I64,
        .as.i64=(int64_t)sqlite3_column_type(
            statement->as.statement, (int)index)
    });
}

static LangNativeResult sqlite_column_count_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count == 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    if (statement == NULL)
        return native_failure("sqlite_column_count expects one Statement");
    return result_value(vm, (LangValue){
        .tag=LANG_VALUE_I64,
        .as.i64=(int64_t)sqlite3_column_count(statement->as.statement)
    });
}

static LangNativeResult sqlite_column_name_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    if (arg_count != 2U || statement == NULL ||
        !integer_arg(&args[1], &index) || index < 0 ||
        index >= sqlite3_column_count(statement->as.statement))
        return native_failure(
            "sqlite_column_name expects a valid Statement column");
    const char *name = sqlite3_column_name(statement->as.statement, (int)index);
    if (name == NULL) return result_error(vm, "SQLite column has no name");
    LangValue value;
    if (!lang_string_value(
            vm, (LangStringView){name, strlen(name)}, &value))
        return native_failure("could not copy SQLite column name");
    return result_value(vm, value);
}

static LangNativeResult sqlite_column_blob_length_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    if (arg_count != 2U || statement == NULL ||
        !integer_arg(&args[1], &index) || index < 0 ||
        index >= sqlite3_column_count(statement->as.statement))
        return native_failure(
            "sqlite_column_blob_length expects a valid Statement column");
    if (sqlite3_column_type(statement->as.statement, (int)index) != SQLITE_BLOB)
        return result_error(vm, "SQLite column is not a blob");
    return result_value(vm, (LangValue){
        .tag=LANG_VALUE_I64,
        .as.i64=(int64_t)sqlite3_column_bytes(
            statement->as.statement, (int)index)
    });
}

static LangNativeResult sqlite_column_blob_copy_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    int64_t index;
    if (arg_count != 3U || statement == NULL ||
        !integer_arg(&args[1], &index) || index < 0 ||
        index >= sqlite3_column_count(statement->as.statement) ||
        args[2].tag != LANG_VALUE_BYTE_SLICE)
        return native_failure(
            "sqlite_column_blob_copy expects `(Statement, column, Slice<byte>)`");
    if (sqlite3_column_type(statement->as.statement, (int)index) != SQLITE_BLOB)
        return result_error(vm, "SQLite column is not a blob");
    int length = sqlite3_column_bytes(statement->as.statement, (int)index);
    if ((size_t)length != args[2].as.bytes.length)
        return result_error(vm, "SQLite blob destination has the wrong length");
    const void *data = sqlite3_column_blob(statement->as.statement, (int)index);
    if (length != 0 && data == NULL)
        return result_error(vm, "could not read SQLite blob column");
    if (length != 0)
        memcpy(args[2].as.bytes.data, data, (size_t)length);
    return result_value(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult sqlite_reset_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count == 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    if (statement == NULL)
        return native_failure("sqlite_reset expects one Statement");
    int status = sqlite3_reset(statement->as.statement);
    if (status != SQLITE_OK)
        return result_error(vm, sqlite3_errmsg(
            sqlite3_db_handle(statement->as.statement)));
    return result_value(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult sqlite_clear_bindings_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count == 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    if (statement == NULL)
        return native_failure("sqlite_clear_bindings expects one Statement");
    int status = sqlite3_clear_bindings(statement->as.statement);
    if (status != SQLITE_OK)
        return result_error(vm, sqlite3_errmsg(
            sqlite3_db_handle(statement->as.statement)));
    return result_value(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult sqlite_statement_changes_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *statement = arg_count == 1U
        ? require_handle(&args[0], SQLITE_HANDLE_STATEMENT) : NULL;
    if (statement == NULL)
        return native_failure("sqlite_statement_changes expects one Statement");
    sqlite3 *database = sqlite3_db_handle(statement->as.statement);
    return result_value(vm, (LangValue){
        .tag=LANG_VALUE_I64,
        .as.i64=(int64_t)sqlite3_changes(database)
    });
}

static LangNativeResult sqlite_changes_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *database =
        arg_count == 1U
        ? require_handle(&args[0], SQLITE_HANDLE_DATABASE) : NULL;
    if (database == NULL)
        return native_failure("sqlite_changes expects one Database");
    return result_value(vm, (LangValue){
        .tag=LANG_VALUE_I64,
        .as.i64=(int64_t)sqlite3_changes(database->as.database)
    });
}

static LangNativeResult sqlite_last_insert_id_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *database =
        arg_count == 1U
        ? require_handle(&args[0], SQLITE_HANDLE_DATABASE) : NULL;
    if (database == NULL)
        return native_failure(
            "sqlite_last_insert_id expects one Database");
    return result_value(vm, (LangValue){
        .tag=LANG_VALUE_I64,
        .as.i64=(int64_t)sqlite3_last_insert_rowid(
            database->as.database)
    });
}

static LangNativeResult sqlite_begin_transaction_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    SqliteHandle *database = arg_count >= 1U
        ? require_handle(&args[0], SQLITE_HANDLE_DATABASE) : NULL;
    int64_t mode;
    if (arg_count != 2U || database == NULL ||
        !integer_arg(&args[1], &mode) || mode < 0 || mode > 2)
        return native_failure(
            "sqlite_begin_transaction expects `(Database, mode)`");
    SqliteHandle *transaction = calloc(1U, sizeof(*transaction));
    if (transaction == NULL)
        return native_failure("out of memory wrapping SQLite transaction");
    transaction->magic = SQLITE_HANDLE_MAGIC;
    transaction->kind = SQLITE_HANDLE_TRANSACTION;
    transaction->as.transaction.database = database->as.database;
    transaction->as.transaction.active = true;
    transaction->as.transaction.nested =
        sqlite3_get_autocommit(database->as.database) == 0;
    char sql[128];
    if (transaction->as.transaction.nested) {
        (void)snprintf(
            transaction->as.transaction.savepoint,
            sizeof(transaction->as.transaction.savepoint),
            "aster_%" PRIxPTR, (uintptr_t)transaction);
        (void)snprintf(
            sql, sizeof(sql), "SAVEPOINT %s",
            transaction->as.transaction.savepoint);
    } else {
        const char *begin = mode == 0 ? "BEGIN DEFERRED"
                          : mode == 1 ? "BEGIN IMMEDIATE"
                                      : "BEGIN EXCLUSIVE";
        (void)snprintf(sql, sizeof(sql), "%s", begin);
    }
    char *message = NULL;
    int status = sqlite3_exec(
        database->as.database, sql, NULL, NULL, &message);
    if (status != SQLITE_OK) {
        LangNativeResult error = result_error(
            vm, message != NULL ? message
                                : sqlite3_errmsg(database->as.database));
        sqlite3_free(message);
        transaction->as.transaction.active = false;
        sqlite_handle_drop(transaction);
        return error;
    }
    LangValue value;
    if (!lang_native_handle_value(
            vm, transaction, sqlite_handle_drop, &value)) {
        sqlite_handle_drop(transaction);
        return native_failure("could not wrap SQLite transaction");
    }
    return result_value(vm, value);
}

static LangNativeResult sqlite_finish_transaction(
    LangVM *vm, const LangValue *args, size_t arg_count,
    const char *sql) {
    SqliteHandle *transaction = arg_count == 1U
        ? require_handle(&args[0], SQLITE_HANDLE_TRANSACTION) : NULL;
    if (transaction == NULL)
        return native_failure("invalid SQLite transaction");
    if (!transaction->as.transaction.active)
        return result_error(vm, "SQLite transaction is already complete");
    char *message = NULL;
    char command[192];
    if (!transaction->as.transaction.nested)
        (void)snprintf(command, sizeof(command), "%s", sql);
    else if (strcmp(sql, "COMMIT") == 0)
        (void)snprintf(
            command, sizeof(command), "RELEASE SAVEPOINT %s",
            transaction->as.transaction.savepoint);
    else
        (void)snprintf(
            command, sizeof(command),
            "ROLLBACK TO SAVEPOINT %s; RELEASE SAVEPOINT %s",
            transaction->as.transaction.savepoint,
            transaction->as.transaction.savepoint);
    int status = sqlite3_exec(
        transaction->as.transaction.database,
        command, NULL, NULL, &message);
    if (status != SQLITE_OK) {
        LangNativeResult error = result_error(
            vm, message != NULL
                ? message
                : sqlite3_errmsg(transaction->as.transaction.database));
        sqlite3_free(message);
        return error;
    }
    transaction->as.transaction.active = false;
    return result_value(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult sqlite_commit_transaction_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return sqlite_finish_transaction(vm, args, arg_count, "COMMIT");
}

static LangNativeResult sqlite_rollback_transaction_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return sqlite_finish_transaction(vm, args, arg_count, "ROLLBACK");
}

#else

static LangNativeResult unavailable(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)args;
    (void)arg_count;
    return result_error(vm, "SQLite support is not available in this build");
}

#endif

void lang_register_sqlite_natives(LangVM *vm) {
#if defined(LANG_HAVE_SQLITE3)
    (void)lang_register_native(vm, "NativeSqliteOpen",
                               sqlite_open_value, 1U);
    (void)lang_register_native(vm, "NativeSqliteExecute",
                               sqlite_execute_value, 2U);
    (void)lang_register_native(vm, "NativeSqlitePrepare",
                               sqlite_prepare_value, 2U);
    (void)lang_register_native(vm, "NativeSqliteBindI64",
                               sqlite_bind_i64_value, 3U);
    (void)lang_register_native(vm, "NativeSqliteBindText",
                               sqlite_bind_text_value, 3U);
    (void)lang_register_native(vm, "NativeSqliteBindDouble",
                               sqlite_bind_f64_value, 3U);
    (void)lang_register_native(vm, "NativeSqliteBindNull",
                               sqlite_bind_null_value, 2U);
    (void)lang_register_native(vm, "NativeSqliteBindBlob",
                               sqlite_bind_blob_value, 3U);
    (void)lang_register_native(vm, "NativeSqliteParameterIndex",
                               sqlite_parameter_index_value, 2U);
    (void)lang_register_native(vm, "NativeSqliteStep",
                               sqlite_step_value, 1U);
    (void)lang_register_native(vm, "NativeSqliteColumnI64",
                               sqlite_column_i64_value, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnText",
                               sqlite_column_text_value, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnDouble",
                               sqlite_column_f64_value, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnType",
                               sqlite_column_type_value, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnCount",
                               sqlite_column_count_value, 1U);
    (void)lang_register_native(vm, "NativeSqliteColumnName",
                               sqlite_column_name_value, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnBlobLength",
                               sqlite_column_blob_length_value, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnBlobCopy",
                               sqlite_column_blob_copy_value, 3U);
    (void)lang_register_native(vm, "NativeSqliteReset",
                               sqlite_reset_value, 1U);
    (void)lang_register_native(vm, "NativeSqliteClearBindings",
                               sqlite_clear_bindings_value, 1U);
    (void)lang_register_native(vm, "NativeSqliteStatementChanges",
                               sqlite_statement_changes_value, 1U);
    (void)lang_register_native(vm, "NativeSqliteChanges",
                               sqlite_changes_value, 1U);
    (void)lang_register_native(vm, "NativeSqliteLastInsertId",
                               sqlite_last_insert_id_value, 1U);
    (void)lang_register_native(vm, "NativeSqliteBeginTransaction",
                               sqlite_begin_transaction_value, 2U);
    (void)lang_register_native(vm, "NativeSqliteCommitTransaction",
                               sqlite_commit_transaction_value, 1U);
    (void)lang_register_native(vm, "NativeSqliteRollbackTransaction",
                               sqlite_rollback_transaction_value, 1U);
#else
    (void)lang_register_native(vm, "NativeSqliteOpen",
                               unavailable, 1U);
    (void)lang_register_native(vm, "NativeSqliteExecute",
                               unavailable, 2U);
    (void)lang_register_native(vm, "NativeSqlitePrepare",
                               unavailable, 2U);
    (void)lang_register_native(vm, "NativeSqliteBindI64",
                               unavailable, 3U);
    (void)lang_register_native(vm, "NativeSqliteBindText",
                               unavailable, 3U);
    (void)lang_register_native(vm, "NativeSqliteBindDouble",
                               unavailable, 3U);
    (void)lang_register_native(vm, "NativeSqliteBindNull",
                               unavailable, 2U);
    (void)lang_register_native(vm, "NativeSqliteBindBlob",
                               unavailable, 3U);
    (void)lang_register_native(vm, "NativeSqliteParameterIndex",
                               unavailable, 2U);
    (void)lang_register_native(vm, "NativeSqliteStep",
                               unavailable, 1U);
    (void)lang_register_native(vm, "NativeSqliteColumnI64",
                               unavailable, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnText",
                               unavailable, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnDouble",
                               unavailable, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnType",
                               unavailable, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnCount",
                               unavailable, 1U);
    (void)lang_register_native(vm, "NativeSqliteColumnName",
                               unavailable, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnBlobLength",
                               unavailable, 2U);
    (void)lang_register_native(vm, "NativeSqliteColumnBlobCopy",
                               unavailable, 3U);
    (void)lang_register_native(vm, "NativeSqliteReset",
                               unavailable, 1U);
    (void)lang_register_native(vm, "NativeSqliteClearBindings",
                               unavailable, 1U);
    (void)lang_register_native(vm, "NativeSqliteStatementChanges",
                               unavailable, 1U);
    (void)lang_register_native(vm, "NativeSqliteChanges",
                               unavailable, 1U);
    (void)lang_register_native(vm, "NativeSqliteLastInsertId",
                               unavailable, 1U);
    (void)lang_register_native(vm, "NativeSqliteBeginTransaction",
                               unavailable, 2U);
    (void)lang_register_native(vm, "NativeSqliteCommitTransaction",
                               unavailable, 1U);
    (void)lang_register_native(vm, "NativeSqliteRollbackTransaction",
                               unavailable, 1U);
#endif
}
