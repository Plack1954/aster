#include "c_backend_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct StaticCssBuffer {
    const char **ids;
    const char **texts;
    size_t *lengths;
    size_t count;
    size_t capacity;
} StaticCssBuffer;

static void collect_static_css_expr(const Expr *expr, StaticCssBuffer *css);

static void collect_static_css_stmt(const Stmt *stmt, StaticCssBuffer *css) {
    if (stmt == NULL) return;
    switch (stmt->kind) {
        case STMT_LET: collect_static_css_expr(stmt->as.let.value, css); break;
        case STMT_DESTRUCTURE:
            collect_static_css_expr(stmt->as.destructure.value, css); break;
        case STMT_EXPR: collect_static_css_expr(stmt->as.expression, css); break;
        case STMT_RETURN:
            collect_static_css_expr(stmt->as.return_value, css); break;
        case STMT_IF:
            collect_static_css_expr(stmt->as.if_.condition, css);
            collect_static_css_stmt(stmt->as.if_.then_branch, css);
            collect_static_css_stmt(stmt->as.if_.else_branch, css);
            break;
        case STMT_WHILE:
            collect_static_css_expr(stmt->as.while_.condition, css);
            collect_static_css_stmt(stmt->as.while_.body, css);
            break;
        case STMT_FOR:
            collect_static_css_expr(stmt->as.for_.iterable, css);
            collect_static_css_expr(stmt->as.for_.range_end, css);
            collect_static_css_stmt(stmt->as.for_.body, css);
            break;
        case STMT_C_FOR:
            collect_static_css_stmt(stmt->as.c_for.initializer, css);
            collect_static_css_expr(stmt->as.c_for.condition, css);
            collect_static_css_expr(stmt->as.c_for.increment, css);
            collect_static_css_stmt(stmt->as.c_for.body, css);
            break;
        case STMT_MATCH:
            collect_static_css_expr(stmt->as.match_.value, css);
            for (size_t i = 0U; i < stmt->as.match_.arm_count; ++i)
                collect_static_css_stmt(stmt->as.match_.arms[i].body, css);
            break;
        case STMT_THROW:
            collect_static_css_expr(stmt->as.throw_value, css);
            break;
        case STMT_TRY:
            collect_static_css_stmt(stmt->as.try_.body, css);
            collect_static_css_stmt(stmt->as.try_.catch_body, css);
            collect_static_css_stmt(stmt->as.try_.finally_body, css);
            break;
        case STMT_BLOCK:
            for (size_t i = 0U; i < stmt->as.block.count; ++i)
                collect_static_css_stmt(stmt->as.block.items[i], css);
            break;
        case STMT_UNSAFE:
            collect_static_css_stmt(stmt->as.unsafe_body, css); break;
        case STMT_BREAK: case STMT_CONTINUE: break;
    }
}

static void static_css_add(StaticCssBuffer *css, const Expr *expr) {
    const char *id = expr->as.element.css_style_attribute;
    for (size_t i = 0U; i < css->count; ++i)
        if (strcmp(css->ids[i], id) == 0) return;
    if (expr->as.element.body_count == 0U ||
        expr->as.element.body[0].is_statement ||
        expr->as.element.body[0].as.expression->kind != EXPR_STRING)
        return;
    if (css->count == css->capacity) {
        size_t capacity = css->capacity == 0U ? 4U : css->capacity * 2U;
        css->ids = realloc(css->ids, capacity * sizeof(*css->ids));
        css->texts = realloc(css->texts, capacity * sizeof(*css->texts));
        css->lengths = realloc(css->lengths, capacity * sizeof(*css->lengths));
        if (css->ids == NULL || css->texts == NULL || css->lengths == NULL) {
            fputs("fatal: out of memory\n", stderr);
            exit(2);
        }
        css->capacity = capacity;
    }
    const Expr *text = expr->as.element.body[0].as.expression;
    css->ids[css->count] = id;
    css->texts[css->count] = text->as.string.data;
    css->lengths[css->count++] = text->as.string.length;
}

