#include "checker_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool check_stmt(Checker *checker, Stmt *stmt) {
    switch (stmt->kind) {
        case STMT_DELETE: {
            Type *type = check_expr(checker, stmt->as.delete_value);
            if (type->kind != TYPE_CLASS)
                lang_diag(checker->diagnostics, stmt->span,
                          "`delete` requires a class reference; found `%s`",
                          type->name);
            if (stmt->as.delete_value->kind == EXPR_NAME &&
                strcmp(stmt->as.delete_value->as.name, "this") == 0)
                lang_diag(checker->diagnostics, stmt->span,
                          "an instance method cannot delete `this`");
            return true;
        }
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
                           ? resolve_declared_type(
                                 checker, stmt->as.let.type_syntax,
                                 stmt->as.let.type_name, stmt->span)
                           : NULL;
            Type *previous_expected = checker->expected_type;
            checker->expected_type = declared;
            Type *value = check_expr(checker, stmt->as.let.value);
            checker->expected_type = previous_expected;
            if (declared == NULL) declared = value;
            if (coerce_literal(checker, stmt->as.let.value, declared))
                value = declared;
            if (!type_assignable(declared, value))
                lang_diag(checker->diagnostics, stmt->span,
                          "initializer expects `%s`, found `%s`",
                          declared->name, value->name);
            if (strcmp(stmt->as.let.name, "_") == 0)
                break;
            checker->locals[checker->local_count++] = (Local){
                stmt->as.let.name, declared, stmt->as.let.mutable_,
                false, checker->depth, stmt->span,
                ++checker->next_local_id, false, true
            };
            stmt->as.let.binding_id =
                checker->locals[checker->local_count - 1U].id;
            stmt->as.let.checked_type = declared;
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
            Type *aggregate = check_place(checker, value_expr);
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
                Type *expected = resolve_type_syntax_in_applied_declaration(
                    checker, aggregate,
                    structure->as.structure.fields[field].type_syntax,
                    structure->as.structure.fields[field].type_name,
                    structure->as.structure.fields[field].span);
                Type *declared = resolve_declared_type(
                    checker, stmt->as.destructure.type_syntaxes[field],
                    stmt->as.destructure.type_names[field], stmt->span);
                stmt->as.destructure.checked_types[field] = expected;
                (void)checker_require_copyable(
                    checker, expected, stmt->span);
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
                    binding_id, false, true
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
            if (!type_assignable(logical_return, actual))
                lang_diag(checker->diagnostics, stmt->span,
                          "return expects `%s`, found `%s`",
                          logical_return->name, actual->name);
            require_assigned_out_parameters(checker, stmt->span);
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
            /*
             * Calls and throws inside the try clean locals introduced after
             * the innermost handler boundary.  If a catch does not match (or
             * an exceptional finally completes), the exception crosses the
             * next boundary as well; retain the cleanup needed for that
             * second transfer on the try statement itself.
             */
            size_t transfer_base = checker->exception_depth != 0U
                ? checker->exception_local_bases[
                      checker->exception_depth - 1U]
                : 0U;
            set_cleanup_plan(
                checker, &stmt->exit_cleanup, transfer_base);
            bool before_try[256];
            bool body_state[256];
            bool catch_state[256];
            snapshot_out_assignment(checker, before_try);
            checker->exception_local_bases[checker->exception_depth++] =
                checker->local_count;
            bool body_falls = check_stmt(checker, stmt->as.try_.body);
            snapshot_out_assignment(checker, body_state);
            --checker->exception_depth;
            bool catch_falls = false;
            if (stmt->as.try_.catch_body != NULL) {
                restore_out_assignment(checker, before_try);
                Type *caught = resolve_declared_type(
                    checker, stmt->as.try_.catch_type_syntax,
                    stmt->as.try_.catch_type_name, stmt->span);
                stmt->as.try_.catch_type = caught;
                if (!is_exception_type(caught))
                    lang_diag(checker->diagnostics, stmt->span,
                              "catch type must derive from `Exception`");
                size_t base = checker->local_count;
                size_t binding = ++checker->next_local_id;
                checker->locals[checker->local_count++] = (Local){
                    stmt->as.try_.catch_name, caught, false, false,
                    checker->depth + 1U, stmt->span, binding,
                    false, true
                };
                stmt->as.try_.catch_binding_id = binding;
                bool catch_has_finally_handler =
                    stmt->as.try_.finally_body != NULL;
                if (catch_has_finally_handler) {
                    if (checker->exception_depth >= 32U) {
                        lang_diag(
                            checker->diagnostics, stmt->span,
                            "exception-handler nesting limit exceeded");
                        catch_has_finally_handler = false;
                    } else {
                        /* A failure in the catch first exits the catch scope,
                         * then runs finally, before crossing the outer
                         * exception boundary. */
                        checker->exception_local_bases[
                            checker->exception_depth++] = base;
                    }
                }
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
                snapshot_out_assignment(checker, catch_state);
                if (pushed_catch)
                    --checker->catch_depth;
                if (catch_has_finally_handler)
                    --checker->exception_depth;
                checker->local_count = base;
            }
            if (body_falls && catch_falls)
                merge_out_assignment(checker, body_state, catch_state);
            else if (body_falls)
                restore_out_assignment(checker, body_state);
            else if (catch_falls)
                restore_out_assignment(checker, catch_state);
            else
                restore_out_assignment(checker, before_try);
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
            bool before[256];
            bool then_state[256];
            bool else_state[256];
            snapshot_out_assignment(checker, before);
            bool then_falls = check_stmt(checker, stmt->as.if_.then_branch);
            snapshot_out_assignment(checker, then_state);
            restore_out_assignment(checker, before);
            bool else_falls = true;
            if (stmt->as.if_.else_branch != NULL)
                else_falls = check_stmt(checker, stmt->as.if_.else_branch);
            snapshot_out_assignment(checker, else_state);
            if (then_falls && else_falls)
                merge_out_assignment(checker, then_state, else_state);
            else if (then_falls)
                restore_out_assignment(checker, then_state);
            else if (else_falls)
                restore_out_assignment(checker, else_state);
            return then_falls || else_falls;
        }
        case STMT_WHILE: {
            if (check_expr(checker, stmt->as.while_.condition)->kind != TYPE_BOOL)
                lang_diag(checker->diagnostics, stmt->as.while_.condition->span,
                          "`while` condition must be `bool`");
            bool before_loop[256];
            snapshot_out_assignment(checker, before_loop);
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
            restore_out_assignment(checker, before_loop);
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
                       iterable->kind != TYPE_READONLY_SPAN &&
                       iterable->kind != TYPE_STRING) {
                lang_diag(checker->diagnostics, stmt->as.for_.iterable->span,
                          stmt->as.for_.foreach
                              ? "`foreach` requires a string, fixed array, Span, or Vec"
                              : "`for` requires a string, fixed array, Span, Vec, or integer range");
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
                Type *declared = resolve_declared_type(
                    checker, stmt->as.for_.type_syntax,
                    stmt->as.for_.type_name, stmt->span);
                if (!same_type(declared, element))
                    lang_diag(
                        checker->diagnostics, stmt->span,
                        "`foreach` variable expects `%s`, found `%s`",
                        declared->name, element->name);
            }
            bool before_loop[256];
            snapshot_out_assignment(checker, before_loop);
            size_t outer_count = checker->local_count;
            ++checker->depth;
            if (checker->local_count < 256U)
                checker->locals[checker->local_count++] = (Local){
                    stmt->as.for_.name,
                    element,
                    true, false,
                    checker->depth, stmt->span,
                    ++checker->next_local_id, false, true
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
            restore_out_assignment(checker, before_loop);
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

            bool before_loop[256];
            snapshot_out_assignment(checker, before_loop);

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
            restore_out_assignment(checker, before_loop);
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
            bool before_match[256];
            bool merged_match[256];
            snapshot_out_assignment(checker, before_match);
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
                restore_out_assignment(checker, before_match);
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
                                if (strcmp(variant->type_name, "Unit") != 0)
                                    payload =
                                        resolve_type_syntax_in_applied_declaration(
                                            checker, matched,
                                            variant->type_syntax,
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
                    Type *declared = resolve_declared_type(
                        checker, arm->binding_type_syntax,
                        arm->binding_type_name, arm->span);
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
                        ++checker->next_local_id, false, true
                    };
                if (arm->binding != NULL && payload != NULL &&
                    checker->local_count > match_local_count)
                    arm->binding_id =
                        checker->locals[checker->local_count - 1U].id;
                bool arm_falls = check_stmt(checker, arm->body);
                if (arm_falls) {
                    bool arm_state[256];
                    snapshot_out_assignment(checker, arm_state);
                    if (!have_fallthrough_arm) {
                        memcpy(merged_match, arm_state,
                               match_local_count *
                                   sizeof(*merged_match));
                    } else {
                        for (size_t local = 0U;
                             local < match_local_count; ++local)
                            merged_match[local] =
                                merged_match[local] && arm_state[local];
                    }
                    have_fallthrough_arm = true;
                }
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
            if (have_fallthrough_arm)
                restore_out_assignment(checker, merged_match);
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
                bool before_unreachable[256];
                if (!falls_through)
                    snapshot_out_assignment(checker, before_unreachable);
                bool statement_falls =
                    check_stmt(checker, stmt->as.block.items[i]);
                if (falls_through) falls_through = statement_falls;
                else
                    restore_out_assignment(checker, before_unreachable);
            }
            set_cleanup_plan(
                checker, &stmt->exit_cleanup,
                function_body ? 0U : start);
            if (function_body && checker->function != NULL &&
                checker->function->is_constructor &&
                stmt->exit_cleanup.count != 0U) {
                size_t output = 0U;
                for (size_t cleanup = 0U;
                     cleanup < stmt->exit_cleanup.count; ++cleanup) {
                    size_t binding =
                        stmt->exit_cleanup.binding_ids[cleanup];
                    bool field = false;
                    for (size_t index = 0U;
                         index < checker->function->constructor_field_count;
                         ++index)
                        field = field ||
                            checker->function
                                ->constructor_field_binding_ids[index] ==
                            binding;
                    if (!field)
                        stmt->exit_cleanup.binding_ids[output++] = binding;
                }
                stmt->exit_cleanup.count = output;
            }
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
