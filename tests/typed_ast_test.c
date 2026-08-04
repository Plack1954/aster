#include "internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

int main(void) {
    LangSource source;
    if (!lang_source_load("tests/typed_ast_test.lang", &source))
        return 1;
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool ok = lang_parse_module(&source, &diagnostics, &module);
    if (ok) ok = lang_check_module(&module, &diagnostics);
    const Decl *increment = NULL;
    const Expr *call = NULL;
    bool local_bindings_resolved = false;
    bool cleanup_plans_explicit = false;
    bool unsafe_index_retained = false;
    if (ok) {
        for (size_t i = 0U; i < module.count; ++i) {
            Decl *decl = module.decls[i];
            if (decl->kind != DECL_FUNCTION) continue;
            if (strcmp(decl->as.function.name, "increment") == 0)
                increment = decl;
            if (strcmp(decl->as.function.name, "CleanupPlan") == 0) {
                Stmt *body = decl->as.function.body;
                if (body->kind == STMT_BLOCK &&
                    body->as.block.count == 4U) {
                    Stmt *buffer = body->as.block.items[0];
                    Stmt *if_ = body->as.block.items[1];
                    Stmt *value = body->as.block.items[2];
                    Stmt *return_ = body->as.block.items[3];
                    Stmt *early_return =
                        if_->kind == STMT_IF &&
                        if_->as.if_.then_branch->kind == STMT_BLOCK &&
                        if_->as.if_.then_branch->as.block.count == 1U
                        ? if_->as.if_.then_branch->as.block.items[0] : NULL;
                    Expr *try_ =
                        value->kind == STMT_LET
                        ? value->as.let.value : NULL;
                    size_t buffer_id =
                        buffer->kind == STMT_LET
                        ? buffer->as.let.binding_id : 0U;
                    cleanup_plans_explicit =
                        buffer_id != 0U &&
                        early_return != NULL &&
                        early_return->kind == STMT_RETURN &&
                        early_return->exit_cleanup.count == 1U &&
                        early_return->exit_cleanup.binding_ids[0] ==
                            buffer_id &&
                        try_ != NULL && try_->kind == EXPR_TRY &&
                        try_->error_cleanup.count == 1U &&
                        try_->error_cleanup.binding_ids[0] == buffer_id &&
                        return_->kind == STMT_RETURN &&
                        return_->exit_cleanup.count == 1U &&
                        return_->exit_cleanup.binding_ids[0] == buffer_id &&
                        body->exit_cleanup.count == 1U &&
                        body->exit_cleanup.binding_ids[0] == buffer_id;
                }
            }
            if (strcmp(
                    decl->as.function.name,
                    "UnsafeIndexIntent") == 0) {
                Stmt *body = decl->as.function.body;
                if (body->kind == STMT_BLOCK &&
                    body->as.block.count == 2U) {
                    Stmt *unsafe_ = body->as.block.items[1];
                    Stmt *unsafe_body =
                        unsafe_->kind == STMT_UNSAFE
                        ? unsafe_->as.unsafe_body : NULL;
                    Stmt *return_ =
                        unsafe_body != NULL &&
                        unsafe_body->kind == STMT_BLOCK &&
                        unsafe_body->as.block.count == 1U
                        ? unsafe_body->as.block.items[0] : NULL;
                    Expr *index =
                        return_ != NULL &&
                        return_->kind == STMT_RETURN
                        ? return_->as.return_value : NULL;
                    unsafe_index_retained =
                        index != NULL &&
                        index->kind == EXPR_INDEX &&
                        index->as.index.unchecked;
                }
            }
            if (strcmp(decl->as.function.name, "main") == 0) {
                Stmt *body = decl->as.function.body;
                if (body->kind == STMT_BLOCK &&
                    body->as.block.count != 0U) {
                    Stmt *statement = body->as.block.items[0];
                    if (statement->kind == STMT_EXPR &&
                        statement->as.expression->kind == EXPR_CALL) {
                        Expr *print_call = statement->as.expression;
                        if (print_call->as.call.arguments.count == 1U)
                            call = print_call->as.call.arguments.items[0];
                    }
                }
            }
        }
        if (increment != NULL) {
            const Function *function = &increment->as.function;
            Stmt *body = function->body;
            if (function->param_count == 1U &&
                body->kind == STMT_BLOCK &&
                body->as.block.count == 3U) {
                Stmt *outer_let = body->as.block.items[0];
                Stmt *nested = body->as.block.items[1];
                Stmt *return_ = body->as.block.items[2];
                if (outer_let->kind == STMT_LET &&
                    nested->kind == STMT_BLOCK &&
                    nested->as.block.count == 2U &&
                    return_->kind == STMT_RETURN) {
                    Stmt *inner_let = nested->as.block.items[0];
                    Stmt *print_stmt = nested->as.block.items[1];
                    Expr *sum = return_->as.return_value;
                    Expr *print_call =
                        print_stmt->kind == STMT_EXPR
                        ? print_stmt->as.expression : NULL;
                    Expr *inner_read =
                        print_call != NULL &&
                        print_call->kind == EXPR_CALL &&
                        print_call->as.call.arguments.count == 1U
                        ? print_call->as.call.arguments.items[0] : NULL;
                    local_bindings_resolved =
                        inner_let->kind == STMT_LET &&
                        sum != NULL && sum->kind == EXPR_BINARY &&
                        inner_read != NULL &&
                        inner_read->kind == EXPR_NAME &&
                        function->params[0].binding_id != 0U &&
                        outer_let->as.let.binding_id != 0U &&
                        inner_let->as.let.binding_id != 0U &&
                        outer_let->as.let.binding_id !=
                            inner_let->as.let.binding_id &&
                        inner_read->resolved_local_id ==
                            inner_let->as.let.binding_id &&
                        sum->as.binary.left->resolved_local_id ==
                            function->params[0].binding_id &&
                        sum->as.binary.right->resolved_local_id ==
                            outer_let->as.let.binding_id;
                }
            }
        }
    }
    bool resolved =
        call != NULL && call->kind == EXPR_CALL &&
        call->type != NULL && call->resolved_decl == increment;
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    return ok && resolved && local_bindings_resolved &&
        cleanup_plans_explicit && unsafe_index_retained ? 0 : 2;
}
