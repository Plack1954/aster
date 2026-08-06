#include "checker_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Type type_error = {.kind=TYPE_ERROR, .name="<error>"};
Type type_unit = {.kind=TYPE_UNIT, .name="Unit"};
Type type_never = {.kind=TYPE_NEVER, .name="never"};
Type type_bool = {.kind=TYPE_BOOL, .name="bool"};
Type type_i8 = {.kind=TYPE_I8, .name="i8"};
Type type_i16 = {.kind=TYPE_I16, .name="i16"};
Type type_i32 = {.kind=TYPE_I32, .name="i32"};
Type type_i64 = {.kind=TYPE_I64, .name="i64"};
Type type_u8 = {.kind=TYPE_U8, .name="u8"};
Type type_u16 = {.kind=TYPE_U16, .name="u16"};
Type type_u32 = {.kind=TYPE_U32, .name="u32"};
Type type_u64 = {.kind=TYPE_U64, .name="u64"};
Type type_isize = {.kind=TYPE_ISIZE, .name="nint"};
Type type_usize = {.kind=TYPE_USIZE, .name="nuint"};
Type type_f32 = {.kind=TYPE_F32, .name="f32"};
Type type_f64 = {.kind=TYPE_F64, .name="f64"};
Type type_char = {.kind=TYPE_CHAR, .name="char"};
Type type_string = {.kind=TYPE_STRING, .name="string", .managed=true};
static FieldDecl exception_fields[] = {
    {.name="Message", .type_name="string"}
};
static Decl exception_decl = {
    .kind=DECL_STRUCT,
    .module_name="",
    .as.structure={
        .name="Exception", .fields=exception_fields, .field_count=1U
    }
};
Type type_exception = {
    .kind=TYPE_NAMED, .name="Exception", .declaration=&exception_decl,
    .requires_cleanup=true
};
#define DEFINE_EXCEPTION_TYPE(variable, public_name)                         \
    static Decl variable##_decl = {                                         \
        .kind=DECL_STRUCT, .module_name="",                                 \
        .as.structure={                                                      \
            .name=public_name, .fields=exception_fields, .field_count=1U    \
        }                                                                    \
    };                                                                       \
    Type variable = {                                                        \
        .kind=TYPE_NAMED, .name=public_name,                                \
        .declaration=&variable##_decl, .requires_cleanup=true               \
    }

DEFINE_EXCEPTION_TYPE(type_format_exception, "FormatException");
DEFINE_EXCEPTION_TYPE(type_overflow_exception, "OverflowException");
DEFINE_EXCEPTION_TYPE(type_argument_exception, "ArgumentException");
DEFINE_EXCEPTION_TYPE(type_invalid_operation_exception,
                      "InvalidOperationException");
DEFINE_EXCEPTION_TYPE(type_io_exception, "IOException");
DEFINE_EXCEPTION_TYPE(type_json_exception, "JsonException");
DEFINE_EXCEPTION_TYPE(type_sqlite_exception, "SqliteException");
DEFINE_EXCEPTION_TYPE(type_operation_canceled_exception,
                      "OperationCanceledException");
DEFINE_EXCEPTION_TYPE(type_task_canceled_exception,
                      "TaskCanceledException");
#undef DEFINE_EXCEPTION_TYPE
Type type_string_builder = {
    .kind=TYPE_STRING_BUILDER, .name="StringBuilder", .requires_cleanup=true
};
Type type_url = {.kind=TYPE_URL, .name="Url", .requires_cleanup=true};
Type type_html = {.kind=TYPE_HTML, .name="Html", .requires_cleanup=true};
Type type_buffer = {.kind=TYPE_BUFFER, .name="Buffer", .requires_cleanup=true};
Type type_arena = {.kind=TYPE_ARENA, .name="Arena", .requires_cleanup=true};
Type type_native_handle = {
    .kind=TYPE_NATIVE_HANDLE, .name="NativeHandle", .requires_cleanup=true
};
Type type_cancellation_token = {
    .kind=TYPE_CANCELLATION_TOKEN, .name="CancellationToken",
    .requires_cleanup=true, .managed=true
};
Type type_cancellation_token_source = {
    .kind=TYPE_CANCELLATION_TOKEN_SOURCE, .name="CancellationTokenSource",
    .requires_cleanup=true, .managed=true
};
Type type_raw_pointer = {
    .kind=TYPE_RAW_POINTER, .name="byte*", .element=&type_u8,
    .pointer_mutable=true
};
Type type_u8_slice = {
    .kind=TYPE_SLICE, .name="Span<u8>", .element=&type_u8
};

bool is_signed_integer(const Type *type) {
    return type->kind == TYPE_I8 || type->kind == TYPE_I16 ||
           type->kind == TYPE_I32 || type->kind == TYPE_I64 ||
           type->kind == TYPE_ISIZE;
}

bool is_unsigned_integer(const Type *type) {
    return type->kind == TYPE_U8 || type->kind == TYPE_U16 ||
           type->kind == TYPE_U32 || type->kind == TYPE_U64 ||
           type->kind == TYPE_USIZE;
}

bool is_integer(const Type *type) {
    return is_signed_integer(type) || is_unsigned_integer(type);
}

bool is_float(const Type *type) {
    return type->kind == TYPE_F32 || type->kind == TYPE_F64;
}

bool is_numeric(const Type *type) {
    return is_integer(type) || is_float(type);
}

bool is_exception_type(const Type *type) {
    return type == &type_exception ||
           type == &type_format_exception ||
           type == &type_overflow_exception ||
           type == &type_argument_exception ||
           type == &type_invalid_operation_exception ||
           type == &type_io_exception ||
           type == &type_json_exception ||
           type == &type_sqlite_exception ||
           type == &type_operation_canceled_exception ||
           type == &type_task_canceled_exception;
}

const char *type_declaration_name(const Decl *decl) {
    if (decl->kind == DECL_STRUCT || decl->kind == DECL_CLASS)
        return decl->as.structure.name;
    if (decl->kind == DECL_ENUM) return decl->as.enumeration.name;
    if (decl->kind == DECL_ALIAS) return decl->as.alias.name;
    return NULL;
}

const char *last_path_separator(const char *path) {
    const char *last = NULL;
    for (const char *cursor = strstr(path, "::");
         cursor != NULL; cursor = strstr(cursor + 2U, "::"))
        last = cursor;
    return last;
}

static bool declaration_path_matches(const char *path,
                                     const char *declaration_name,
                                     const char *module_name) {
    if (strcmp(path, declaration_name) == 0) return true;
    const char *separator = last_path_separator(path);
    if (separator == NULL ||
        strcmp(separator + 2U, declaration_name) != 0 ||
        module_name == NULL)
        return false;
    size_t qualifier_length = (size_t)(separator - path);
    size_t module_length = strlen(module_name);
    if (qualifier_length == module_length &&
        memcmp(path, module_name, qualifier_length) == 0)
        return true;
    return module_length > qualifier_length + 2U &&
        module_name[module_length - qualifier_length - 2U] == ':' &&
        module_name[module_length - qualifier_length - 1U] == ':' &&
        memcmp(path, module_name + module_length - qualifier_length,
               qualifier_length) == 0;
}

bool imported_declaration_matches(const Checker *checker,
                                         const char *use_name,
                                         const char *declaration_name,
                                         const char *declaration_module) {
    if (checker->current_module == NULL || declaration_module == NULL)
        return false;
    bool targeted_module = false;
    for (size_t i = 0U; i < checker->module->import_count; ++i) {
        const ImportDecl *import_decl = &checker->module->imports[i];
        if (import_decl->owner_module == NULL ||
            strcmp(import_decl->owner_module,
                   checker->current_module) != 0 ||
            strcmp(import_decl->module_path,
                   declaration_module) != 0)
            continue;
        targeted_module = true;
        if (import_decl->item_count != 0U) {
            for (size_t item = 0U;
                 item < import_decl->item_count; ++item) {
                const ImportItem *selected = &import_decl->items[item];
                const char *visible_name = selected->alias != NULL
                                         ? selected->alias
                                         : selected->name;
                if (strcmp(selected->name, declaration_name) == 0 &&
                    strcmp(use_name, visible_name) == 0)
                    return true;
            }
            continue;
        }
        if (import_decl->alias != NULL) {
            size_t alias_length = strlen(import_decl->alias);
            size_t use_length = strlen(use_name);
            if (use_length > alias_length + 2U &&
                strncmp(use_name, import_decl->alias, alias_length) == 0 &&
                use_name[alias_length] == ':' &&
                use_name[alias_length + 1U] == ':' &&
                strcmp(use_name + alias_length + 2U,
                       declaration_name) == 0)
                return true;
            continue;
        }
        if (declaration_path_matches(
                use_name, declaration_name, declaration_module))
            return true;
    }
    if (targeted_module || checker->module->strict_imports)
        return false;
    /*
     * Version 0 accepted imports whose source module declaration did not
     * mirror the historical relative filename. Preserve that behavior for
     * single-file commands; manifest projects use strict deterministic
     * mapping and never take this compatibility path.
     */
    return declaration_path_matches(
        use_name, declaration_name, declaration_module);
}

