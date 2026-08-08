#include "checker_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool class_member_accessible(
    const Checker *checker, const Decl *owner, bool is_public
) {
    if (owner == NULL || owner->kind != DECL_CLASS || is_public)
        return true;
    return checker->function != NULL &&
           checker->function->owner_type != NULL &&
           strcmp(checker->function->owner_type,
                  owner->as.structure.name) == 0 &&
           checker->current_module != NULL && owner->module_name != NULL &&
           strcmp(checker->current_module, owner->module_name) == 0;
}
static Function *declared_property_accessor_inner(
    const Decl *owner, const char *name, bool setter, size_t depth
) {
    if (owner == NULL ||
        (owner->kind != DECL_STRUCT && owner->kind != DECL_CLASS) ||
        depth >= 256U)
        return NULL;
    for (size_t member = 0U;
         member < owner->as.structure.member_count; ++member) {
        Function *function =
            &owner->as.structure.members[member]->as.function;
        if ((setter ? function->is_property_setter
                    : function->is_property_getter) &&
            function->property_name != NULL &&
            strcmp(function->property_name, name) == 0)
            return function;
    }
    Function *inherited = declared_property_accessor_inner(
        owner->as.structure.base_class, name, setter, depth + 1U);
    if (inherited != NULL) return inherited;
    for (size_t interface = 0U;
         interface < owner->as.structure.interface_count; ++interface) {
        inherited = declared_property_accessor_inner(
            owner->as.structure.interfaces[interface], name, setter,
            depth + 1U);
        if (inherited != NULL) return inherited;
    }
    return NULL;
}

Function *declared_property_accessor(
    const Decl *owner, const char *name, bool setter
) {
    return declared_property_accessor_inner(owner, name, setter, 0U);
}

const Decl *current_property_owner(Checker *checker) {
    if (checker->function == NULL ||
        checker->function->owner_type == NULL)
        return NULL;
    for (size_t i = 0U; i < checker->module->count; ++i) {
        const Decl *decl = checker->module->decls[i];
        if ((decl->kind == DECL_STRUCT || decl->kind == DECL_CLASS) &&
            strcmp(decl->as.structure.name,
                   checker->function->owner_type) == 0 &&
            decl->module_name != NULL && checker->current_module != NULL &&
            strcmp(decl->module_name, checker->current_module) == 0)
            return decl;
    }
    return NULL;
}

FieldDecl *checker_static_field_from_path(
    Checker *checker, const char *path, const Decl **out_owner
) {
    const char *separator = last_path_separator(path);
    if (separator == NULL) return NULL;
    size_t owner_length = (size_t)(separator - path);
    char *owner_path = lang_arena_strndup(
        &checker->module->arena, path, owner_length);
    const char *field_name = separator + 2U;
    for (size_t i = 0U; i < checker->module->count; ++i) {
        Decl *owner = checker->module->decls[i];
        if (owner->kind != DECL_STRUCT && owner->kind != DECL_CLASS)
            continue;
        if (!visible_declaration_path_matches(
                checker, owner_path, owner->as.structure.name,
                owner->module_name))
            continue;
        for (size_t field = 0U;
             field < owner->as.structure.static_field_count; ++field) {
            FieldDecl *candidate =
                &owner->as.structure.static_fields[field];
            if (strcmp(candidate->name, field_name) != 0) continue;
            if (out_owner != NULL) *out_owner = owner;
            return candidate;
        }
    }
    return NULL;
}

void checker_rewrite_unqualified_static_field(
    Checker *checker, Expr *expr
) {
    const Decl *owner = current_property_owner(checker);
    if (owner == NULL) return;
    for (size_t field = 0U;
         field < owner->as.structure.static_field_count; ++field) {
        FieldDecl *candidate = &owner->as.structure.static_fields[field];
        if (strcmp(candidate->name, expr->as.name) != 0) continue;
        Expr *type_name = lang_arena_alloc(
            &checker->module->arena, sizeof(*type_name));
        memset(type_name, 0, sizeof(*type_name));
        type_name->kind = EXPR_NAME;
        type_name->span = expr->span;
        type_name->as.name = owner->as.structure.name;
        const char *field_name = expr->as.name;
        expr->kind = EXPR_FIELD;
        expr->as.field.object = type_name;
        expr->as.field.field = field_name;
        return;
    }
}

Function *static_property_accessor(
    Checker *checker, const char *name, bool setter
) {
    for (size_t i = 0U; i < checker->module->count; ++i) {
        Decl *decl = checker->module->decls[i];
        Function *function = decl->kind == DECL_FUNCTION
            ? &decl->as.function : NULL;
        if (function != NULL && function->is_static_member &&
            (setter ? function->is_property_setter
                    : function->is_property_getter) &&
            strcmp(function->name, name) == 0)
            return function;
    }
    return NULL;
}

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

