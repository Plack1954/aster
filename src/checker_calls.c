#include "checker_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct CallOverloadSet {
    const Decl *first_named;
    const Decl *selected;
    size_t named_count;
    size_t arity_count;
    size_t exact_count;
} CallOverloadSet;

static ParameterMode call_argument_mode(const Expr *call, size_t index) {
    return call->as.call.argument_modes != NULL
        ? call->as.call.argument_modes[index]
        : PARAMETER_MODE_VALUE;
}

static bool list_element_has_default_equality(const Type *type) {
    return type != NULL &&
        (type->kind == TYPE_BOOL || is_numeric(type) ||
         type->kind == TYPE_CHAR || type->kind == TYPE_STR ||
         type->kind == TYPE_STRING || type->kind == TYPE_RAW_POINTER);
}

static bool is_hash_storage_type(const Type *type) {
    return type != NULL &&
        (type->kind == TYPE_DICTIONARY || type->kind == TYPE_HASH_SET);
}

static void require_mutable_dictionary(
    Checker *checker, Expr *receiver) {
    if (receiver->kind != EXPR_NAME) return;
    Local *local = find_local(checker, receiver->as.name);
    if (local != NULL && !local->mutable_)
        lang_diag(checker->diagnostics, receiver->span,
                  "cannot mutate immutable hash collection `%s`", local->name);
}

static bool call_candidate_named_and_visible(
    const Checker *checker, const Decl *decl, const char *name,
    bool current_only) {
    if (decl->kind != DECL_FUNCTION) return false;
    bool current = checker->current_module != NULL &&
        decl->module_name != NULL &&
        strcmp(checker->current_module, decl->module_name) == 0 &&
        strcmp(decl->as.function.name, name) == 0;
    if (current_only) return current;
    return current ||
        (decl->is_public && imported_declaration_matches(
            checker, name, decl->as.function.name,
            decl->module_name));
}

static bool has_current_call_candidate(
    const Checker *checker, const char *name) {
    for (size_t i = 0U; i < checker->module->count; ++i)
        if (call_candidate_named_and_visible(
                checker, checker->module->decls[i], name, true))
            return true;
    return false;
}

static size_t overload_argument_rank(
    const Expr *argument, const Type *actual, const Type *expected) {
    if (expected == NULL) return SIZE_MAX;
    if (argument->kind == EXPR_INT && is_integer(expected)) {
        switch (expected->kind) {
            case TYPE_I32: return 0U;
            case TYPE_U32: return 1U;
            case TYPE_I64: return 2U;
            case TYPE_U64: return 3U;
            case TYPE_I16: return 4U;
            case TYPE_U16: return 5U;
            case TYPE_I8: return 6U;
            case TYPE_U8: return 7U;
            case TYPE_ISIZE: return 8U;
            case TYPE_USIZE: return 9U;
            default: return SIZE_MAX;
        }
    }
    if (argument->kind == EXPR_UNARY &&
        argument->as.unary.op == TOK_MINUS &&
        argument->as.unary.operand->kind == EXPR_INT &&
        is_signed_integer(expected)) {
        switch (expected->kind) {
            case TYPE_I32: return 0U;
            case TYPE_I64: return 1U;
            case TYPE_I16: return 2U;
            case TYPE_I8: return 3U;
            case TYPE_ISIZE: return 4U;
            default: return SIZE_MAX;
        }
    }
    if (argument->kind == EXPR_FLOAT && is_float(expected))
        return expected->kind == TYPE_F64 ? 0U : 1U;
    if (actual != NULL && same_type(actual, expected)) return 0U;
    if (actual != NULL && actual->kind == TYPE_SLICE &&
        expected->kind == TYPE_READONLY_SPAN &&
        same_type(actual->element, expected->element))
        return 0U;
    if (argument->kind == EXPR_NULL &&
        (expected->kind == TYPE_OPTION ||
         expected->kind == TYPE_RAW_POINTER))
        return 0U;
    if (argument->kind == EXPR_STRUCT &&
        argument->as.structure.name == NULL &&
        expected->kind == TYPE_NAMED)
        return 0U;
    return SIZE_MAX;
}

