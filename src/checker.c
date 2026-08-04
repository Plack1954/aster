#include "checker_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Local *find_local(Checker *checker, const char *name) {
    for (size_t i = checker->local_count; i > 0U; --i)
        if (strcmp(checker->locals[i - 1U].name, name) == 0)
            return &checker->locals[i - 1U];
    return NULL;
}

Function *find_function(Checker *checker, const char *name,
                               LangSpan use_span) {
    Function *imported = NULL;
    const Decl *first_import = NULL;
    for (size_t i = 0U; i < checker->module->count; ++i) {
        Decl *decl = checker->module->decls[i];
        if (decl->kind != DECL_FUNCTION)
            continue;
        if (checker->current_module != NULL &&
            decl->module_name != NULL &&
            strcmp(checker->current_module, decl->module_name) == 0 &&
            strcmp(name, decl->as.function.name) == 0)
            return &decl->as.function;
        if (!decl->is_public ||
            !imported_declaration_matches(
                checker, name, decl->as.function.name,
                decl->module_name))
            continue;
        if (imported != NULL) {
            LangDiagnostic *diagnostic =
                lang_diag(checker->diagnostics, use_span,
                          "ambiguous imported function `%s`", name);
            if (first_import != NULL)
                lang_diag_secondary(diagnostic, first_import->span,
                                    "first public candidate");
            lang_diag_secondary(diagnostic, decl->span,
                                "another public candidate");
            lang_diag_help(
                diagnostic,
                "qualify the declaration or use a namespace alias");
            return imported;
        }
        imported = &decl->as.function;
        first_import = decl;
    }
    return imported;
}

const Decl *function_declaration(const Checker *checker,
                                        const Function *function) {
    if (function == NULL) return NULL;
    for (size_t i = 0U; i < checker->module->count; ++i)
        if (checker->module->decls[i]->kind == DECL_FUNCTION &&
            &checker->module->decls[i]->as.function == function)
            return checker->module->decls[i];
    return NULL;
}

const char *function_module_name(const Checker *checker,
                                        const Function *function) {
    const Decl *decl = function_declaration(checker, function);
    return decl != NULL ? decl->module_name : checker->current_module;
}

Type *resolve_type_in_module(Checker *checker, const char *name,
                                    LangSpan span,
                                    const char *module_name) {
    const char *previous_module = checker->current_module;
    checker->current_module = module_name;
    Type *type = resolve_type(checker, name, span);
    checker->current_module = previous_module;
    return type;
}

Type *check_expr(Checker *checker, Expr *expr);
/* Returns true when execution can continue after the statement. */
bool check_stmt(Checker *checker, Stmt *stmt);

static bool type_needs_cleanup(const Type *type) {
    return type != NULL &&
        (type->requires_cleanup || type->managed ||
         type->kind == TYPE_ARRAY ||
         type->kind == TYPE_OPTION ||
         type->kind == TYPE_RESULT ||
         type->kind == TYPE_NAMED);
}

static void set_cleanup_plan(Checker *checker, CleanupPlan *plan,
                             size_t begin) {
    size_t count = 0U;
    for (size_t i = checker->local_count; i > begin; --i) {
        const Local *local = &checker->locals[i - 1U];
        if (!local->borrowed && type_needs_cleanup(local->type))
            ++count;
    }
    if (count == 0U) return;
    plan->binding_ids = lang_arena_alloc(
        &checker->module->arena, count * sizeof(*plan->binding_ids));
    plan->count = count;
    size_t output = 0U;
    for (size_t i = checker->local_count; i > begin; --i) {
        const Local *local = &checker->locals[i - 1U];
        if (!local->borrowed && type_needs_cleanup(local->type))
            plan->binding_ids[output++] = local->id;
    }
}

Type *check_place(Checker *checker, Expr *expr) {
    if (expr->kind == EXPR_NAME) {
        Local *local = find_local(checker, expr->as.name);
        if (local == NULL) {
            lang_diag(checker->diagnostics, expr->span,
                      "unknown name `%s`", expr->as.name);
            expr->type = &type_error;
            return &type_error;
        }
        expr->type = local->type;
        expr->resolved_local_id = local->id;
        return local->type;
    }
    if (expr->kind == EXPR_FIELD) {
        Type *object = check_place(checker, expr->as.field.object);
        Type *result = &type_error;
        if (object->kind == TYPE_BUFFER &&
            strcmp(expr->as.field.field, "len") == 0) {
            result = &type_i64;
        } else if (object->kind == TYPE_NAMED) {
            const Decl *decl = object->declaration;
            if (decl != NULL && decl->kind == DECL_STRUCT) {
                const char *previous_module = checker->current_module;
                checker->current_module = decl->module_name;
                for (size_t field = 0U;
                     field < decl->as.structure.field_count; ++field)
                    if (strcmp(
                            decl->as.structure.fields[field].name,
                            expr->as.field.field) == 0)
                        result = resolve_type_in_applied_declaration(
                            checker, object,
                            decl->as.structure.fields[field].type_name,
                            decl->as.structure.fields[field].span);
                checker->current_module = previous_module;
            }
        }
        if (result == &type_error)
            lang_diag(
                checker->diagnostics, expr->span,
                "unknown field `%s` on `%s`",
                expr->as.field.field, object->name);
        expr->type = result;
        return result;
    }
    return check_expr(checker, expr);
}

static Expr *value_block_tail(Checker *checker, Stmt *block,
                              const char *context) {
    if (block == NULL || block->kind != STMT_BLOCK ||
        block->as.block.count == 0U) {
        lang_diag(checker->diagnostics,
                  block != NULL ? block->span : (LangSpan){0},
                  "%s must end with a value expression", context);
        return NULL;
    }
    Stmt *tail = block->as.block.items[block->as.block.count - 1U];
    if (tail->kind != STMT_EXPR || tail->expression_terminated) {
        lang_diag(checker->diagnostics, tail->span,
                  "%s must end with an expression without `;`", context);
        return NULL;
    }
    return tail->as.expression;
}

static Type *check_if_expression(Checker *checker, Expr *expr) {
    Stmt statement = {
        .kind=STMT_IF,
        .span=expr->span,
        .as.if_={
            expr->as.if_.condition,
            expr->as.if_.then_branch,
            expr->as.if_.else_branch
        }
    };
    (void)check_stmt(checker, &statement);
    Expr *then_value = value_block_tail(
        checker, expr->as.if_.then_branch, "`if` expression branch");
    Expr *else_value = value_block_tail(
        checker, expr->as.if_.else_branch, "`else` expression branch");
    if (then_value == NULL || else_value == NULL)
        return &type_error;
    Type *result = then_value->type;
    if (checker->expected_type != NULL) {
        if (coerce_literal(checker, then_value, checker->expected_type))
            result = checker->expected_type;
        (void)coerce_literal(checker, else_value, checker->expected_type);
    } else if (coerce_literal(checker, else_value, result)) {
        /* The first branch determines an otherwise unconstrained literal. */
    } else if (coerce_literal(checker, then_value, else_value->type)) {
        result = else_value->type;
    }
    if (!same_type(result, else_value->type) &&
        else_value->type->kind != TYPE_NEVER &&
        result->kind != TYPE_NEVER)
        lang_diag(checker->diagnostics, expr->span,
                  "`if` expression branches produce `%s` and `%s`",
                  result->name, else_value->type->name);
    if (result->kind == TYPE_NEVER)
        result = else_value->type;
    return result;
}

static Type *check_match_expression(Checker *checker, Expr *expr) {
    Stmt statement = {
        .kind=STMT_MATCH,
        .span=expr->span,
        .as.match_={
            expr->as.match_.value,
            expr->as.match_.arms,
            expr->as.match_.arm_count
        }
    };
    (void)check_stmt(checker, &statement);
    Type *result = NULL;
    for (size_t i = 0U; i < expr->as.match_.arm_count; ++i) {
        Expr *value = value_block_tail(
            checker, expr->as.match_.arms[i].body,
            "`switch` expression case");
        if (value == NULL) continue;
        Type *arm_type = value->type;
        if (checker->expected_type != NULL &&
            coerce_literal(checker, value, checker->expected_type))
            arm_type = checker->expected_type;
        if (arm_type->kind == TYPE_NEVER) continue;
        if (result == NULL) {
            result = arm_type;
        } else if (coerce_literal(checker, value, result)) {
            arm_type = result;
        } else if (!same_type(result, arm_type)) {
            lang_diag(checker->diagnostics, value->span,
                      "`switch` expression case produces `%s`; expected `%s`",
                      arm_type->name, result->name);
        }
    }
    return result != NULL ? result : &type_never;
}

static void validate_compound_assignment(
    Checker *checker, const Expr *expr, Type *type
) {
    TokenKind op = expr->as.assign.compound_op;
    if (op == TOK_ERROR || type == &type_error) return;
    if (op == TOK_AMP || op == TOK_PIPE || op == TOK_CARET ||
        op == TOK_SHIFT_LEFT || op == TOK_SHIFT_RIGHT) {
        bool plain_enum =
            type->kind == TYPE_NAMED && type->declaration != NULL &&
            type->declaration->kind == DECL_ENUM &&
            !type->declaration->as.enumeration.is_union;
        bool enum_bitwise = plain_enum &&
            (op == TOK_AMP || op == TOK_PIPE || op == TOK_CARET);
        if (!is_integer(type) && !enum_bitwise)
            lang_diag(checker->diagnostics, expr->span,
                      "compound bitwise assignment requires an integer or enum place");
    } else if (op == TOK_PERCENT) {
        if (!is_integer(type))
            lang_diag(checker->diagnostics, expr->span,
                      "compound remainder assignment requires an integer place");
    } else if (!is_numeric(type)) {
        lang_diag(checker->diagnostics, expr->span,
                  "compound arithmetic assignment requires a numeric place");
    }
}

