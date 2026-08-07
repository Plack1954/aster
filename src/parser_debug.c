#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void lang_module_free(Module *module) {
    lang_arena_free(&module->arena);
    memset(module, 0, sizeof(*module));
}

static void indent(int depth) {
    for (int i = 0; i < depth; ++i) fputs("  ", stdout);
}

static void print_expr(const Expr *expr, int depth);

static void print_stmt(const Stmt *stmt, int depth) {
    indent(depth);
    switch (stmt->kind) {
        case STMT_LET:
            printf("let %s\n", stmt->as.let.name);
            print_expr(stmt->as.let.value, depth + 1);
            break;
        case STMT_DESTRUCTURE:
            printf("destructure %zu\n", stmt->as.destructure.count);
            print_expr(stmt->as.destructure.value, depth + 1);
            break;
        case STMT_DELETE:
            puts("delete");
            print_expr(stmt->as.delete_value, depth + 1);
            break;
        case STMT_EXPR:
            puts("expr");
            print_expr(stmt->as.expression, depth + 1);
            break;
        case STMT_RETURN:
            puts("return");
            if (stmt->as.return_value != NULL)
                print_expr(stmt->as.return_value, depth + 1);
            break;
        case STMT_IF:
            puts("if");
            print_expr(stmt->as.if_.condition, depth + 1);
            print_stmt(stmt->as.if_.then_branch, depth + 1);
            break;
        case STMT_WHILE:
            puts("while");
            print_expr(stmt->as.while_.condition, depth + 1);
            print_stmt(stmt->as.while_.body, depth + 1);
            break;
        case STMT_FOR:
            printf("%s %s%s\n",
                   stmt->as.for_.foreach ? "foreach" : "for",
                   stmt->as.for_.name,
                   stmt->as.for_.range_end != NULL ? " range" : "");
            print_expr(stmt->as.for_.iterable, depth + 1);
            if (stmt->as.for_.range_end != NULL)
                print_expr(stmt->as.for_.range_end, depth + 1);
            print_stmt(stmt->as.for_.body, depth + 1);
            break;
        case STMT_C_FOR:
            puts("for");
            if (stmt->as.c_for.initializer != NULL)
                print_stmt(stmt->as.c_for.initializer, depth + 1);
            if (stmt->as.c_for.condition != NULL)
                print_expr(stmt->as.c_for.condition, depth + 1);
            if (stmt->as.c_for.increment != NULL)
                print_expr(stmt->as.c_for.increment, depth + 1);
            print_stmt(stmt->as.c_for.body, depth + 1);
            break;
        case STMT_MATCH:
            puts("switch");
            print_expr(stmt->as.match_.value, depth + 1);
            for (size_t i = 0U; i < stmt->as.match_.arm_count; ++i) {
                indent(depth + 1);
                printf("arm %s%s%s\n", stmt->as.match_.arms[i].variant,
                       stmt->as.match_.arms[i].binding != NULL ? " as " : "",
                       stmt->as.match_.arms[i].binding != NULL
                           ? stmt->as.match_.arms[i].binding : "");
                print_stmt(stmt->as.match_.arms[i].body, depth + 2);
            }
            break;
        case STMT_THROW:
            puts("throw");
            if (stmt->as.throw_value != NULL)
                print_expr(stmt->as.throw_value, depth + 1);
            break;
        case STMT_TRY:
            puts("try");
            print_stmt(stmt->as.try_.body, depth + 1);
            if (stmt->as.try_.catch_body != NULL) {
                indent(depth);
                printf("catch %s %s\n", stmt->as.try_.catch_type_name,
                       stmt->as.try_.catch_name);
                print_stmt(stmt->as.try_.catch_body, depth + 1);
            }
            if (stmt->as.try_.finally_body != NULL) {
                indent(depth);
                puts("finally");
                print_stmt(stmt->as.try_.finally_body, depth + 1);
            }
            break;
        case STMT_BREAK:
            puts("break");
            break;
        case STMT_CONTINUE:
            puts("continue");
            break;
        case STMT_BLOCK:
            puts("block");
            for (size_t i = 0U; i < stmt->as.block.count; ++i)
                print_stmt(stmt->as.block.items[i], depth + 1);
            break;
        case STMT_UNSAFE:
            puts("unsafe");
            print_stmt(stmt->as.unsafe_body, depth + 1);
            break;
    }
}

