#include "internal.h"

#include <stdbool.h>
#include <stddef.h>

static void scan_expr(const Expr *expr, bool *saw_if, bool *saw_for,
                      bool *saw_match, bool *saw_element_block);

static void scan_text_stmt(const Stmt *stmt, const LangSource *source,
                           bool *saw_static, bool *saw_dynamic,
                           bool *valid_spans);

static void scan_text_expr(const Expr *expr, const LangSource *source,
                           bool *saw_static, bool *saw_dynamic,
                           bool *valid_spans) {
    if (expr->kind != EXPR_ELEMENT) return;
    for (size_t i = 0U; i < expr->as.element.body_count; ++i) {
        const ElementBodyItem *item = &expr->as.element.body[i];
        if (item->is_statement) {
            scan_text_stmt(item->as.statement, source, saw_static,
                           saw_dynamic, valid_spans);
        } else if (item->is_static_text) {
            const Expr *text = item->as.expression;
            *saw_static = true;
            *valid_spans &= text->kind == EXPR_STRING &&
                text->span.file != NULL && text->span.start < text->span.end &&
                text->span.end <= source->length;
        } else if (item->as.expression->kind == EXPR_ELEMENT) {
            scan_text_expr(item->as.expression, source, saw_static,
                           saw_dynamic, valid_spans);
        } else {
            *saw_dynamic = true;
            *valid_spans &= item->as.expression->span.file != NULL &&
                item->as.expression->span.start <
                    item->as.expression->span.end &&
                item->as.expression->span.end <= source->length;
        }
    }
}

static void scan_text_stmt(const Stmt *stmt, const LangSource *source,
                           bool *saw_static, bool *saw_dynamic,
                           bool *valid_spans) {
    if (stmt->kind == STMT_IF) {
        scan_text_stmt(stmt->as.if_.then_branch, source, saw_static,
                       saw_dynamic, valid_spans);
        if (stmt->as.if_.else_branch != NULL)
            scan_text_stmt(stmt->as.if_.else_branch, source, saw_static,
                           saw_dynamic, valid_spans);
    } else if (stmt->kind == STMT_FOR) {
        scan_text_stmt(stmt->as.for_.body, source, saw_static,
                       saw_dynamic, valid_spans);
    } else if (stmt->kind == STMT_MATCH) {
        for (size_t i = 0U; i < stmt->as.match_.arm_count; ++i)
            scan_text_stmt(stmt->as.match_.arms[i].body, source, saw_static,
                           saw_dynamic, valid_spans);
    } else if (stmt->kind == STMT_BLOCK) {
        for (size_t i = 0U; i < stmt->as.block.count; ++i)
            scan_text_stmt(stmt->as.block.items[i], source, saw_static,
                           saw_dynamic, valid_spans);
    } else if (stmt->kind == STMT_EXPR) {
        scan_text_expr(stmt->as.expression, source, saw_static,
                       saw_dynamic, valid_spans);
    } else if (stmt->kind == STMT_RETURN &&
               stmt->as.return_value != NULL) {
        scan_text_expr(stmt->as.return_value, source, saw_static,
                       saw_dynamic, valid_spans);
    }
}

static void scan_stmt(const Stmt *stmt, bool *saw_if, bool *saw_for,
                      bool *saw_match, bool *saw_element_block) {
    if (stmt->kind == STMT_IF) {
        *saw_if = true;
        scan_stmt(stmt->as.if_.then_branch, saw_if, saw_for, saw_match,
                  saw_element_block);
    } else if (stmt->kind == STMT_FOR) {
        *saw_for = true;
        scan_stmt(stmt->as.for_.body, saw_if, saw_for, saw_match,
                  saw_element_block);
    } else if (stmt->kind == STMT_MATCH) {
        *saw_match = true;
        for (size_t i = 0U; i < stmt->as.match_.arm_count; ++i)
            scan_stmt(stmt->as.match_.arms[i].body, saw_if, saw_for,
                      saw_match, saw_element_block);
    } else if (stmt->kind == STMT_BLOCK) {
        for (size_t i = 0U; i < stmt->as.block.count; ++i)
            scan_stmt(stmt->as.block.items[i], saw_if, saw_for, saw_match,
                      saw_element_block);
    } else if (stmt->kind == STMT_EXPR) {
        scan_expr(stmt->as.expression, saw_if, saw_for, saw_match,
                  saw_element_block);
    } else if (stmt->kind == STMT_RETURN && stmt->as.return_value != NULL) {
        scan_expr(stmt->as.return_value, saw_if, saw_for, saw_match,
                  saw_element_block);
    }
}

static void scan_expr(const Expr *expr, bool *saw_if, bool *saw_for,
                      bool *saw_match, bool *saw_element_block) {
    if (expr->kind != EXPR_ELEMENT) return;
    for (size_t i = 0U; i < expr->as.element.body_count; ++i) {
        const ElementBodyItem *item = &expr->as.element.body[i];
        if (item->is_statement) {
            if (item->as.statement->kind == STMT_BLOCK)
                *saw_element_block = true;
            scan_stmt(item->as.statement, saw_if, saw_for, saw_match,
                      saw_element_block);
        }
        else
            scan_expr(item->as.expression, saw_if, saw_for, saw_match,
                      saw_element_block);
    }
}

int main(void) {
    LangSource source;
    if (!lang_source_load("examples/html_control.lang", &source)) return 1;
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool ok = lang_parse_module(&source, &diagnostics, &module);
    bool saw_if = false, saw_for = false, saw_match = false;
    bool saw_element_block = false;
    bool saw_static = false, saw_dynamic = false, valid_spans = true;
    if (ok) {
        for (size_t i = 0U; i < module.count; ++i)
            if (module.decls[i]->kind == DECL_FUNCTION) {
                scan_stmt(module.decls[i]->as.function.body, &saw_if,
                          &saw_for, &saw_match, &saw_element_block);
                scan_text_stmt(module.decls[i]->as.function.body, &source,
                               &saw_static, &saw_dynamic, &valid_spans);
            }
    }
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    if (!ok || !saw_if || !saw_for || !saw_element_block ||
        !saw_static || !saw_dynamic || !valid_spans)
        return 2;

    if (!lang_source_load("examples/html_match.lang", &source)) return 3;
    lang_diagnostics_init(&diagnostics);
    ok = lang_parse_module(&source, &diagnostics, &module);
    if (ok) {
        for (size_t i = 0U; i < module.count; ++i)
            if (module.decls[i]->kind == DECL_FUNCTION)
                scan_stmt(module.decls[i]->as.function.body, &saw_if,
                          &saw_for, &saw_match, &saw_element_block);
    }
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    return ok && saw_match ? 0 : 4;
}