Type *resolve_declared_type_in_module(
    Checker *checker, const TypeSyntax *syntax, const char *fallback_name,
    LangSpan span, const char *module_name) {
    const char *previous_module = checker->current_module;
    checker->current_module = module_name;
    Type *type = resolve_declared_type(
        checker, syntax, fallback_name, span);
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

void set_cleanup_plan(Checker *checker, CleanupPlan *plan,
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

void snapshot_local_flow(const Checker *checker,
                         LocalFlowState state[256]) {
    for (size_t i = 0U; i < checker->local_count; ++i) {
        state[i].definitely_assigned =
            checker->locals[i].definitely_assigned;
        state[i].available = checker->locals[i].available;
        state[i].moved_at = checker->locals[i].moved_at;
    }
}

void restore_local_flow(Checker *checker,
                        const LocalFlowState state[256]) {
    for (size_t i = 0U; i < checker->local_count; ++i)
    {
        if (checker->locals[i].is_out_parameter)
            checker->locals[i].definitely_assigned =
                state[i].definitely_assigned;
        checker->locals[i].available = state[i].available;
        checker->locals[i].moved_at = state[i].moved_at;
    }
}

void merge_local_flow(Checker *checker,
                      const LocalFlowState left[256],
                      const LocalFlowState right[256]) {
    for (size_t i = 0U; i < checker->local_count; ++i) {
        if (checker->locals[i].is_out_parameter)
            checker->locals[i].definitely_assigned =
                left[i].definitely_assigned &&
                right[i].definitely_assigned;
        checker->locals[i].available =
            left[i].available && right[i].available;
        checker->locals[i].moved_at = !left[i].available
            ? left[i].moved_at : right[i].moved_at;
    }
}

bool checker_require_available(
    Checker *checker, const Local *local, LangSpan use_span
) {
    if (local == NULL || local->available) return true;
    if (checker->last_unavailable_local_id == local->id &&
        checker->last_unavailable_use.file == use_span.file &&
        checker->last_unavailable_use.start == use_span.start &&
        checker->last_unavailable_use.end == use_span.end)
        return false;
    checker->last_unavailable_local_id = local->id;
    checker->last_unavailable_use = use_span;
    LangDiagnostic *diagnostic = lang_diag(
        checker->diagnostics, use_span,
        "`%s` was moved and cannot be used before reassignment",
        local->name);
    if (local->moved_at.file != NULL)
        lang_diag_secondary(
            diagnostic, local->moved_at, "value moved here");
    return false;
}

void checker_move_local(
    Checker *checker, Local *local, LangSpan move_span
) {
    if (!checker_require_available(checker, local, move_span)) return;
    if (local->borrowed) {
        lang_diag(
            checker->diagnostics, move_span,
            "cannot move from borrowed local `%s`; use `copy(%s)`",
            local->name, local->name);
        return;
    }
    local->available = false;
    local->moved_at = move_span;
}

void require_assigned_out_parameters(Checker *checker,
                                            LangSpan span) {
    for (size_t i = 0U; i < checker->local_count; ++i) {
        const Local *local = &checker->locals[i];
        if (local->is_out_parameter && !local->definitely_assigned) {
            bool constructor_field = false;
            if (checker->function != NULL &&
                checker->function->is_constructor)
                for (size_t field = 0U;
                     field < checker->function->constructor_field_count;
                     ++field)
                    if (checker->function
                            ->constructor_field_binding_ids[field] ==
                        local->id)
                        constructor_field = true;
            if (constructor_field)
                lang_diag(checker->diagnostics, span,
                          "constructor field `%s` must be assigned before returning",
                          local->name);
            else
                lang_diag(checker->diagnostics, span,
                          "`out` parameter `%s` must be assigned before returning",
                          local->name);
        }
    }
}

Type *check_place(Checker *checker, Expr *expr) {
    if (expr->kind == EXPR_NAME) {
        if (find_local(checker, expr->as.name) == NULL)
            checker_rewrite_unqualified_static_field(checker, expr);
        if (expr->kind != EXPR_NAME)
            return check_place(checker, expr);
        Local *local = NULL;
        if (expr->resolved_local_id != 0U)
            for (size_t i = 0U; i < checker->local_count; ++i)
                if (checker->locals[i].id == expr->resolved_local_id)
                    local = &checker->locals[i];
        if (local == NULL) local = find_local(checker, expr->as.name);
        Local *this_local = find_local(checker, "this");
        if (local == NULL && this_local != NULL &&
            (this_local->type->kind == TYPE_NAMED ||
             this_local->type->kind == TYPE_CLASS) &&
            this_local->type->declaration != NULL &&
            (this_local->type->declaration->kind == DECL_STRUCT ||
             this_local->type->declaration->kind == DECL_CLASS)) {
            const Decl *owner = this_local->type->declaration;
            for (size_t field = 0U;
                 field < owner->as.structure.field_count; ++field) {
                if (strcmp(owner->as.structure.fields[field].name,
                           expr->as.name) != 0)
                    continue;
                Expr *object = lang_arena_alloc(
                    &checker->module->arena, sizeof(*object));
                memset(object, 0, sizeof(*object));
                object->kind = EXPR_NAME;
                object->span = expr->span;
                object->as.name = "this";
                const char *field_name = expr->as.name;
                expr->kind = EXPR_FIELD;
                expr->as.field.object = object;
                expr->as.field.field = field_name;
                return check_place(checker, expr);
            }
        }
        if (local == NULL) {
            lang_diag(checker->diagnostics, expr->span,
                      "unknown name `%s`", expr->as.name);
            expr->type = &type_error;
            return &type_error;
        }
        expr->type = local->type;
        expr->resolved_local_id = local->id;
        if (local->is_out_parameter &&
            !local->definitely_assigned &&
            checker->allowed_unassigned_out_place != expr)
            lang_diag(checker->diagnostics, expr->span,
                      "`out` parameter `%s` cannot be read before assignment",
                      local->name);
        if (checker->allowed_unassigned_out_place != expr)
            (void)checker_require_available(
                checker, local, expr->span);
        return local->type;
    }
    if (expr->kind == EXPR_FIELD) {
        const char *static_path = checker_static_call_path(checker, expr);
        const Decl *static_owner = NULL;
        FieldDecl *static_field = static_path != NULL
            ? checker_static_field_from_path(
                  checker, static_path, &static_owner)
            : NULL;
        if (static_field != NULL) {
            if (!class_member_accessible(
                    checker, static_owner, static_field->is_public))
                lang_diag(checker->diagnostics, expr->span,
                          "static field `%s` is private to class `%s`",
                          static_field->name,
                          static_owner->as.structure.name);
            expr->as.field.static_field = true;
            expr->resolved_decl = static_owner;
            expr->type = static_field->checked_type;
            return static_field->checked_type;
        }
        if (expr->as.field.object->kind == EXPR_NAME &&
            strcmp(expr->as.field.object->as.name, "this") == 0 &&
            find_local(checker, "this") == NULL &&
            checker->function != NULL &&
            checker->function->is_constructor) {
            for (size_t field = 0U;
                 field < checker->function->constructor_field_count;
                 ++field) {
                Local *candidate = NULL;
                for (size_t local = 0U;
                     local < checker->local_count; ++local)
                    if (checker->locals[local].id ==
                        checker->function
                            ->constructor_field_binding_ids[field])
                        candidate = &checker->locals[local];
                if (candidate == NULL ||
                    strcmp(candidate->name,
                           expr->as.field.field) != 0)
                    continue;
                expr->kind = EXPR_NAME;
                expr->as.name = candidate->name;
                expr->resolved_local_id = candidate->id;
                return check_place(checker, expr);
            }
        }
        Type *object = check_place(checker, expr->as.field.object);
        Type *result = &type_error;
        if (object->kind == TYPE_OPTION &&
            strcmp(expr->as.field.field, "Value") == 0) {
            result = object->element;
        } else if (object->kind == TYPE_BUFFER &&
            strcmp(expr->as.field.field, "len") == 0) {
            result = &type_i64;
        } else if (object->kind == TYPE_NAMED ||
                   object->kind == TYPE_CLASS) {
            const Decl *decl = object->declaration;
            if (decl != NULL &&
                (decl->kind == DECL_STRUCT || decl->kind == DECL_CLASS)) {
                const char *previous_module = checker->current_module;
                checker->current_module = decl->module_name;
                for (size_t field = 0U;
                     field < decl->as.structure.field_count; ++field)
                    if (strcmp(
                            decl->as.structure.fields[field].name,
                            expr->as.field.field) == 0) {
                        if (!class_member_accessible(
                                checker, decl,
                                decl->as.structure.fields[field].is_public))
                            lang_diag(checker->diagnostics, expr->span,
                                      "field `%s` is private to class `%s`",
                                      expr->as.field.field,
                                      decl->as.structure.name);
                        result = resolve_type_syntax_in_applied_declaration(
                            checker, object,
                            decl->as.structure.fields[field].type_syntax,
                            decl->as.structure.fields[field].type_name,
                            decl->as.structure.fields[field].span);
                    }
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
    if (expr->kind == EXPR_INDEX) {
        Type *object = check_place(checker, expr->as.index.object);
        Type *index = check_expr(checker, expr->as.index.index);
        if (object->kind != TYPE_ARRAY) {
            lang_diag(
                checker->diagnostics, expr->span,
                "borrowed indexed places currently require a fixed array");
            expr->type = &type_error;
            return &type_error;
        }
        if (!is_integer(index))
            lang_diag(
                checker->diagnostics, expr->as.index.index->span,
                "array index must be an integer");
        if (expr->as.index.index->kind == EXPR_INT &&
            expr->as.index.index->as.integer >= object->array_length)
            lang_diag(
                checker->diagnostics, expr->as.index.index->span,
                "constant array index is out of bounds for length %zu",
                object->array_length);
        expr->as.index.unchecked = checker->unsafe_depth != 0U;
        expr->type = object->element;
        return object->element;
    }
    return check_expr(checker, expr);
}

static Type *checker_local_place_type(
    Checker *checker, const Expr *expr
) {
    if (expr == NULL) return false;
    if (expr->kind == EXPR_NAME) {
        Local *local = find_local(checker, expr->as.name);
        return local != NULL ? local->type : NULL;
    }
    if (expr->kind == EXPR_FIELD && !expr->as.field.static_field) {
        Type *object = checker_local_place_type(
            checker, expr->as.field.object);
        if (object == NULL) return NULL;
        if (object->kind == TYPE_OPTION &&
            strcmp(expr->as.field.field, "Value") == 0)
            return object->element;
        if (object->kind == TYPE_BUFFER &&
            strcmp(expr->as.field.field, "len") == 0)
            return &type_i64;
        if ((object->kind == TYPE_NAMED || object->kind == TYPE_CLASS) &&
            object->declaration != NULL) {
            for (const Decl *owner = object->declaration;
                 owner != NULL;
                 owner = object->kind == TYPE_CLASS
                    ? owner->as.structure.base_class : NULL)
                for (size_t field = 0U;
                     field < owner->as.structure.field_count; ++field)
                    if (strcmp(
                            owner->as.structure.fields[field].name,
                            expr->as.field.field) == 0) {
                        FieldDecl *declared =
                            &owner->as.structure.fields[field];
                        return resolve_type_syntax_in_applied_declaration(
                            checker, object, declared->type_syntax,
                            declared->type_name, declared->span);
                    }
        }
        return NULL;
    }
    if (expr->kind == EXPR_INDEX) {
        Type *object = checker_local_place_type(
            checker, expr->as.index.object);
        return object != NULL && object->kind == TYPE_ARRAY
            ? object->element : NULL;
    }
    return NULL;
}

bool checker_expression_is_local_place(
    Checker *checker, const Expr *expr
) {
    return checker_local_place_type(checker, expr) != NULL;
}

bool checker_expression_is_borrowable(
    Checker *checker, const Expr *expr
) {
    if (checker_expression_is_local_place(checker, expr)) return true;
    if (expr != NULL && expr->kind == EXPR_INDEX) {
        Type *object = checker_local_place_type(
            checker, expr->as.index.object);
        if (object != NULL &&
            (object->kind == TYPE_VEC ||
             object->kind == TYPE_DICTIONARY ||
             object->kind == TYPE_HASH_SET ||
             object->kind == TYPE_QUEUE ||
             object->kind == TYPE_STACK))
            return true;
    }
    if (expr == NULL || expr->kind != EXPR_CALL)
        return false;
    if (expr->as.call.callee->kind == EXPR_FIELD &&
        expr->as.call.arguments.count == 0U &&
        (strcmp(expr->as.call.callee->as.field.field, "Peek") == 0) &&
        checker_expression_is_local_place(
            checker, expr->as.call.callee->as.field.object))
        return true;
    if (expr->as.call.callee->kind != EXPR_NAME ||
        expr->as.call.arguments.count == 0U)
        return false;
    const char *name = expr->as.call.callee->as.name;
    bool projection =
        strcmp(name, "List::Get") == 0 ||
        strcmp(name, "Queue::Peek") == 0 ||
        strcmp(name, "Stack::Peek") == 0 ||
        strcmp(name, "Dictionary::Get") == 0 ||
        strcmp(name, "Dictionary::KeyAt") == 0 ||
        strcmp(name, "Dictionary::ValueAt") == 0;
    return projection && checker_expression_is_local_place(
        checker, expr->as.call.arguments.items[0]);
}

Type *check_borrowed_expr(Checker *checker, Expr *expr) {
    if (expr->kind == EXPR_NAME)
        return check_place(checker, expr);
    ++checker->borrow_depth;
    Type *result = check_expr(checker, expr);
    --checker->borrow_depth;
    return result;
}