static CallOverloadSet collect_call_overloads(
    Checker *checker, const char *name, const Expr *call,
    bool require_exact_types) {
    CallOverloadSet result = {0};
    size_t best_rank = SIZE_MAX;
    bool current_only = has_current_call_candidate(checker, name);
    for (size_t i = 0U; i < checker->module->count; ++i) {
        const Decl *decl = checker->module->decls[i];
        if (!call_candidate_named_and_visible(
                checker, decl, name, current_only))
            continue;
        if (result.first_named == NULL) result.first_named = decl;
        ++result.named_count;
        const Function *function = &decl->as.function;
        if (function->param_count != call->as.call.arguments.count)
            continue;
        ++result.arity_count;
        if (!require_exact_types) {
            result.selected = decl;
            continue;
        }
        if (decl->type_param_count != 0U) continue;
        bool exact = true;
        size_t rank = 0U;
        for (size_t p = 0U; p < function->param_count; ++p) {
            ParameterMode call_mode = call_argument_mode(call, p);
            bool call_ref =
                call_mode == PARAMETER_MODE_MUTABLE_REFERENCE;
            bool call_out = call_mode == PARAMETER_MODE_OUT;
            bool parameter_ref = function->params[p].by_ref &&
                !function->params[p].by_out;
            if (!(p == 0U && call->as.call.implicit_receiver) &&
                (call_ref != parameter_ref ||
                 call_out != function->params[p].by_out)) {
                exact = false;
                break;
            }
            Type *actual = call->as.call.arguments.items[p]->type;
            Type *expected = function->params[p].checked_type;
            size_t argument_rank = overload_argument_rank(
                call->as.call.arguments.items[p], actual, expected);
            if (argument_rank == SIZE_MAX) {
                exact = false;
                break;
            }
            rank += argument_rank;
        }
        if (!exact) continue;
        if (rank < best_rank) {
            best_rank = rank;
            result.selected = decl;
            result.exact_count = 1U;
        } else if (rank == best_rank) {
            result.selected = decl;
            ++result.exact_count;
        }
    }
    return result;
}

static void diagnose_call_overloads(
    Checker *checker, const char *name, const Expr *call,
    const CallOverloadSet *set) {
    LangDiagnostic *diagnostic = lang_diag(
        checker->diagnostics, call->span,
        set->exact_count > 1U
            ? "call to `%s` is ambiguous"
            : "no overload of `%s` matches the argument types",
        name);
    bool current_only = has_current_call_candidate(checker, name);
    for (size_t i = 0U; i < checker->module->count; ++i) {
        const Decl *decl = checker->module->decls[i];
        if (!call_candidate_named_and_visible(
                checker, decl, name, current_only) ||
            decl->as.function.param_count !=
                call->as.call.arguments.count)
            continue;
        lang_diag_secondary(
            diagnostic, decl->span, "overload candidate");
    }
}

static const Decl *resolve_function_value_overload(
    Checker *checker, const char *name, LangSpan span) {
    bool current_only = has_current_call_candidate(checker, name);
    const Type *expected = checker->expected_type;
    const Decl *selected = NULL;
    size_t named_count = 0U;
    size_t exact_count = 0U;
    for (size_t i = 0U; i < checker->module->count; ++i) {
        const Decl *decl = checker->module->decls[i];
        if (!call_candidate_named_and_visible(
                checker, decl, name, current_only))
            continue;
        ++named_count;
        if (expected == NULL || expected->kind != TYPE_FUNCTION ||
            decl->type_param_count != 0U)
            continue;
        const Function *function = &decl->as.function;
        if (function->param_count != expected->argument_count ||
            !same_type(function->checked_return_type,
                       expected->element))
            continue;
        bool exact = true;
        for (size_t p = 0U; p < function->param_count; ++p) {
            ParameterMode expected_mode = expected->parameter_modes[p];
            bool expected_ref = parameter_mode_is_reference(expected_mode);
            bool expected_mutable = parameter_mode_is_mutable(expected_mode);
            bool expected_out = expected_mode == PARAMETER_MODE_OUT;
            if (!same_type(function->params[p].checked_type,
                           expected->arguments[p]) ||
                function->params[p].by_ref != expected_ref ||
                function->params[p].by_ref != expected_mutable ||
                function->params[p].by_out != expected_out) {
                exact = false;
                break;
            }
        }
        if (!exact) continue;
        selected = decl;
        ++exact_count;
    }
    if (exact_count == 1U) return selected;
    if (named_count <= 1U) {
        Function *function = find_function(checker, name, span);
        return function_declaration(checker, function);
    }
    LangDiagnostic *diagnostic = lang_diag(
        checker->diagnostics, span,
        expected != NULL && expected->kind == TYPE_FUNCTION
            ? "no overload of `%s` matches the target function type"
            : "overloaded function `%s` requires a target function type",
        name);
    for (size_t i = 0U; i < checker->module->count; ++i) {
        const Decl *decl = checker->module->decls[i];
        if (call_candidate_named_and_visible(
                checker, decl, name, current_only))
            lang_diag_secondary(
                diagnostic, decl->span, "overload candidate");
    }
    return selected;
}