bool visible_declaration_path_matches(
    const Checker *checker, const char *use_name,
    const char *declaration_name, const char *declaration_module) {
    if (checker->current_module != NULL &&
        declaration_module != NULL &&
        strcmp(checker->current_module, declaration_module) == 0)
        return declaration_path_matches(
            use_name, declaration_name, declaration_module);
    return imported_declaration_matches(
        checker, use_name, declaration_name, declaration_module);
}

Decl *find_type_declaration(Checker *checker, const char *name,
                                   LangSpan use_span) {
    Decl *imported = NULL;
    Decl *first_import = NULL;
    for (size_t i = 0U; i < checker->module->count; ++i) {
        Decl *decl = checker->module->decls[i];
        const char *candidate = type_declaration_name(decl);
        if (candidate == NULL)
            continue;
        if (checker->current_module != NULL &&
            decl->module_name != NULL &&
            strcmp(checker->current_module, decl->module_name) == 0 &&
            strcmp(name, candidate) == 0)
            return decl;
        if (!decl->is_public ||
            !imported_declaration_matches(
                checker, name, candidate, decl->module_name))
            continue;
        if (imported != NULL) {
            LangDiagnostic *diagnostic =
                lang_diag(checker->diagnostics, use_span,
                          "ambiguous imported type `%s`", name);
            lang_diag_secondary(diagnostic, first_import->span,
                                "first public candidate");
            lang_diag_secondary(diagnostic, decl->span,
                                "another public candidate");
            lang_diag_help(
                diagnostic,
                "qualify the declaration or use a namespace alias");
            return imported;
        }
        imported = decl;
        first_import = decl;
    }
    return imported;
}

static const char *array_type_separator(const char *name) {
    unsigned square_depth = 0U;
    unsigned generic_depth = 0U;
    for (const char *cursor = name + 1U; *cursor != '\0'; ++cursor) {
        if (*cursor == '[') {
            ++square_depth;
        } else if (*cursor == ']') {
            if (square_depth != 0U) --square_depth;
        } else if (*cursor == '<') {
            ++generic_depth;
        } else if (*cursor == '>') {
            if (generic_depth != 0U) --generic_depth;
        } else if (*cursor == ';' &&
                   square_depth == 0U && generic_depth == 0U) {
            return cursor;
        }
    }
    return NULL;
}

static bool type_syntax_is_requires_cleanup(
    Checker *checker, const TypeSyntax *syntax, const char **seen,
    size_t seen_count);
static bool type_syntax_is_managed(
    Checker *checker, const TypeSyntax *syntax, const char **seen,
    size_t seen_count);

static bool type_name_is_requires_cleanup(Checker *checker, const char *name,
                                   const char **seen, size_t seen_count) {
    if (name == NULL) return false;
    if (strcmp(name, "StringBuilder") == 0 ||
        strcmp(name, "Url") == 0 || strcmp(name, "Html") == 0 ||
        strcmp(name, "Buffer") == 0 || strcmp(name, "Arena") == 0 ||
        strcmp(name, "NativeHandle") == 0 ||
        strncmp(name, "List<", 5U) == 0 ||
        strncmp(name, "Dictionary<", 11U) == 0 ||
        strncmp(name, "HashSet<", 8U) == 0 ||
        strncmp(name, "Queue<", 6U) == 0 ||
        strncmp(name, "Stack<", 6U) == 0)
        return true;
    if (strncmp(name, "Option<", 7U) == 0) {
        size_t length = strlen(name);
        if (length < 9U || name[length - 1U] != '>') return false;
        char *element = lang_arena_strndup(
            &checker->module->arena, name + 7U, length - 8U);
        return type_name_is_requires_cleanup(checker, element, seen, seen_count);
    }
    if (strncmp(name, "Result<", 7U) == 0) {
        size_t length = strlen(name);
        if (length < 10U || name[length - 1U] != '>') return true;
        const char *arguments = name + 7U;
        const char *comma = NULL;
        unsigned generic_depth = 0U;
        for (const char *cursor = arguments;
             cursor < name + length - 1U; ++cursor) {
            if (*cursor == '<') {
                ++generic_depth;
            } else if (*cursor == '>') {
                if (generic_depth != 0U) --generic_depth;
            } else if (*cursor == ',' && generic_depth == 0U) {
                comma = cursor;
                break;
            }
        }
        if (comma == NULL) return true;
        char *success = lang_arena_strndup(
            &checker->module->arena, arguments,
            (size_t)(comma - arguments));
        char *error = lang_arena_strndup(
            &checker->module->arena, comma + 1U,
            (size_t)((name + length - 1U) - (comma + 1U)));
        return type_name_is_requires_cleanup(
                   checker, success, seen, seen_count) ||
               type_name_is_requires_cleanup(
                   checker, error, seen, seen_count);
    }
    if (name[0] == '[') {
        const char *semi = array_type_separator(name);
        if (semi == NULL || semi <= name + 1) return false;
        char *element = lang_arena_strndup(&checker->module->arena, name + 1,
                                           (size_t)(semi - (name + 1)));
        return type_name_is_requires_cleanup(checker, element, seen, seen_count);
    }
    for (size_t i = 0U; i < seen_count; ++i)
        if (strcmp(seen[i], name) == 0)
            return true;
    if (seen_count >= 64U) return true;
    const char *next_seen[64];
    for (size_t i = 0U; i < seen_count; ++i) next_seen[i] = seen[i];
    next_seen[seen_count++] = name;
    LangSpan lookup_span = {checker->module->source->path, 0U, 0U};
    Decl *decl = find_type_declaration(checker, name, lookup_span);
    if (decl == NULL) return false;
    if (decl->kind == DECL_CLASS) return false;
    const char *declared_name = type_declaration_name(decl);
    for (size_t i = 0U; i < checker->module->count; ++i) {
        Decl *drop = checker->module->decls[i];
        if (drop->kind == DECL_FUNCTION &&
            drop->as.function.is_drop &&
            drop->as.function.param_count == 1U &&
            drop->module_name != NULL &&
            decl->module_name != NULL &&
            strcmp(drop->module_name, decl->module_name) == 0 &&
            declared_name != NULL &&
            strcmp(
                drop->as.function.params[0].type_name,
                declared_name) == 0)
            return true;
    }
    const char *previous_module = checker->current_module;
    checker->current_module = decl->module_name;
    if (decl->kind == DECL_ALIAS) {
        bool requires_cleanup = decl->as.alias.target_syntax != NULL
            ? type_syntax_is_requires_cleanup(
                  checker, decl->as.alias.target_syntax,
                  next_seen, seen_count)
            : type_name_is_requires_cleanup(
                  checker, decl->as.alias.target, next_seen, seen_count);
        checker->current_module = previous_module;
        return requires_cleanup;
    }
    FieldDecl *fields = decl->kind == DECL_STRUCT
                      ? decl->as.structure.fields
                      : decl->as.enumeration.variants;
    size_t field_count = decl->kind == DECL_STRUCT
                       ? decl->as.structure.field_count
                       : decl->as.enumeration.variant_count;
    for (size_t field = 0U; field < field_count; ++field)
        if ((fields[field].type_syntax != NULL
                 ? type_syntax_is_requires_cleanup(
                       checker, fields[field].type_syntax,
                       next_seen, seen_count)
                 : type_name_is_requires_cleanup(
                       checker, fields[field].type_name,
                       next_seen, seen_count))) {
            checker->current_module = previous_module;
            return true;
        }
    checker->current_module = previous_module;
    return false;
}

