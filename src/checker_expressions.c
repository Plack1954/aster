#include "checker_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    expr->as.call.argument_modes = NULL;
    expr->as.call.implicit_receiver = true;
    expr->as.call.implicit_enum_value = false;
    return checker_check_call(checker, expr);
}

static Type *rewrite_instance_property_call(
    Checker *checker, Expr *expr, const char *name, Expr *receiver) {
    Expr *callee = lang_arena_alloc(
        &checker->module->arena, sizeof(*callee));
    memset(callee, 0, sizeof(*callee));
    callee->kind = EXPR_NAME;
    callee->span = expr->span;
    callee->as.name = name;
    Expr **arguments = lang_arena_alloc(
        &checker->module->arena, sizeof(*arguments));
    arguments[0] = receiver;
    expr->kind = EXPR_CALL;
    expr->as.call.callee = callee;
    expr->as.call.arguments.items = arguments;
    expr->as.call.arguments.count = 1U;
    expr->as.call.argument_modes = NULL;
    expr->as.call.implicit_receiver = true;
    return checker_check_call(checker, expr);
}

static Type *rewrite_property_setter_call(
    Checker *checker, Expr *expr, const Function *setter,
    Expr *receiver, Expr *assigned_value
) {
    Expr *callee = lang_arena_alloc(
        &checker->module->arena, sizeof(*callee));
    memset(callee, 0, sizeof(*callee));
    callee->kind = EXPR_NAME;
    callee->span = expr->span;
    callee->as.name = setter->name;
    size_t argument_count = receiver != NULL ? 2U : 1U;
    Expr **arguments = lang_arena_alloc(
        &checker->module->arena,
        argument_count * sizeof(*arguments));
    size_t next = 0U;
    if (receiver != NULL) arguments[next++] = receiver;
    arguments[next] = assigned_value;
    expr->kind = EXPR_CALL;
    expr->as.call.callee = callee;
    expr->as.call.arguments.items = arguments;
    expr->as.call.arguments.count = argument_count;
    expr->as.call.argument_modes = NULL;
    expr->as.call.implicit_receiver = receiver != NULL;
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
    expr->as.call.argument_modes = NULL;
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
                (checker->expected_type->kind == TYPE_RAW_POINTER ||
                 checker->expected_type->kind == TYPE_CLASS))
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
            TokenKind op = expr->as.binary.op;
            bool comparison =
                op == TOK_EQUAL_EQUAL || op == TOK_BANG_EQUAL ||
                op == TOK_LESS || op == TOK_LESS_EQUAL ||
                op == TOK_GREATER || op == TOK_GREATER_EQUAL;
            bool borrow_left = comparison &&
                checker_expression_is_borrowable(
                    checker, expr->as.binary.left);
            Type *left = borrow_left
                ? check_borrowed_expr(checker, expr->as.binary.left)
                : check_expr(checker, expr->as.binary.left);
            expr->as.binary.borrow_left = borrow_left &&
                checker_expression_is_borrowable(
                    checker, expr->as.binary.left);
            Type *previous_expected = checker->expected_type;
            if (expr->as.binary.right->kind == EXPR_NULL)
                checker->expected_type = left;
            bool borrow_right = comparison &&
                checker_expression_is_borrowable(
                    checker, expr->as.binary.right);
            Type *right = borrow_right
                ? check_borrowed_expr(checker, expr->as.binary.right)
                : check_expr(checker, expr->as.binary.right);
            expr->as.binary.borrow_right = borrow_right &&
                checker_expression_is_borrowable(
                    checker, expr->as.binary.right);
            checker->expected_type = previous_expected;
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
            break;
        case EXPR_ASSIGN: {
            bool discard_assignment =
                expr->as.assign.target->kind == EXPR_NAME &&
                strcmp(expr->as.assign.target->as.name, "_") == 0;
            if (discard_assignment &&
                expr->as.assign.compound_op != TOK_ERROR) {
                lang_diag(checker->diagnostics, expr->span,
                          "discard assignment does not support compound operators");
            }
            if (expr->as.assign.target->kind == EXPR_NAME &&
                find_local(checker,
                           expr->as.assign.target->as.name) == NULL)
                checker_rewrite_unqualified_static_field(
                    checker, expr->as.assign.target);
            if (expr->as.assign.target->kind == EXPR_NAME &&
                find_local(checker,
                           expr->as.assign.target->as.name) == NULL) {
                const Decl *owner = current_property_owner(checker);
                const char *property_name =
                    expr->as.assign.target->as.name;
                Function *property_getter = declared_property_accessor(
                    owner, property_name, false);
                Function *property_setter = declared_property_accessor(
                    owner, property_name, true);
                if (property_getter != NULL || property_setter != NULL) {
                    bool property_static =
                        (property_getter != NULL &&
                         property_getter->is_static_member) ||
                        (property_setter != NULL &&
                         property_setter->is_static_member);
                    Expr *object = lang_arena_alloc(
                        &checker->module->arena, sizeof(*object));
                    memset(object, 0, sizeof(*object));
                    object->kind = EXPR_NAME;
                    object->span = expr->as.assign.target->span;
                    object->as.name = property_static
                        ? owner->as.structure.name : "this";
                    expr->as.assign.target->kind = EXPR_FIELD;
                    expr->as.assign.target->as.field.object = object;
                    expr->as.assign.target->as.field.field = property_name;
                }
            }
            if (expr->as.assign.target->kind == EXPR_FIELD) {
                Expr *target = expr->as.assign.target;
                Expr *receiver = target->as.field.object;
                const Decl *owner = NULL;
                if (receiver->kind == EXPR_NAME) {
                    Local *local = find_local(checker, receiver->as.name);
                    if (local != NULL && local->type != NULL)
                        owner = local->type->declaration;
                    else if (strcmp(receiver->as.name, "this") == 0)
                        owner = current_property_owner(checker);
                }
                const char *static_path = owner == NULL
                    ? checker_static_call_path(checker, target) : NULL;
                Function *setter = static_path != NULL
                    ? static_property_accessor(
                        checker, static_path, true) : NULL;
                Function *getter = static_path != NULL
                    ? static_property_accessor(
                        checker, static_path, false) : NULL;
                if (owner != NULL) {
                    setter = declared_property_accessor(
                        owner, target->as.field.field, true);
                    getter = declared_property_accessor(
                        owner, target->as.field.field, false);
                }
                const char *backing_field = setter != NULL &&
                    setter->property_backing_field != NULL
                        ? setter->property_backing_field
                        : getter != NULL
                          ? getter->property_backing_field : NULL;
                if (owner != NULL && checker->function != NULL &&
                    checker->function->is_constructor &&
                    strcmp(receiver->as.name, "this") == 0 &&
                    backing_field != NULL) {
                    target->kind = EXPR_NAME;
                    target->as.name = backing_field;
                } else if (setter != NULL) {
                    Expr *assigned_value = expr->as.assign.value;
                    if (expr->as.assign.compound_op != TOK_ERROR) {
                        if (getter == NULL) {
                            lang_diag(
                                checker->diagnostics, target->span,
                                "compound assignment requires property `%s` to have a getter",
                                target->as.field.field);
                        } else {
                            Expr *read = lang_arena_alloc(
                                &checker->module->arena, sizeof(*read));
                            *read = *target;
                            Expr *binary = lang_arena_alloc(
                                &checker->module->arena, sizeof(*binary));
                            memset(binary, 0, sizeof(*binary));
                            binary->kind = EXPR_BINARY;
                            binary->span = expr->span;
                            binary->as.binary.op =
                                expr->as.assign.compound_op;
                            binary->as.binary.left = read;
                            binary->as.binary.right = assigned_value;
                            assigned_value = binary;
                        }
                    }
                    result = rewrite_property_setter_call(
                        checker, expr, setter,
                        static_path != NULL ? NULL : receiver,
                        assigned_value);
                    goto checked_expression;
                } else if (getter != NULL) {
                    lang_diag(checker->diagnostics, target->span,
                              "property `%s` is read-only",
                              target->as.field.field);
                    result = &type_error;
                    goto checked_expression;
                }
            }
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
                    expr->as.call.argument_modes = NULL;
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
                    expr->as.call.argument_modes = NULL;
                    expr->as.call.implicit_enum_value = false;
                    result = checker_check_call(checker, expr);
                    goto checked_expression;
                }
            }
            Type *assignment_expected = NULL;
            FieldDecl *assignment_static_field = NULL;
            if (expr->as.assign.target->kind == EXPR_FIELD) {
                const char *static_path = checker_static_call_path(
                    checker, expr->as.assign.target);
                const Decl *static_owner = NULL;
                assignment_static_field = static_path != NULL
                    ? checker_static_field_from_path(
                          checker, static_path, &static_owner)
                    : NULL;
                if (assignment_static_field != NULL) {
                    if (!class_member_accessible(
                            checker, static_owner,
                            assignment_static_field->is_public))
                        lang_diag(
                            checker->diagnostics,
                            expr->as.assign.target->span,
                            "static field `%s` is private to class `%s`",
                            assignment_static_field->name,
                            static_owner->as.structure.name);
                    expr->as.assign.target->as.field.static_field = true;
                    expr->as.assign.target->resolved_decl = static_owner;
                    expr->as.assign.target->type =
                        assignment_static_field->checked_type;
                    assignment_expected =
                        assignment_static_field->checked_type;
                }
            }
            if (expr->as.assign.target->kind == EXPR_FIELD &&
                expr->as.assign.target->as.field.object->kind == EXPR_NAME &&
                strcmp(expr->as.assign.target->as.field.object->as.name,
                       "this") == 0 &&
                checker->function != NULL &&
                checker->function->is_constructor) {
                for (size_t field = 0U;
                     field < checker->function->constructor_field_count;
                     ++field) {
                    size_t binding = checker->function
                        ->constructor_field_binding_ids[field];
                    for (size_t local = 0U;
                         local < checker->local_count; ++local) {
                        Local *candidate = &checker->locals[local];
                        if (candidate->id != binding ||
                            strcmp(candidate->name,
                                   expr->as.assign.target
                                       ->as.field.field) != 0)
                            continue;
                        expr->as.assign.target->kind = EXPR_NAME;
                        expr->as.assign.target->as.name = candidate->name;
                        expr->as.assign.target->resolved_local_id =
                            candidate->id;
                    }
                }
            }
            if (expr->as.assign.target->kind == EXPR_NAME &&
                find_local(checker,
                           expr->as.assign.target->as.name) == NULL) {
                Local *this_local = find_local(checker, "this");
                if (this_local != NULL &&
                (this_local->type->kind == TYPE_NAMED ||
                 this_local->type->kind == TYPE_CLASS) &&
                    this_local->type->declaration != NULL &&
                (this_local->type->declaration->kind == DECL_STRUCT ||
                 this_local->type->declaration->kind == DECL_CLASS)) {
                    const Decl *owner = this_local->type->declaration;
                    for (size_t field = 0U;
                         field < owner->as.structure.field_count; ++field) {
                        if (strcmp(owner->as.structure.fields[field].name,
                                   expr->as.assign.target->as.name) != 0)
                            continue;
                        Expr *object = lang_arena_alloc(
                            &checker->module->arena, sizeof(*object));
                        memset(object, 0, sizeof(*object));
                        object->kind = EXPR_NAME;
                        object->span = expr->as.assign.target->span;
                        object->as.name = "this";
                        const char *field_name =
                            expr->as.assign.target->as.name;
                        expr->as.assign.target->kind = EXPR_FIELD;
                        expr->as.assign.target->as.field.object = object;
                        expr->as.assign.target->as.field.field = field_name;
                        break;
                    }
                }
            }
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
            if (discard_assignment) {
                target->type = value;
                result = &type_unit;
                break;
            }
            if (assignment_static_field != NULL) {
                Type *place_type = assignment_static_field->checked_type;
                if (coerce_literal(
                        checker, expr->as.assign.value, place_type))
                    value = place_type;
                if (assignment_static_field->is_readonly)
                    lang_diag(
                        checker->diagnostics, target->span,
                        "cannot assign to readonly static field `%s`",
                        assignment_static_field->name);
                if (!type_assignable(place_type, value))
                    lang_diag(
                        checker->diagnostics, expr->span,
                        "assignment expects `%s`, found `%s`",
                        type_display_name(checker, place_type),
                        type_display_name(checker, value));
                validate_compound_assignment(
                    checker, expr, place_type);
                result = &type_unit;
                break;
            }
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
                    if (!pointer->pointer_mutable)
                        lang_diag(checker->diagnostics, target->span,
                                  "cannot store through a const pointer");
                    if (place_type->kind != TYPE_I64)
                        lang_diag(checker->diagnostics, target->span,
                                  "pointer store currently supports `long*`");
                }
                if (coerce_literal(
                        checker, expr->as.assign.value, place_type))
                    value = place_type;
                if (!type_assignable(place_type, value))
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
                Local *local = NULL;
                if (target->resolved_local_id != 0U)
                    for (size_t i = 0U; i < checker->local_count; ++i)
                        if (checker->locals[i].id ==
                            target->resolved_local_id)
                            local = &checker->locals[i];
                if (local == NULL)
                    local = find_local(checker, target->as.name);
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
                if (!type_assignable(local->type, value))
                    lang_diag(checker->diagnostics, expr->span,
                              "assignment expects `%s`, found `%s`",
                              type_display_name(checker, local->type),
                              type_display_name(checker, value));
                validate_compound_assignment(
                    checker, expr, local->type);
                if (local->is_out_parameter &&
                    !local->definitely_assigned &&
                    expr->as.assign.compound_op != TOK_ERROR)
                    lang_diag(checker->diagnostics, expr->span,
                              "`out` parameter `%s` cannot be read before assignment",
                              local->name);
                local->definitely_assigned = true;
                local->available = true;
                local->moved_at = (LangSpan){0};
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
            if (local->is_out_parameter &&
                !local->definitely_assigned)
                lang_diag(checker->diagnostics, object_expr->span,
                          "`out` parameter `%s` cannot be read before assignment",
                          local->name);
            (void)checker_require_available(
                checker, local, object_expr->span);
            Type *place_type = &type_error;
            if (target->kind == EXPR_FIELD) {
                if ((local->type->kind != TYPE_NAMED &&
                     local->type->kind != TYPE_CLASS) ||
                    local->type->declaration == NULL ||
                    (local->type->declaration->kind != DECL_STRUCT &&
                     local->type->declaration->kind != DECL_CLASS)) {
                    lang_diag(checker->diagnostics, expr->span,
                              "field assignment requires a struct or class local");
                } else {
                    const Decl *structure = local->type->declaration;
                    for (size_t i = 0U;
                         i < structure->as.structure.field_count; ++i)
                        if (strcmp(
                                structure->as.structure.fields[i].name,
                                target->as.field.field) == 0) {
                            if (!class_member_accessible(
                                    checker, structure,
                                    structure->as.structure.fields[i].is_public))
                                lang_diag(
                                    checker->diagnostics, target->span,
                                    "field `%s` is private to class `%s`",
                                    target->as.field.field,
                                    structure->as.structure.name);
                            place_type =
                                resolve_type_syntax_in_applied_declaration(
                                checker, local->type,
                                structure->as.structure.fields[i].type_syntax,
                                structure->as.structure.fields[i].type_name,
                                structure->as.structure.fields[i].span);
                        }
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
            if (!type_assignable(place_type, value))
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
        case EXPR_COPY: {
            Expr *value = expr->as.copy.value;
            if (value->kind == EXPR_NAME) {
                Local *local = find_local(checker, value->as.name);
                if (local != NULL) {
                    value->type = local->type;
                    value->resolved_local_id = local->id;
                    result = local->type;
                    (void)checker_require_available(
                        checker, local, value->span);
                    if (!type_is_copyable(checker, local->type))
                        lang_diag(checker->diagnostics, expr->span,
                                  "type `%s` is not copyable",
                                  local->type->name);
                } else {
                    ++checker->copy_depth;
                    result = check_expr(checker, value);
                    --checker->copy_depth;
                    if (!type_is_copyable(checker, result))
                        lang_diag(checker->diagnostics, expr->span,
                                  "type `%s` is not copyable",
                                  result->name);
                }
            } else {
                ++checker->copy_depth;
                result = check_expr(checker, value);
                --checker->copy_depth;
                if (!type_is_copyable(checker, result))
                    lang_diag(checker->diagnostics, expr->span,
                              "type `%s` is not copyable",
                              result->name);
            }
            break;
        }
        case EXPR_ENSURE_MOVE: {
            Expr *value = expr->as.copy.value;
            result = check_expr(checker, value);
            if (value->kind != EXPR_NAME && value->kind != EXPR_FIELD)
                lang_diag(checker->diagnostics, expr->span,
                          "`ensure_move` requires a direct local or field");
            if (!type_is_copyable(checker, result))
                lang_diag(checker->diagnostics, expr->span,
                          "`ensure_move` requires a copyable owned value");
            break;
        }
        case EXPR_ASSERT_NO_COPIES:
            result = &type_unit;
            break;
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
            Type *target = resolve_declared_type(
                checker, expr->as.cast.type_syntax,
                expr->as.cast.type_name, expr->span);
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
            if (result != &type_error &&
                type_moves_by_default(checker, result) &&
                checker->copy_depth == 0U &&
                checker->borrow_depth == 0U &&
                !type_is_copyable(checker, result))
                lang_diag(
                    checker->diagnostics, expr->span,
                    "cannot copy noncopyable array element `%s`",
                    result->name);
            break;
        }
        case EXPR_FIELD: {
            const char *static_name = checker_static_call_path(checker, expr);
            if (static_name != NULL) {
                Function *static_member = find_function(
                    checker, static_name, expr->span);
                if (static_member != NULL &&
                    static_member->is_property_getter &&
                    static_member->is_static_member) {
                    result = rewrite_zero_argument_builtin_call(
                        checker, expr, static_name);
                    goto checked_expression;
                }
                const Decl *static_owner = NULL;
                FieldDecl *static_field = checker_static_field_from_path(
                    checker, static_name, &static_owner);
                if (static_field != NULL) {
                    if (!class_member_accessible(
                            checker, static_owner,
                            static_field->is_public))
                        lang_diag(
                            checker->diagnostics, expr->span,
                            "static field `%s` is private to class `%s`",
                            static_field->name,
                            static_owner->as.structure.name);
                    expr->as.field.static_field = true;
                    expr->resolved_decl = static_owner;
                    result = static_field->checked_type;
                    break;
                }
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
                                strcmp(candidate->type_name, "Unit") != 0)
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
            bool local_place = checker_expression_is_local_place(
                checker, expr->as.field.object);
            Type *object = local_place
                ? check_place(checker, expr->as.field.object)
                : check_expr(checker, expr->as.field.object);
            if ((object->kind == TYPE_NAMED ||
                 object->kind == TYPE_CLASS) &&
                object->declaration != NULL &&
                (object->declaration->kind == DECL_STRUCT ||
                 object->declaration->kind == DECL_CLASS)) {
                const Decl *structure = object->declaration;
                const Decl *property_owners[256];
                size_t property_owner_count = 1U;
                property_owners[0] = structure;
                while (property_owner_count != 0U) {
                    const Decl *property_owner =
                        property_owners[--property_owner_count];
                    for (size_t member = 0U;
                         member < property_owner->as.structure.member_count;
                         ++member) {
                        Decl *candidate_decl =
                            property_owner->as.structure.members[member];
                        Function *candidate = &candidate_decl->as.function;
                        const char *separator = strrchr(candidate->name, ':');
                        const char *short_name = separator != NULL
                            ? separator + 1U : candidate->name;
                        if (candidate->is_property_getter &&
                            !candidate->is_static_member &&
                            strcmp(short_name, expr->as.field.field) == 0) {
                            result = rewrite_instance_property_call(
                                checker, expr, candidate->name,
                                expr->as.field.object);
                            goto checked_expression;
                        }
                    }
                    if (object->kind == TYPE_CLASS) {
                        if (property_owner->as.structure.base_class != NULL &&
                            property_owner_count < 256U)
                            property_owners[property_owner_count++] =
                                property_owner->as.structure.base_class;
                        for (size_t interface = 0U;
                             interface < property_owner->as.structure
                                 .interface_count &&
                             property_owner_count < 256U; ++interface)
                            property_owners[property_owner_count++] =
                                property_owner->as.structure
                                    .interfaces[interface];
                    }
                }
                const Decl *method_structure = structure;
                if (structure->as.structure.is_interface) {
                    const Decl *pending[256];
                    size_t pending_count = 0U;
                    for (size_t interface = 0U;
                         interface < structure->as.structure.interface_count;
                         ++interface)
                        pending[pending_count++] =
                            structure->as.structure.interfaces[interface];
                    while (pending_count != 0U &&
                           method_structure == structure) {
                        const Decl *candidate_owner =
                            pending[--pending_count];
                        for (size_t member = 0U;
                             member < candidate_owner->as.structure
                                 .member_count; ++member) {
                            Function *candidate = &candidate_owner
                                ->as.structure.members[member]->as.function;
                            const char *candidate_separator =
                                strrchr(candidate->name, ':');
                            const char *candidate_name =
                                candidate_separator != NULL
                                    ? candidate_separator + 1U
                                    : candidate->name;
                            if (!candidate->is_static_member &&
                                !candidate->is_constructor &&
                                !candidate->is_drop &&
                                !candidate->is_property_getter &&
                                !candidate->is_property_setter &&
                                strcmp(candidate_name,
                                       expr->as.field.field) == 0) {
                                method_structure = candidate_owner;
                                break;
                            }
                        }
                        for (size_t parent = 0U;
                             parent < candidate_owner->as.structure
                                 .interface_count &&
                             pending_count < 256U; ++parent)
                            pending[pending_count++] = candidate_owner
                                ->as.structure.interfaces[parent];
                    }
                }
                const Decl *selected_method = NULL;
                size_t named_methods = 0U;
                size_t matching_methods = 0U;
                for (size_t member = 0U;
                     member < method_structure->as.structure.member_count;
                     ++member) {
                    Decl *candidate_decl =
                        method_structure->as.structure.members[member];
                    Function *candidate = &candidate_decl->as.function;
                    const char *separator = strrchr(candidate->name, ':');
                    const char *short_name = separator != NULL
                        ? separator + 1U : candidate->name;
                    if (candidate->is_static_member ||
                        candidate->is_constructor || candidate->is_drop ||
                        candidate->is_property_getter ||
                        candidate->is_property_setter ||
                        strcmp(short_name, expr->as.field.field) != 0)
                        continue;
                    ++named_methods;
                    if (checker->expected_type == NULL ||
                        checker->expected_type->kind != TYPE_FUNCTION ||
                        object->kind != TYPE_CLASS ||
                        candidate->param_count == 0U ||
                        candidate->param_count - 1U !=
                            checker->expected_type->argument_count ||
                        !same_type(candidate->checked_return_type,
                                   checker->expected_type->element))
                        continue;
                    bool exact = true;
                    for (size_t parameter = 1U;
                         parameter < candidate->param_count; ++parameter) {
                        ParameterMode mode = parameter_mode_from_param(
                            &candidate->params[parameter]);
                        if (!same_type(
                                candidate->params[parameter].checked_type,
                                checker->expected_type
                                    ->arguments[parameter - 1U]) ||
                            mode != checker->expected_type
                                ->parameter_modes[parameter - 1U]) {
                            exact = false;
                            break;
                        }
                    }
                    if (!exact) continue;
                    selected_method = candidate_decl;
                    ++matching_methods;
                }
                if (named_methods != 0U) {
                    if (object->kind != TYPE_CLASS) {
                        lang_diag(
                            checker->diagnostics, expr->span,
                            "bound instance delegates currently require a class receiver");
                        result = &type_error;
                        break;
                    }
                    if (checker->expected_type == NULL ||
                        checker->expected_type->kind != TYPE_FUNCTION) {
                        lang_diag(
                            checker->diagnostics, expr->span,
                            "bound method `%s` requires a target delegate type",
                            expr->as.field.field);
                        result = &type_error;
                        break;
                    }
                    if (matching_methods != 1U ||
                        selected_method == NULL) {
                        lang_diag(
                            checker->diagnostics, expr->span,
                            matching_methods > 1U
                                ? "bound method `%s` is ambiguous for the target delegate type"
                                : "no overload of bound method `%s` matches the target delegate type",
                            expr->as.field.field);
                        result = &type_error;
                        break;
                    }
                    if (!class_member_accessible(
                            checker, structure,
                            selected_method->is_public))
                        lang_diag(
                            checker->diagnostics, expr->span,
                            "method `%s` is private to class `%s`",
                            expr->as.field.field,
                            structure->as.structure.name);
                    expr->resolved_decl = selected_method;
                    expr->as.field.bound_method = true;
                    result = checker->expected_type;
                    goto checked_expression;
                }
                if (declared_property_accessor(
                        structure, expr->as.field.field, true) != NULL) {
                    lang_diag(checker->diagnostics, expr->span,
                              "property `%s` is write-only",
                              expr->as.field.field);
                    result = &type_error;
                    break;
                }
            }
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
            else if (object->kind == TYPE_NAMED ||
                     object->kind == TYPE_CLASS) {
                const Decl *decl = object->declaration;
                if (decl != NULL &&
                    (decl->kind == DECL_STRUCT ||
                     decl->kind == DECL_CLASS)) {
                    const char *previous_module = checker->current_module;
                    checker->current_module = decl->module_name;
                    for (size_t f = 0U; f < decl->as.structure.field_count; ++f)
                        if (strcmp(decl->as.structure.fields[f].name,
                                   expr->as.field.field) == 0) {
                            if (!class_member_accessible(
                                    checker, decl,
                                    decl->as.structure.fields[f].is_public))
                                lang_diag(
                                    checker->diagnostics, expr->span,
                                    "field `%s` is private to class `%s`",
                                    expr->as.field.field,
                                    decl->as.structure.name);
                            result = resolve_type_syntax_in_applied_declaration(
                                checker, object,
                                decl->as.structure.fields[f].type_syntax,
                                decl->as.structure.fields[f].type_name,
                                decl->as.structure.fields[f].span);
                        }
                    checker->current_module = previous_module;
                }
                if (result == &type_error)
                    lang_diag(checker->diagnostics, expr->span,
                              "unknown field `%s` on `%s`",
                              expr->as.field.field, object->name);
            } else
                lang_diag(checker->diagnostics, expr->span,
                          "unknown field `%s` on `%s`", expr->as.field.field, object->name);
            if (result != &type_error &&
                type_moves_by_default(checker, result)) {
                bool copyable = type_is_copyable(checker, result);
                if (!copyable && checker->copy_depth == 0U &&
                    checker->borrow_depth == 0U &&
                    object->kind == TYPE_OPTION &&
                    expr->as.field.object->kind == EXPR_NAME) {
                    Local *owner = find_local(
                        checker, expr->as.field.object->as.name);
                    if (owner != NULL)
                        checker_move_local(
                            checker, owner,
                            expr->as.field.object->span);
                } else if (checker->copy_depth == 0U &&
                           checker->borrow_depth == 0U &&
                           checker_expression_is_local_place(
                               checker, expr)) {
                    Expr *owner_expr = expr->as.field.object;
                    const Decl *owner_decl = owner_expr->type != NULL
                        ? owner_expr->type->declaration : NULL;
                    if (owner_expr->kind == EXPR_NAME &&
                        owner_decl != NULL &&
                        owner_decl->kind == DECL_STRUCT) {
                        Local *owner = find_local(
                            checker, owner_expr->as.name);
                        bool movable = owner != NULL &&
                            owner->available && !owner->borrowed;
                        if (!copyable && owner != NULL)
                            checker_move_local(
                                checker, owner, owner_expr->span);
                        expr->as.field.move_out = movable;
                    } else if (!copyable) {
                        lang_diag(
                            checker->diagnostics, expr->span,
                            "cannot copy noncopyable field `%s`",
                            expr->as.field.field);
                    }
                }
            }
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
                result = resolve_declared_type(
                    checker, expr->as.structure.type_syntax,
                    expr->as.structure.name, expr->span);
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
                    ? resolve_type_syntax_in_applied_declaration(
                        checker, result,
                        declared_field->type_syntax,
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
                    if (!type_assignable(expected, actual))
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
    if (expr->kind == EXPR_CALL || expr->kind == EXPR_COPY ||
        expr->kind == EXPR_NAME || expr->kind == EXPR_FIELD ||
        expr->kind == EXPR_INDEX)
        set_cleanup_plan(
            checker, &expr->error_cleanup,
            checker->exception_depth != 0U
                ? checker->exception_local_bases[
                    checker->exception_depth - 1U]
                : 0U);
    expr->type = result;
    return result;
}