static Type *function_value_type(
    Checker *checker, const Decl *declaration, LangSpan span,
    const Decl **resolved_declaration) {
    if (declaration->type_param_count != 0U) {
        lang_diag(
            checker->diagnostics, span,
            "generic function `%s` cannot be used as a value without concrete type arguments",
            declaration->as.function.name);
        return &type_error;
    }
    if (declaration->as.function.is_extern) {
        const Function *source = &declaration->as.function;
        Decl *wrapper = lang_arena_alloc(
            &checker->module->arena, sizeof(*wrapper));
        memset(wrapper, 0, sizeof(*wrapper));
        wrapper->kind = DECL_FUNCTION;
        wrapper->span = declaration->span;
        wrapper->module_name = declaration->module_name;
        wrapper->is_public = false;
        wrapper->has_explicit_visibility = true;
        Function *function = &wrapper->as.function;
        *function = *source;
        size_t name_length = strlen(source->name) + 48U;
        char *name = lang_arena_alloc(
            &checker->module->arena, name_length);
        (void)snprintf(
            name, name_length, "<extern-value:%s:%zu>",
            source->name, checker->module->count);
        function->name = name;
        function->is_extern = false;
        function->checked_return_type = NULL;
        function->local_count = 0U;
        if (source->param_count != 0U) {
            function->params = lang_arena_alloc(
                &checker->module->arena,
                source->param_count * sizeof(*function->params));
            memcpy(function->params, source->params,
                   source->param_count * sizeof(*function->params));
            for (size_t i = 0U; i < source->param_count; ++i) {
                function->params[i].checked_type = NULL;
                function->params[i].binding_id = 0U;
            }
        }
        Expr *callee = lang_arena_alloc(
            &checker->module->arena, sizeof(*callee));
        memset(callee, 0, sizeof(*callee));
        callee->kind = EXPR_NAME;
        callee->span = span;
        callee->as.name = source->name;
        Expr *call = lang_arena_alloc(
            &checker->module->arena, sizeof(*call));
        memset(call, 0, sizeof(*call));
        call->kind = EXPR_CALL;
        call->span = span;
        call->as.call.callee = callee;
        call->as.call.arguments.count = source->param_count;
        if (source->param_count != 0U) {
            call->as.call.arguments.items = lang_arena_alloc(
                &checker->module->arena,
                source->param_count *
                    sizeof(*call->as.call.arguments.items));
            call->as.call.argument_modes = lang_arena_alloc(
                &checker->module->arena,
                source->param_count *
                    sizeof(*call->as.call.argument_modes));
            for (size_t i = 0U; i < source->param_count; ++i) {
                Expr *argument = lang_arena_alloc(
                    &checker->module->arena, sizeof(*argument));
                memset(argument, 0, sizeof(*argument));
                argument->kind = EXPR_NAME;
                argument->span = span;
                argument->as.name = source->params[i].name;
                call->as.call.arguments.items[i] = argument;
                call->as.call.argument_modes[i] =
                    parameter_mode_from_param(&source->params[i]);
            }
        }
        Stmt *return_statement = lang_arena_alloc(
            &checker->module->arena, sizeof(*return_statement));
        memset(return_statement, 0, sizeof(*return_statement));
        return_statement->kind = STMT_RETURN;
        return_statement->span = span;
        return_statement->as.return_value = call;
        Stmt *body = lang_arena_alloc(
            &checker->module->arena, sizeof(*body));
        memset(body, 0, sizeof(*body));
        body->kind = STMT_BLOCK;
        body->span = span;
        body->as.block.count = 1U;
        body->as.block.items = lang_arena_alloc(
            &checker->module->arena, sizeof(*body->as.block.items));
        body->as.block.items[0] = return_statement;
        function->body = body;
        Decl **declarations = lang_arena_alloc(
            &checker->module->arena,
            (checker->module->count + 1U) * sizeof(*declarations));
        memcpy(declarations, checker->module->decls,
               checker->module->count * sizeof(*declarations));
        declarations[checker->module->count] = wrapper;
        checker->module->decls = declarations;
        ++checker->module->count;
        declaration = wrapper;
    }
    if (resolved_declaration != NULL)
        *resolved_declaration = declaration;
    const Function *function = &declaration->as.function;
    Type *type =
        lang_arena_alloc(&checker->module->arena, sizeof(*type));
    type->kind = TYPE_FUNCTION;
    type->requires_cleanup = false;
    type->argument_count = function->param_count;
    if (function->param_count != 0U)
        type->arguments = lang_arena_alloc(
            &checker->module->arena,
            function->param_count * sizeof(*type->arguments));
    if (function->param_count != 0U)
        type->parameter_modes = lang_arena_alloc(
            &checker->module->arena,
            function->param_count * sizeof(*type->parameter_modes));
    for (size_t i = 0U; i < function->param_count; ++i) {
        type->arguments[i] = resolve_declared_type_in_module(
            checker, function->params[i].type_syntax,
            function->params[i].type_name,
            function->params[i].span, declaration->module_name);
        type->parameter_modes[i] =
            parameter_mode_from_param(&function->params[i]);
    }
    type->element = resolve_declared_type_in_module(
        checker, function->return_type_syntax,
        function->return_type,
        function->span, declaration->module_name);
    bool action = type->element->kind == TYPE_UNIT;
    size_t length = strlen(action ? "Action<>" : "Func<>") +
        (action ? 0U : strlen(type->element->name) + 1U) + 1U;
    for (size_t i = 0U; i < type->argument_count; ++i)
        length += strlen(type->arguments[i]->name) + 5U +
                  (i == 0U ? 0U : 1U);
    char *name =
        lang_arena_alloc(&checker->module->arena, length);
    size_t offset = 0U;
    const char *family = action ? "Action<" : "Func<";
    size_t family_length = strlen(family);
    memcpy(name + offset, family, family_length);
    offset += family_length;
    for (size_t i = 0U; i < type->argument_count; ++i) {
        if (i != 0U) name[offset++] = ',';
        if (type->parameter_modes[i] == PARAMETER_MODE_OUT) {
            memcpy(name + offset, "out ", 4U);
            offset += 4U;
        } else if (type->parameter_modes[i] ==
                   PARAMETER_MODE_MUTABLE_REFERENCE) {
            memcpy(name + offset, "ref ", 4U);
            offset += 4U;
        }
        size_t argument_length =
            strlen(type->arguments[i]->name);
        memcpy(name + offset, type->arguments[i]->name,
               argument_length);
        offset += argument_length;
    }
    if (!action) {
        if (type->argument_count != 0U) name[offset++] = ',';
        size_t result_length = strlen(type->element->name);
        memcpy(name + offset, type->element->name, result_length);
        offset += result_length;
    }
    name[offset++] = '>';
    name[offset] = '\0';
    type->name = name;
    return type;
}

