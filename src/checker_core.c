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

void snapshot_out_assignment(const Checker *checker,
                                    bool assigned[256]) {
    for (size_t i = 0U; i < checker->local_count; ++i)
        assigned[i] = checker->locals[i].definitely_assigned;
}

void restore_out_assignment(Checker *checker,
                                   const bool assigned[256]) {
    for (size_t i = 0U; i < checker->local_count; ++i)
        if (checker->locals[i].is_out_parameter)
            checker->locals[i].definitely_assigned = assigned[i];
}

void merge_out_assignment(Checker *checker,
                                 const bool left[256],
                                 const bool right[256]) {
    for (size_t i = 0U; i < checker->local_count; ++i)
        if (checker->locals[i].is_out_parameter)
            checker->locals[i].definitely_assigned =
                left[i] && right[i];
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
        if (object->kind == TYPE_BUFFER &&
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
    return check_expr(checker, expr);
}