static bool type_name_is_managed(Checker *checker, const char *name,
                                 const char **seen, size_t seen_count) {
    if (name == NULL) return false;
    if (strcmp(name, "string") == 0) return true;
    if (strncmp(name, "Option<", 7U) == 0) {
        size_t length = strlen(name);
        if (length < 9U || name[length - 1U] != '>') return false;
        char *element = lang_arena_strndup(
            &checker->module->arena, name + 7U, length - 8U);
        return type_name_is_managed(checker, element, seen, seen_count);
    }
    if (strncmp(name, "Result<", 7U) == 0) {
        size_t length = strlen(name);
        if (length < 10U || name[length - 1U] != '>') return false;
        const char *arguments = name + 7U;
        const char *comma = NULL;
        unsigned depth = 0U;
        for (const char *cursor = arguments;
             cursor < name + length - 1U; ++cursor) {
            if (*cursor == '<') ++depth;
            else if (*cursor == '>' && depth != 0U) --depth;
            else if (*cursor == ',' && depth == 0U) {
                comma = cursor;
                break;
            }
        }
        if (comma == NULL) return false;
        char *success = lang_arena_strndup(
            &checker->module->arena, arguments,
            (size_t)(comma - arguments));
        char *error = lang_arena_strndup(
            &checker->module->arena, comma + 1U,
            (size_t)((name + length - 1U) - (comma + 1U)));
        return type_name_is_managed(checker, success, seen, seen_count) ||
               type_name_is_managed(checker, error, seen, seen_count);
    }
    if (name[0] == '[') {
        const char *semi = array_type_separator(name);
        if (semi == NULL || semi <= name + 1) return false;
        char *element = lang_arena_strndup(
            &checker->module->arena, name + 1,
            (size_t)(semi - (name + 1)));
        return type_name_is_managed(checker, element, seen, seen_count);
    }
    for (size_t i = 0U; i < seen_count; ++i)
        if (strcmp(seen[i], name) == 0)
            return false;
    if (seen_count >= 64U) return false;
    const char *next_seen[64];
    for (size_t i = 0U; i < seen_count; ++i) next_seen[i] = seen[i];
    next_seen[seen_count++] = name;
    LangSpan lookup_span = {checker->module->source->path, 0U, 0U};
    Decl *decl = find_type_declaration(checker, name, lookup_span);
    if (decl == NULL) return false;
    const char *previous_module = checker->current_module;
    checker->current_module = decl->module_name;
    if (decl->kind == DECL_ALIAS) {
        bool managed = decl->as.alias.target_syntax != NULL
            ? type_syntax_is_managed(
                  checker, decl->as.alias.target_syntax,
                  next_seen, seen_count)
            : type_name_is_managed(
                  checker, decl->as.alias.target, next_seen, seen_count);
        checker->current_module = previous_module;
        return managed;
    }
    if (decl->kind == DECL_CLASS) {
        checker->current_module = previous_module;
        return false;
    }
    FieldDecl *fields = decl->kind == DECL_STRUCT
                      ? decl->as.structure.fields
                      : decl->as.enumeration.variants;
    size_t field_count = decl->kind == DECL_STRUCT
                       ? decl->as.structure.field_count
                       : decl->as.enumeration.variant_count;
    for (size_t field = 0U; field < field_count; ++field) {
        if ((fields[field].type_syntax != NULL
                 ? type_syntax_is_managed(
                       checker, fields[field].type_syntax,
                       next_seen, seen_count)
                 : type_name_is_managed(
                       checker, fields[field].type_name,
                       next_seen, seen_count))) {
            checker->current_module = previous_module;
            return true;
        }
    }
    checker->current_module = previous_module;
    return false;
}

static bool type_syntax_is_requires_cleanup(
    Checker *checker, const TypeSyntax *syntax, const char **seen,
    size_t seen_count) {
    if (syntax == NULL) return false;
    switch (syntax->kind) {
        case TYPE_SYNTAX_NAMED:
            return type_name_is_requires_cleanup(
                checker, syntax->as.name, seen, seen_count);
        case TYPE_SYNTAX_GENERIC: {
            const TypeSyntax *base = syntax->as.generic.base;
            if (base->kind != TYPE_SYNTAX_NAMED) return true;
            const char *name = base->as.name;
            if (strcmp(name, "List") == 0 ||
                strcmp(name, "Dictionary") == 0 ||
                strcmp(name, "HashSet") == 0 ||
                strcmp(name, "Queue") == 0 ||
                strcmp(name, "Stack") == 0 ||
                strcmp(name, "Task") == 0)
                return true;
            if (strcmp(name, "Option") != 0 &&
                strcmp(name, "Result") != 0)
                return false;
            for (size_t i = 0U; i < syntax->as.generic.argument_count; ++i)
                if (type_syntax_is_requires_cleanup(
                        checker, syntax->as.generic.arguments[i],
                        seen, seen_count))
                    return true;
            return false;
        }
        case TYPE_SYNTAX_ARRAY:
            return type_syntax_is_requires_cleanup(
                checker, syntax->as.array.element, seen, seen_count);
        case TYPE_SYNTAX_TUPLE:
            for (size_t i = 0U; i < syntax->as.tuple.element_count; ++i)
                if (type_syntax_is_requires_cleanup(
                        checker, syntax->as.tuple.elements[i],
                        seen, seen_count))
                    return true;
            return false;
        case TYPE_SYNTAX_FUNCTION:
        case TYPE_SYNTAX_POINTER:
        case TYPE_SYNTAX_ERROR:
            return false;
    }
    return false;
}

static bool type_syntax_is_managed(
    Checker *checker, const TypeSyntax *syntax, const char **seen,
    size_t seen_count) {
    if (syntax == NULL) return false;
    switch (syntax->kind) {
        case TYPE_SYNTAX_NAMED:
            return type_name_is_managed(
                checker, syntax->as.name, seen, seen_count);
        case TYPE_SYNTAX_GENERIC: {
            const TypeSyntax *base = syntax->as.generic.base;
            if (base->kind == TYPE_SYNTAX_NAMED &&
                strcmp(base->as.name, "Task") == 0)
                return true;
            if (base->kind != TYPE_SYNTAX_NAMED ||
                (strcmp(base->as.name, "Option") != 0 &&
                 strcmp(base->as.name, "Result") != 0))
                return false;
            for (size_t i = 0U; i < syntax->as.generic.argument_count; ++i)
                if (type_syntax_is_managed(
                        checker, syntax->as.generic.arguments[i],
                        seen, seen_count))
                    return true;
            return false;
        }
        case TYPE_SYNTAX_ARRAY:
            return type_syntax_is_managed(
                checker, syntax->as.array.element, seen, seen_count);
        case TYPE_SYNTAX_TUPLE:
            for (size_t i = 0U; i < syntax->as.tuple.element_count; ++i)
                if (type_syntax_is_managed(
                        checker, syntax->as.tuple.elements[i],
                        seen, seen_count))
                    return true;
            return false;
        case TYPE_SYNTAX_FUNCTION:
        case TYPE_SYNTAX_POINTER:
        case TYPE_SYNTAX_ERROR:
            return false;
    }
    return false;
}

bool split_generic_application(
    Checker *checker, const char *name, char **out_base,
    char ***out_arguments, size_t *out_count) {
    const char *open = strchr(name, '<');
    size_t length = strlen(name);
    if (open == NULL || length < 3U || name[length - 1U] != '>')
        return false;
    unsigned depth = 0U;
    const char *argument_start = open + 1U;
    const char *end = name + length - 1U;
    char **arguments = NULL;
    size_t count = 0U;
    for (const char *cursor = argument_start; cursor <= end; ++cursor) {
        bool boundary = cursor == end;
        if (!boundary && *cursor == '<') {
            ++depth;
        } else if (!boundary && *cursor == '>') {
            if (depth == 0U) return false;
            --depth;
        } else if (!boundary && *cursor == ',' && depth == 0U) {
            boundary = true;
        }
        if (!boundary) continue;
        if (cursor == argument_start) return false;
        char **next = lang_arena_alloc(
            &checker->module->arena,
            (count + 1U) * sizeof(*next));
        if (arguments != NULL)
            memcpy(next, arguments, count * sizeof(*next));
        arguments = next;
        arguments[count++] = lang_arena_strndup(
            &checker->module->arena, argument_start,
            (size_t)(cursor - argument_start));
        argument_start = cursor + 1U;
    }
    if (depth != 0U || count == 0U) return false;
    *out_base = lang_arena_strndup(
        &checker->module->arena, name,
        (size_t)(open - name));
    *out_arguments = arguments;
    *out_count = count;
    return true;
}

static Type *find_type_instantiation(
    const Module *module, const Decl *declaration,
    Type **arguments, size_t argument_count) {
    for (size_t i = 0U;
         i < module->type_instantiation_count; ++i) {
        Type *candidate = module->type_instantiations[i];
        if (candidate->declaration != declaration ||
            candidate->argument_count != argument_count)
            continue;
        bool equal = true;
        for (size_t argument = 0U;
             argument < argument_count; ++argument)
            if (!same_type(candidate->arguments[argument],
                           arguments[argument])) {
                equal = false;
                break;
            }
        if (equal) return candidate;
    }
    return NULL;
}

static const char *canonical_instantiation_name(
    Checker *checker, const Decl *declaration,
    Type **arguments, size_t argument_count) {
    const char *base = type_declaration_name(declaration);
    size_t length = strlen(base) + 3U;
    for (size_t i = 0U; i < argument_count; ++i)
        length += strlen(arguments[i]->name) + (i == 0U ? 0U : 1U);
    char *name = lang_arena_alloc(&checker->module->arena, length);
    size_t offset = (size_t)snprintf(name, length, "%s<", base);
    for (size_t i = 0U; i < argument_count; ++i) {
        if (i != 0U) name[offset++] = ',';
        size_t argument_length = strlen(arguments[i]->name);
        memcpy(name + offset, arguments[i]->name, argument_length);
        offset += argument_length;
    }
    name[offset++] = '>';
    name[offset] = '\0';
    return name;
}