Type *checker_check_name(Checker *checker, Expr *expr) {
    Local *local = find_local(checker, expr->as.name);
    if (local != NULL) {
        expr->resolved_local_id = local->id;
        if (local->is_out_parameter &&
            !local->definitely_assigned)
            lang_diag(checker->diagnostics, expr->span,
                      "`out` parameter `%s` cannot be read before assignment",
                      local->name);
        if (checker_require_available(checker, local, expr->span) &&
            type_moves_by_default(checker, local->type) &&
            !type_is_copyable(checker, local->type))
            checker_move_local(checker, local, expr->span);
        return local->type;
    }
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
            return check_expr(checker, expr);
        }
        for (size_t member = 0U;
             member < owner->as.structure.member_count; ++member) {
            const Function *candidate =
                &owner->as.structure.members[member]->as.function;
            if (!candidate->is_property_getter ||
                candidate->is_static_member ||
                candidate->property_name == NULL ||
                strcmp(candidate->property_name, expr->as.name) != 0)
                continue;
            Expr *object = lang_arena_alloc(
                &checker->module->arena, sizeof(*object));
            memset(object, 0, sizeof(*object));
            object->kind = EXPR_NAME;
            object->span = expr->span;
            object->as.name = "this";
            const char *property_name = expr->as.name;
            expr->kind = EXPR_FIELD;
            expr->as.field.object = object;
            expr->as.field.field = property_name;
            return check_expr(checker, expr);
        }
    }
    if (checker->function != NULL &&
        checker->function->owner_type != NULL) {
        for (size_t i = 0U; i < checker->module->count; ++i) {
            Decl *candidate_decl = checker->module->decls[i];
            if (candidate_decl->kind != DECL_FUNCTION) continue;
            Function *candidate = &candidate_decl->as.function;
            if (!candidate->is_static_member ||
                !candidate->is_property_getter ||
                candidate->property_name == NULL ||
                candidate->owner_type == NULL ||
                strcmp(candidate->property_name, expr->as.name) != 0 ||
                strcmp(candidate->owner_type,
                       checker->function->owner_type) != 0)
                continue;
            Expr *callee = lang_arena_alloc(
                &checker->module->arena, sizeof(*callee));
            memset(callee, 0, sizeof(*callee));
            callee->kind = EXPR_NAME;
            callee->span = expr->span;
            callee->as.name = candidate->name;
            expr->kind = EXPR_CALL;
            expr->as.call.callee = callee;
            expr->as.call.arguments.items = NULL;
            expr->as.call.arguments.count = 0U;
            expr->as.call.argument_modes = NULL;
            return checker_check_call(checker, expr);
        }
    }
    checker_rewrite_unqualified_static_field(checker, expr);
    if (expr->kind != EXPR_NAME)
        return check_expr(checker, expr);
    const Decl *declaration = resolve_function_value_overload(
        checker, expr->as.name, expr->span);
    if (declaration != NULL) {
        Type *type = function_value_type(
            checker, declaration, expr->span, &declaration);
        expr->resolved_decl = declaration;
        return type;
    }
    if (strcmp(expr->as.name, "Console::WriteLine") == 0 ||
        strcmp(expr->as.name, "Console::Write") == 0 ||
        strcmp(expr->as.name, "Console::Error::WriteLine") == 0 ||
        strcmp(expr->as.name, "Console::Error::Write") == 0 ||
        strcmp(expr->as.name, "Html::ToHtmlString") == 0 ||
        strcmp(expr->as.name, "Html::UnsafeRaw") == 0 ||
        strcmp(expr->as.name, "Buffer::allocate") == 0 ||
        strcmp(expr->as.name, "StringBuilder::New") == 0 ||
        strcmp(expr->as.name, "StringBuilder::Append") == 0 ||
        strcmp(expr->as.name, "StringBuilder::AppendByte") == 0 ||
        strcmp(expr->as.name, "StringBuilder::Finish") == 0 ||
        strcmp(expr->as.name, "StringBuilder::ToString") == 0 ||
        strcmp(expr->as.name, "StringBuilder::Length") == 0 ||
        strcmp(expr->as.name, "StringBuilder::Clear") == 0 ||
        strcmp(expr->as.name, "Url::relative") == 0 ||
        strcmp(expr->as.name, "Url::fragment") == 0 ||
        strcmp(expr->as.name, "List::New") == 0 ||
        strcmp(expr->as.name, "List::Add") == 0 ||
        strcmp(expr->as.name, "List::Count") == 0 ||
        strcmp(expr->as.name, "List::Get") == 0 ||
        strcmp(expr->as.name, "List::Capacity") == 0 ||
        strcmp(expr->as.name, "List::Clear") == 0 ||
        strcmp(expr->as.name, "List::Insert") == 0 ||
        strcmp(expr->as.name, "List::RemoveAt") == 0 ||
        strcmp(expr->as.name, "List::Set") == 0 ||
        strcmp(expr->as.name, "List::Contains") == 0 ||
        strcmp(expr->as.name, "List::IndexOf") == 0 ||
        strcmp(expr->as.name, "List::LastIndexOf") == 0 ||
        strcmp(expr->as.name, "List::Remove") == 0 ||
        strcmp(expr->as.name, "List::AddRange") == 0 ||
        strcmp(expr->as.name, "List::InsertRange") == 0 ||
        strcmp(expr->as.name, "List::RemoveRange") == 0 ||
        strcmp(expr->as.name, "List::GetRange") == 0 ||
        strcmp(expr->as.name, "List::Reverse") == 0 ||
        strcmp(expr->as.name, "List::EnsureCapacity") == 0 ||
        strcmp(expr->as.name, "List::TrimExcess") == 0 ||
        strcmp(expr->as.name, "List::SetCapacity") == 0 ||
        strcmp(expr->as.name, "List::Exists") == 0 ||
        strcmp(expr->as.name, "List::FindAll") == 0 ||
        strcmp(expr->as.name, "List::FindIndex") == 0 ||
        strcmp(expr->as.name, "List::FindLastIndex") == 0 ||
        strcmp(expr->as.name, "List::RemoveAll") == 0 ||
        strcmp(expr->as.name, "List::ForEach") == 0 ||
        strcmp(expr->as.name, "List::TrueForAll") == 0 ||
        strcmp(expr->as.name, "Dictionary::New") == 0 ||
        strcmp(expr->as.name, "Dictionary::Add") == 0 ||
        strcmp(expr->as.name, "Dictionary::Count") == 0 ||
        strcmp(expr->as.name, "Dictionary::ContainsKey") == 0 ||
        strcmp(expr->as.name, "Dictionary::Remove") == 0 ||
        strcmp(expr->as.name, "Dictionary::Clear") == 0 ||
        strcmp(expr->as.name, "Dictionary::Get") == 0 ||
        strcmp(expr->as.name, "Dictionary::Set") == 0 ||
        strcmp(expr->as.name, "Dictionary::TryAdd") == 0 ||
        strcmp(expr->as.name, "Dictionary::TryGetValue") == 0 ||
        strcmp(expr->as.name, "Dictionary::ContainsValue") == 0 ||
        strcmp(expr->as.name, "Dictionary::EnsureCapacity") == 0 ||
        strcmp(expr->as.name, "Dictionary::TrimExcess") == 0 ||
        strcmp(expr->as.name, "Dictionary::Capacity") == 0 ||
        strcmp(expr->as.name, "Dictionary::KeyAt") == 0 ||
        strcmp(expr->as.name, "Dictionary::ValueAt") == 0 ||
        strcmp(expr->as.name, "Queue::New") == 0 ||
        strcmp(expr->as.name, "Queue::Enqueue") == 0 ||
        strcmp(expr->as.name, "Queue::Dequeue") == 0 ||
        strcmp(expr->as.name, "Queue::Peek") == 0 ||
        strcmp(expr->as.name, "Queue::TryDequeue") == 0 ||
        strcmp(expr->as.name, "Queue::TryPeek") == 0 ||
        strcmp(expr->as.name, "Queue::Count") == 0 ||
        strcmp(expr->as.name, "Queue::Clear") == 0 ||
        strcmp(expr->as.name, "Queue::EnsureCapacity") == 0 ||
        strcmp(expr->as.name, "Queue::TrimExcess") == 0 ||
        strcmp(expr->as.name, "Queue::Capacity") == 0 ||
        strcmp(expr->as.name, "Stack::New") == 0 ||
        strcmp(expr->as.name, "Stack::Push") == 0 ||
        strcmp(expr->as.name, "Stack::Pop") == 0 ||
        strcmp(expr->as.name, "Stack::Peek") == 0 ||
        strcmp(expr->as.name, "Stack::TryPop") == 0 ||
        strcmp(expr->as.name, "Stack::TryPeek") == 0 ||
        strcmp(expr->as.name, "Stack::Count") == 0 ||
        strcmp(expr->as.name, "Stack::Clear") == 0 ||
        strcmp(expr->as.name, "Stack::EnsureCapacity") == 0 ||
        strcmp(expr->as.name, "Stack::TrimExcess") == 0 ||
        strcmp(expr->as.name, "Stack::Capacity") == 0 ||
        strcmp(expr->as.name, "TextLen") == 0 ||
        strcmp(expr->as.name, "StringByteAt") == 0 ||
        strcmp(expr->as.name, "BufferAsMutSlice") == 0 ||
        strcmp(expr->as.name, "BufferAsSlice") == 0 ||
        strcmp(expr->as.name, "panic") == 0 ||
        strcmp(expr->as.name, "trap") == 0)
        return &type_unit;
    lang_diag(checker->diagnostics, expr->span, "unknown name `%s`", expr->as.name);
    return &type_error;
}