static Type *rewrite_builtin_call(
    Checker *checker, Expr *expr, const char *name,
    Expr *first, Expr *second) {
    Expr *callee = lang_arena_alloc(
        &checker->module->arena, sizeof(*callee));
    memset(callee, 0, sizeof(*callee));
    callee->kind = EXPR_NAME;
    callee->span = expr->span;
    callee->as.name = name;
    Expr **arguments = lang_arena_alloc(
        &checker->module->arena,
        (second == NULL ? 1U : 2U) * sizeof(*arguments));
    arguments[0] = first;
    if (second != NULL) arguments[1] = second;
    expr->kind = EXPR_CALL;
    expr->as.call.callee = callee;
    expr->as.call.arguments.items = arguments;
    expr->as.call.arguments.count = second == NULL ? 1U : 2U;
    expr->as.call.ref_argument_mask = 0U;
    expr->as.call.out_argument_mask = 0U;
    expr->as.call.implicit_enum_value = false;
    return checker_check_call(checker, expr);
}

static Type *rewrite_zero_argument_builtin_call(
    Checker *checker, Expr *expr, const char *name) {
    Expr *callee = lang_arena_alloc(
        &checker->module->arena, sizeof(*callee));
    memset(callee, 0, sizeof(*callee));
    callee->kind = EXPR_NAME;
    callee->span = expr->span;
    callee->as.name = name;
    expr->kind = EXPR_CALL;
    expr->as.call.callee = callee;
    expr->as.call.arguments.items = NULL;
    expr->as.call.arguments.count = 0U;
    expr->as.call.ref_argument_mask = 0U;
    expr->as.call.out_argument_mask = 0U;
    expr->as.call.implicit_enum_value = false;
    return checker_check_call(checker, expr);
}