Type *resolve_type_in_applied_declaration(
    Checker *checker, const Type *applied, const char *name,
    LangSpan span) {
    if (applied == NULL ||
        (applied->kind != TYPE_NAMED && applied->kind != TYPE_CLASS) ||
        applied->declaration == NULL) {
        lang_diag(checker->diagnostics, span,
                  "cannot resolve `%s` without a concrete aggregate type",
                  name != NULL ? name : "<unknown>");
        return &type_error;
    }
    const char *previous_module = checker->current_module;
    const Decl *previous_decl = checker->substitution_decl;
    Type **previous_arguments = checker->substitution_arguments;
    size_t previous_count = checker->substitution_argument_count;
    checker->current_module = applied->declaration->module_name;
    checker->substitution_decl = applied->declaration;
    checker->substitution_arguments = applied->arguments;
    checker->substitution_argument_count = applied->argument_count;
    Type *result = resolve_type(checker, name, span);
    checker->current_module = previous_module;
    checker->substitution_decl = previous_decl;
    checker->substitution_arguments = previous_arguments;
    checker->substitution_argument_count = previous_count;
    return result;
}

Type *resolve_type_syntax_in_applied_declaration(
    Checker *checker, const Type *applied, const TypeSyntax *syntax,
    const char *fallback_name, LangSpan span) {
    if (syntax == NULL)
        return resolve_type_in_applied_declaration(
            checker, applied, fallback_name, span);
    if (applied == NULL ||
        (applied->kind != TYPE_NAMED && applied->kind != TYPE_CLASS) ||
        applied->declaration == NULL) {
        lang_diag(checker->diagnostics, span,
                  "cannot resolve `%s` without a concrete aggregate type",
                  fallback_name != NULL ? fallback_name : "<unknown>");
        return &type_error;
    }
    const char *previous_module = checker->current_module;
    const Decl *previous_decl = checker->substitution_decl;
    Type **previous_arguments = checker->substitution_arguments;
    size_t previous_count = checker->substitution_argument_count;
    checker->current_module = applied->declaration->module_name;
    checker->substitution_decl = applied->declaration;
    checker->substitution_arguments = applied->arguments;
    checker->substitution_argument_count = applied->argument_count;
    Type *result = resolve_type_syntax(checker, syntax);
    checker->current_module = previous_module;
    checker->substitution_decl = previous_decl;
    checker->substitution_arguments = previous_arguments;
    checker->substitution_argument_count = previous_count;
    return result;
}

static const char *canonical_constructed_name(
    Checker *checker, const char *base, Type **arguments,
    size_t argument_count) {
    size_t length = strlen(base) + 3U;
    for (size_t i = 0U; i < argument_count; ++i)
        length += strlen(arguments[i]->name) + (i == 0U ? 0U : 1U);
    char *name = lang_arena_alloc(&checker->module->arena, length);
    size_t offset = (size_t)snprintf(name, length, "%s<", base);
    for (size_t i = 0U; i < argument_count; ++i) {
        if (i != 0U) name[offset++] = ',';
        size_t argument_length = strlen(arguments[i]->name);
        memcpy(name + offset, arguments[i]->name, argument_length);
        offset += argument_length;
    }
    name[offset++] = '>';
    name[offset] = '\0';
    return name;
}

static bool builtin_equality_type(const Type *type) {
    return type->kind == TYPE_BOOL || type->kind == TYPE_CHAR ||
           type->kind == TYPE_STRING || type->kind == TYPE_RAW_POINTER ||
           is_numeric(type);
}

static Type *resolve_generic_syntax(Checker *checker,
                                    const TypeSyntax *syntax) {
    const TypeSyntax *base_syntax = syntax->as.generic.base;
    if (base_syntax == NULL || base_syntax->kind != TYPE_SYNTAX_NAMED) {
        lang_diag(checker->diagnostics, syntax->span,
                  "generic type base must be a named type");
        return &type_error;
    }
    const char *base = base_syntax->as.name;
    size_t count = syntax->as.generic.argument_count;
    Type **arguments = count == 0U ? NULL : lang_arena_alloc(
        &checker->module->arena, count * sizeof(*arguments));
    for (size_t i = 0U; i < count; ++i)
        arguments[i] = resolve_type_syntax(
            checker, syntax->as.generic.arguments[i]);
    const char *name = canonical_constructed_name(
        checker, base, arguments, count);

    TypeKind kind = TYPE_ERROR;
    size_t expected = 1U;
    bool requires_cleanup = false;
    bool managed = false;
    if (strcmp(base, "Span") == 0) kind = TYPE_SLICE;
    else if (strcmp(base, "ReadOnlySpan") == 0)
        kind = TYPE_READONLY_SPAN;
    else if (strcmp(base, "List") == 0) {
        kind = TYPE_VEC;
        requires_cleanup = true;
    } else if (strcmp(base, "Dictionary") == 0) {
        kind = TYPE_DICTIONARY;
        expected = 2U;
        requires_cleanup = true;
    } else if (strcmp(base, "HashSet") == 0) {
        kind = TYPE_HASH_SET;
        requires_cleanup = true;
    } else if (strcmp(base, "Queue") == 0) {
        kind = TYPE_QUEUE;
        requires_cleanup = true;
    } else if (strcmp(base, "Stack") == 0) {
        kind = TYPE_STACK;
        requires_cleanup = true;
    } else if (strcmp(base, "Option") == 0) {
        kind = TYPE_OPTION;
        if (count == 1U) {
            requires_cleanup = arguments[0]->requires_cleanup;
            managed = arguments[0]->managed;
        }
    } else if (strcmp(base, "Result") == 0) {
        kind = TYPE_RESULT;
        expected = 2U;
        if (count == 2U) {
            requires_cleanup = arguments[0]->requires_cleanup ||
                arguments[1]->requires_cleanup;
            managed = arguments[0]->managed || arguments[1]->managed;
        }
    } else if (strcmp(base, "Task") == 0) {
        kind = TYPE_TASK;
        requires_cleanup = true;
        managed = true;
    }
    if (kind != TYPE_ERROR) {
        if (count != expected) {
            lang_diag(checker->diagnostics, syntax->span,
                      "%s requires %zu type argument%s",
                      base, expected, expected == 1U ? "" : "s");
            return &type_error;
        }
        Type *type = lang_arena_alloc(
            &checker->module->arena, sizeof(*type));
        type->kind = kind;
        type->name = name;
        type->element = arguments[0];
        if (count == 2U) type->error_type = arguments[1];
        type->requires_cleanup = requires_cleanup;
        type->managed = managed;
        if ((kind == TYPE_DICTIONARY || kind == TYPE_HASH_SET) &&
            !builtin_equality_type(arguments[0]))
            lang_diag(checker->diagnostics, syntax->span,
                      "%s key type `%s` does not have built-in equality",
                      base, arguments[0]->name);
        if (kind == TYPE_HASH_SET) type->error_type = &type_bool;
        return type;
    }

    Decl *generic_decl = find_type_declaration(
        checker, base, syntax->span);
    if (generic_decl == NULL) {
        lang_diag(checker->diagnostics, syntax->span,
                  "unknown generic type `%s`", base);
        return &type_error;
    }
    if (generic_decl->kind == DECL_ALIAS &&
        generic_decl->type_param_count == count) {
        const Decl *previous_decl = checker->substitution_decl;
        Type **previous_arguments = checker->substitution_arguments;
        size_t previous_count = checker->substitution_argument_count;
        const char *previous_module = checker->current_module;
        checker->substitution_decl = generic_decl;
        checker->substitution_arguments = arguments;
        checker->substitution_argument_count = count;
        checker->current_module = generic_decl->module_name;
        Type *resolved = resolve_type_syntax(
            checker, generic_decl->as.alias.target_syntax);
        checker->current_module = previous_module;
        checker->substitution_decl = previous_decl;
        checker->substitution_arguments = previous_arguments;
        checker->substitution_argument_count = previous_count;
        return resolved;
    }
    if ((generic_decl->kind != DECL_STRUCT &&
         generic_decl->kind != DECL_ENUM) ||
        generic_decl->type_param_count != count) {
        lang_diag(checker->diagnostics, syntax->span,
                  "generic type `%s` expects %zu type arguments, found %zu",
                  base, generic_decl->type_param_count, count);
        return &type_error;
    }
    if (checker->generic_instantiation_depth >= 64U) {
        lang_diag(checker->diagnostics, syntax->span,
                  "generic instantiation is recursively expanding `%s`",
                  name);
        return &type_error;
    }
    ++checker->generic_instantiation_depth;
    Type *existing = find_type_instantiation(
        checker->module, generic_decl, arguments, count);
    if (existing != NULL) {
        --checker->generic_instantiation_depth;
        return existing;
    }
    Type *applied = lang_arena_alloc(
        &checker->module->arena, sizeof(*applied));
    applied->kind = TYPE_NAMED;
    applied->declaration = generic_decl;
    applied->arguments = arguments;
    applied->argument_count = count;
    applied->name = canonical_instantiation_name(
        checker, generic_decl, arguments, count);
    applied->instantiation_resolving = true;
    Type **next = lang_arena_alloc(
        &checker->module->arena,
        (checker->module->type_instantiation_count + 1U) * sizeof(*next));
    if (checker->module->type_instantiations != NULL)
        memcpy(next, checker->module->type_instantiations,
               checker->module->type_instantiation_count * sizeof(*next));
    checker->module->type_instantiations = next;
    checker->module->type_instantiations[
        checker->module->type_instantiation_count++] = applied;
    FieldDecl *fields = generic_decl->kind == DECL_STRUCT
        ? generic_decl->as.structure.fields
        : generic_decl->as.enumeration.variants;
    size_t field_count = generic_decl->kind == DECL_STRUCT
        ? generic_decl->as.structure.field_count
        : generic_decl->as.enumeration.variant_count;
    for (size_t i = 0U; i < field_count; ++i) {
        Type *field_type = resolve_type_syntax_in_applied_declaration(
            checker, applied, fields[i].type_syntax,
            fields[i].type_name, fields[i].span);
        if (field_type == applied) {
            lang_diag(checker->diagnostics, fields[i].span,
                      "generic type `%s` contains itself inline through field `%s`",
                      applied->name, fields[i].name);
            applied->requires_cleanup = true;
        } else if (field_type->requires_cleanup) {
            applied->requires_cleanup = true;
        }
        if (field_type->managed) applied->managed = true;
    }
    applied->instantiation_resolving = false;
    --checker->generic_instantiation_depth;
    return applied;
}