const char *checker_static_call_path(Checker *checker, Expr *expr) {
    if (expr->kind == EXPR_NAME)
        return find_local(checker, expr->as.name) == NULL
            ? expr->as.name : NULL;
    if (expr->kind != EXPR_FIELD) return NULL;
    const char *owner = checker_static_call_path(checker, expr->as.field.object);
    if (owner == NULL) return NULL;
    size_t owner_length = strlen(owner);
    size_t member_length = strlen(expr->as.field.field);
    char *path = lang_arena_alloc(
        &checker->module->arena,
        owner_length + member_length + 3U);
    memcpy(path, owner, owner_length);
    memcpy(path + owner_length, "::", 2U);
    memcpy(path + owner_length + 2U,
           expr->as.field.field, member_length + 1U);
    return path;
}

Type *checker_check_call(Checker *checker, Expr *expr) {
    if (expr->as.call.callee->kind == EXPR_NAME &&
        (strcmp(expr->as.call.callee->as.name, "ensure_move") == 0 ||
         strcmp(expr->as.call.callee->as.name, "assert_move") == 0)) {
        if (expr->as.call.arguments.count != 1U) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects exactly one value",
                      expr->as.call.callee->as.name);
            return &type_error;
        }
        Expr *value = expr->as.call.arguments.items[0];
        expr->kind = EXPR_ENSURE_MOVE;
        expr->as.copy.value = value;
        return check_expr(checker, expr);
    }
    if (expr->as.call.callee->kind == EXPR_NAME &&
        strcmp(expr->as.call.callee->as.name,
               "assert_no_semantic_copies") == 0) {
        if (expr->as.call.arguments.count != 0U) {
            lang_diag(checker->diagnostics, expr->span,
                      "`assert_no_semantic_copies` expects no arguments");
            return &type_error;
        }
        expr->kind = EXPR_ASSERT_NO_COPIES;
        expr->type = &type_unit;
        return &type_unit;
    }
    if (expr->as.call.callee->kind == EXPR_NAME &&
        strcmp(expr->as.call.callee->as.name, "copy") == 0) {
        if (expr->as.call.arguments.count != 1U) {
            lang_diag(
                checker->diagnostics, expr->span,
                "`copy` expects exactly one value");
            return &type_error;
        }
        Expr *value = expr->as.call.arguments.items[0];
        expr->kind = EXPR_COPY;
        expr->as.copy.value = value;
        return check_expr(checker, expr);
    }
#include "checker_call_instance.inc"
#include "checker_call_resolution.inc"
#include "checker_call_primitives.inc"
#include "checker_call_collections.inc"
#include "checker_call_dictionaries.inc"
#include "checker_call_lists.inc"
#include "checker_call_memory_unions.inc"
}
