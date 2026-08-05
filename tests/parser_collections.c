#include "internal.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TextBuilder {
    char *text;
    size_t length;
    size_t capacity;
} TextBuilder;

static bool append_text(TextBuilder *builder, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int needed = vsnprintf(NULL, 0U, format, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(arguments);
        return false;
    }
    size_t required = builder->length + (size_t)needed + 1U;
    if (required > builder->capacity) {
        size_t capacity = builder->capacity == 0U ? 1024U : builder->capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) {
                va_end(arguments);
                return false;
            }
            capacity *= 2U;
        }
        char *text = realloc(builder->text, capacity);
        if (text == NULL) {
            va_end(arguments);
            return false;
        }
        builder->text = text;
        builder->capacity = capacity;
    }
    (void)vsnprintf(builder->text + builder->length,
                    builder->capacity - builder->length,
                    format, arguments);
    va_end(arguments);
    builder->length += (size_t)needed;
    return true;
}

static const Decl *find_function(const Module *module, const char *name) {
    for (size_t i = 0U; i < module->count; ++i) {
        const Decl *decl = module->decls[i];
        if (decl->kind == DECL_FUNCTION &&
            strcmp(decl->as.function.name, name) == 0)
            return decl;
    }
    return NULL;
}

int main(void) {
    const size_t item_count = 128U;
    TextBuilder text = {0};
    for (size_t i = 0U; i < item_count; ++i)
        if (!append_text(&text, "using Collection%zu;\n", i)) return 1;
    for (size_t i = 0U; i < item_count; ++i)
        if (!append_text(&text, "struct Empty%zu {}\n", i)) return 1;
    if (!append_text(&text, "struct Wide {")) return 1;
    for (size_t i = 0U; i < item_count; ++i)
        if (!append_text(&text, "int field%zu,", i)) return 1;
    if (!append_text(&text, "}\nint target(")) return 1;
    for (size_t i = 0U; i < item_count; ++i)
        if (!append_text(&text, "%sint parameter%zu", i == 0U ? "" : ",", i))
            return 1;
    if (!append_text(&text, "){ return 0; }\nint caller(){\n")) return 1;
    for (size_t i = 0U; i < item_count; ++i)
        if (!append_text(&text, "0;\n")) return 1;
    if (!append_text(&text, "target(")) return 1;
    for (size_t i = 0U; i < item_count; ++i)
        if (!append_text(&text, "%s%zu", i == 0U ? "" : ",", i)) return 1;
    if (!append_text(&text, ");\nreturn 0;\n}\n")) return 1;

    char *path = malloc(sizeof("<parser-collections>"));
    if (path == NULL) {
        free(text.text);
        return 1;
    }
    memcpy(path, "<parser-collections>", sizeof("<parser-collections>"));
    LangSource source = {
        .text=text.text, .length=text.length, .path=path
    };
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool parsed = lang_parse_module(&source, &diagnostics, &module);

    const Decl *wide = NULL;
    for (size_t i = 0U; i < module.count; ++i)
        if (module.decls[i]->kind == DECL_STRUCT &&
            strcmp(module.decls[i]->as.structure.name, "Wide") == 0)
            wide = module.decls[i];
    const Decl *target = find_function(&module, "target");
    const Decl *caller = find_function(&module, "caller");
    const Stmt *caller_body = caller == NULL
        ? NULL : caller->as.function.body;
    const Expr *call = NULL;
    if (caller_body != NULL && caller_body->kind == STMT_BLOCK &&
        caller_body->as.block.count == item_count + 2U) {
        const Stmt *call_statement = caller_body->as.block.items[item_count];
        if (call_statement->kind == STMT_EXPR)
            call = call_statement->as.expression;
    }
    bool valid = parsed && diagnostics.count == 0U &&
        module.import_count == item_count &&
        module.count == item_count + 3U &&
        wide != NULL && wide->as.structure.field_count == item_count &&
        target != NULL && target->as.function.param_count == item_count &&
        call != NULL && call->kind == EXPR_CALL &&
        call->as.call.arguments.count == item_count;

    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    return valid ? 0 : 2;
}