static void collect_static_css_expr(const Expr *expr, StaticCssBuffer *css) {
    if (expr == NULL) return;
    if (expr->kind == EXPR_ELEMENT) {
        if (expr->as.element.css_style_attribute != NULL)
            static_css_add(css, expr);
        for (size_t i = 0U; i < expr->as.element.property_count; ++i)
            collect_static_css_expr(expr->as.element.properties[i].value, css);
        for (size_t i = 0U; i < expr->as.element.body_count; ++i)
            if (expr->as.element.body[i].is_statement)
                collect_static_css_stmt(
                    expr->as.element.body[i].as.statement, css);
            else
                collect_static_css_expr(
                    expr->as.element.body[i].as.expression, css);
        return;
    }
    switch (expr->kind) {
        case EXPR_INTERPOLATION:
            for (size_t i = 0U; i < expr->as.interpolation.part_count; ++i)
                collect_static_css_expr(
                    expr->as.interpolation.parts[i].expression, css);
            break;
        case EXPR_BINARY:
            collect_static_css_expr(expr->as.binary.left, css);
            collect_static_css_expr(expr->as.binary.right, css); break;
        case EXPR_UNARY: collect_static_css_expr(expr->as.unary.operand, css); break;
        case EXPR_CALL:
            collect_static_css_expr(expr->as.call.callee, css);
            for (size_t i = 0U; i < expr->as.call.arguments.count; ++i)
                collect_static_css_expr(expr->as.call.arguments.items[i], css);
            break;
        case EXPR_ASSIGN:
            collect_static_css_expr(expr->as.assign.target, css);
            collect_static_css_expr(expr->as.assign.value, css); break;
        case EXPR_CLONE: collect_static_css_expr(expr->as.clone.value, css); break;
        case EXPR_TRY: collect_static_css_expr(expr->as.try_.value, css); break;
        case EXPR_AWAIT: collect_static_css_expr(expr->as.try_.value, css); break;
        case EXPR_CAST: collect_static_css_expr(expr->as.cast.value, css); break;
        case EXPR_ARRAY:
            for (size_t i = 0U; i < expr->as.array.count; ++i)
                collect_static_css_expr(expr->as.array.items[i], css);
            break;
        case EXPR_INDEX:
            collect_static_css_expr(expr->as.index.object, css);
            collect_static_css_expr(expr->as.index.index, css); break;
        case EXPR_FIELD: collect_static_css_expr(expr->as.field.object, css); break;
        case EXPR_STRUCT:
            for (size_t i = 0U; i < expr->as.structure.field_count; ++i)
                collect_static_css_expr(expr->as.structure.fields[i].value, css);
            break;
        case EXPR_IF:
            collect_static_css_expr(expr->as.if_.condition, css);
            collect_static_css_stmt(expr->as.if_.then_branch, css);
            collect_static_css_stmt(expr->as.if_.else_branch, css); break;
        case EXPR_MATCH:
            collect_static_css_expr(expr->as.match_.value, css);
            for (size_t i = 0U; i < expr->as.match_.arm_count; ++i)
                collect_static_css_stmt(expr->as.match_.arms[i].body, css);
            break;
        case EXPR_INT: case EXPR_FLOAT: case EXPR_STRING: case EXPR_BOOL:
        case EXPR_NULL: case EXPR_NAME: case EXPR_ELEMENT: break;
    }
}

static uint64_t static_css_hash(uint64_t hash, const char *text, size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        hash ^= (unsigned char)text[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

char *c_backend_emit_static_css_asset(CEmitter *emitter,
                                      const char *directory) {
    StaticCssBuffer css = {0};
    for (size_t f = 0U; f < emitter->ir->function_count; ++f) {
        const IrFunction *function = &emitter->ir->functions[f];
        if (!emitter->reachable_functions[f] || function->declaration == NULL ||
            function->declaration->kind != DECL_FUNCTION)
            continue;
        collect_static_css_stmt(function->declaration->as.function.body, &css);
    }
    if (css.count == 0U) {
        free(css.ids); free(css.texts); free(css.lengths);
        return NULL;
    }
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0U; i < css.count; ++i) {
        hash = static_css_hash(hash, css.texts[i], css.lengths[i]);
        hash = static_css_hash(hash, "\n", 1U);
    }
    char basename[64];
    (void)snprintf(basename, sizeof(basename),
                   "site-%016" PRIx64 ".css", hash);
    size_t path_length = strlen(directory) + strlen(basename) + 2U;
    char *path = malloc(path_length);
    if (path == NULL) { fputs("fatal: out of memory\n", stderr); exit(2); }
    (void)snprintf(path, path_length, "%s/%s", directory, basename);
    FILE *asset = fopen(path, "wb");
    if (asset == NULL) {
        lang_diag(emitter->diagnostics, (LangSpan){NULL, 0U, 0U},
                  "cannot write extracted stylesheet `%s`: %s",
                  path, strerror(errno));
        emitter->failed = true;
    } else {
        for (size_t i = 0U; i < css.count; ++i) {
            if (css.lengths[i] != 0U)
                (void)fwrite(css.texts[i], 1U, css.lengths[i], asset);
            fputc('\n', asset);
        }
        if (fclose(asset) != 0) emitter->failed = true;
    }
    free(path);
    free(css.ids); free(css.texts); free(css.lengths);
    size_t href_length = strlen("/assets/") + strlen(basename) + 1U;
    char *href = malloc(href_length);
    if (href == NULL) { fputs("fatal: out of memory\n", stderr); exit(2); }
    (void)snprintf(href, href_length, "/assets/%s", basename);
    return href;
}