static void print_expr(const Expr *expr, int depth) {
    indent(depth);
    switch (expr->kind) {
        case EXPR_INT:
            printf("int %" PRIu64 "\n", expr->as.integer);
            break;
        case EXPR_FLOAT:
            printf("float %g\n", expr->as.floating);
            break;
        case EXPR_STRING:
            printf("string length=%zu\n", expr->as.string.length);
            break;
        case EXPR_INTERPOLATION:
            printf("interpolation parts=%zu\n",
                   expr->as.interpolation.part_count);
            for (size_t i = 0U;
                 i < expr->as.interpolation.part_count; ++i) {
                const InterpolationPart *part =
                    &expr->as.interpolation.parts[i];
                if (part->expression != NULL) {
                    print_expr(part->expression, depth + 1);
                } else {
                    indent(depth + 1);
                    printf("text length=%zu\n", part->text_length);
                }
            }
            break;
        case EXPR_BOOL:
            printf("bool %s\n", expr->as.boolean ? "true" : "false");
            break;
        case EXPR_NULL:
            puts("null");
            break;
        case EXPR_NAME:
            printf("name %s\n", expr->as.name);
            break;
        case EXPR_BINARY:
            printf("binary %s\n", lang_token_name(expr->as.binary.op));
            print_expr(expr->as.binary.left, depth + 1);
            print_expr(expr->as.binary.right, depth + 1);
            break;
        case EXPR_UNARY:
            printf("unary %s\n", lang_token_name(expr->as.unary.op));
            print_expr(expr->as.unary.operand, depth + 1);
            break;
        case EXPR_CALL:
            puts("call");
            print_expr(expr->as.call.callee, depth + 1);
            break;
        case EXPR_ASSIGN:
            puts("assign");
            print_expr(expr->as.assign.target, depth + 1);
            break;
        case EXPR_CLONE:
            puts("clone");
            print_expr(expr->as.clone.value, depth + 1);
            break;
        case EXPR_TRY:
            puts("try");
            print_expr(expr->as.try_.value, depth + 1);
            break;
        case EXPR_AWAIT:
            puts("await");
            print_expr(expr->as.try_.value, depth + 1);
            break;
        case EXPR_CAST:
            printf("cast %s\n", expr->as.cast.type_name);
            print_expr(expr->as.cast.value, depth + 1);
            break;
        case EXPR_ARRAY:
            printf("array [%zu]\n", expr->as.array.count);
            break;
        case EXPR_INDEX:
            puts("index");
            break;
        case EXPR_FIELD:
            printf("field .%s\n", expr->as.field.field);
            break;
        case EXPR_STRUCT:
            printf("construct %s\n", expr->as.structure.name);
            break;
        case EXPR_ELEMENT:
            printf("element <%s> body=%zu\n", expr->as.element.name,
                   expr->as.element.body_count);
            for (size_t i = 0U; i < expr->as.element.body_count; ++i) {
                if (expr->as.element.body[i].is_statement)
                    print_stmt(
                        expr->as.element.body[i].as.statement, depth + 1);
                else
                    print_expr(
                        expr->as.element.body[i].as.expression, depth + 1);
            }
            break;
        case EXPR_IF:
            puts("if expression");
            print_expr(expr->as.if_.condition, depth + 1);
            print_stmt(expr->as.if_.then_branch, depth + 1);
            print_stmt(expr->as.if_.else_branch, depth + 1);
            break;
        case EXPR_MATCH:
            puts("switch expression");
            print_expr(expr->as.match_.value, depth + 1);
            for (size_t i = 0U; i < expr->as.match_.arm_count; ++i)
                print_stmt(expr->as.match_.arms[i].body, depth + 1);
            break;
    }
}

void lang_dump_ast(const Module *module) {
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *decl = module->decls[i];
        if (decl->kind == DECL_FUNCTION) {
            const Function *function = &decl->as.function;
            bool is_main = decl->type_param_count == 0U &&
                !function->is_extern &&
                strcmp(function->name, "main") == 0;
            const char *visibility =
                function->is_drop || is_main
                    ? ""
                    : decl->is_public ? "public " : "private ";
            printf("%s%s%s %s", visibility,
                   function->is_extern ? "extern " : "",
                   function->return_type, function->name);
            if (decl->type_param_count != 0U) {
                putchar('<');
                for (size_t parameter = 0U;
                     parameter < decl->type_param_count; ++parameter)
                    printf("%s%s", parameter == 0U ? "" : ",",
                           decl->type_params[parameter]);
                putchar('>');
            }
            putchar('\n');
            if (function->body != NULL) print_stmt(function->body, 1);
        } else if (decl->kind == DECL_STRUCT || decl->kind == DECL_CLASS) {
            printf("%s %s", decl->kind == DECL_CLASS ? "class" : "struct",
                   decl->as.structure.name);
            for (size_t parameter = 0U;
                 parameter < decl->type_param_count; ++parameter)
                printf("%s%s", parameter == 0U ? "<" : ",",
                       decl->type_params[parameter]);
            puts(decl->type_param_count == 0U ? "" : ">");
        } else if (decl->kind == DECL_ENUM) {
            printf("%s %s",
                   decl->as.enumeration.is_union ? "union" : "enum",
                   decl->as.enumeration.name);
            for (size_t parameter = 0U;
                 parameter < decl->type_param_count; ++parameter)
                printf("%s%s", parameter == 0U ? "<" : ",",
                       decl->type_params[parameter]);
            puts(decl->type_param_count == 0U ? "" : ">");
        } else if (decl->kind == DECL_ALIAS) {
            printf("type %s = %s\n", decl->as.alias.name,
                   decl->as.alias.target);
        } else {
            printf("element %s -> %s\n", decl->as.element.name,
                   decl->as.element.result_type);
        }
    }
}