Type *resolve_type_syntax(Checker *checker, const TypeSyntax *syntax) {
    if (syntax == NULL) return NULL;
    switch (syntax->kind) {
        case TYPE_SYNTAX_NAMED:
            return resolve_type(checker, syntax->as.name, syntax->span);
        case TYPE_SYNTAX_GENERIC:
            return resolve_generic_syntax(checker, syntax);
        case TYPE_SYNTAX_FUNCTION: {
            Type *function = lang_arena_alloc(
                &checker->module->arena, sizeof(*function));
            function->kind = TYPE_FUNCTION;
            function->argument_count = syntax->as.function.parameter_count;
            if (function->argument_count != 0U) {
                function->arguments = lang_arena_alloc(
                    &checker->module->arena,
                    function->argument_count * sizeof(*function->arguments));
                function->parameter_modes = lang_arena_alloc(
                    &checker->module->arena,
                    function->argument_count *
                        sizeof(*function->parameter_modes));
                for (size_t i = 0U; i < function->argument_count; ++i) {
                    function->arguments[i] = resolve_type_syntax(
                        checker, syntax->as.function.parameters[i]);
                    function->parameter_modes[i] =
                        syntax->as.function.parameter_modes[i];
                }
            }
            function->element = resolve_type_syntax(
                checker, syntax->as.function.return_type);
            bool action = function->element->kind == TYPE_UNIT;
            size_t length = strlen(action ? "Action<>" : "Func<>") +
                (action ? 0U : strlen(function->element->name) + 1U) + 1U;
            for (size_t i = 0U; i < function->argument_count; ++i)
                length += strlen(function->arguments[i]->name) + 6U;
            char *name = lang_arena_alloc(&checker->module->arena, length);
            size_t offset = (size_t)snprintf(
                name, length, "%s<", action ? "Action" : "Func");
            for (size_t i = 0U; i < function->argument_count; ++i) {
                if (i != 0U) name[offset++] = ',';
                ParameterMode mode = function->parameter_modes[i];
                if (mode != PARAMETER_MODE_VALUE)
                    offset += (size_t)snprintf(
                        name + offset, length - offset, "%s ",
                        mode == PARAMETER_MODE_OUT ? "out" : "ref");
                size_t item_length = strlen(function->arguments[i]->name);
                memcpy(name + offset, function->arguments[i]->name, item_length);
                offset += item_length;
            }
            if (!action) {
                if (function->argument_count != 0U) name[offset++] = ',';
                size_t result_length = strlen(function->element->name);
                memcpy(name + offset, function->element->name, result_length);
                offset += result_length;
            }
            name[offset++] = '>';
            name[offset] = '\0';
            (void)offset;
            function->name = name;
            return function;
        }
        case TYPE_SYNTAX_POINTER: {
            Type *element = resolve_type_syntax(
                checker, syntax->as.pointer.element);
            const char *prefix = syntax->as.pointer.mutable_ ? "" : "const ";
            size_t length = strlen(prefix) + strlen(element->name) + 2U;
            char *name = lang_arena_alloc(&checker->module->arena, length);
            (void)snprintf(name, length, "%s%s*", prefix, element->name);
            Type *pointer = lang_arena_alloc(
                &checker->module->arena, sizeof(*pointer));
            pointer->kind = TYPE_RAW_POINTER;
            pointer->name = name;
            pointer->element = element;
            pointer->pointer_mutable = syntax->as.pointer.mutable_;
            return pointer;
        }
        case TYPE_SYNTAX_ARRAY: {
            Type *element = resolve_type_syntax(
                checker, syntax->as.array.element);
            size_t length = strlen(element->name) + 32U;
            char *name = lang_arena_alloc(&checker->module->arena, length);
            (void)snprintf(name, length, "%s[%zu]", element->name,
                           syntax->as.array.count);
            Type *array = lang_arena_alloc(
                &checker->module->arena, sizeof(*array));
            array->kind = TYPE_ARRAY;
            array->name = name;
            array->element = element;
            array->array_length = syntax->as.array.count;
            array->requires_cleanup = element->requires_cleanup;
            array->managed = element->managed;
            return array;
        }
        case TYPE_SYNTAX_TUPLE:
            lang_diag(checker->diagnostics, syntax->span,
                      "tuple types are not yet supported");
            return &type_error;
        case TYPE_SYNTAX_ERROR:
            return &type_error;
    }
    return &type_error;
}

Type *resolve_declared_type(Checker *checker, const TypeSyntax *syntax,
                            const char *fallback_name, LangSpan span) {
    return syntax != NULL
        ? resolve_type_syntax(checker, syntax)
        : resolve_type(checker, fallback_name, span);
}

