#include "internal.h"

#include <stdbool.h>
#include <stddef.h>

static void scan_expr(const Expr *expr, bool *saw_if, bool *saw_for,
                      bool *saw_match, bool *saw_element_block);

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
    if (ok) {
        for (size_t i = 0U; i < module.count; ++i)
            if (module.decls[i]->kind == DECL_FUNCTION)
                scan_stmt(module.decls[i]->as.function.body, &saw_if,
                          &saw_for, &saw_match, &saw_element_block);
    }
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    if (!ok || !saw_if || !saw_for || !saw_element_block) return 2;

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