Type *check_expr(Checker *checker, Expr *expr) {
    Type *result = &type_error;
    switch (expr->kind) {
        case EXPR_INT: result = &type_i64; break;
        case EXPR_FLOAT: result = &type_f64; break;
        case EXPR_STRING: result = &type_string; break;
        case EXPR_INTERPOLATION:
            for (size_t i = 0U;
                 i < expr->as.interpolation.part_count; ++i) {
                InterpolationPart *part =
                    &expr->as.interpolation.parts[i];
                if (part->expression == NULL)
                    continue;
                bool borrowable_place =
                    checker->html_interpolation_destination &&
                    (part->expression->kind == EXPR_NAME ||
                     (part->expression->kind == EXPR_FIELD &&
                      part->expression->as.field.object->kind ==
                          EXPR_NAME));
                Type *value = borrowable_place
                    ? check_place(
                          checker, part->expression)
                    : check_expr(
                          checker, part->expression);
                part->borrow_owned_string =
                    borrowable_place &&
                    value->kind == TYPE_STRING;
                if (value->kind != TYPE_STR &&
                    value->kind != TYPE_STRING &&
                    value->kind != TYPE_BOOL &&
                    value->kind != TYPE_CHAR &&
                    !is_numeric(value) &&
                    value->kind != TYPE_ERROR)
                    lang_diag(
                        checker->diagnostics,
                        part->expression->span,
                        "interpolation supports text, bool, char, and numeric values; found `%s`",
                        type_display_name(checker, value));
            }
            result = &type_string;
            break;
        case EXPR_BOOL: result = &type_bool; break;
        case EXPR_NULL:
            if (checker->expected_type != NULL &&
                checker->expected_type->kind == TYPE_RAW_POINTER)
                result = checker->expected_type;
            else if (checker->expected_type != NULL &&
                     checker->expected_type->kind == TYPE_OPTION) {
                Expr *callee = lang_arena_alloc(
                    &checker->module->arena, sizeof(*callee));
                memset(callee, 0, sizeof(*callee));
                callee->kind = EXPR_NAME;
                callee->span = expr->span;
                callee->as.name = "Option::None";
                expr->kind = EXPR_CALL;
                expr->as.call.callee = callee;
                expr->as.call.arguments.items = NULL;
                expr->as.call.arguments.count = 0U;
                expr->as.call.implicit_enum_value = true;
                result = check_expr(checker, expr);
                goto checked_expression;
            }
            else {
                lang_diag(checker->diagnostics, expr->span,
                          "`null` requires an expected nullable or raw pointer type");
                result = &type_error;
            }
            break;
        case EXPR_NAME: result = checker_check_name(checker, expr); break;
        case EXPR_BINARY: {
            Type *left = check_expr(checker, expr->as.binary.left);
            Type *previous_expected = checker->expected_type;
            if (expr->as.binary.right->kind == EXPR_NULL)
                checker->expected_type = left;
            Type *right = check_expr(checker, expr->as.binary.right);
            checker->expected_type = previous_expected;
            TokenKind op = expr->as.binary.op;
            if (op == TOK_AND_AND || op == TOK_OR_OR) {
                if (left->kind != TYPE_BOOL || right->kind != TYPE_BOOL)
                    lang_diag(
                        checker->diagnostics, expr->span,
                        "logical operands must be `bool`; found `%s` and `%s`",
                        left->name, right->name);
                result = &type_bool;
                break;
            }
            (void)coerce_literal(checker, expr->as.binary.left, left);
            (void)coerce_literal(checker, expr->as.binary.right, right);
            if (!same_type(left, right)) {
                if (coerce_literal(checker, expr->as.binary.right, left))
                    right = left;
                else if (coerce_literal(checker, expr->as.binary.left, right))
                    left = right;
            }
            if (!same_type(left, right))
                lang_diag(checker->diagnostics, expr->span,
                          "binary operands must have matching types; found `%s` and `%s`",
                          left->name, right->name);
            bool bitwise =
                op == TOK_AMP || op == TOK_PIPE || op == TOK_CARET;
            bool plain_enum =
                left->kind == TYPE_NAMED && left->declaration != NULL &&
                left->declaration->kind == DECL_ENUM &&
                !left->declaration->as.enumeration.is_union;
            result = (op == TOK_EQUAL_EQUAL || op == TOK_BANG_EQUAL ||
                      op == TOK_LESS || op == TOK_LESS_EQUAL ||
                      op == TOK_GREATER || op == TOK_GREATER_EQUAL)
                   ? &type_bool : left;
            if (bitwise && !is_integer(left) && !plain_enum)
                lang_diag(checker->diagnostics, expr->span,
                          "bitwise operators require integer or enum operands");
            else if (result != &type_bool && !is_numeric(left) &&
                     !(bitwise && plain_enum))
                lang_diag(checker->diagnostics, expr->span,
                          "arithmetic requires numeric operands");
            if ((op == TOK_SHIFT_LEFT || op == TOK_SHIFT_RIGHT) &&
                !is_integer(left))
                lang_diag(checker->diagnostics, expr->span,
                          "shift operands must be integers");
            if ((op == TOK_LESS || op == TOK_LESS_EQUAL ||
                 op == TOK_GREATER || op == TOK_GREATER_EQUAL) &&
                !is_numeric(left))
                lang_diag(checker->diagnostics, expr->span,
                          "ordering comparisons require numeric operands");
            if (op == TOK_PERCENT && is_float(left))
                lang_diag(checker->diagnostics, expr->span,
                          "remainder requires integer operands");
            break;
        }
        case EXPR_UNARY:
            result = check_expr(checker, expr->as.unary.operand);
            if (expr->as.unary.op == TOK_STAR) {
                if (checker->unsafe_depth == 0U)
                    lang_diag(checker->diagnostics, expr->span,
                              "pointer dereference requires an unsafe block");
                if (result->kind != TYPE_RAW_POINTER ||
                    result->element == NULL) {
                    lang_diag(checker->diagnostics, expr->span,
                              "dereference requires a raw pointer");
                    result = &type_error;
                } else {
                    result = result->element;
                    if (result->kind != TYPE_I64)
                        lang_diag(checker->diagnostics, expr->span,
                                  "pointer dereference currently supports `long*`");
                }
            } else if (expr->as.unary.op == TOK_BANG) {
                if (result->kind != TYPE_BOOL)
                    lang_diag(checker->diagnostics, expr->span,
                              "logical negation requires `bool`");
                result = &type_bool;
            } else if (expr->as.unary.op == TOK_TILDE) {
                bool plain_enum =
                    result->kind == TYPE_NAMED &&
                    result->declaration != NULL &&
                    result->declaration->kind == DECL_ENUM &&
                    !result->declaration->as.enumeration.is_union;
                if (!is_integer(result) && !plain_enum)
                    lang_diag(checker->diagnostics, expr->span,
                              "bitwise complement requires an integer or enum operand");
            } else if (!is_numeric(result)) {
                lang_diag(checker->diagnostics, expr->span,
                          "numeric negation requires a numeric operand");
            } else if (is_unsigned_integer(result)) {
                lang_diag(checker->diagnostics, expr->span,
                          "cannot negate unsigned type `%s`", result->name);
            }
            break;
        case EXPR_CALL:
            result = checker_check_call(checker, expr);
            set_cleanup_plan(
                checker, &expr->error_cleanup,
                checker->exception_depth != 0U
                    ? checker->exception_local_bases[
                        checker->exception_depth - 1U]
                    : 0U);
            break;
        case EXPR_ASSIGN: {
            if (expr->as.assign.compound_op == TOK_ERROR &&
                expr->as.assign.target->kind == EXPR_FIELD &&
                strcmp(expr->as.assign.target->as.field.field,
                       "Capacity") == 0 &&
                expr->as.assign.target->as.field.object->kind == EXPR_NAME) {
                Expr *object = expr->as.assign.target->as.field.object;
                Local *local = find_local(checker, object->as.name);
                if (local != NULL && local->type->kind == TYPE_VEC) {
                    Expr *value = expr->as.assign.value;
                    Expr *callee = lang_arena_alloc(
                        &checker->module->arena, sizeof(*callee));
                    Expr **arguments = lang_arena_alloc(
                        &checker->module->arena, 2U * sizeof(*arguments));
                    callee->kind = EXPR_NAME;
                    callee->span = expr->span;
                    callee->as.name = "List::SetCapacity";
                    arguments[0] = object;
                    arguments[1] = value;
                    expr->kind = EXPR_CALL;
                    expr->as.call.callee = callee;
                    expr->as.call.arguments.items = arguments;
                    expr->as.call.arguments.count = 2U;
                    expr->as.call.ref_argument_mask = 0U;
                    expr->as.call.out_argument_mask = 0U;
                    expr->as.call.implicit_enum_value = false;
                    result = checker_check_call(checker, expr);
                    goto checked_expression;
                }
            }
            if (expr->as.assign.target->kind == EXPR_INDEX &&
                expr->as.assign.target->as.index.object->kind == EXPR_NAME) {
                Expr *object = expr->as.assign.target->as.index.object;
                Local *local = find_local(checker, object->as.name);
                if (local != NULL &&
                    local->type->kind == TYPE_DICTIONARY &&
                    expr->as.assign.compound_op != TOK_ERROR) {
                    lang_diag(checker->diagnostics, expr->span,
                              "compound assignment to a Dictionary indexer is not yet supported");
                    result = &type_error;
                    goto checked_expression;
                }
                if (local != NULL &&
                    (local->type->kind == TYPE_VEC ||
                     local->type->kind == TYPE_DICTIONARY)) {
                    Expr *index = expr->as.assign.target->as.index.index;
                    Expr *value = expr->as.assign.value;
                    Expr *callee = lang_arena_alloc(
                        &checker->module->arena, sizeof(*callee));
                    Expr **arguments = lang_arena_alloc(
                        &checker->module->arena, 3U * sizeof(*arguments));
                    callee->kind = EXPR_NAME;
                    callee->span = expr->span;
                    callee->as.name = local->type->kind == TYPE_VEC
                        ? "List::Set" : "Dictionary::Set";
                    arguments[0] = object;
                    arguments[1] = index;
                    arguments[2] = value;
                    expr->kind = EXPR_CALL;
                    expr->as.call.callee = callee;
                    expr->as.call.arguments.items = arguments;
                    expr->as.call.arguments.count = 3U;
                    expr->as.call.ref_argument_mask = 0U;
                    expr->as.call.out_argument_mask = 0U;
                    expr->as.call.implicit_enum_value = false;
                    result = checker_check_call(checker, expr);
                    goto checked_expression;
                }
            }
            Type *assignment_expected = NULL;
            if (expr->as.assign.target->kind == EXPR_NAME) {
                Local *assignment_local = find_local(
                    checker, expr->as.assign.target->as.name);
                if (assignment_local != NULL)
                    assignment_expected = assignment_local->type;
            }
            Type *previous_expected = checker->expected_type;
            checker->expected_type = assignment_expected;
            Type *value = check_expr(checker, expr->as.assign.value);
            checker->expected_type = previous_expected;
            Expr *target = expr->as.assign.target;
            if (target->kind == EXPR_UNARY &&
                target->as.unary.op == TOK_STAR) {
                Type *pointer = check_expr(
                    checker, target->as.unary.operand);
                Type *place_type = &type_error;
                if (checker->unsafe_depth == 0U)
                    lang_diag(checker->diagnostics, target->span,
                              "pointer store requires an unsafe block");
                if (pointer->kind != TYPE_RAW_POINTER ||
                    pointer->element == NULL) {
                    lang_diag(checker->diagnostics, target->span,
                              "pointer store requires a raw pointer");
                } else {
                    place_type = pointer->element;
                    if (strncmp(pointer->name, "*mut ", 5U) != 0)
                        lang_diag(checker->diagnostics, target->span,
                                  "cannot store through a const pointer");
                    if (place_type->kind != TYPE_I64)
                        lang_diag(checker->diagnostics, target->span,
                                  "pointer store currently supports `long*`");
                }
                if (coerce_literal(
                        checker, expr->as.assign.value, place_type))
                    value = place_type;
                if (!same_type(place_type, value))
                    lang_diag(checker->diagnostics, expr->span,
                              "assignment expects `%s`, found `%s`",
                              type_display_name(checker, place_type),
                              type_display_name(checker, value));
                validate_compound_assignment(
                    checker, expr, place_type);
                target->type = place_type;
                result = &type_unit;
                break;
            }
            if (target->kind == EXPR_NAME) {
                Local *local = find_local(checker, target->as.name);
                if (local == NULL) {
                    lang_diag(checker->diagnostics, expr->span,
                              "unknown assignment target");
                    result = &type_error;
                    break;
                }
                if (coerce_literal(checker, expr->as.assign.value,
                                   local->type))
                    value = local->type;
                if (!local->mutable_)
                    lang_diag(checker->diagnostics, expr->span,
                              "cannot assign to immutable local `%s`",
                              local->name);
                if (!same_type(local->type, value))
                    lang_diag(checker->diagnostics, expr->span,
                              "assignment expects `%s`, found `%s`",
                              type_display_name(checker, local->type),
                              type_display_name(checker, value));
                validate_compound_assignment(
                    checker, expr, local->type);
                target->type = local->type;
                target->resolved_local_id = local->id;
                result = &type_unit;
                break;
            }
            Expr *object_expr =
                target->kind == EXPR_FIELD ? target->as.field.object :
                target->kind == EXPR_INDEX ? target->as.index.object : NULL;
            if (object_expr == NULL || object_expr->kind != EXPR_NAME) {
                lang_diag(checker->diagnostics, expr->span,
                          "aggregate assignment must target a direct local field or index");
                result = &type_error;
                break;
            }
            Local *local = find_local(checker, object_expr->as.name);
            if (local == NULL) {
                lang_diag(checker->diagnostics, expr->span,
                          "unknown assignment target");
                result = &type_error;
                break;
            }
            if (!local->mutable_)
                lang_diag(checker->diagnostics, expr->span,
                          "cannot mutate immutable local `%s`", local->name);
            Type *place_type = &type_error;
            if (target->kind == EXPR_FIELD) {
                if (local->type->kind != TYPE_NAMED ||
                    local->type->declaration == NULL ||
                    local->type->declaration->kind != DECL_STRUCT) {
                    lang_diag(checker->diagnostics, expr->span,
                              "field assignment requires a struct local");
                } else {
                    const Decl *structure = local->type->declaration;
                    for (size_t i = 0U;
                         i < structure->as.structure.field_count; ++i)
                        if (strcmp(
                                structure->as.structure.fields[i].name,
                                target->as.field.field) == 0)
                            place_type =
                                resolve_type_in_applied_declaration(
                                checker, local->type,
                                structure->as.structure.fields[i].type_name,
                                structure->as.structure.fields[i].span);
                    if (place_type == &type_error)
                        lang_diag(checker->diagnostics, target->span,
                                  "unknown field `%s` on `%s`",
                                  target->as.field.field,
                                  local->type->name);
                }
            } else {
                if (local->type->kind != TYPE_ARRAY) {
                    lang_diag(checker->diagnostics, expr->span,
                              "indexed assignment requires an array local");
                } else {
                    Type *index = check_expr(
                        checker, target->as.index.index);
                    if (!is_integer(index))
                        lang_diag(checker->diagnostics,
                                  target->as.index.index->span,
                                  "array index must be an integer");
                    if (target->as.index.index->kind == EXPR_INT &&
                        target->as.index.index->as.integer >=
                            local->type->array_length)
                        lang_diag(checker->diagnostics,
                                  target->as.index.index->span,
                                  "constant array index is out of bounds for length %zu",
                                  local->type->array_length);
                    place_type = local->type->element;
                }
                target->as.index.unchecked =
                    checker->unsafe_depth != 0U;
            }
            if (coerce_literal(checker, expr->as.assign.value, place_type))
                value = place_type;
            if (!same_type(place_type, value))
                lang_diag(checker->diagnostics, expr->span,
                          "assignment expects `%s`, found `%s`",
                          type_display_name(checker, place_type),
                          type_display_name(checker, value));
            validate_compound_assignment(checker, expr, place_type);
            target->type = place_type;
            object_expr->type = local->type;
            object_expr->resolved_local_id = local->id;
            result = &type_unit;
            break;
        }
        case EXPR_CLONE: {
            Expr *value = expr->as.clone.value;
            if (value->kind == EXPR_NAME) {
                Local *local = find_local(checker, value->as.name);
                if (local == NULL) {
                    lang_diag(checker->diagnostics, value->span,
                              "cannot clone unavailable value");
                    result = &type_error;
                } else {
                    value->type = local->type;
                    value->resolved_local_id = local->id;
                    result = local->type;
                    if (!type_is_copyable(checker, local->type))
                        lang_diag(checker->diagnostics, expr->span,
                                  "type `%s` does not implement clone",
                                  local->type->name);
                }
            } else {
                result = value->kind == EXPR_FIELD
                       ? check_place(checker, value)
                       : check_expr(checker, value);
                if (!type_is_copyable(checker, result))
                    lang_diag(checker->diagnostics, expr->span,
                              "type `%s` does not implement clone",
                              result->name);
            }
            break;
        }
        case EXPR_TRY: {
            Type *operand = check_expr(checker, expr->as.try_.value);
            set_cleanup_plan(checker, &expr->error_cleanup, 0U);
            if (operand->kind != TYPE_RESULT) {
                lang_diag(checker->diagnostics, expr->span,
                          "`try` expects a Result value, found `%s`",
                          operand->name);
                result = &type_error;
            } else if (checker->function->checked_return_type->kind != TYPE_RESULT) {
                lang_diag(checker->diagnostics, expr->span,
                          "`try` can only be used in a Result-returning function");
                result = operand->element;
            } else {
                Type *function_error = checker->function->checked_return_type->error_type;
                if (!same_type(function_error, operand->error_type))
                    lang_diag(checker->diagnostics, expr->span,
                              "`try` error type `%s` does not match function error type `%s`",
                              operand->error_type->name, function_error->name);
                result = operand->element;
            }
            break;
        }
        case EXPR_AWAIT: {
            Type *operand = check_expr(checker, expr->as.try_.value);
            set_cleanup_plan(
                checker, &expr->error_cleanup,
                checker->exception_depth != 0U
                    ? checker->exception_local_bases[
                        checker->exception_depth - 1U]
                    : 0U);
            if (checker->function == NULL ||
                !checker->function->is_async) {
                lang_diag(checker->diagnostics, expr->span,
                          "`await` can only be used in an async function");
            }
            if (operand->kind != TYPE_TASK) {
                lang_diag(checker->diagnostics, expr->span,
                          "`await` expects a Task value, found `%s`",
                          type_display_name(checker, operand));
                result = &type_error;
            } else {
                result = operand->element;
            }
            break;
        }
        case EXPR_CAST: {
            Type *source = check_expr(checker, expr->as.cast.value);
            Type *target = resolve_type(
                checker, expr->as.cast.type_name, expr->span);
            bool numeric_cast =
                (is_numeric(source) || source->kind == TYPE_CHAR) &&
                (is_numeric(target) || target->kind == TYPE_CHAR);
            if (!numeric_cast)
                lang_diag(checker->diagnostics, expr->span,
                          "cannot cast `%s` to `%s`",
                          source->name, target->name);
            result = target;
            break;
        }
        case EXPR_ARRAY: {
            Type *context_element =
                checker->expected_type != NULL &&
                checker->expected_type->kind == TYPE_ARRAY
                ? checker->expected_type->element : NULL;
            Type *previous_expected = checker->expected_type;
            checker->expected_type = context_element;
            Type *element = expr->as.array.count == 0U
                          ? (context_element != NULL
                             ? context_element : &type_i64)
                          : check_expr(
                                checker, expr->as.array.items[0]);
            for (size_t i = 1U; i < expr->as.array.count; ++i) {
                Type *item = check_expr(checker, expr->as.array.items[i]);
                if (!same_type(element, item))
                    lang_diag(checker->diagnostics, expr->as.array.items[i]->span,
                              "array elements must have one type");
            }
            checker->expected_type = previous_expected;
            Type *array = lang_arena_alloc(&checker->module->arena, sizeof(*array));
            array->kind = TYPE_ARRAY; array->element = element;
            array->array_length = expr->as.array.count; array->name = "array";
            array->requires_cleanup = element->requires_cleanup;
            result = array;
            break;
        }
        case EXPR_INDEX: {
            expr->as.index.unchecked = checker->unsafe_depth != 0U;
            Expr *object_expr = expr->as.index.object;
            Expr *index_expr = expr->as.index.index;
            Type *object = check_place(checker, object_expr);
            if (object->kind == TYPE_VEC) {
                result = rewrite_builtin_call(
                    checker, expr, "List::Get",
                    object_expr, index_expr);
                goto checked_expression;
            }
            if (object->kind == TYPE_DICTIONARY) {
                result = rewrite_builtin_call(
                    checker, expr, "Dictionary::Get",
                    object_expr, index_expr);
                goto checked_expression;
            }
            if (object->kind == TYPE_STRING) {
                result = rewrite_builtin_call(
                    checker, expr, "StringByteAt",
                    object_expr, index_expr);
                goto checked_expression;
            }
            if (object->kind == TYPE_NAMED &&
                object->declaration != NULL) {
                const char *owner = type_declaration_name(
                    object->declaration);
                size_t owner_length = strlen(owner);
                char *item = lang_arena_alloc(
                    &checker->module->arena,
                    owner_length + sizeof("::Item"));
                memcpy(item, owner, owner_length);
                memcpy(item + owner_length, "::Item", sizeof("::Item"));
                result = rewrite_builtin_call(
                    checker, expr, item, object_expr, index_expr);
                goto checked_expression;
            }
            Type *index = check_expr(checker, index_expr);
            if (object->kind != TYPE_ARRAY)
                lang_diag(checker->diagnostics, expr->as.index.object->span,
                          "indexing requires an array");
            if (!is_integer(index))
                lang_diag(checker->diagnostics, expr->as.index.index->span,
                          "array index must be an integer");
            if (object->kind == TYPE_ARRAY &&
                expr->as.index.index->kind == EXPR_INT &&
                expr->as.index.index->as.integer >= object->array_length)
                lang_diag(checker->diagnostics, expr->as.index.index->span,
                          "constant array index is out of bounds for length %zu",
                          object->array_length);
            result = object->kind == TYPE_ARRAY ? object->element : &type_error;
            break;
        }
        case EXPR_FIELD: {
            const char *static_name = checker_static_call_path(checker, expr);
            if (static_name != NULL) {
                if (strcmp(static_name, "string::Empty") == 0) {
                    expr->kind = EXPR_STRING;
                    expr->as.string.data = "";
                    expr->as.string.length = 0U;
                    result = &type_string;
                    break;
                }
                if (strcmp(static_name, "CancellationToken::None") == 0) {
                    result = rewrite_zero_argument_builtin_call(
                        checker, expr, "CancellationToken::None");
                    goto checked_expression;
                }
                if (strcmp(static_name, "Option::None") == 0) {
                    Expr *callee = lang_arena_alloc(
                        &checker->module->arena, sizeof(*callee));
                    callee->kind = EXPR_NAME;
                    callee->span = expr->span;
                    callee->as.name = static_name;
                    expr->kind = EXPR_CALL;
                    expr->as.call.callee = callee;
                    expr->as.call.arguments.items = NULL;
                    expr->as.call.arguments.count = 0U;
                    expr->as.call.implicit_enum_value = true;
                    result = check_expr(checker, expr);
                    goto checked_expression;
                }
                const char *separator = last_path_separator(static_name);
                if (separator != NULL) {
                    char *owner = lang_arena_strndup(
                        &checker->module->arena, static_name,
                        (size_t)(separator - static_name));
                    Decl *enumeration = find_type_declaration(
                        checker, owner, expr->span);
                    if (enumeration != NULL &&
                        enumeration->kind == DECL_ENUM) {
                        const char *variant = separator + 2U;
                        for (size_t v = 0U;
                             v < enumeration->as.enumeration.variant_count;
                             ++v) {
                            FieldDecl *candidate =
                                &enumeration->as.enumeration.variants[v];
                            if (strcmp(candidate->name, variant) != 0 ||
                                strcmp(candidate->type_name, "unit") != 0)
                                continue;
                            Expr *callee = lang_arena_alloc(
                                &checker->module->arena, sizeof(*callee));
                            callee->kind = EXPR_NAME;
                            callee->span = expr->span;
                            callee->as.name = static_name;
                            expr->kind = EXPR_CALL;
                            expr->as.call.callee = callee;
                            expr->as.call.arguments.items = NULL;
                            expr->as.call.arguments.count = 0U;
                            expr->as.call.implicit_enum_value = true;
                            result = check_expr(checker, expr);
                            goto checked_expression;
                        }
                    }
                }
                expr->kind = EXPR_NAME;
                expr->as.name = static_name;
                result = checker_check_name(checker, expr);
                break;
            }
            Type *object = check_place(checker, expr->as.field.object);
            if (object->kind == TYPE_CANCELLATION_TOKEN_SOURCE &&
                strcmp(expr->as.field.field, "Token") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "CancellationTokenSource::Token",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            if ((object->kind == TYPE_CANCELLATION_TOKEN ||
                 object->kind == TYPE_CANCELLATION_TOKEN_SOURCE) &&
                strcmp(expr->as.field.field,
                       "IsCancellationRequested") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "CancellationToken::IsCancellationRequested",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            if (object->kind == TYPE_STRING &&
                strcmp(expr->as.field.field, "Length") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "TextLen",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            if (object->kind == TYPE_VEC &&
                strcmp(expr->as.field.field, "Count") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "List::Count",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            if ((object->kind == TYPE_DICTIONARY ||
                 object->kind == TYPE_HASH_SET) &&
                strcmp(expr->as.field.field, "Count") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "Dictionary::Count",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            if (object->kind == TYPE_QUEUE &&
                strcmp(expr->as.field.field, "Count") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "Queue::Count",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            if (object->kind == TYPE_QUEUE &&
                strcmp(expr->as.field.field, "Capacity") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "Queue::Capacity",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            if (object->kind == TYPE_STACK &&
                strcmp(expr->as.field.field, "Count") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "Stack::Count",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            if (object->kind == TYPE_STACK &&
                strcmp(expr->as.field.field, "Capacity") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "Stack::Capacity",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            if ((object->kind == TYPE_DICTIONARY ||
                 object->kind == TYPE_HASH_SET) &&
                strcmp(expr->as.field.field, "Capacity") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "Dictionary::Capacity",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            if (object->kind == TYPE_VEC &&
                strcmp(expr->as.field.field, "Capacity") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "List::Capacity",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            if (object->kind == TYPE_STRING_BUILDER &&
                strcmp(expr->as.field.field, "Length") == 0) {
                result = rewrite_builtin_call(
                    checker, expr, "StringBuilder::Length",
                    expr->as.field.object, NULL);
                goto checked_expression;
            }
            result = &type_error;
            if (object->kind == TYPE_OPTION &&
                strcmp(expr->as.field.field, "Value") == 0)
                result = object->element;
            else if (object->kind == TYPE_BUFFER && strcmp(expr->as.field.field, "len") == 0)
                result = &type_i64;
            else if (object->kind == TYPE_NAMED) {
                const Decl *decl = object->declaration;
                if (decl != NULL && decl->kind == DECL_STRUCT) {
                    const char *previous_module = checker->current_module;
                    checker->current_module = decl->module_name;
                    for (size_t f = 0U; f < decl->as.structure.field_count; ++f)
                        if (strcmp(decl->as.structure.fields[f].name,
                                   expr->as.field.field) == 0)
                            result = resolve_type_in_applied_declaration(
                                checker, object,
                                decl->as.structure.fields[f].type_name,
                                decl->as.structure.fields[f].span);
                    checker->current_module = previous_module;
                }
                if (result == &type_error)
                    lang_diag(checker->diagnostics, expr->span,
                              "unknown field `%s` on `%s`",
                              expr->as.field.field, object->name);
            } else
                lang_diag(checker->diagnostics, expr->span,
                          "unknown field `%s` on `%s`", expr->as.field.field, object->name);
            break;
        }
        case EXPR_STRUCT: {
            bool cancellation_source =
                expr->as.structure.field_count == 0U &&
                ((checker->expected_type != NULL &&
                  checker->expected_type->kind ==
                      TYPE_CANCELLATION_TOKEN_SOURCE) ||
                 (expr->as.structure.name != NULL &&
                  strcmp(expr->as.structure.name,
                         "CancellationTokenSource") == 0));
            if (cancellation_source) {
                result = rewrite_zero_argument_builtin_call(
                    checker, expr, "CancellationTokenSource::New");
                goto checked_expression;
            }
            if (checker->expected_type != NULL &&
                checker->expected_type->kind == TYPE_NAMED &&
                checker->expected_type->declaration != NULL &&
                checker->expected_type->declaration->kind == DECL_STRUCT &&
                (expr->as.structure.name == NULL ||
                 visible_declaration_path_matches(
                     checker,
                     expr->as.structure.name,
                     checker->expected_type->declaration->as.structure.name,
                     checker->expected_type->declaration->module_name)))
                result = checker->expected_type;
            else if (expr->as.structure.name != NULL)
                result = resolve_type(
                    checker, expr->as.structure.name, expr->span);
            else {
                lang_diag(checker->diagnostics, expr->span,
                          "target-typed `new()` requires an expected struct type");
                result = &type_error;
            }
            Decl *structure =
                result->kind == TYPE_NAMED &&
                result->declaration != NULL &&
                result->declaration->kind == DECL_STRUCT
                ? (Decl *)result->declaration : NULL;
            expr->resolved_decl = structure;
            if (structure == NULL) {
                lang_diag(checker->diagnostics, expr->span,
                          "constructed value is not a struct type%s%s",
                          expr->as.structure.name != NULL ? ": `" : "",
                          expr->as.structure.name != NULL
                              ? expr->as.structure.name : "");
            }
            for (size_t i = 0U; i < expr->as.structure.field_count; ++i) {
                ElementProperty *field = &expr->as.structure.fields[i];
                for (size_t prior = 0U; prior < i; ++prior)
                    if (strcmp(field->name,
                               expr->as.structure.fields[prior].name) == 0)
                        lang_diag(checker->diagnostics, field->span,
                                  "duplicate field `%s`", field->name);
                FieldDecl *declared_field = NULL;
                if (structure != NULL)
                    for (size_t f = 0U;
                         f < structure->as.structure.field_count; ++f)
                        if (strcmp(structure->as.structure.fields[f].name,
                                   field->name) == 0)
                            declared_field =
                                &structure->as.structure.fields[f];
                Type *expected = declared_field != NULL
                    ? resolve_type_in_applied_declaration(
                        checker, result,
                        declared_field->type_name,
                        declared_field->span)
                    : NULL;
                Type *previous_expected = checker->expected_type;
                checker->expected_type = expected;
                Type *actual = check_expr(checker, field->value);
                checker->expected_type = previous_expected;
                if (declared_field == NULL) {
                    lang_diag(checker->diagnostics, field->span,
                              "unknown field `%s` on `%s`",
                              field->name,
                              result != NULL ? result->name : "<error>");
                } else {
                    (void)coerce_literal(checker, field->value, expected);
                    actual = field->value->type;
                    if (!same_type(expected, actual))
                        lang_diag(checker->diagnostics, field->value->span,
                                  "field `%s` expects `%s`, found `%s`",
                                  field->name, expected->name, actual->name);
                }
            }
            if (structure != NULL) {
                for (size_t f = 0U;
                     f < structure->as.structure.field_count; ++f) {
                    bool found = false;
                    for (size_t i = 0U;
                         i < expr->as.structure.field_count; ++i)
                        if (strcmp(structure->as.structure.fields[f].name,
                                   expr->as.structure.fields[i].name) == 0)
                            found = true;
                    if (!found)
                        lang_diag(checker->diagnostics, expr->span,
                                  "missing required field `%s`",
                                  structure->as.structure.fields[f].name);
                }
            }
            break;
        }
        case EXPR_ELEMENT: result = check_element(checker, expr); break;
        case EXPR_IF: result = check_if_expression(checker, expr); break;
        case EXPR_MATCH: result = check_match_expression(checker, expr); break;
    }
checked_expression:
    expr->type = result;
    return result;
}

bool check_stmt(Checker *checker, Stmt *stmt) {
    switch (stmt->kind) {
        case STMT_LET: {
            if (checker->local_count >= sizeof(checker->locals) / sizeof(checker->locals[0])) {
                lang_diag(checker->diagnostics, stmt->span, "too many local variables");
                break;
            }
            for (size_t i = checker->local_count; i > 0U; --i) {
                Local *other = &checker->locals[i - 1U];
                if (other->depth != checker->depth) break;
                if (strcmp(other->name, stmt->as.let.name) == 0)
                    lang_diag(checker->diagnostics, stmt->span,
                              "duplicate local `%s` in this scope", stmt->as.let.name);
            }
            Type *declared = stmt->as.let.type_name != NULL
                           ? resolve_type(checker, stmt->as.let.type_name,
                                          stmt->span)
                           : NULL;
            Type *previous_expected = checker->expected_type;
            checker->expected_type = declared;
            Type *value = check_expr(checker, stmt->as.let.value);
            checker->expected_type = previous_expected;
            if (declared == NULL) declared = value;
            if (coerce_literal(checker, stmt->as.let.value, declared))
                value = declared;
            if (!same_type(declared, value))
                lang_diag(checker->diagnostics, stmt->span,
                          "initializer expects `%s`, found `%s`",
                          declared->name, value->name);
            if (strcmp(stmt->as.let.name, "_") == 0)
                break;
            checker->locals[checker->local_count++] = (Local){
                stmt->as.let.name, declared, stmt->as.let.mutable_,
                false, checker->depth, stmt->span,
                ++checker->next_local_id
            };
            stmt->as.let.binding_id =
                checker->locals[checker->local_count - 1U].id;
            break;
        }
        case STMT_DESTRUCTURE: {
            Expr *value_expr = stmt->as.destructure.value;
            if (value_expr->kind != EXPR_NAME) {
                lang_diag(
                    checker->diagnostics, value_expr->span,
                    "deconstruction requires a direct local");
                (void)check_expr(checker, value_expr);
                break;
            }
            Type *aggregate = check_expr(checker, value_expr);
            if (aggregate->kind != TYPE_NAMED ||
                aggregate->declaration == NULL ||
                aggregate->declaration->kind != DECL_STRUCT) {
                lang_diag(
                    checker->diagnostics, value_expr->span,
                    "deconstruction requires a struct value, found `%s`",
                    aggregate->name);
                break;
            }
            const Decl *structure = aggregate->declaration;
            size_t field_count = structure->as.structure.field_count;
            if (stmt->as.destructure.count != field_count) {
                lang_diag(
                    checker->diagnostics, stmt->span,
                    "deconstruction of `%s` expects %zu fields, found %zu",
                    aggregate->name, field_count,
                    stmt->as.destructure.count);
                break;
            }
            stmt->as.destructure.checked_types = lang_arena_alloc(
                &checker->module->arena,
                field_count * sizeof(*stmt->as.destructure.checked_types));
            stmt->as.destructure.binding_ids = lang_arena_alloc(
                &checker->module->arena,
                field_count * sizeof(*stmt->as.destructure.binding_ids));
            for (size_t field = 0U; field < field_count; ++field) {
                if (checker->local_count >=
                    sizeof(checker->locals) / sizeof(checker->locals[0])) {
                    lang_diag(checker->diagnostics, stmt->span,
                              "too many local variables");
                    break;
                }
                Type *expected = resolve_type_in_applied_declaration(
                    checker, aggregate,
                    structure->as.structure.fields[field].type_name,
                    structure->as.structure.fields[field].span);
                Type *declared = resolve_type(
                    checker, stmt->as.destructure.type_names[field],
                    stmt->span);
                stmt->as.destructure.checked_types[field] = expected;
                if (!same_type(expected, declared))
                    lang_diag(
                        checker->diagnostics, stmt->span,
                        "deconstruction field %zu expects `%s`, found `%s`",
                        field + 1U, expected->name, declared->name);
                const char *name = stmt->as.destructure.names[field];
                for (size_t i = checker->local_count; i > 0U; --i) {
                    Local *other = &checker->locals[i - 1U];
                    if (other->depth != checker->depth) break;
                    if (strcmp(other->name, name) == 0)
                        lang_diag(
                            checker->diagnostics, stmt->span,
                            "duplicate local `%s` in this scope", name);
                }
                size_t binding_id = ++checker->next_local_id;
                checker->locals[checker->local_count++] = (Local){
                    name, expected, true, false,
                    checker->depth, stmt->span,
                    binding_id
                };
                stmt->as.destructure.binding_ids[field] = binding_id;
            }
            break;
        }
        case STMT_EXPR:
            return check_expr(checker, stmt->as.expression)->kind != TYPE_NEVER;
        case STMT_RETURN: {
            if (checker->finally_depth != 0U)
                lang_diag(checker->diagnostics, stmt->span,
                          "control cannot leave a `finally` block with `return`");
            Type *previous_expected = checker->expected_type;
            Type *declared_return =
                checker->function->checked_return_type;
            Type *logical_return =
                checker->function->is_async &&
                declared_return->kind == TYPE_TASK
                    ? declared_return->element
                    : declared_return;
            checker->expected_type = logical_return;
            Type *actual = stmt->as.return_value != NULL
                         ? check_expr(checker, stmt->as.return_value)
                         : &type_unit;
            checker->expected_type = previous_expected;
            if (stmt->as.return_value != NULL &&
                coerce_literal(checker, stmt->as.return_value,
                               logical_return))
                actual = logical_return;
            if (!same_type(logical_return, actual))
                lang_diag(checker->diagnostics, stmt->span,
                          "return expects `%s`, found `%s`",
                          logical_return->name, actual->name);
            set_cleanup_plan(checker, &stmt->exit_cleanup, 0U);
            return false;
        }
        case STMT_THROW: {
            if (stmt->as.throw_value == NULL) {
                if (checker->catch_depth == 0U) {
                    lang_diag(checker->diagnostics, stmt->span,
                              "bare `throw;` is only valid inside a `catch`");
                    return false;
                }
                Expr *caught = lang_arena_alloc(
                    &checker->module->arena, sizeof(*caught));
                caught->kind = EXPR_NAME;
                caught->span = stmt->span;
                caught->as.name = checker->catch_names[
                    checker->catch_depth - 1U];
                stmt->as.throw_value = caught;
            }
            Type *value = check_expr(checker, stmt->as.throw_value);
            if (!is_exception_type(value))
                lang_diag(checker->diagnostics, stmt->as.throw_value->span,
                          "`throw` expects an `Exception`, found `%s`",
                          value->name);
            set_cleanup_plan(
                checker, &stmt->exit_cleanup,
                checker->exception_depth != 0U
                    ? checker->exception_local_bases[
                        checker->exception_depth - 1U]
                    : 0U);
            return false;
        }
        case STMT_TRY: {
            if (checker->exception_depth >= 32U) {
                lang_diag(checker->diagnostics, stmt->span,
                          "exception-handler nesting limit exceeded");
                return true;
            }
            checker->exception_local_bases[checker->exception_depth++] =
                checker->local_count;
            bool body_falls = check_stmt(checker, stmt->as.try_.body);
            --checker->exception_depth;
            bool catch_falls = false;
            if (stmt->as.try_.catch_body != NULL) {
                Type *caught = resolve_type(
                    checker, stmt->as.try_.catch_type_name, stmt->span);
                stmt->as.try_.catch_type = caught;
                if (!is_exception_type(caught))
                    lang_diag(checker->diagnostics, stmt->span,
                              "catch type must derive from `Exception`");
                size_t base = checker->local_count;
                size_t binding = ++checker->next_local_id;
                checker->locals[checker->local_count++] = (Local){
                    stmt->as.try_.catch_name, caught, false, false,
                    checker->depth + 1U, stmt->span, binding
                };
                stmt->as.try_.catch_binding_id = binding;
                bool pushed_catch = checker->catch_depth < 32U;
                if (!pushed_catch) {
                    lang_diag(checker->diagnostics, stmt->span,
                              "catch nesting limit exceeded");
                } else {
                    checker->catch_names[checker->catch_depth++] =
                        stmt->as.try_.catch_name;
                }
                catch_falls = check_stmt(
                    checker, stmt->as.try_.catch_body);
                if (pushed_catch)
                    --checker->catch_depth;
                checker->local_count = base;
            }
            bool finally_falls = true;
            if (stmt->as.try_.finally_body != NULL) {
                ++checker->finally_depth;
                finally_falls = check_stmt(
                    checker, stmt->as.try_.finally_body);
                --checker->finally_depth;
            }
            return finally_falls && (body_falls || catch_falls);
        }
        case STMT_IF: {
            if (check_expr(checker, stmt->as.if_.condition)->kind != TYPE_BOOL)
                lang_diag(checker->diagnostics, stmt->as.if_.condition->span,
                          "`if` condition must be `bool`");
            bool then_falls = check_stmt(checker, stmt->as.if_.then_branch);
            bool else_falls = true;
            if (stmt->as.if_.else_branch != NULL)
                else_falls = check_stmt(checker, stmt->as.if_.else_branch);
            return then_falls || else_falls;
        }
        case STMT_WHILE: {
            if (check_expr(checker, stmt->as.while_.condition)->kind != TYPE_BOOL)
                lang_diag(checker->diagnostics, stmt->as.while_.condition->span,
                          "`while` condition must be `bool`");
            size_t count = checker->local_count;
            if (checker->loop_depth >=
                sizeof(checker->loop_local_bases) /
                    sizeof(checker->loop_local_bases[0])) {
                lang_diag(checker->diagnostics, stmt->span,
                          "loop nesting limit exceeded");
                break;
            }
            checker->loop_local_bases[checker->loop_depth] = count;
            ++checker->loop_depth;
            (void)check_stmt(checker, stmt->as.while_.body);
            --checker->loop_depth;
            break;
        }
        case STMT_FOR: {
            Type *iterable;
            if (stmt->as.for_.borrowed) {
                if (stmt->as.for_.range_end != NULL)
                    lang_diag(checker->diagnostics, stmt->span,
                              "`borrow` is not used with integer ranges");
                bool direct_place =
                    stmt->as.for_.iterable->kind == EXPR_NAME ||
                    (stmt->as.for_.iterable->kind == EXPR_FIELD &&
                     stmt->as.for_.iterable->as.field.object->kind ==
                         EXPR_NAME);
                if (!direct_place)
                    lang_diag(
                        checker->diagnostics,
                        stmt->as.for_.iterable->span,
                        stmt->as.for_.foreach
                            ? "`foreach` requires a collection local or direct field"
                            : "viewed iteration currently requires a direct local");
                iterable = check_place(
                    checker, stmt->as.for_.iterable);
            } else {
                iterable = check_expr(
                    checker, stmt->as.for_.iterable);
            }
            Type *element = &type_error;
            if (stmt->as.for_.range_end != NULL) {
                Type *end = check_expr(
                    checker, stmt->as.for_.range_end);
                if (!same_type(iterable, end)) {
                    if (coerce_literal(
                            checker, stmt->as.for_.range_end,
                            iterable))
                        end = iterable;
                    else if (coerce_literal(
                                 checker, stmt->as.for_.iterable,
                                 end))
                        iterable = end;
                }
                if (!same_type(iterable, end))
                    lang_diag(
                        checker->diagnostics, stmt->span,
                        "range bounds must have matching types; found `%s` and `%s`",
                        iterable->name, end->name);
                if (!is_integer(iterable))
                    lang_diag(
                        checker->diagnostics, stmt->span,
                        "range bounds must be integers; found `%s`",
                        iterable->name);
                element = iterable;
            } else if (iterable->kind != TYPE_ARRAY &&
                       iterable->kind != TYPE_VEC &&
                       iterable->kind != TYPE_SLICE &&
                       iterable->kind != TYPE_STRING) {
                lang_diag(checker->diagnostics, stmt->as.for_.iterable->span,
                          stmt->as.for_.foreach
                              ? "`foreach` requires a string, fixed array, Slice, or Vec"
                              : "`for` requires a string, fixed array, Slice, Vec, or integer range");
            } else {
                element = iterable->kind == TYPE_STRING
                    ? &type_char : iterable->element;
                if (iterable->kind == TYPE_ARRAY &&
                    iterable->element->requires_cleanup &&
                    !stmt->as.for_.borrowed)
                    lang_diag(
                        checker->diagnostics,
                        stmt->as.for_.iterable->span,
                        "`for` cannot copy noncopyable array elements");
            }
            stmt->as.for_.element_type = element;
            if (stmt->as.for_.foreach &&
                stmt->as.for_.type_name != NULL) {
                Type *declared = resolve_type(
                    checker, stmt->as.for_.type_name, stmt->span);
                if (!same_type(declared, element))
                    lang_diag(
                        checker->diagnostics, stmt->span,
                        "`foreach` variable expects `%s`, found `%s`",
                        declared->name, element->name);
            }
            size_t outer_count = checker->local_count;
            ++checker->depth;
            if (checker->local_count < 256U)
                checker->locals[checker->local_count++] = (Local){
                    stmt->as.for_.name,
                    element,
                    true, false,
                    checker->depth, stmt->span,
                    ++checker->next_local_id
                };
            if (checker->local_count > outer_count)
                stmt->as.for_.binding_id =
                    checker->locals[checker->local_count - 1U].id;
            if (checker->loop_depth >=
                sizeof(checker->loop_local_bases) /
                    sizeof(checker->loop_local_bases[0])) {
                lang_diag(checker->diagnostics, stmt->span,
                          "loop nesting limit exceeded");
                checker->local_count = outer_count;
                --checker->depth;
                break;
            }
            checker->loop_local_bases[checker->loop_depth] = outer_count;
            ++checker->loop_depth;
            (void)check_stmt(checker, stmt->as.for_.body);
            --checker->loop_depth;
            checker->local_count = outer_count;
            --checker->depth;
            break;
        }
        case STMT_C_FOR: {
            size_t outer_count = checker->local_count;
            ++checker->depth;
            if (stmt->as.c_for.initializer != NULL)
                (void)check_stmt(
                    checker, stmt->as.c_for.initializer);
            if (stmt->as.c_for.condition != NULL &&
                check_expr(checker, stmt->as.c_for.condition)->kind !=
                    TYPE_BOOL)
                lang_diag(checker->diagnostics,
                          stmt->as.c_for.condition->span,
                          "`for` condition must be `bool`");

            size_t loop_count = checker->local_count;
            if (checker->loop_depth >=
                sizeof(checker->loop_local_bases) /
                    sizeof(checker->loop_local_bases[0])) {
                lang_diag(checker->diagnostics, stmt->span,
                          "loop nesting limit exceeded");
            } else {
                checker->loop_local_bases[checker->loop_depth] =
                    loop_count;
                ++checker->loop_depth;
                (void)check_stmt(checker, stmt->as.c_for.body);
                if (stmt->as.c_for.increment != NULL)
                    (void)check_expr(
                        checker, stmt->as.c_for.increment);
                --checker->loop_depth;
            }
            set_cleanup_plan(
                checker, &stmt->exit_cleanup, outer_count);
            checker->local_count = outer_count;
            --checker->depth;
            break;
        }
        case STMT_MATCH: {
            Type *matched = check_expr(checker, stmt->as.match_.value);
            const Decl *enum_decl = NULL;
            size_t match_local_count = checker->local_count;
            bool have_fallthrough_arm = false;
            if (matched->kind == TYPE_NAMED &&
                matched->declaration != NULL &&
                matched->declaration->kind == DECL_ENUM)
                enum_decl = matched->declaration;
            if (matched->kind != TYPE_RESULT &&
                matched->kind != TYPE_OPTION && enum_decl == NULL) {
                lang_diag(checker->diagnostics, stmt->as.match_.value->span,
                          "`switch` expects an enum or union value, found `%s`",
                          matched->name);
            }
            for (size_t a = 0U; a < stmt->as.match_.arm_count; ++a) {
                checker->local_count = match_local_count;
                MatchArm *arm = &stmt->as.match_.arms[a];
                if (enum_decl != NULL) {
                    const char *separator =
                        last_path_separator(arm->variant);
                    if (separator != NULL) {
                        char *enum_path = lang_arena_strndup(
                            &checker->module->arena, arm->variant,
                            (size_t)(separator - arm->variant));
                        if (visible_declaration_path_matches(
                                checker,
                                enum_path,
                                enum_decl->as.enumeration.name,
                                enum_decl->module_name)) {
                            const char *variant_name = separator + 2U;
                            size_t canonical_length =
                                strlen(
                                    enum_decl->as.enumeration.name) +
                                strlen(variant_name) + 3U;
                            char *canonical = lang_arena_alloc(
                                &checker->module->arena,
                                canonical_length);
                            (void)snprintf(
                                canonical, canonical_length,
                                "%s::%s",
                                enum_decl->as.enumeration.name,
                                variant_name);
                            arm->variant = canonical;
                        }
                    }
                }
                for (size_t prior = 0U; prior < a; ++prior)
                    if (strcmp(arm->variant,
                               stmt->as.match_.arms[prior].variant) == 0)
                        lang_diag(checker->diagnostics, arm->span,
                                  "duplicate switch case `%s`", arm->variant);
                Type *payload = NULL;
                bool valid = false;
                if (matched->kind == TYPE_RESULT) {
                    if (strcmp(arm->variant, "Result::Ok") == 0) {
                        payload = matched->element;
                        valid = true;
                    } else if (strcmp(arm->variant, "Result::Err") == 0) {
                        payload = matched->error_type;
                        valid = true;
                    }
                } else if (matched->kind == TYPE_OPTION) {
                    if (strcmp(arm->variant, "Option::Some") == 0) {
                        payload = matched->element;
                        valid = true;
                    } else if (strcmp(arm->variant, "Option::None") == 0) {
                        valid = true;
                    }
                } else if (enum_decl != NULL) {
                    const char *separator = strstr(arm->variant, "::");
                    if (separator != NULL &&
                        (size_t)(separator - arm->variant) ==
                            strlen(enum_decl->as.enumeration.name) &&
                        memcmp(arm->variant, enum_decl->as.enumeration.name,
                               strlen(enum_decl->as.enumeration.name)) == 0) {
                        const char *variant_name = separator + 2;
                        for (size_t v = 0U;
                             v < enum_decl->as.enumeration.variant_count; ++v) {
                            FieldDecl *variant =
                                &enum_decl->as.enumeration.variants[v];
                            if (strcmp(variant->name, variant_name) == 0) {
                                valid = true;
                                if (strcmp(variant->type_name, "unit") != 0)
                                    payload =
                                        resolve_type_in_applied_declaration(
                                            checker, matched,
                                            variant->type_name,
                                            variant->span);
                            }
                        }
                    }
                }
                if (!valid)
                    lang_diag(checker->diagnostics, arm->span,
                              "variant `%s` does not belong to `%s`",
                              arm->variant, matched->name);
                if (payload == NULL && arm->binding != NULL)
                    lang_diag(checker->diagnostics, arm->span,
                              "variant `%s` has no payload to bind",
                              arm->variant);
                if (payload != NULL &&
                    arm->binding_type_name != NULL) {
                    Type *declared = resolve_type(
                        checker, arm->binding_type_name, arm->span);
                    if (!same_type(declared, payload))
                        lang_diag(
                            checker->diagnostics, arm->span,
                            "switch case `%s` binds `%s`; expected `%s`",
                            arm->variant, declared->name, payload->name);
                }
                arm->binding_type = payload;
                if (arm->binding != NULL && payload != NULL &&
                    checker->local_count < 256U)
                    checker->locals[checker->local_count++] = (Local){
                        arm->binding, payload, false, false,
                        checker->depth + 1U, arm->span,
                        ++checker->next_local_id
                    };
                if (arm->binding != NULL && payload != NULL &&
                    checker->local_count > match_local_count)
                    arm->binding_id =
                        checker->locals[checker->local_count - 1U].id;
                bool arm_falls = check_stmt(checker, arm->body);
                if (arm_falls) have_fallthrough_arm = true;
            }
            if (matched->kind == TYPE_RESULT ||
                matched->kind == TYPE_OPTION) {
                const char *result_required[] = {
                    "Result::Ok", "Result::Err"
                };
                const char *option_required[] = {
                    "Option::Some", "Option::None"
                };
                const char **required = matched->kind == TYPE_RESULT
                                      ? result_required : option_required;
                for (size_t r = 0U; r < 2U; ++r) {
                    bool found = false;
                    for (size_t a = 0U; a < stmt->as.match_.arm_count; ++a)
                        if (strcmp(stmt->as.match_.arms[a].variant,
                                   required[r]) == 0) found = true;
                    if (!found)
                        lang_diag(checker->diagnostics, stmt->span,
                                  "non-exhaustive switch: missing `%s`",
                                  required[r]);
                }
            } else if (enum_decl != NULL) {
                for (size_t v = 0U;
                     v < enum_decl->as.enumeration.variant_count; ++v) {
                    bool found = false;
                    char expected[256];
                    (void)snprintf(expected, sizeof(expected), "%s::%s",
                        enum_decl->as.enumeration.name,
                        enum_decl->as.enumeration.variants[v].name);
                    for (size_t a = 0U; a < stmt->as.match_.arm_count; ++a)
                        if (strcmp(stmt->as.match_.arms[a].variant,
                                   expected) == 0) found = true;
                    if (!found)
                        lang_diag(checker->diagnostics, stmt->span,
                                  "non-exhaustive switch: missing `%s`",
                                  expected);
                }
            }
            checker->local_count = match_local_count;
            return have_fallthrough_arm;
        }
        case STMT_BREAK:
            if (checker->finally_depth != 0U)
                lang_diag(checker->diagnostics, stmt->span,
                          "control cannot leave a `finally` block with `break`");
            if (checker->loop_depth == 0U)
                lang_diag(checker->diagnostics, stmt->span,
                          "`break` can only be used inside a loop");
            else
                set_cleanup_plan(
                    checker, &stmt->exit_cleanup,
                    checker->loop_local_bases[checker->loop_depth - 1U]);
            return false;
        case STMT_CONTINUE:
            if (checker->finally_depth != 0U)
                lang_diag(checker->diagnostics, stmt->span,
                          "control cannot leave a `finally` block with `continue`");
            if (checker->loop_depth == 0U)
                lang_diag(checker->diagnostics, stmt->span,
                          "`continue` can only be used inside a loop");
            else
                set_cleanup_plan(
                    checker, &stmt->exit_cleanup,
                    checker->loop_local_bases[checker->loop_depth - 1U]);
            return false;
        case STMT_BLOCK: {
            bool function_body = checker->depth == 0U;
            ++checker->depth;
            size_t start = checker->local_count;
            bool falls_through = true;
            for (size_t i = 0U; i < stmt->as.block.count; ++i) {
                bool statement_falls =
                    check_stmt(checker, stmt->as.block.items[i]);
                if (falls_through) falls_through = statement_falls;
            }
            set_cleanup_plan(
                checker, &stmt->exit_cleanup,
                function_body ? 0U : start);
            checker->local_count = start;
            --checker->depth;
            return falls_through;
        }
        case STMT_UNSAFE: {
            ++checker->unsafe_depth;
            bool falls_through = check_stmt(checker, stmt->as.unsafe_body);
            --checker->unsafe_depth;
            return falls_through;
        }
    }
    return true;
}

static bool type_has_c_abi(Checker *checker, Type *type,
                           const Decl **seen, size_t seen_count) {
    if (type == NULL) return false;
    switch (type->kind) {
        case TYPE_BOOL:
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
        case TYPE_ISIZE: case TYPE_USIZE:
        case TYPE_F32: case TYPE_F64: case TYPE_CHAR:
        case TYPE_RAW_POINTER:
            return true;
        case TYPE_ARRAY:
            return type_has_c_abi(
                checker, type->element, seen, seen_count);
        case TYPE_NAMED: {
            const Decl *decl = type->declaration;
            if (decl == NULL) return false;
            for (size_t i = 0U; i < seen_count; ++i)
                if (seen[i] == decl) return false;
            if (seen_count >= 64U) return false;
            const Decl *next_seen[64];
            for (size_t i = 0U; i < seen_count; ++i)
                next_seen[i] = seen[i];
            next_seen[seen_count++] = decl;
            const char *previous_module = checker->current_module;
            checker->current_module = decl->module_name;
            if (decl->kind == DECL_ALIAS) {
                Type *target = resolve_type(
                    checker, decl->as.alias.target, decl->span);
                bool safe = type_has_c_abi(
                    checker, target, next_seen, seen_count);
                checker->current_module = previous_module;
                return safe;
            }
            if (decl->kind == DECL_ENUM &&
                !decl->as.enumeration.is_union) {
                checker->current_module = previous_module;
                return true;
            }
            if (decl->kind != DECL_STRUCT ||
                !decl->as.structure.is_extern) {
                checker->current_module = previous_module;
                return false;
            }
            for (size_t field = 0U;
                 field < decl->as.structure.field_count; ++field) {
                FieldDecl *field_decl =
                    &decl->as.structure.fields[field];
                Type *field_type = resolve_type(
                    checker, field_decl->type_name,
                    field_decl->span);
                if (!type_has_c_abi(
                        checker, field_type,
                        next_seen, seen_count)) {
                    checker->current_module = previous_module;
                    return false;
                }
            }
            checker->current_module = previous_module;
            return true;
        }
        case TYPE_ERROR:
        case TYPE_UNIT: case TYPE_NEVER:
        case TYPE_STR: case TYPE_STRING: case TYPE_STRING_BUILDER:
        case TYPE_URL: case TYPE_HTML: case TYPE_BUFFER: case TYPE_ARENA:
        case TYPE_NATIVE_HANDLE:
        case TYPE_CANCELLATION_TOKEN:
        case TYPE_CANCELLATION_TOKEN_SOURCE:
        case TYPE_SLICE: case TYPE_VEC:
        case TYPE_DICTIONARY: case TYPE_HASH_SET: case TYPE_QUEUE:
        case TYPE_STACK:
        case TYPE_OPTION: case TYPE_RESULT: case TYPE_TASK:
        case TYPE_FUNCTION:
            return false;
    }
    return false;
}

bool lang_check_module(Module *module, LangDiagnostics *diagnostics) {
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *first = module->decls[i];
        const char *first_name =
            first->kind == DECL_STRUCT ? first->as.structure.name :
            first->kind == DECL_ENUM ? first->as.enumeration.name :
            first->kind == DECL_ALIAS ? first->as.alias.name : NULL;
        if (first_name == NULL) continue;
        for (size_t j = i + 1U; j < module->count; ++j) {
            Decl *second = module->decls[j];
            const char *second_name =
                second->kind == DECL_STRUCT ? second->as.structure.name :
                second->kind == DECL_ENUM ? second->as.enumeration.name :
                second->kind == DECL_ALIAS ? second->as.alias.name : NULL;
            if (second_name != NULL &&
                first->module_name != NULL &&
                second->module_name != NULL &&
                strcmp(first->module_name,
                       second->module_name) == 0 &&
                strcmp(first_name, second_name) == 0)
                lang_diag(diagnostics, second->span,
                          "duplicate type `%s`", second_name);
        }
    }
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *decl = module->decls[i];
        if (decl->kind != DECL_STRUCT ||
            !decl->as.structure.is_extern)
            continue;
        Checker checker;
        memset(&checker, 0, sizeof(checker));
        checker.module = module;
        checker.diagnostics = diagnostics;
        checker.current_module = decl->module_name;
        if (decl->type_param_count != 0U) {
            lang_diag(
                diagnostics, decl->span,
                "extern struct `%s` cannot be generic",
                decl->as.structure.name);
            continue;
        }
        for (size_t field = 0U;
             field < decl->as.structure.field_count; ++field) {
            FieldDecl *field_decl =
                &decl->as.structure.fields[field];
            Type *field_type = resolve_type(
                &checker, field_decl->type_name, field_decl->span);
            if (!type_has_c_abi(&checker, field_type, NULL, 0U))
                lang_diag(
                    diagnostics, field_decl->span,
                    "field `%s` of extern struct `%s` has non-C-ABI type `%s`",
                    field_decl->name, decl->as.structure.name,
                    field_decl->type_name);
        }
    }
    /* Resolve callable ownership before checking any body. */
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *decl = module->decls[i];
        if (decl->kind != DECL_FUNCTION ||
            decl->type_param_count != 0U)
            continue;
        Checker checker;
        memset(&checker, 0, sizeof(checker));
        checker.module = module;
        checker.diagnostics = diagnostics;
        checker.current_module = decl->module_name;
        Function *function = &decl->as.function;
        function->checked_return_type = resolve_type(
            &checker, function->return_type, function->span);
        for (size_t p = 0U; p < function->param_count; ++p) {
            Param *parameter = &function->params[p];
            Type *type = resolve_type(
                &checker, parameter->type_name, parameter->span);
            parameter->checked_type = type;
        }
    }
    for (size_t i = 0U; i < module->count; ++i) {
        if (module->decls[i]->kind != DECL_FUNCTION) continue;
        if (module->decls[i]->type_param_count != 0U) continue;
        Function *function = &module->decls[i]->as.function;
        for (size_t j = i + 1U; j < module->count; ++j) {
            Decl *candidate_decl = module->decls[j];
            if (candidate_decl->kind != DECL_FUNCTION ||
                candidate_decl->type_param_count != 0U ||
                function->span.file == NULL ||
                candidate_decl->as.function.span.file == NULL ||
                module->decls[i]->module_name == NULL ||
                candidate_decl->module_name == NULL ||
                strcmp(module->decls[i]->module_name,
                       candidate_decl->module_name) != 0 ||
                strcmp(function->name,
                       candidate_decl->as.function.name) != 0)
                continue;
            Function *candidate = &candidate_decl->as.function;
            if (function->param_count != candidate->param_count)
                continue;
            bool same_signature = true;
            for (size_t p = 0U; p < function->param_count; ++p) {
                if (function->params[p].by_ref !=
                        candidate->params[p].by_ref ||
                    function->params[p].by_out !=
                        candidate->params[p].by_out ||
                    !same_type(function->params[p].checked_type,
                               candidate->params[p].checked_type)) {
                    same_signature = false;
                    break;
                }
            }
            if (same_signature) {
                LangDiagnostic *diagnostic = lang_diag(
                    diagnostics, candidate_decl->span,
                    "duplicate overload signature for `%s`",
                    function->name);
                lang_diag_secondary(
                    diagnostic, module->decls[i]->span,
                    "first declaration with this signature");
            }
        }
        Checker checker;
        memset(&checker, 0, sizeof(checker));
        checker.module = module;
        checker.diagnostics = diagnostics;
        checker.function = function;
        checker.current_module = module->decls[i]->module_name;
        if (module->decls[i]->generic_origin != NULL) {
            checker.substitution_decl =
                module->decls[i]->generic_origin;
            checker.substitution_arguments =
                module->decls[i]->generic_arguments;
            checker.substitution_argument_count =
                module->decls[i]->generic_argument_count;
        }
        function->checked_return_type = resolve_type(&checker, function->return_type,
                                                     function->span);
        if (function->is_async && function->is_extern)
            lang_diag(diagnostics, function->span,
                      "extern functions cannot be declared async");
        if (function->is_async &&
            function->checked_return_type->kind != TYPE_TASK)
            lang_diag(diagnostics, function->span,
                      "async function `%s` must return Task or Task<T>",
                      function->name);
        if (function->is_async)
            for (size_t parameter = 0U;
                 parameter < function->param_count; ++parameter)
                if (function->params[parameter].by_ref ||
                    function->params[parameter].by_out)
                    lang_diag(
                        diagnostics,
                        function->params[parameter].span,
                        "async functions cannot have ref or out parameters");
        for (size_t j = 0U; j < function->param_count; ++j) {
            Type *type = resolve_type(&checker, function->params[j].type_name,
                                      function->params[j].span);
            function->params[j].checked_type = type;
            function->params[j].borrowed = function->params[j].by_ref;
            function->params[j].mutable_ = true;
            function->params[j].binding_id = ++checker.next_local_id;
            checker.locals[checker.local_count++] = (Local){
                function->params[j].name, type,
                function->params[j].mutable_,
                function->params[j].borrowed, 0U,
                function->params[j].span,
                function->params[j].binding_id
            };
        }
        if (function->is_extern) {
            function->local_count = function->param_count;
            continue;
        }
        if (function->is_drop &&
            (function->param_count != 1U ||
             function->params[0].checked_type->kind != TYPE_NAMED))
            lang_diag(diagnostics, function->span,
                      "a destructor must target one user-defined struct or enum");
        bool falls_through = check_stmt(&checker, function->body);
        function->local_count = checker.local_count + 64U;
        Type *logical_return =
            function->is_async &&
            function->checked_return_type->kind == TYPE_TASK
                ? function->checked_return_type->element
                : function->checked_return_type;
        if (logical_return->kind != TYPE_UNIT && falls_through)
            lang_diag(diagnostics, function->span,
                      "function `%s` may exit without returning `%s`",
                      function->name, logical_return->name);
    }
    bool found_main = false;
    for (size_t i = 0U; i < module->count; ++i)
        if (module->decls[i]->kind == DECL_FUNCTION &&
            module->decls[i]->type_param_count == 0U &&
            !module->decls[i]->as.function.is_extern &&
            strcmp(module->decls[i]->as.function.name, "main") == 0 &&
            module->entry_module != NULL &&
            module->decls[i]->module_name != NULL &&
            strcmp(module->entry_module,
                   module->decls[i]->module_name) == 0)
            found_main = true;
    if (module->require_entrypoint && !found_main) {
        LangSpan span = {module->source->path, 0U, 0U};
        lang_diag(diagnostics, span, "module has no `main` function");
    }
    return diagnostics->count == 0U;
}