Type *resolve_type(Checker *checker, const char *name, LangSpan span) {
    if (name == NULL) return NULL;
    if (checker->substitution_decl != NULL)
        for (size_t i = 0U;
             i < checker->substitution_decl->type_param_count &&
             i < checker->substitution_argument_count; ++i)
            if (strcmp(
                    name,
                    checker->substitution_decl->type_params[i]) == 0)
                return checker->substitution_arguments[i];
    if (strcmp(name, "Unit") == 0) return &type_unit;
    if (strcmp(name, "void") == 0) return &type_unit;
    if (strcmp(name, "never") == 0) return &type_never;
    if (strcmp(name, "bool") == 0) return &type_bool;
    if (strcmp(name, "i8") == 0) return &type_i8;
    if (strcmp(name, "sbyte") == 0) return &type_i8;
    if (strcmp(name, "i16") == 0) return &type_i16;
    if (strcmp(name, "short") == 0) return &type_i16;
    if (strcmp(name, "i32") == 0) return &type_i32;
    if (strcmp(name, "int") == 0) return &type_i32;
    if (strcmp(name, "i64") == 0) return &type_i64;
    if (strcmp(name, "long") == 0) return &type_i64;
    if (strcmp(name, "u8") == 0) return &type_u8;
    if (strcmp(name, "byte") == 0) return &type_u8;
    if (strcmp(name, "u16") == 0) return &type_u16;
    if (strcmp(name, "ushort") == 0) return &type_u16;
    if (strcmp(name, "u32") == 0) return &type_u32;
    if (strcmp(name, "uint") == 0) return &type_u32;
    if (strcmp(name, "u64") == 0) return &type_u64;
    if (strcmp(name, "ulong") == 0) return &type_u64;
    if (strcmp(name, "nint") == 0) return &type_isize;
    if (strcmp(name, "nuint") == 0) return &type_usize;
    if (strcmp(name, "f32") == 0) return &type_f32;
    if (strcmp(name, "float") == 0) return &type_f32;
    if (strcmp(name, "f64") == 0) return &type_f64;
    if (strcmp(name, "double") == 0) return &type_f64;
    if (strcmp(name, "char") == 0) return &type_char;
    if (strcmp(name, "string") == 0) return &type_string;
    if (strcmp(name, "Exception") == 0) return &type_exception;
    if (strcmp(name, "FormatException") == 0)
        return &type_format_exception;
    if (strcmp(name, "OverflowException") == 0)
        return &type_overflow_exception;
    if (strcmp(name, "ArgumentException") == 0)
        return &type_argument_exception;
    if (strcmp(name, "InvalidOperationException") == 0)
        return &type_invalid_operation_exception;
    if (strcmp(name, "IOException") == 0) return &type_io_exception;
    if (strcmp(name, "JsonException") == 0) return &type_json_exception;
    if (strcmp(name, "SqliteException") == 0)
        return &type_sqlite_exception;
    if (strcmp(name, "OperationCanceledException") == 0)
        return &type_operation_canceled_exception;
    if (strcmp(name, "TaskCanceledException") == 0)
        return &type_task_canceled_exception;
    if (strcmp(name, "StringBuilder") == 0) return &type_string_builder;
    if (strcmp(name, "Url") == 0) return &type_url;
    if (strcmp(name, "Html") == 0) return &type_html;
    if (strcmp(name, "Buffer") == 0) return &type_buffer;
    if (strcmp(name, "Arena") == 0) return &type_arena;
    if (strcmp(name, "NativeHandle") == 0) return &type_native_handle;
    if (strcmp(name, "CancellationToken") == 0)
        return &type_cancellation_token;
    if (strcmp(name, "CancellationTokenSource") == 0)
        return &type_cancellation_token_source;
    if (strncmp(name, "Span<", 5U) == 0 ||
        strncmp(name, "ReadOnlySpan<", 13U) == 0) {
        bool read_only = name[0] == 'R';
        size_t prefix = read_only ? 13U : 5U;
        size_t length = strlen(name);
        if (length < prefix + 2U || name[length - 1U] != '>') {
            lang_diag(checker->diagnostics, span,
                      "malformed span type `%s`", name);
            return &type_error;
        }
        char *element_name = lang_arena_strndup(
            &checker->module->arena, name + prefix,
            length - prefix - 1U);
        Type *slice =
            lang_arena_alloc(&checker->module->arena, sizeof(*slice));
        slice->kind = read_only ? TYPE_READONLY_SPAN : TYPE_SLICE;
        slice->name = name;
        slice->element = resolve_type(checker, element_name, span);
        slice->requires_cleanup = false;
        return slice;
    }
    if (strncmp(name, "List<", 5U) == 0) {
        size_t length = strlen(name);
        if (length < 7U || name[length - 1U] != '>') {
            lang_diag(checker->diagnostics, span,
                      "malformed List type `%s`", name);
            return &type_error;
        }
        char *element_name = lang_arena_strndup(
            &checker->module->arena, name + 5U, length - 6U);
        Type *vector =
            lang_arena_alloc(&checker->module->arena, sizeof(*vector));
        vector->kind = TYPE_VEC;
        vector->name = name;
        vector->element = resolve_type(checker, element_name, span);
        vector->requires_cleanup = true;
        return vector;
    }
    if (strncmp(name, "Dictionary<", 11U) == 0) {
        char *base = NULL;
        char **arguments = NULL;
        size_t argument_count = 0U;
        if (!split_generic_application(
                checker, name, &base, &arguments, &argument_count) ||
            strcmp(base, "Dictionary") != 0 || argument_count != 2U) {
            lang_diag(checker->diagnostics, span,
                      "Dictionary requires key and value type arguments");
            return &type_error;
        }
        Type *dictionary = lang_arena_alloc(
            &checker->module->arena, sizeof(*dictionary));
        dictionary->kind = TYPE_DICTIONARY;
        dictionary->name = name;
        dictionary->element = resolve_type(checker, arguments[0], span);
        dictionary->error_type = resolve_type(checker, arguments[1], span);
        if (!(dictionary->element->kind == TYPE_BOOL ||
              dictionary->element->kind == TYPE_CHAR ||
              dictionary->element->kind == TYPE_STRING ||
              dictionary->element->kind == TYPE_RAW_POINTER ||
              is_numeric(dictionary->element)))
            lang_diag(checker->diagnostics, span,
                      "Dictionary key type `%s` does not have built-in equality",
                      dictionary->element->name);
        dictionary->requires_cleanup = true;
        return dictionary;
    }
    if (strncmp(name, "HashSet<", 8U) == 0) {
        size_t length = strlen(name);
        if (length < 10U || name[length - 1U] != '>') {
            lang_diag(checker->diagnostics, span,
                      "malformed HashSet type `%s`", name);
            return &type_error;
        }
        char *element_name = lang_arena_strndup(
            &checker->module->arena, name + 8U, length - 9U);
        Type *set = lang_arena_alloc(
            &checker->module->arena, sizeof(*set));
        set->kind = TYPE_HASH_SET;
        set->name = name;
        set->element = resolve_type(checker, element_name, span);
        set->error_type = &type_bool;
        if (!(set->element->kind == TYPE_BOOL ||
              set->element->kind == TYPE_CHAR ||
              set->element->kind == TYPE_STRING ||
              set->element->kind == TYPE_RAW_POINTER ||
              is_numeric(set->element)))
            lang_diag(checker->diagnostics, span,
                      "HashSet element type `%s` does not have built-in equality",
                      set->element->name);
        set->requires_cleanup = true;
        return set;
    }
    if (strncmp(name, "Queue<", 6U) == 0) {
        size_t length = strlen(name);
        if (length < 8U || name[length - 1U] != '>') {
            lang_diag(checker->diagnostics, span,
                      "malformed Queue type `%s`", name);
            return &type_error;
        }
        char *element_name = lang_arena_strndup(
            &checker->module->arena, name + 6U, length - 7U);
        Type *queue = lang_arena_alloc(
            &checker->module->arena, sizeof(*queue));
        queue->kind = TYPE_QUEUE;
        queue->name = name;
        queue->element = resolve_type(checker, element_name, span);
        queue->requires_cleanup = true;
        return queue;
    }
    if (strncmp(name, "Stack<", 6U) == 0) {
        size_t length = strlen(name);
        if (length < 8U || name[length - 1U] != '>') {
            lang_diag(checker->diagnostics, span,
                      "malformed Stack type `%s`", name);
            return &type_error;
        }
        char *element_name = lang_arena_strndup(
            &checker->module->arena, name + 6U, length - 7U);
        Type *stack = lang_arena_alloc(
            &checker->module->arena, sizeof(*stack));
        stack->kind = TYPE_STACK;
        stack->name = name;
        stack->element = resolve_type(checker, element_name, span);
        stack->requires_cleanup = true;
        return stack;
    }
    if (strncmp(name, "Option<", 7U) == 0) {
        size_t length = strlen(name);
        if (length < 9U || name[length - 1U] != '>') {
            lang_diag(checker->diagnostics, span,
                      "malformed Option type `%s`", name);
            return &type_error;
        }
        char *element_name = lang_arena_strndup(
            &checker->module->arena, name + 7U, length - 8U);
        Type *option =
            lang_arena_alloc(&checker->module->arena, sizeof(*option));
        option->kind = TYPE_OPTION;
        option->name = name;
        option->element = resolve_type(checker, element_name, span);
        option->requires_cleanup = option->element->requires_cleanup;
        option->managed = option->element->managed;
        return option;
    }
    if (strncmp(name, "Result<", 7U) == 0) {
        size_t length = strlen(name);
        if (length < 10U || name[length - 1U] != '>') {
            lang_diag(checker->diagnostics, span, "malformed Result type `%s`", name);
            return &type_error;
        }
        const char *arguments = name + 7U;
        const char *comma = NULL;
        unsigned depth = 0U;
        for (const char *cursor = arguments; cursor < name + length - 1U; ++cursor) {
            if (*cursor == '<') ++depth;
            else if (*cursor == '>') --depth;
            else if (*cursor == ',' && depth == 0U) { comma = cursor; break; }
        }
        if (comma == NULL) {
            lang_diag(checker->diagnostics, span,
                      "Result requires success and error type arguments");
            return &type_error;
        }
        char *ok_name = lang_arena_strndup(&checker->module->arena, arguments,
                                           (size_t)(comma - arguments));
        char *error_name = lang_arena_strndup(&checker->module->arena, comma + 1,
            (size_t)((name + length - 1U) - (comma + 1)));
        Type *result = lang_arena_alloc(&checker->module->arena, sizeof(*result));
        result->kind = TYPE_RESULT;
        result->name = name;
        result->element = resolve_type(checker, ok_name, span);
        result->error_type = resolve_type(checker, error_name, span);
        result->requires_cleanup =
            result->element->requires_cleanup || result->error_type->requires_cleanup;
        result->managed =
            result->element->managed || result->error_type->managed;
        return result;
    }
    if (strcmp(name, "Task") == 0) {
        Type *task = lang_arena_alloc(
            &checker->module->arena, sizeof(*task));
        task->kind = TYPE_TASK;
        task->name = name;
        task->element = &type_unit;
        task->requires_cleanup = true;
        task->managed = true;
        return task;
    }
    if (strncmp(name, "Task<", 5U) == 0) {
        size_t length = strlen(name);
        if (length < 7U || name[length - 1U] != '>') {
            lang_diag(checker->diagnostics, span,
                      "malformed Task type `%s`", name);
            return &type_error;
        }
        char *result_name = lang_arena_strndup(
            &checker->module->arena, name + 5U, length - 6U);
        Type *task = lang_arena_alloc(
            &checker->module->arena, sizeof(*task));
        task->kind = TYPE_TASK;
        task->name = name;
        task->element = resolve_type(checker, result_name, span);
        task->requires_cleanup = true;
        task->managed = true;
        return task;
    }
    if (name[0] == '[') {
        Type *array = lang_arena_alloc(&checker->module->arena, sizeof(*array));
        array->kind = TYPE_ARRAY; array->name = name;
        const char *semi = array_type_separator(name);
        if (semi != NULL && semi > name + 1) {
            size_t element_length = (size_t)(semi - (name + 1));
            char *element_name = lang_arena_strndup(&checker->module->arena,
                                                    name + 1, element_length);
            array->element = resolve_type(checker, element_name, span);
        } else {
            array->element = &type_error;
        }
        array->array_length = semi != NULL ? (size_t)strtoull(semi + 1, NULL, 10) : 0U;
        array->requires_cleanup = array->element->requires_cleanup;
        array->managed = array->element->managed;
        return array;
    }
    char *generic_base = NULL;
    char **generic_argument_names = NULL;
    size_t generic_argument_count = 0U;
    if (split_generic_application(
            checker, name, &generic_base,
            &generic_argument_names, &generic_argument_count)) {
        Decl *generic_decl = find_type_declaration(
            checker, generic_base, span);
        if (generic_decl == NULL) {
            lang_diag(checker->diagnostics, span,
                      "unknown generic type `%s`", generic_base);
            return &type_error;
        }
        if (generic_decl->kind != DECL_STRUCT &&
            generic_decl->kind != DECL_ENUM) {
            lang_diag(checker->diagnostics, span,
                      "`%s` is not a generic aggregate type",
                      generic_base);
            return &type_error;
        }
        if (generic_decl->type_param_count !=
            generic_argument_count) {
            lang_diag(
                checker->diagnostics, span,
                "generic type `%s` expects %zu type arguments, found %zu",
                generic_base, generic_decl->type_param_count,
                generic_argument_count);
            return &type_error;
        }
        if (checker->generic_instantiation_depth >= 64U) {
            lang_diag(checker->diagnostics, span,
                      "generic instantiation is recursively expanding `%s`",
                      name);
            return &type_error;
        }
        ++checker->generic_instantiation_depth;
        Type **arguments = lang_arena_alloc(
            &checker->module->arena,
            generic_argument_count * sizeof(*arguments));
        for (size_t i = 0U; i < generic_argument_count; ++i)
            arguments[i] = resolve_type(
                checker, generic_argument_names[i], span);
        Type *existing = find_type_instantiation(
            checker->module, generic_decl,
            arguments, generic_argument_count);
        if (existing != NULL) {
            --checker->generic_instantiation_depth;
            return existing;
        }
        Type *applied = lang_arena_alloc(
            &checker->module->arena, sizeof(*applied));
        applied->kind = TYPE_NAMED;
        applied->declaration = generic_decl;
        applied->arguments = arguments;
        applied->argument_count = generic_argument_count;
        applied->name = canonical_instantiation_name(
            checker, generic_decl, arguments,
            generic_argument_count);
        applied->instantiation_resolving = true;
        Type **next_instantiations = lang_arena_alloc(
            &checker->module->arena,
            (checker->module->type_instantiation_count + 1U) *
                sizeof(*next_instantiations));
        if (checker->module->type_instantiations != NULL)
            memcpy(
                next_instantiations,
                checker->module->type_instantiations,
                checker->module->type_instantiation_count *
                    sizeof(*next_instantiations));
        checker->module->type_instantiations = next_instantiations;
        checker->module->type_instantiations[
            checker->module->type_instantiation_count++] = applied;
        FieldDecl *fields = generic_decl->kind == DECL_STRUCT
                          ? generic_decl->as.structure.fields
                          : generic_decl->as.enumeration.variants;
        size_t field_count = generic_decl->kind == DECL_STRUCT
                           ? generic_decl->as.structure.field_count
                           : generic_decl->as.enumeration.variant_count;
        for (size_t field = 0U; field < field_count; ++field) {
            Type *field_type = resolve_type_syntax_in_applied_declaration(
                checker, applied, fields[field].type_syntax,
                fields[field].type_name, fields[field].span);
            if (field_type == applied) {
                lang_diag(
                    checker->diagnostics, fields[field].span,
                    "generic type `%s` contains itself inline through field `%s`",
                    applied->name, fields[field].name);
                applied->requires_cleanup = true;
            } else if (field_type->requires_cleanup) {
                applied->requires_cleanup = true;
            }
            if (field_type->managed)
                applied->managed = true;
        }
        applied->instantiation_resolving = false;
        --checker->generic_instantiation_depth;
        return applied;
    }
    Decl *type_decl = find_type_declaration(checker, name, span);
    if (type_decl != NULL && type_decl->kind == DECL_ALIAS) {
        Decl *decl = type_decl;
        for (size_t active = 0U;
             active < checker->resolving_alias_count; ++active)
            if (strcmp(checker->resolving_aliases[active], name) == 0) {
                lang_diag(checker->diagnostics, span,
                          "cyclic type alias involving `%s`", name);
                return &type_error;
            }
        if (checker->resolving_alias_count >= 64U) {
            lang_diag(checker->diagnostics, span,
                      "type alias nesting is too deep");
            return &type_error;
        }
        checker->resolving_aliases[checker->resolving_alias_count++] = name;
        const char *previous_module = checker->current_module;
        checker->current_module = decl->module_name;
        Type *target = resolve_declared_type(
            checker, decl->as.alias.target_syntax,
            decl->as.alias.target, decl->span);
        checker->current_module = previous_module;
        --checker->resolving_alias_count;
        return target;
    }
    if (type_decl != NULL) {
        const char *decl_name = type_declaration_name(type_decl);
        if (type_decl->type_param_count != 0U) {
            lang_diag(
                checker->diagnostics, span,
                "generic type `%s` requires %zu type arguments",
                decl_name, type_decl->type_param_count);
            return &type_error;
        }
        Type *type = lang_arena_alloc(&checker->module->arena, sizeof(*type));
        type->kind = type_decl->kind == DECL_CLASS
                   ? TYPE_CLASS : TYPE_NAMED;
        type->name = decl_name;
        type->declaration = type_decl;
        const char *previous_module = checker->current_module;
        checker->current_module = type_decl->module_name;
        type->requires_cleanup =
            type_name_is_requires_cleanup(checker, decl_name, NULL, 0U);
        type->managed =
            type_name_is_managed(checker, decl_name, NULL, 0U);
        checker->current_module = previous_module;
        return type;
    }
    lang_diag(checker->diagnostics, span, "unknown type `%s`", name);
    return &type_error;
}

Type *lang_checker_resolve_aggregate_member(
    Module *module, LangDiagnostics *diagnostics,
    const Type *aggregate, size_t member_index) {
    if (module == NULL || aggregate == NULL ||
        (aggregate->kind != TYPE_NAMED &&
         aggregate->kind != TYPE_CLASS) ||
        aggregate->declaration == NULL)
        return NULL;
    const Decl *decl = aggregate->declaration;
    FieldDecl *members = NULL;
    size_t member_count = 0U;
    if (decl->kind == DECL_STRUCT || decl->kind == DECL_CLASS) {
        members = decl->as.structure.fields;
        member_count = decl->as.structure.field_count;
    } else if (decl->kind == DECL_ENUM) {
        members = decl->as.enumeration.variants;
        member_count = decl->as.enumeration.variant_count;
    }
    if (members == NULL || member_index >= member_count ||
        members[member_index].type_name == NULL ||
        (decl->kind == DECL_ENUM &&
         strcmp(members[member_index].type_name, "Unit") == 0))
        return NULL;
    Checker checker;
    memset(&checker, 0, sizeof(checker));
    checker.module = module;
    checker.diagnostics = diagnostics;
    return resolve_type_syntax_in_applied_declaration(
        &checker, aggregate, members[member_index].type_syntax,
        members[member_index].type_name,
        members[member_index].span);
}

bool same_type(const Type *a, const Type *b) {
    if (a == &type_error || b == &type_error) return true;
    if (a->kind != b->kind) return false;
    if (a->kind == TYPE_ARRAY)
        return a->array_length == b->array_length &&
               same_type(a->element, b->element);
    if (a->kind == TYPE_RESULT || a->kind == TYPE_DICTIONARY)
        return same_type(a->element, b->element) &&
               same_type(a->error_type, b->error_type);
    if (a->kind == TYPE_HASH_SET)
        return same_type(a->element, b->element);
    if (a->kind == TYPE_OPTION || a->kind == TYPE_SLICE ||
        a->kind == TYPE_READONLY_SPAN ||
        a->kind == TYPE_VEC || a->kind == TYPE_QUEUE)
        return same_type(a->element, b->element);
    if (a->kind == TYPE_TASK)
        return same_type(a->element, b->element);
    if (a->kind == TYPE_STACK)
        return same_type(a->element, b->element);
    if (a->kind == TYPE_RAW_POINTER)
        return a->pointer_mutable == b->pointer_mutable &&
               same_type(a->element, b->element);
    if (a->kind == TYPE_FUNCTION) {
        if (a->argument_count != b->argument_count ||
            !same_type(a->element, b->element))
            return false;
        for (size_t i = 0U; i < a->argument_count; ++i)
            if (a->parameter_modes[i] != b->parameter_modes[i] ||
                !same_type(a->arguments[i], b->arguments[i]))
                return false;
        return true;
    }
    if (a->kind == TYPE_NAMED || a->kind == TYPE_CLASS) {
        if (a->declaration != NULL && b->declaration != NULL) {
            if (a->declaration != b->declaration ||
                a->argument_count != b->argument_count)
                return false;
            for (size_t i = 0U; i < a->argument_count; ++i)
                if (!same_type(a->arguments[i], b->arguments[i]))
                    return false;
            return true;
        } else {
            return strcmp(a->name, b->name) == 0;
        }
    }
    return true;
}

static bool class_or_interface_assignable(
    const Decl *expected, const Decl *actual, size_t depth
) {
    if (expected == actual) return true;
    if (expected == NULL || actual == NULL || depth >= 256U)
        return false;
    if (class_or_interface_assignable(
            expected, actual->as.structure.base_class, depth + 1U))
        return true;
    for (size_t interface = 0U;
         interface < actual->as.structure.interface_count; ++interface)
        if (class_or_interface_assignable(
                expected, actual->as.structure.interfaces[interface],
                depth + 1U))
            return true;
    return false;
}

bool type_assignable(const Type *expected, const Type *actual) {
    if (same_type(expected, actual)) return true;
    if (expected != NULL && actual != NULL &&
        expected->kind == TYPE_CLASS && actual->kind == TYPE_CLASS &&
        expected->declaration != NULL && actual->declaration != NULL) {
        if (class_or_interface_assignable(
                expected->declaration, actual->declaration, 0U))
            return true;
    }
    return expected != NULL && actual != NULL &&
           expected->kind == TYPE_READONLY_SPAN &&
           actual->kind == TYPE_SLICE &&
           same_type(expected->element, actual->element);
}

const char *type_display_name(Checker *checker, const Type *type) {
    if (type == NULL) return "<unknown>";
    if ((type->kind != TYPE_NAMED && type->kind != TYPE_CLASS) ||
        type->declaration == NULL ||
        type->declaration->module_name == NULL)
        return type->name;
    size_t length = strlen(type->declaration->module_name) +
                    strlen(type->name) + 3U;
    char *display = lang_arena_alloc(&checker->module->arena, length);
    (void)snprintf(display, length, "%s::%s",
                   type->declaration->module_name, type->name);
    return display;
}

static unsigned integer_width(const Type *type) {
    switch (type->kind) {
        case TYPE_I8: case TYPE_U8: return 8U;
        case TYPE_I16: case TYPE_U16: return 16U;
        case TYPE_I32: case TYPE_U32: return 32U;
        case TYPE_I64: case TYPE_U64:
        case TYPE_ISIZE: case TYPE_USIZE: return 64U;
        default: return 0U;
    }
}

bool coerce_literal(Checker *checker, Expr *expr, Type *expected) {
    if (expr->kind == EXPR_INT && is_integer(expected)) {
        uint64_t value = expr->as.integer;
        unsigned width = integer_width(expected);
        uint64_t maximum;
        if (is_signed_integer(expected))
            maximum = width == 64U ? (uint64_t)INT64_MAX
                                   : (UINT64_C(1) << (width - 1U)) - 1U;
        else
            maximum = width == 64U ? UINT64_MAX
                                   : (UINT64_C(1) << width) - 1U;
        if (value > maximum) {
            lang_diag(checker->diagnostics, expr->span,
                      "integer literal does not fit `%s`", expected->name);
            expr->type = expected;
            return true;
        }
        expr->type = expected;
        return true;
    }
    if (expr->kind == EXPR_FLOAT && is_float(expected)) {
        expr->type = expected;
        return true;
    }
    if (expr->kind == EXPR_UNARY && expr->as.unary.op == TOK_MINUS &&
        expr->as.unary.operand->kind == EXPR_INT &&
        is_signed_integer(expected)) {
        uint64_t magnitude =
            expr->as.unary.operand->as.integer;
        unsigned width = integer_width(expected);
        uint64_t maximum_magnitude =
            width == 64U ? UINT64_C(1) << 63U
                         : UINT64_C(1) << (width - 1U);
        if (magnitude > maximum_magnitude) {
            lang_diag(checker->diagnostics, expr->span,
                      "integer literal does not fit `%s`", expected->name);
            return false;
        }
        expr->as.unary.operand->type = expected;
        expr->type = expected;
        return true;
    }
    if (expr->kind == EXPR_ARRAY && expected->kind == TYPE_ARRAY &&
        expr->as.array.count == expected->array_length) {
        bool ok = true;
        for (size_t i = 0U; i < expr->as.array.count; ++i)
            if (!coerce_literal(checker, expr->as.array.items[i],
                                expected->element) &&
                !same_type(expr->as.array.items[i]->type, expected->element))
                ok = false;
        if (ok) expr->type = expected;
        return ok;
    }
    if (expected->kind == TYPE_OPTION && expr->type != NULL &&
        same_type(expr->type, expected->element)) {
        Expr *value = lang_arena_alloc(
            &checker->module->arena, sizeof(*value));
        *value = *expr;
        Expr *callee = lang_arena_alloc(
            &checker->module->arena, sizeof(*callee));
        memset(callee, 0, sizeof(*callee));
        callee->kind = EXPR_NAME;
        callee->span = expr->span;
        callee->as.name = "Option::Some";
        memset(expr, 0, sizeof(*expr));
        expr->kind = EXPR_CALL;
        expr->span = value->span;
        expr->as.call.callee = callee;
        expr->as.call.arguments.items = lang_arena_alloc(
            &checker->module->arena, sizeof(Expr *));
        expr->as.call.arguments.items[0] = value;
        expr->as.call.arguments.count = 1U;
        Type *previous_expected = checker->expected_type;
        checker->expected_type = expected;
        Type *wrapped = check_expr(checker, expr);
        checker->expected_type = previous_expected;
        return same_type(wrapped, expected);
    }
    if (expected->kind == TYPE_READONLY_SPAN && expr->type != NULL &&
        expr->type->kind == TYPE_SLICE &&
        same_type(expected->element, expr->type->element)) {
        return true;
    }
    return false;
}

const Decl *type_copy_constructor(const Type *type) {
    if (type == NULL || type->declaration == NULL ||
        type->declaration->kind != DECL_STRUCT)
        return NULL;
    const Decl *declaration = type->declaration;
    for (size_t member = 0U;
         member < declaration->as.structure.member_count; ++member) {
        const Decl *candidate = declaration->as.structure.members[member];
        if (candidate->kind == DECL_FUNCTION &&
            candidate->as.function.is_copy_constructor)
            return candidate;
    }
    return NULL;
}

static bool type_is_copyable_inner(Checker *checker, Type *type,
                                   const Type **seen, size_t seen_count,
                                   bool allow_custom_copy) {
    if (type->kind == TYPE_ARENA)
        return false;
    if (type->kind == TYPE_ARRAY)
        return type_is_copyable_inner(
            checker, type->element, seen, seen_count,
            allow_custom_copy);
    if (type->kind == TYPE_OPTION)
        return type_is_copyable_inner(
            checker, type->element, seen, seen_count, false);
    if (type->kind == TYPE_VEC || type->kind == TYPE_QUEUE ||
        type->kind == TYPE_STACK)
        return type_is_copyable_inner(
            checker, type->element, seen, seen_count, false);
    if (type->kind == TYPE_DICTIONARY)
        return type_is_copyable_inner(
                   checker, type->element, seen, seen_count, false) &&
               type_is_copyable_inner(
                   checker, type->error_type, seen, seen_count, false);
    if (type->kind == TYPE_HASH_SET)
        return type_is_copyable_inner(
            checker, type->element, seen, seen_count, false);
    if (type->kind == TYPE_RESULT)
        return type_is_copyable_inner(
                   checker, type->element, seen, seen_count, false) &&
               type_is_copyable_inner(
                   checker, type->error_type, seen, seen_count, false);
    if (type->kind == TYPE_CLASS) return true;
    if (type->kind != TYPE_NAMED) return true;
    const Decl *type_decl = type->declaration;
    if (type_decl == NULL) return false;
    const Decl *copy = type_copy_constructor(type);
    if (copy != NULL)
        return allow_custom_copy && !copy->as.function.is_deleted;
    for (size_t i = 0U; i < seen_count; ++i)
        if (same_type(seen[i], type)) return false;
    if (seen_count >= 64U) return false;
    const Type *next_seen[64];
    for (size_t i = 0U; i < seen_count; ++i) next_seen[i] = seen[i];
    next_seen[seen_count++] = type;
    FieldDecl *fields = NULL;
    size_t field_count = 0U;
    if (type_decl->kind == DECL_STRUCT) {
        fields = type_decl->as.structure.fields;
        field_count = type_decl->as.structure.field_count;
    } else if (type_decl->kind == DECL_ENUM) {
        fields = type_decl->as.enumeration.variants;
        field_count = type_decl->as.enumeration.variant_count;
    }
    if (fields == NULL) return false;
    bool copyable = true;
    bool allow_member_custom =
        allow_custom_copy && type_decl->kind == DECL_STRUCT;
    for (size_t field = 0U; field < field_count; ++field) {
        Type *field_type = resolve_type_syntax_in_applied_declaration(
            checker, type, fields[field].type_syntax,
            fields[field].type_name,
            fields[field].span);
        if (!type_is_copyable_inner(
                checker, field_type, next_seen, seen_count,
                allow_member_custom)) {
            copyable = false;
            break;
        }
    }
    return copyable;
}

bool type_is_copyable(Checker *checker, Type *type) {
    return type_is_copyable_inner(checker, type, NULL, 0U, true);
}
