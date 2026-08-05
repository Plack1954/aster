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
    Checker *checker, const Decl *declaration, LangSpan span) {
    if (declaration->type_param_count != 0U) {
        lang_diag(
            checker->diagnostics, span,
            "generic function `%s` cannot be used as a value without concrete type arguments",
            declaration->as.function.name);
        return &type_error;
    }
    if (declaration->as.function.is_extern) {
        lang_diag(
            checker->diagnostics, span,
            "extern function `%s` cannot be used as a function value",
            declaration->as.function.name);
        return &type_error;
    }
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
        if (local->type->requires_cleanup) {
            Expr *source = lang_arena_alloc(
                &checker->module->arena, sizeof(*source));
            *source = *expr;
            source->type = local->type;
            expr->kind = EXPR_CLONE;
            expr->as.clone.value = source;
            if (!type_is_copyable(checker, local->type))
                lang_diag(checker->diagnostics, expr->span,
                          "type `%s` is not copyable; pass `%s` with `ref` or a pointer",
                          local->type->name, local->name);
        }
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
        expr->resolved_decl = declaration;
        return function_value_type(
            checker, declaration, expr->span);
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
    if (expr->as.call.callee->kind == EXPR_FIELD) {
        Expr *field = expr->as.call.callee;
        const char *static_name = checker_static_call_path(checker, field);
        if (field->as.field.object->kind == EXPR_FIELD) {
            const char *receiver_static = checker_static_call_path(
                checker, field->as.field.object);
            Function *receiver_member = receiver_static != NULL
                ? find_function(checker, receiver_static, field->span)
                : NULL;
            if (receiver_member != NULL &&
                receiver_member->is_property_getter &&
                receiver_member->is_static_member)
                static_name = NULL;
        }
        if (static_name != NULL) {
            field->kind = EXPR_NAME;
            field->as.name = static_name;
            goto static_call;
        }
        Expr *receiver = field->as.field.object;
        Type *receiver_type = &type_error;
        if (receiver->kind == EXPR_NAME) {
            Local *local = find_local(checker, receiver->as.name);
            if (local != NULL)
                receiver_type = local->type;
            else
                lang_diag(checker->diagnostics, receiver->span,
                          "unknown method receiver `%s`",
                          receiver->as.name);
        } else if (receiver->kind == EXPR_FIELD) {
            receiver_type = check_expr(checker, receiver);
        } else {
            receiver_type = check_expr(checker, receiver);
        }

        const char *owner = receiver_type->name;
        switch (receiver_type->kind) {
            case TYPE_I8: owner = "sbyte"; break;
            case TYPE_I16: owner = "short"; break;
            case TYPE_I32: owner = "int"; break;
            case TYPE_I64: owner = "long"; break;
            case TYPE_U8: owner = "byte"; break;
            case TYPE_U16: owner = "ushort"; break;
            case TYPE_U32: owner = "uint"; break;
            case TYPE_U64: owner = "ulong"; break;
            case TYPE_F32: owner = "float"; break;
            case TYPE_F64: owner = "double"; break;
            default: break;
        }
        if ((receiver_type->kind == TYPE_NAMED ||
             receiver_type->kind == TYPE_CLASS) &&
            receiver_type->declaration != NULL)
            owner = type_declaration_name(
                receiver_type->declaration);
        else if (receiver_type->kind == TYPE_VEC)
            owner = "List";
        else if (receiver_type->kind == TYPE_DICTIONARY)
            owner = "Dictionary";
        else if (receiver_type->kind == TYPE_HASH_SET)
            owner = "HashSet";
        else if (receiver_type->kind == TYPE_QUEUE)
            owner = "Queue";
        else if (receiver_type->kind == TYPE_STACK)
            owner = "Stack";
        size_t owner_length = strcspn(owner, "<");
        size_t method_length = strlen(field->as.field.field);
        char *qualified = lang_arena_alloc(
            &checker->module->arena,
            owner_length + method_length + 3U);
        memcpy(qualified, owner, owner_length);
        memcpy(qualified + owner_length, "::", 2U);
        memcpy(
            qualified + owner_length + 2U,
            field->as.field.field, method_length + 1U);

        if (receiver_type->kind == TYPE_VEC) {
            if (strcmp(field->as.field.field, "Add") == 0)
                qualified = "List::Add";
            else if (strcmp(field->as.field.field, "Count") == 0)
                qualified = "List::Count";
            else if (strcmp(field->as.field.field, "Get") == 0)
                qualified = "List::Get";
            else if (strcmp(field->as.field.field, "Capacity") == 0)
                qualified = "List::Capacity";
            else if (strcmp(field->as.field.field, "Clear") == 0)
                qualified = "List::Clear";
            else if (strcmp(field->as.field.field, "Insert") == 0)
                qualified = "List::Insert";
            else if (strcmp(field->as.field.field, "RemoveAt") == 0)
                qualified = "List::RemoveAt";
            else if (strcmp(field->as.field.field, "Set") == 0)
                qualified = "List::Set";
            else if (strcmp(field->as.field.field, "Contains") == 0)
                qualified = "List::Contains";
            else if (strcmp(field->as.field.field, "IndexOf") == 0)
                qualified = "List::IndexOf";
            else if (strcmp(field->as.field.field, "LastIndexOf") == 0)
                qualified = "List::LastIndexOf";
            else if (strcmp(field->as.field.field, "Remove") == 0)
                qualified = "List::Remove";
            else if (strcmp(field->as.field.field, "AddRange") == 0)
                qualified = "List::AddRange";
            else if (strcmp(field->as.field.field, "InsertRange") == 0)
                qualified = "List::InsertRange";
            else if (strcmp(field->as.field.field, "RemoveRange") == 0)
                qualified = "List::RemoveRange";
            else if (strcmp(field->as.field.field, "GetRange") == 0)
                qualified = "List::GetRange";
            else if (strcmp(field->as.field.field, "Reverse") == 0)
                qualified = "List::Reverse";
            else if (strcmp(field->as.field.field, "EnsureCapacity") == 0)
                qualified = "List::EnsureCapacity";
            else if (strcmp(field->as.field.field, "TrimExcess") == 0)
                qualified = "List::TrimExcess";
            else if (strcmp(field->as.field.field, "Exists") == 0)
                qualified = "List::Exists";
            else if (strcmp(field->as.field.field, "FindAll") == 0)
                qualified = "List::FindAll";
            else if (strcmp(field->as.field.field, "FindIndex") == 0)
                qualified = "List::FindIndex";
            else if (strcmp(field->as.field.field, "FindLastIndex") == 0)
                qualified = "List::FindLastIndex";
            else if (strcmp(field->as.field.field, "RemoveAll") == 0)
                qualified = "List::RemoveAll";
            else if (strcmp(field->as.field.field, "ForEach") == 0)
                qualified = "List::ForEach";
            else if (strcmp(field->as.field.field, "TrueForAll") == 0)
                qualified = "List::TrueForAll";
        } else if (receiver_type->kind == TYPE_DICTIONARY) {
            if (strcmp(field->as.field.field, "Add") == 0)
                qualified = "Dictionary::Add";
            else if (strcmp(field->as.field.field, "ContainsKey") == 0)
                qualified = "Dictionary::ContainsKey";
            else if (strcmp(field->as.field.field, "Remove") == 0)
                qualified = "Dictionary::Remove";
            else if (strcmp(field->as.field.field, "Clear") == 0)
                qualified = "Dictionary::Clear";
            else if (strcmp(field->as.field.field, "Get") == 0)
                qualified = "Dictionary::Get";
            else if (strcmp(field->as.field.field, "Set") == 0)
                qualified = "Dictionary::Set";
            else if (strcmp(field->as.field.field, "TryAdd") == 0)
                qualified = "Dictionary::TryAdd";
            else if (strcmp(field->as.field.field, "TryGetValue") == 0)
                qualified = "Dictionary::TryGetValue";
            else if (strcmp(field->as.field.field, "ContainsValue") == 0)
                qualified = "Dictionary::ContainsValue";
            else if (strcmp(field->as.field.field, "EnsureCapacity") == 0)
                qualified = "Dictionary::EnsureCapacity";
            else if (strcmp(field->as.field.field, "TrimExcess") == 0)
                qualified = "Dictionary::TrimExcess";
            else if (strcmp(field->as.field.field, "Capacity") == 0)
                qualified = "Dictionary::Capacity";
        } else if (receiver_type->kind == TYPE_HASH_SET) {
            if (strcmp(field->as.field.field, "Add") == 0)
                qualified = "Dictionary::TryAdd";
            else if (strcmp(field->as.field.field, "Contains") == 0)
                qualified = "Dictionary::ContainsKey";
            else if (strcmp(field->as.field.field, "Remove") == 0)
                qualified = "Dictionary::Remove";
            else if (strcmp(field->as.field.field, "Clear") == 0)
                qualified = "Dictionary::Clear";
            else if (strcmp(field->as.field.field, "EnsureCapacity") == 0)
                qualified = "Dictionary::EnsureCapacity";
            else if (strcmp(field->as.field.field, "TrimExcess") == 0)
                qualified = "Dictionary::TrimExcess";
            else if (strcmp(field->as.field.field, "Capacity") == 0)
                qualified = "Dictionary::Capacity";
        } else if (receiver_type->kind == TYPE_QUEUE) {
            if (strcmp(field->as.field.field, "Enqueue") == 0)
                qualified = "Queue::Enqueue";
            else if (strcmp(field->as.field.field, "Dequeue") == 0)
                qualified = "Queue::Dequeue";
            else if (strcmp(field->as.field.field, "Peek") == 0)
                qualified = "Queue::Peek";
            else if (strcmp(field->as.field.field, "TryDequeue") == 0)
                qualified = "Queue::TryDequeue";
            else if (strcmp(field->as.field.field, "TryPeek") == 0)
                qualified = "Queue::TryPeek";
            else if (strcmp(field->as.field.field, "Clear") == 0)
                qualified = "Queue::Clear";
            else if (strcmp(field->as.field.field, "EnsureCapacity") == 0)
                qualified = "Queue::EnsureCapacity";
            else if (strcmp(field->as.field.field, "TrimExcess") == 0)
                qualified = "Queue::TrimExcess";
            else if (strcmp(field->as.field.field, "Capacity") == 0)
                qualified = "Queue::Capacity";
        } else if (receiver_type->kind == TYPE_STACK) {
            if (strcmp(field->as.field.field, "Push") == 0)
                qualified = "Stack::Push";
            else if (strcmp(field->as.field.field, "Pop") == 0)
                qualified = "Stack::Pop";
            else if (strcmp(field->as.field.field, "Peek") == 0)
                qualified = "Stack::Peek";
            else if (strcmp(field->as.field.field, "TryPop") == 0)
                qualified = "Stack::TryPop";
            else if (strcmp(field->as.field.field, "TryPeek") == 0)
                qualified = "Stack::TryPeek";
            else if (strcmp(field->as.field.field, "Clear") == 0)
                qualified = "Stack::Clear";
            else if (strcmp(field->as.field.field, "EnsureCapacity") == 0)
                qualified = "Stack::EnsureCapacity";
            else if (strcmp(field->as.field.field, "TrimExcess") == 0)
                qualified = "Stack::TrimExcess";
            else if (strcmp(field->as.field.field, "Capacity") == 0)
                qualified = "Stack::Capacity";
        }

        size_t old_count = expr->as.call.arguments.count;
        bool hash_set_add = receiver_type->kind == TYPE_HASH_SET &&
            strcmp(field->as.field.field, "Add") == 0;
        Expr **arguments = lang_arena_alloc(
            &checker->module->arena,
            (old_count + (hash_set_add ? 2U : 1U)) * sizeof(*arguments));
        arguments[0] = receiver;
        if (old_count != 0U)
            memcpy(
                arguments + 1U,
                expr->as.call.arguments.items,
                old_count * sizeof(*arguments));
        if (hash_set_add) {
            Expr *present = lang_arena_alloc(
                &checker->module->arena, sizeof(*present));
            present->kind = EXPR_BOOL;
            present->span = expr->span;
            present->as.boolean = true;
            arguments[old_count + 1U] = present;
        }
        expr->as.call.arguments.items = arguments;
        ParameterMode *argument_modes = lang_arena_alloc(
            &checker->module->arena,
            (old_count + (hash_set_add ? 2U : 1U)) *
                sizeof(*argument_modes));
        argument_modes[0] = PARAMETER_MODE_VALUE;
        for (size_t i = 0U; i < old_count; ++i)
            argument_modes[i + 1U] = call_argument_mode(expr, i);
        if (hash_set_add)
            argument_modes[old_count + 1U] = PARAMETER_MODE_VALUE;
        expr->as.call.argument_modes = argument_modes;
        expr->as.call.arguments.count =
            old_count + (hash_set_add ? 2U : 1U);
        expr->as.call.implicit_receiver =
            receiver_type->kind != TYPE_CANCELLATION_TOKEN &&
            receiver_type->kind != TYPE_CANCELLATION_TOKEN_SOURCE;
        field->kind = EXPR_NAME;
        field->as.name = qualified;
    }
static_call:
    if (expr->as.call.callee->kind != EXPR_NAME) {
        lang_diag(checker->diagnostics, expr->as.call.callee->span,
                  "only statically named functions are supported");
        return &type_error;
    }
    const char *name = expr->as.call.callee->as.name;
    if (strcmp(name, "$target::new") == 0) {
        if (checker->expected_type == NULL ||
            (checker->expected_type->kind != TYPE_NAMED &&
             checker->expected_type->kind != TYPE_CLASS) ||
            checker->expected_type->declaration == NULL) {
            lang_diag(checker->diagnostics, expr->span,
                      "target-typed `new(...)` requires an expected struct or class type");
            return &type_error;
        }
        const char *owner = type_declaration_name(
            checker->expected_type->declaration);
        size_t owner_length = strlen(owner);
        char *constructor = lang_arena_alloc(
            &checker->module->arena, owner_length + sizeof("::new"));
        memcpy(constructor, owner, owner_length);
        memcpy(constructor + owner_length, "::new", sizeof("::new"));
        expr->as.call.callee->as.name = constructor;
        name = expr->as.call.callee->as.name;
    }
    Local *callee_local = find_local(checker, name);
    if (callee_local != NULL &&
        callee_local->type->kind == TYPE_FUNCTION) {
        expr->as.call.callee->resolved_local_id =
            callee_local->id;
        expr->as.call.callee->type = callee_local->type;
        Type *function_type = callee_local->type;
        if (function_type->argument_count !=
            expr->as.call.arguments.count)
            lang_diag(
                checker->diagnostics, expr->span,
                "function value `%s` expects %zu arguments, found %zu",
                name, function_type->argument_count,
                expr->as.call.arguments.count);
        size_t count =
            function_type->argument_count <
                expr->as.call.arguments.count
            ? function_type->argument_count
            : expr->as.call.arguments.count;
        for (size_t i = 0U;
             i < expr->as.call.arguments.count; ++i) {
            Type *previous_expected = checker->expected_type;
            if (i < function_type->argument_count)
                checker->expected_type =
                    function_type->arguments[i];
            ParameterMode expected_mode = function_type->parameter_modes[i];
            ParameterMode actual_mode = call_argument_mode(expr, i);
            bool borrowed = parameter_mode_is_reference(expected_mode);
            bool mutable_borrow = parameter_mode_is_mutable(expected_mode);
            bool explicit_ref =
                actual_mode == PARAMETER_MODE_MUTABLE_REFERENCE;
            bool explicit_out = actual_mode == PARAMETER_MODE_OUT;
            bool expected_out = expected_mode == PARAMETER_MODE_OUT;
            if (explicit_ref && (!mutable_borrow || expected_out))
                lang_diag(
                    checker->diagnostics,
                    expr->as.call.arguments.items[i]->span,
                    "argument %zu to function value `%s` is not a `ref` parameter",
                    i + 1U, name);
            if (explicit_out != expected_out)
                lang_diag(
                    checker->diagnostics,
                    expr->as.call.arguments.items[i]->span,
                    "argument %zu to function value `%s` %s `out`",
                    i + 1U, name,
                    expected_out ? "must use" : "must not use");
            borrowed = borrowed || explicit_ref || explicit_out;
            mutable_borrow = mutable_borrow || explicit_ref || explicit_out;
            Expr *argument = expr->as.call.arguments.items[i];
            if (borrowed) {
                bool borrowable_place =
                    argument->kind == EXPR_NAME ||
                    (argument->kind == EXPR_FIELD &&
                     argument->as.field.object->kind == EXPR_NAME);
                if (!borrowable_place && mutable_borrow) {
                    lang_diag(
                        checker->diagnostics, argument->span,
                        "`ref` argument must be an available place");
                    (void)check_expr(checker, argument);
                } else if (borrowable_place) {
                    const Expr *previous_allowed =
                        checker->allowed_unassigned_out_place;
                    if (expected_out)
                        checker->allowed_unassigned_out_place = argument;
                    (void)check_place(checker, argument);
                    checker->allowed_unassigned_out_place = previous_allowed;
                    if (mutable_borrow) {
                        Expr *root = argument;
                        while (root->kind == EXPR_FIELD)
                            root = root->as.field.object;
                        Local *local = root->kind == EXPR_NAME
                            ? find_local(checker, root->as.name) : NULL;
                        if (local == NULL || !local->mutable_)
                            lang_diag(
                                checker->diagnostics, argument->span,
                                "`ref` argument requires a mutable local");
                    }
                    if (expected_out && argument->kind == EXPR_NAME) {
                        Local *local = find_local(
                            checker, argument->as.name);
                        if (local != NULL && local->is_out_parameter)
                            local->definitely_assigned = true;
                    }
                } else {
                    (void)check_expr(checker, argument);
                }
            } else {
                (void)check_expr(checker, argument);
            }
            checker->expected_type = previous_expected;
        }
        for (size_t i = 0U; i < count; ++i) {
            Type *expected = function_type->arguments[i];
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[i],
                expected);
            Type *actual =
                expr->as.call.arguments.items[i]->type;
            if (!type_assignable(expected, actual))
                lang_diag(
                    checker->diagnostics,
                    expr->as.call.arguments.items[i]->span,
                    "argument %zu to function value `%s` expects `%s`, found `%s`",
                    i + 1U, name, expected->name,
                    actual->name);
        }
        return function_type->element;
    }
    if (strstr(name, "::") == NULL && checker->function != NULL &&
        checker->function->owner_type != NULL) {
        size_t owner_length = strlen(checker->function->owner_type);
        size_t name_length = strlen(name);
        char *qualified = lang_arena_alloc(
            &checker->module->arena,
            owner_length + name_length + 3U);
        memcpy(qualified, checker->function->owner_type, owner_length);
        memcpy(qualified + owner_length, "::", 2U);
        memcpy(qualified + owner_length + 2U, name, name_length + 1U);
        for (size_t i = 0U; i < checker->module->count; ++i) {
            Decl *candidate = checker->module->decls[i];
            if (candidate->kind != DECL_FUNCTION ||
                !candidate->as.function.is_static_member ||
                strcmp(candidate->as.function.name, qualified) != 0)
                continue;
            expr->as.call.callee->as.name = qualified;
            name = qualified;
            break;
        }
    }
    if (expr->as.call.implicit_receiver &&
        expr->as.call.arguments.count != 0U) {
        Expr *receiver = expr->as.call.arguments.items[0];
        Type *receiver_type = receiver->type;
        if (receiver_type != NULL && receiver_type->kind == TYPE_CLASS &&
            receiver_type->declaration != NULL) {
            const char *separator = last_path_separator(name);
            const char *short_name = separator != NULL
                ? separator + 2U : name;
            const char *requested_name = name;
            bool exact_exists = false;
            for (size_t i = 0U; i < checker->module->count; ++i) {
                Decl *candidate = checker->module->decls[i];
                if (candidate->kind == DECL_FUNCTION &&
                    strcmp(candidate->as.function.name, name) == 0) {
                    exact_exists = true;
                    break;
                }
            }
            for (const Decl *base = exact_exists
                     ? NULL
                     : receiver_type->declaration->as.structure.base_class;
                 base != NULL; base = base->as.structure.base_class) {
                size_t owner_length = strlen(base->as.structure.name);
                size_t method_length = strlen(short_name);
                char *qualified = lang_arena_alloc(
                    &checker->module->arena,
                    owner_length + method_length + 3U);
                memcpy(qualified, base->as.structure.name, owner_length);
                memcpy(qualified + owner_length, "::", 2U);
                memcpy(qualified + owner_length + 2U,
                       short_name, method_length + 1U);
                for (size_t i = 0U; i < checker->module->count; ++i) {
                    Decl *candidate = checker->module->decls[i];
                    if (candidate->kind != DECL_FUNCTION ||
                        strcmp(candidate->as.function.name,
                               qualified) != 0)
                        continue;
                    expr->as.call.callee->as.name = qualified;
                    name = qualified;
                    base = NULL;
                    break;
                }
                if (base == NULL) break;
            }
            if (!exact_exists && strcmp(name, requested_name) == 0) {
                const Decl *interfaces[256];
                size_t interface_count = 0U;
                for (const Decl *owner = receiver_type->declaration;
                     owner != NULL; owner = owner->as.structure.base_class)
                    for (size_t interface = 0U;
                         interface < owner->as.structure.interface_count &&
                         interface_count < 256U; ++interface)
                        interfaces[interface_count++] =
                            owner->as.structure.interfaces[interface];
                while (interface_count != 0U &&
                       strcmp(name, requested_name) == 0) {
                    const Decl *interface = interfaces[--interface_count];
                    size_t owner_length =
                        strlen(interface->as.structure.name);
                    size_t method_length = strlen(short_name);
                    char *qualified = lang_arena_alloc(
                        &checker->module->arena,
                        owner_length + method_length + 3U);
                    memcpy(qualified, interface->as.structure.name,
                           owner_length);
                    memcpy(qualified + owner_length, "::", 2U);
                    memcpy(qualified + owner_length + 2U,
                           short_name, method_length + 1U);
                    for (size_t i = 0U;
                         i < checker->module->count; ++i) {
                        Decl *candidate = checker->module->decls[i];
                        if (candidate->kind == DECL_FUNCTION &&
                            strcmp(candidate->as.function.name,
                                   qualified) == 0) {
                            expr->as.call.callee->as.name = qualified;
                            name = qualified;
                            break;
                        }
                    }
                    for (size_t parent = 0U;
                         parent < interface->as.structure.interface_count &&
                         interface_count < 256U; ++parent)
                        interfaces[interface_count++] =
                            interface->as.structure.interfaces[parent];
                }
            }
        }
    }
    CallOverloadSet overloads = collect_call_overloads(
        checker, name, expr, false);
    bool pending_overload = overloads.arity_count > 1U;
    const Decl *declared_declaration =
        overloads.arity_count == 1U
            ? overloads.selected
            : (overloads.arity_count == 0U
                ? overloads.first_named : NULL);
    Function *declared_function = declared_declaration != NULL
        ? (Function *)&declared_declaration->as.function : NULL;
    bool same_member_owner = declared_function != NULL &&
        declared_function->owner_type != NULL &&
        checker->function != NULL &&
        checker->function->owner_type != NULL &&
        strcmp(checker->function->owner_type,
               declared_function->owner_type) == 0 &&
        checker->current_module != NULL &&
        declared_declaration->module_name != NULL &&
        strcmp(checker->current_module,
               declared_declaration->module_name) == 0;
    if (declared_function != NULL &&
        declared_function->owner_type != NULL &&
        !declared_declaration->is_public &&
        !same_member_owner)
        lang_diag(checker->diagnostics, expr->span,
                  "member `%s` is private to class `%s`",
                  declared_function->name,
                  declared_function->owner_type);
    expr->resolved_decl = declared_declaration;
    if (declared_function != NULL &&
        expr->as.call.implicit_receiver &&
        (declared_function->is_virtual_member ||
         declared_function->is_override_member))
        expr->as.call.virtual_dispatch = true;
    if (expr->resolved_decl != NULL &&
        expr->resolved_decl->type_param_count != 0U)
        return check_generic_call(
            checker, expr, expr->resolved_decl);
    const char *declared_module = declared_declaration != NULL
        ? declared_declaration->module_name : checker->current_module;
    if (declared_function != NULL && declared_function->is_drop)
        lang_diag(checker->diagnostics, expr->span,
                  "destructor `%s` cannot be called explicitly", name);
    bool builtin_borrow =
        strcmp(name, "ArenaAlloc") == 0 ||
        strcmp(name, "ArenaReset") == 0 ||
        strcmp(name, "StringBuilder::Append") == 0 ||
        strcmp(name, "StringBuilder::AppendByte") == 0 ||
        strcmp(name, "StringBuilder::ToString") == 0 ||
        strcmp(name, "StringBuilder::Length") == 0 ||
        strcmp(name, "StringBuilder::Clear") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "List::Add") == 0 ||
        strcmp(name, "List::Count") == 0 ||
        strcmp(name, "List::Get") == 0 ||
        strcmp(name, "List::Capacity") == 0 ||
        strcmp(name, "List::Clear") == 0 ||
        strcmp(name, "List::Insert") == 0 ||
        strcmp(name, "List::RemoveAt") == 0 ||
        strcmp(name, "List::Set") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "List::Contains") == 0 ||
        strcmp(name, "List::IndexOf") == 0 ||
        strcmp(name, "List::LastIndexOf") == 0 ||
        strcmp(name, "List::Remove") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "List::AddRange") == 0 ||
        strcmp(name, "List::InsertRange") == 0 ||
        strcmp(name, "List::RemoveRange") == 0 ||
        strcmp(name, "List::GetRange") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "List::Reverse") == 0 ||
        strcmp(name, "List::EnsureCapacity") == 0 ||
        strcmp(name, "List::TrimExcess") == 0 ||
        strcmp(name, "List::SetCapacity") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "List::Exists") == 0 ||
        strcmp(name, "List::FindAll") == 0 ||
        strcmp(name, "List::FindIndex") == 0 ||
        strcmp(name, "List::FindLastIndex") == 0 ||
        strcmp(name, "List::RemoveAll") == 0 ||
        strcmp(name, "List::ForEach") == 0 ||
        strcmp(name, "List::TrueForAll") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "BufferAsMutSlice") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Dictionary::Add") == 0 ||
        strcmp(name, "Dictionary::Count") == 0 ||
        strcmp(name, "Dictionary::ContainsKey") == 0 ||
        strcmp(name, "Dictionary::Remove") == 0 ||
        strcmp(name, "Dictionary::Clear") == 0 ||
        strcmp(name, "Dictionary::Get") == 0 ||
        strcmp(name, "Dictionary::Set") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Dictionary::TryAdd") == 0 ||
        strcmp(name, "Dictionary::TryGetValue") == 0 ||
        strcmp(name, "Dictionary::ContainsValue") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Dictionary::EnsureCapacity") == 0 ||
        strcmp(name, "Dictionary::TrimExcess") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Dictionary::Capacity") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Queue::Enqueue") == 0 ||
        strcmp(name, "Queue::Dequeue") == 0 ||
        strcmp(name, "Queue::Peek") == 0 ||
        strcmp(name, "Queue::TryDequeue") == 0 ||
        strcmp(name, "Queue::TryPeek") == 0 ||
        strcmp(name, "Queue::Count") == 0 ||
        strcmp(name, "Queue::Clear") == 0 ||
        strcmp(name, "Queue::EnsureCapacity") == 0 ||
        strcmp(name, "Queue::TrimExcess") == 0 ||
        strcmp(name, "Queue::Capacity") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Stack::Push") == 0 ||
        strcmp(name, "Stack::Pop") == 0 ||
        strcmp(name, "Stack::Peek") == 0 ||
        strcmp(name, "Stack::TryPop") == 0 ||
        strcmp(name, "Stack::TryPeek") == 0 ||
        strcmp(name, "Stack::Count") == 0 ||
        strcmp(name, "Stack::Clear") == 0 ||
        strcmp(name, "Stack::EnsureCapacity") == 0 ||
        strcmp(name, "Stack::TrimExcess") == 0 ||
        strcmp(name, "Stack::Capacity") == 0;
    for (size_t i = 0U; i < expr->as.call.arguments.count; ++i) {
        ParameterMode actual_mode = call_argument_mode(expr, i);
        bool explicit_call_ref =
            actual_mode == PARAMETER_MODE_MUTABLE_REFERENCE;
        bool explicit_call_out = actual_mode == PARAMETER_MODE_OUT;
        bool builtin_out =
            ((strcmp(name, "Dictionary::TryGetValue") == 0 && i == 2U) ||
             ((strcmp(name, "Queue::TryDequeue") == 0 ||
               strcmp(name, "Queue::TryPeek") == 0 ||
               strcmp(name, "Stack::TryPop") == 0 ||
               strcmp(name, "Stack::TryPeek") == 0) && i == 1U));
        if (explicit_call_ref && !pending_overload &&
            (declared_function == NULL ||
             i >= declared_function->param_count ||
             !declared_function->params[i].by_ref ||
             declared_function->params[i].by_out))
            lang_diag(
                checker->diagnostics,
                expr->as.call.arguments.items[i]->span,
                "argument %zu to `%s` is not a `ref` parameter",
                i + 1U, name);
        if (explicit_call_out && !pending_overload && !builtin_out &&
            (declared_function == NULL ||
             i >= declared_function->param_count ||
             !declared_function->params[i].by_out))
            lang_diag(
                checker->diagnostics,
                expr->as.call.arguments.items[i]->span,
                "argument %zu to `%s` is not an `out` parameter",
                i + 1U, name);
        if (!explicit_call_out && !pending_overload &&
            declared_function != NULL &&
            i < declared_function->param_count &&
            declared_function->params[i].by_out)
            lang_diag(
                checker->diagnostics,
                expr->as.call.arguments.items[i]->span,
                "argument %zu to `%s` must use `out`",
                i + 1U, name);
        bool output_local =
            (strcmp(name, "Console::WriteLine") == 0 ||
             strcmp(name, "Console::Write") == 0 ||
             strcmp(name, "Console::Error::WriteLine") == 0 ||
             strcmp(name, "Console::Error::Write") == 0) &&
            i == 0U &&
            expr->as.call.arguments.items[i]->kind == EXPR_NAME;
        bool text_local =
            strcmp(name, "TextLen") == 0 && i == 0U &&
            expr->as.call.arguments.items[i]->kind == EXPR_NAME;
        /* Method syntax never evaluates its receiver as an ordinary value.
         * In particular, overload resolution happens after argument typing,
         * so declared_function is temporarily NULL for overloaded methods.
         * Treating the receiver as a value here inserted a defensive clone;
         * the selected ref-self overload then mutated that clone. */
        bool implicit_receiver =
            expr->as.call.implicit_receiver && i == 0U;
        bool borrowed = explicit_call_ref || explicit_call_out ||
            implicit_receiver ||
            (builtin_borrow && i == 0U) || output_local ||
            text_local ||
            (declared_function != NULL &&
             i < declared_function->param_count &&
             declared_function->params[i].by_ref);
        if (borrowed) {
            bool builtin_place =
                (strcmp(name, "List::Add") == 0 ||
                 strcmp(name, "List::Count") == 0 ||
                 strcmp(name, "List::Get") == 0 ||
                 strcmp(name, "List::Capacity") == 0 ||
                 strcmp(name, "List::Clear") == 0 ||
                 strcmp(name, "List::Insert") == 0 ||
                 strcmp(name, "List::RemoveAt") == 0 ||
                 strcmp(name, "List::Set") == 0) &&
                i == 0U;
            builtin_place = builtin_place ||
                ((strcmp(name, "List::Contains") == 0 ||
                  strcmp(name, "List::IndexOf") == 0 ||
                  strcmp(name, "List::LastIndexOf") == 0 ||
                  strcmp(name, "List::Remove") == 0) &&
                i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "List::AddRange") == 0 ||
                  strcmp(name, "List::InsertRange") == 0 ||
                  strcmp(name, "List::RemoveRange") == 0 ||
                  strcmp(name, "List::GetRange") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "List::Reverse") == 0 ||
                  strcmp(name, "List::EnsureCapacity") == 0 ||
                  strcmp(name, "List::TrimExcess") == 0 ||
                  strcmp(name, "List::SetCapacity") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "List::Exists") == 0 ||
                  strcmp(name, "List::FindAll") == 0 ||
                  strcmp(name, "List::FindIndex") == 0 ||
                  strcmp(name, "List::FindLastIndex") == 0 ||
                  strcmp(name, "List::RemoveAll") == 0 ||
                  strcmp(name, "List::ForEach") == 0 ||
                  strcmp(name, "List::TrueForAll") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "Dictionary::Add") == 0 ||
                  strcmp(name, "Dictionary::Count") == 0 ||
                  strcmp(name, "Dictionary::ContainsKey") == 0 ||
                  strcmp(name, "Dictionary::Remove") == 0 ||
                  strcmp(name, "Dictionary::Clear") == 0 ||
                  strcmp(name, "Dictionary::Get") == 0 ||
                  strcmp(name, "Dictionary::Set") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "Dictionary::TryAdd") == 0 ||
                  strcmp(name, "Dictionary::ContainsValue") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "Dictionary::EnsureCapacity") == 0 ||
                  strcmp(name, "Dictionary::TrimExcess") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                (strcmp(name, "Dictionary::Capacity") == 0 && i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "Queue::Enqueue") == 0 ||
                  strcmp(name, "Queue::Dequeue") == 0 ||
                  strcmp(name, "Queue::Peek") == 0 ||
                  strcmp(name, "Queue::Count") == 0 ||
                  strcmp(name, "Queue::Clear") == 0 ||
                  strcmp(name, "Queue::EnsureCapacity") == 0 ||
                  strcmp(name, "Queue::TrimExcess") == 0 ||
                  strcmp(name, "Queue::Capacity") == 0) && i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "Stack::Push") == 0 ||
                  strcmp(name, "Stack::Pop") == 0 ||
                  strcmp(name, "Stack::Peek") == 0 ||
                  strcmp(name, "Stack::Count") == 0 ||
                  strcmp(name, "Stack::Clear") == 0 ||
                  strcmp(name, "Stack::EnsureCapacity") == 0 ||
                  strcmp(name, "Stack::TrimExcess") == 0 ||
                  strcmp(name, "Stack::Capacity") == 0) && i == 0U);
            ExprKind argument_kind =
                expr->as.call.arguments.items[i]->kind;
            bool borrowable_place =
                argument_kind == EXPR_NAME ||
                (argument_kind == EXPR_FIELD &&
                 expr->as.call.arguments.items[i]
                         ->as.field.object->kind == EXPR_NAME);
            bool explicit_ref =
                declared_function != NULL &&
                i < declared_function->param_count &&
                declared_function->params[i].by_ref;
            if (!borrowable_place && (builtin_place || explicit_ref)) {
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[i]->span,
                          declared_function != NULL &&
                          i < declared_function->param_count &&
                          declared_function->params[i].by_out
                              ? "`out` argument must be an available place"
                              : "`ref` argument must be an available place");
                (void)check_expr(checker, expr->as.call.arguments.items[i]);
            } else if (borrowable_place) {
                Expr *argument = expr->as.call.arguments.items[i];
                bool expected_out = declared_function != NULL &&
                    i < declared_function->param_count &&
                    declared_function->params[i].by_out;
                const Expr *previous_allowed =
                    checker->allowed_unassigned_out_place;
                if (expected_out)
                    checker->allowed_unassigned_out_place = argument;
                (void)check_place(checker, argument);
                checker->allowed_unassigned_out_place = previous_allowed;
                bool mutable_borrow =
                    declared_function != NULL &&
                    i < declared_function->param_count &&
                    declared_function->params[i].by_ref &&
                    declared_function->params[i].mutable_;
                if (mutable_borrow) {
                    Expr *root = expr->as.call.arguments.items[i];
                    while (root->kind == EXPR_FIELD)
                        root = root->as.field.object;
                    Local *local = root->kind == EXPR_NAME
                                 ? find_local(
                                       checker, root->as.name)
                                 : NULL;
                    if (local == NULL || !local->mutable_)
                        lang_diag(
                            checker->diagnostics,
                            expr->as.call.arguments.items[i]->span,
                            "`ref` argument requires a mutable local");
                }
                if (expected_out && argument->kind == EXPR_NAME) {
                    Local *local = find_local(checker, argument->as.name);
                    if (local != NULL && local->is_out_parameter)
                        local->definitely_assigned = true;
                }
            } else {
                Type *previous_expected = checker->expected_type;
                if (declared_function != NULL &&
                    i < declared_function->param_count)
                    checker->expected_type = resolve_declared_type_in_module(
                        checker,
                        declared_function->params[i].type_syntax,
                        declared_function->params[i].type_name,
                        declared_function->params[i].span,
                        declared_module);
                (void)check_expr(
                    checker, expr->as.call.arguments.items[i]);
                checker->expected_type = previous_expected;
            }
        } else {
            Type *previous_expected = checker->expected_type;
            if (declared_function != NULL &&
                i < declared_function->param_count)
                checker->expected_type = resolve_declared_type_in_module(
                    checker,
                    declared_function->params[i].type_syntax,
                    declared_function->params[i].type_name,
                    declared_function->params[i].span,
                    declared_module);
            else if (strcmp(name, "List::Add") == 0 &&
                     i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_VEC)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if ((strcmp(name, "List::Insert") == 0 ||
                      strcmp(name, "List::Set") == 0) &&
                     i == 2U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_VEC)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if ((strcmp(name, "List::Contains") == 0 ||
                      strcmp(name, "List::IndexOf") == 0 ||
                      strcmp(name, "List::LastIndexOf") == 0 ||
                      strcmp(name, "List::Remove") == 0) &&
                     i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_VEC)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if (strcmp(name, "List::AddRange") == 0 &&
                     i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_VEC)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type;
            else if (strcmp(name, "List::InsertRange") == 0 &&
                     i == 2U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_VEC)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type;
            else if ((strcmp(name, "Dictionary::Add") == 0 ||
                      strcmp(name, "Dictionary::TryAdd") == 0 ||
                      strcmp(name, "Dictionary::Set") == 0) &&
                     i == 2U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     is_hash_storage_type(
                         expr->as.call.arguments.items[0]->type))
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->error_type;
            else if ((strcmp(name, "Dictionary::Add") == 0 ||
                      strcmp(name, "Dictionary::TryAdd") == 0 ||
                      strcmp(name, "Dictionary::ContainsKey") == 0 ||
                      strcmp(name, "Dictionary::Remove") == 0 ||
                      strcmp(name, "Dictionary::Get") == 0 ||
                      strcmp(name, "Dictionary::Set") == 0) &&
                     i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     is_hash_storage_type(
                         expr->as.call.arguments.items[0]->type))
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if (strcmp(name, "Dictionary::ContainsValue") == 0 &&
                     i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     is_hash_storage_type(
                         expr->as.call.arguments.items[0]->type))
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->error_type;
            else if ((strcmp(name, "Dictionary::EnsureCapacity") == 0 ||
                      strcmp(name, "Dictionary::TrimExcess") == 0) &&
                     i == 1U)
                checker->expected_type = &type_usize;
            else if (strcmp(name, "Queue::Enqueue") == 0 && i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_QUEUE)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if (strcmp(name, "Queue::EnsureCapacity") == 0 && i == 1U)
                checker->expected_type = &type_usize;
            else if (strcmp(name, "Stack::Push") == 0 && i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_STACK)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if ((strcmp(name, "Stack::EnsureCapacity") == 0 ||
                      strcmp(name, "Stack::TrimExcess") == 0) && i == 1U)
                checker->expected_type = &type_usize;
            else if (strcmp(name, "Option::Some") == 0 &&
                     checker->expected_type != NULL &&
                     checker->expected_type->kind == TYPE_OPTION &&
                     i == 0U)
                checker->expected_type =
                    checker->expected_type->element;
            else if ((strcmp(name, "Result::Ok") == 0 ||
                      strcmp(name, "Result::Err") == 0) &&
                     i == 0U) {
                Type *result =
                    checker->expected_type != NULL &&
                    checker->expected_type->kind == TYPE_RESULT
                    ? checker->expected_type
                    : checker->function->checked_return_type;
                if (result != NULL && result->kind == TYPE_RESULT)
                    checker->expected_type =
                        strcmp(name, "Result::Ok") == 0
                        ? result->element : result->error_type;
            }
            (void)check_expr(
                checker, expr->as.call.arguments.items[i]);
            checker->expected_type = previous_expected;
        }
    }
    if (pending_overload) {
        overloads = collect_call_overloads(
            checker, name, expr, true);
        if (overloads.exact_count == 1U) {
            declared_declaration = overloads.selected;
            declared_function =
                (Function *)&declared_declaration->as.function;
            declared_module = declared_declaration->module_name;
            expr->resolved_decl = declared_declaration;
        } else {
            diagnose_call_overloads(
                checker, name, expr, &overloads);
            CallOverloadSet fallback = collect_call_overloads(
                checker, name, expr, false);
            declared_declaration = fallback.selected;
            declared_function = declared_declaration != NULL
                ? (Function *)&declared_declaration->as.function
                : NULL;
            declared_module = declared_declaration != NULL
                ? declared_declaration->module_name
                : checker->current_module;
            expr->resolved_decl = declared_declaration;
        }
    }
    if (strcmp(name, "Task::WhenAll") == 0 ||
        strcmp(name, "Task::WhenAny") == 0) {
        bool when_all = strcmp(name, "Task::WhenAll") == 0;
        if (expr->as.call.arguments.count != 1U) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Task.%s` expects one List<Task<T>> value",
                      when_all ? "WhenAll" : "WhenAny");
            return &type_error;
        }
        Type *list = expr->as.call.arguments.items[0]->type;
        if (list == NULL || list->kind != TYPE_VEC ||
            list->element == NULL || list->element->kind != TYPE_TASK) {
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[0]->span,
                      "`Task.%s` expects a List<Task<T>>, found `%s`",
                      when_all ? "WhenAll" : "WhenAny",
                      list != NULL ? list->name : "<unknown>");
            return &type_error;
        }
        Type *result_element = when_all ? NULL : list->element;
        if (when_all) {
            if (list->element->element->kind == TYPE_UNIT)
                result_element = &type_unit;
            else {
                size_t length = strlen(list->element->element->name) + 7U;
                char *list_name = lang_arena_alloc(
                    &checker->module->arena, length);
                (void)snprintf(list_name, length, "List<%s>",
                               list->element->element->name);
                result_element = resolve_type(checker, list_name, expr->span);
            }
        }
        if (result_element->kind == TYPE_UNIT)
            return resolve_type(checker, "Task", expr->span);
        size_t length = strlen(result_element->name) + 7U;
        char *task_name = lang_arena_alloc(&checker->module->arena, length);
        (void)snprintf(task_name, length, "Task<%s>", result_element->name);
        return resolve_type(checker, task_name, expr->span);
    }
    if (strcmp(name, "CancellationTokenSource::New") == 0) {
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`CancellationTokenSource` constructor expects no arguments");
        return &type_cancellation_token_source;
    }
    if (strcmp(name, "CancellationToken::None") == 0) {
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`CancellationToken.None` takes no arguments");
        return &type_cancellation_token;
    }
    if (strcmp(name, "CancellationTokenSource::Token") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_CANCELLATION_TOKEN_SOURCE)
            lang_diag(checker->diagnostics, expr->span,
                      "`CancellationTokenSource.Token` expects one source");
        return &type_cancellation_token;
    }
    if (strcmp(name, "CancellationTokenSource::Cancel") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_CANCELLATION_TOKEN_SOURCE)
            lang_diag(checker->diagnostics, expr->span,
                      "`CancellationTokenSource.Cancel` expects one source");
        return &type_unit;
    }
    if (strcmp(name, "CancellationToken::IsCancellationRequested") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            (expr->as.call.arguments.items[0]->type->kind !=
                 TYPE_CANCELLATION_TOKEN &&
             expr->as.call.arguments.items[0]->type->kind !=
                 TYPE_CANCELLATION_TOKEN_SOURCE))
            lang_diag(checker->diagnostics, expr->span,
                      "`IsCancellationRequested` expects a cancellation token or source");
        return &type_bool;
    }
    if (strcmp(name, "CancellationToken::ThrowIfCancellationRequested") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_CANCELLATION_TOKEN)
            lang_diag(checker->diagnostics, expr->span,
                      "`ThrowIfCancellationRequested` expects one CancellationToken");
        return &type_unit;
    }
    if (strcmp(name, "Console::WriteLine") == 0 ||
        strcmp(name, "Console::Write") == 0 ||
        strcmp(name, "Console::Error::WriteLine") == 0 ||
        strcmp(name, "Console::Error::Write") == 0) {
        if (expr->as.call.arguments.count != 1U)
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects one argument", name);
        else if (expr->as.call.arguments.items[0]->kind == EXPR_INT)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[0], &type_i64);
        else if (expr->as.call.arguments.items[0]->kind == EXPR_FLOAT)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[0], &type_f64);
        return &type_unit;
    }
    if (strcmp(name, "Html::ToHtmlString") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            !same_type(expr->as.call.arguments.items[0]->type, &type_html))
            lang_diag(checker->diagnostics, expr->span,
                      "`Html.ToHtmlString` expects one Html value");
        return &type_string;
    }
    if (strcmp(name, "TextLen") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING)
            lang_diag(checker->diagnostics, expr->span,
                      "`TextLen` expects one `string` value");
        return &type_usize;
    }
    if (strcmp(name, "StringByteAt") == 0) {
        if (expr->as.call.arguments.count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1],
                &type_usize);
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING ||
            !same_type(expr->as.call.arguments.items[1]->type,
                       &type_usize))
            lang_diag(
                checker->diagnostics, expr->span,
                "string indexing expects `(string, nuint)`");
        return &type_u8;
    }
    if (strcmp(name, "Html::UnsafeRaw") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING)
            lang_diag(checker->diagnostics, expr->span,
                      "`Html::unsafe_raw` expects one `string` value");
        return &type_html;
    }
    if (strcmp(name, "panic") == 0 || strcmp(name, "trap") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING)
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects one `string` message", name);
        return &type_never;
    }
    if (strcmp(name, "StringBuilder::New") == 0) {
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` expects no arguments");
        return &type_string_builder;
    }
    if (strcmp(name, "StringBuilder::Append") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_STRING_BUILDER ||
            (expr->as.call.arguments.items[1]->type->kind != TYPE_STRING &&
             !is_numeric(expr->as.call.arguments.items[1]->type) &&
             expr->as.call.arguments.items[1]->type->kind != TYPE_BOOL &&
             expr->as.call.arguments.items[1]->type->kind != TYPE_CHAR)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`StringBuilder.Append` expects text or a scalar value");
        } else if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *builder = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (builder != NULL && !builder->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable StringBuilder `%s`",
                          builder->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "StringBuilder::AppendByte") == 0) {
        if (expr->as.call.arguments.count == 2U &&
            expr->as.call.arguments.items[1]->kind == EXPR_INT)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_u8);
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_STRING_BUILDER ||
            expr->as.call.arguments.items[1]->type->kind != TYPE_U8) {
            lang_diag(
                checker->diagnostics, expr->span,
                "`StringBuilder.AppendByte` expects a byte");
        } else if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *builder = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (builder != NULL && !builder->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable StringBuilder `%s`",
                          builder->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "StringBuilder::Finish") == 0 ||
        strcmp(name, "StringBuilder::ToString") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_STRING_BUILDER)
            lang_diag(checker->diagnostics, expr->span,
                      "`StringBuilder.ToString` expects no arguments");
        return &type_string;
    }
    if (strcmp(name, "StringBuilder::Length") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_STRING_BUILDER)
            lang_diag(checker->diagnostics, expr->span,
                      "`StringBuilder.Length` expects no arguments");
        return &type_usize;
    }
    if (strcmp(name, "StringBuilder::Clear") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_STRING_BUILDER)
            lang_diag(checker->diagnostics, expr->span,
                      "`StringBuilder.Clear` expects no arguments");
        else if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *builder = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (builder != NULL && !builder->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable StringBuilder `%s`",
                          builder->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "Url::relative") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING)
            lang_diag(checker->diagnostics, expr->span,
                      "`Url::relative` expects one `string` or owned `String` value");
        return &type_url;
    }
    if (strcmp(name, "Url::fragment") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING)
            lang_diag(checker->diagnostics, expr->span,
                      "`Url::fragment` expects one `string` value");
        return &type_url;
    }
    if (strcmp(name, "List::New") == 0) {
        Type *vector = checker->expected_type;
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` expects no arguments");
        if (vector != NULL && vector->kind == TYPE_STRING_BUILDER) {
            expr->as.call.callee->as.name = "StringBuilder::New";
            return &type_string_builder;
        }
        if (vector != NULL && vector->kind == TYPE_QUEUE) {
            expr->as.call.callee->as.name = "Queue::New";
            return vector;
        }
        if (vector != NULL && vector->kind == TYPE_STACK) {
            expr->as.call.callee->as.name = "Stack::New";
            return vector;
        }
        if (vector != NULL &&
            vector->kind == TYPE_CANCELLATION_TOKEN_SOURCE) {
            expr->as.call.callee->as.name =
                "CancellationTokenSource::New";
            return vector;
        }
        if (is_hash_storage_type(vector)) {
            expr->as.call.callee->as.name = "Dictionary::New";
            return vector;
        }
        if (vector == NULL || vector->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` requires an expected constructible type");
            return &type_error;
        }
        return vector;
    }
    if (strcmp(name, "Dictionary::New") == 0) {
        Type *dictionary = checker->expected_type;
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` expects no arguments");
        if (!is_hash_storage_type(dictionary)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` requires an expected Dictionary type");
            return &type_error;
        }
        return dictionary;
    }
    if (strcmp(name, "Queue::New") == 0) {
        Type *queue = checker->expected_type;
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` expects no arguments");
        if (queue == NULL || queue->kind != TYPE_QUEUE) {
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` requires an expected Queue type");
            return &type_error;
        }
        return queue;
    }
    if (strcmp(name, "Queue::Enqueue") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_QUEUE) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Queue.Enqueue` expects a Queue and one value");
            return &type_unit;
        }
        Type *queue = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             queue->element);
        if (!same_type(queue->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Queue value expects `%s`, found `%s`",
                      queue->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_unit;
    }
    if (strcmp(name, "Queue::Dequeue") == 0 ||
        strcmp(name, "Queue::Peek") == 0) {
        bool dequeue = strcmp(name, "Queue::Dequeue") == 0;
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_QUEUE) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Queue.%s` expects one Queue value",
                      dequeue ? "Dequeue" : "Peek");
            return &type_error;
        }
        Type *queue = expr->as.call.arguments.items[0]->type;
        if (dequeue)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        return queue->element;
    }
    if (strcmp(name, "Queue::TryDequeue") == 0 ||
        strcmp(name, "Queue::TryPeek") == 0) {
        bool dequeue = strcmp(name, "Queue::TryDequeue") == 0;
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_QUEUE) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Queue.%s` expects a Queue and an out value",
                      dequeue ? "TryDequeue" : "TryPeek");
            return &type_bool;
        }
        Type *queue = expr->as.call.arguments.items[0]->type;
        if (call_argument_mode(expr, 1U) != PARAMETER_MODE_OUT)
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "argument 2 to `%s` must use `out`", name);
        if (!same_type(queue->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Queue output expects `%s`, found `%s`",
                      queue->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (dequeue)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        return &type_bool;
    }
    if (strcmp(name, "Queue::Count") == 0 ||
        strcmp(name, "Queue::Capacity") == 0 ||
        strcmp(name, "Queue::Clear") == 0 ||
        strcmp(name, "Queue::TrimExcess") == 0) {
        bool read_only = strcmp(name, "Queue::Count") == 0 ||
            strcmp(name, "Queue::Capacity") == 0;
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_QUEUE)
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects one Queue value", name);
        else if (!read_only)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        return read_only ? &type_usize : &type_unit;
    }
    if (strcmp(name, "Queue::EnsureCapacity") == 0) {
        if (expr->as.call.arguments.count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_QUEUE ||
            !same_type(expr->as.call.arguments.items[1]->type, &type_usize)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Queue.EnsureCapacity` expects a Queue and nuint capacity");
            return &type_usize;
        }
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_usize;
    }
    if (strcmp(name, "Stack::New") == 0) {
        Type *stack = checker->expected_type;
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` expects no arguments");
        if (stack == NULL || stack->kind != TYPE_STACK) {
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` requires an expected Stack type");
            return &type_error;
        }
        return stack;
    }
    if (strcmp(name, "Stack::Push") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack.Push` expects a Stack and one value");
            return &type_unit;
        }
        Type *stack = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             stack->element);
        if (!same_type(stack->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Stack value expects `%s`, found `%s`",
                      stack->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_unit;
    }
    if (strcmp(name, "Stack::Pop") == 0 ||
        strcmp(name, "Stack::Peek") == 0) {
        bool pop = strcmp(name, "Stack::Pop") == 0;
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack.%s` expects one Stack value",
                      pop ? "Pop" : "Peek");
            return &type_error;
        }
        Type *stack = expr->as.call.arguments.items[0]->type;
        if (pop)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        return stack->element;
    }
    if (strcmp(name, "Stack::TryPop") == 0 ||
        strcmp(name, "Stack::TryPeek") == 0) {
        bool pop = strcmp(name, "Stack::TryPop") == 0;
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack.%s` expects a Stack and an out value",
                      pop ? "TryPop" : "TryPeek");
            return &type_bool;
        }
        Type *stack = expr->as.call.arguments.items[0]->type;
        if (call_argument_mode(expr, 1U) != PARAMETER_MODE_OUT)
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "argument 2 to `%s` must use `out`", name);
        if (!same_type(stack->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Stack output expects `%s`, found `%s`",
                      stack->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (pop)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        return &type_bool;
    }
    if (strcmp(name, "Stack::Count") == 0 ||
        strcmp(name, "Stack::Capacity") == 0 ||
        strcmp(name, "Stack::Clear") == 0) {
        bool read_only = strcmp(name, "Stack::Count") == 0 ||
            strcmp(name, "Stack::Capacity") == 0;
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK)
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects one Stack value", name);
        else if (!read_only)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        return read_only ? &type_usize : &type_unit;
    }
    if (strcmp(name, "Stack::EnsureCapacity") == 0) {
        if (expr->as.call.arguments.count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK ||
            !same_type(expr->as.call.arguments.items[1]->type, &type_usize)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack.EnsureCapacity` expects a Stack and nuint capacity");
            return &type_usize;
        }
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_usize;
    }
    if (strcmp(name, "Stack::TrimExcess") == 0) {
        size_t count = expr->as.call.arguments.count;
        if (count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
        if ((count != 1U && count != 2U) ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK ||
            (count == 2U && !same_type(
                expr->as.call.arguments.items[1]->type, &type_usize))) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack.TrimExcess` expects a Stack and optional nuint capacity");
            return &type_unit;
        }
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_unit;
    }
    if (strcmp(name, "Dictionary::Add") == 0 ||
        strcmp(name, "Dictionary::TryAdd") == 0 ||
        strcmp(name, "Dictionary::Set") == 0) {
        bool set = strcmp(name, "Dictionary::Set") == 0;
        bool try_add = strcmp(name, "Dictionary::TryAdd") == 0;
        if (expr->as.call.arguments.count != 3U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.%s` expects a Dictionary, key, and value",
                      set ? "Set" : "Add");
            return &type_unit;
        }
        Type *dictionary = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             dictionary->element);
        (void)coerce_literal(checker, expr->as.call.arguments.items[2],
                             dictionary->error_type);
        if (!same_type(dictionary->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Dictionary key expects `%s`, found `%s`",
                      dictionary->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (!same_type(dictionary->error_type,
                       expr->as.call.arguments.items[2]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[2]->span,
                      "Dictionary value expects `%s`, found `%s`",
                      dictionary->error_type->name,
                      expr->as.call.arguments.items[2]->type->name);
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return try_add ? &type_bool : &type_unit;
    }
    if (strcmp(name, "Dictionary::Count") == 0 ||
        strcmp(name, "Dictionary::Clear") == 0) {
        bool count = strcmp(name, "Dictionary::Count") == 0;
        if (expr->as.call.arguments.count != 1U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type))
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.%s` expects one Dictionary value",
                      count ? "Count" : "Clear");
        if (!count && expr->as.call.arguments.count == 1U)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        return count ? &type_usize : &type_unit;
    }
    if (strcmp(name, "Dictionary::ContainsKey") == 0 ||
        strcmp(name, "Dictionary::Remove") == 0 ||
        strcmp(name, "Dictionary::Get") == 0) {
        bool get = strcmp(name, "Dictionary::Get") == 0;
        if (expr->as.call.arguments.count != 2U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects a Dictionary and key", name);
            return get ? &type_error : &type_bool;
        }
        Type *dictionary = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             dictionary->element);
        if (!same_type(dictionary->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Dictionary key expects `%s`, found `%s`",
                      dictionary->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (strcmp(name, "Dictionary::Remove") == 0)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        return get ? dictionary->error_type : &type_bool;
    }
    if (strcmp(name, "Dictionary::TryGetValue") == 0) {
        if (expr->as.call.arguments.count != 3U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.TryGetValue` expects a Dictionary, key, and out value");
            return &type_bool;
        }
        Type *dictionary = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             dictionary->element);
        if (!same_type(dictionary->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Dictionary key expects `%s`, found `%s`",
                      dictionary->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (call_argument_mode(expr, 2U) != PARAMETER_MODE_OUT)
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[2]->span,
                      "argument 3 to `Dictionary::TryGetValue` must use `out`");
        if (!same_type(dictionary->error_type,
                       expr->as.call.arguments.items[2]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[2]->span,
                      "Dictionary output expects `%s`, found `%s`",
                      dictionary->error_type->name,
                      expr->as.call.arguments.items[2]->type->name);
        return &type_bool;
    }
    if (strcmp(name, "Dictionary::ContainsValue") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.ContainsValue` expects a Dictionary and value");
            return &type_bool;
        }
        Type *dictionary = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             dictionary->error_type);
        if (!same_type(dictionary->error_type,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Dictionary value expects `%s`, found `%s`",
                      dictionary->error_type->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (!list_element_has_default_equality(dictionary->error_type))
            lang_diag(checker->diagnostics, expr->span,
                      "Dictionary value type `%s` does not have built-in equality",
                      dictionary->error_type->name);
        return &type_bool;
    }
    if (strcmp(name, "Dictionary::EnsureCapacity") == 0) {
        if (expr->as.call.arguments.count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
        if (expr->as.call.arguments.count != 2U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type) ||
            !same_type(expr->as.call.arguments.items[1]->type, &type_usize)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.EnsureCapacity` expects a Dictionary and nuint capacity");
            return &type_usize;
        }
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_usize;
    }
    if (strcmp(name, "Dictionary::TrimExcess") == 0) {
        size_t count = expr->as.call.arguments.count;
        if (count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
        if ((count != 1U && count != 2U) ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type) ||
            (count == 2U && !same_type(
                expr->as.call.arguments.items[1]->type, &type_usize))) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.TrimExcess` expects a Dictionary and optional nuint capacity");
            return &type_unit;
        }
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_unit;
    }
    if (strcmp(name, "Dictionary::Capacity") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type))
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.Capacity` expects one Dictionary value");
        return &type_usize;
    }
    if (strcmp(name, "List::Add") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Add` expects a List and one element");
            return &type_unit;
        }
        Type *vector = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             vector->element);
        Type *item = expr->as.call.arguments.items[1]->type;
        if (!same_type(vector->element, item))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "`List::Add` expects `%s`, found `%s`",
                      vector->element->name, item->name);
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::Count") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC)
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Count` expects one List value");
        return &type_usize;
    }
    if (strcmp(name, "List::Capacity") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC)
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Capacity` expects one List value");
        return &type_usize;
    }
    if (strcmp(name, "List::Clear") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Clear` expects one List value");
            return &type_unit;
        }
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::Insert") == 0 ||
        strcmp(name, "List::Set") == 0) {
        const char *member = strcmp(name, "List::Insert") == 0
                           ? "Insert" : "index assignment";
        if (expr->as.call.arguments.count != 3U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.%s` expects a List, a nuint index, and an element",
                      member);
            return &type_unit;
        }
        Type *vector = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[1], &type_usize);
        if (!same_type(expr->as.call.arguments.items[1]->type, &type_usize))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "List index expects `nuint`, found `%s`",
                      expr->as.call.arguments.items[1]->type->name);
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[2], vector->element);
        if (!same_type(expr->as.call.arguments.items[2]->type,
                       vector->element))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[2]->span,
                      "List element expects `%s`, found `%s`",
                      vector->element->name,
                      expr->as.call.arguments.items[2]->type->name);
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::RemoveAt") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.RemoveAt` expects a List and a nuint index");
            return &type_unit;
        }
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[1], &type_usize);
        if (!same_type(expr->as.call.arguments.items[1]->type, &type_usize))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "List index expects `nuint`, found `%s`",
                      expr->as.call.arguments.items[1]->type->name);
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::Contains") == 0 ||
        strcmp(name, "List::IndexOf") == 0 ||
        strcmp(name, "List::LastIndexOf") == 0 ||
        strcmp(name, "List::Remove") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects a List and one element", name);
            return strcmp(name, "List::Contains") == 0 ||
                   strcmp(name, "List::Remove") == 0
                 ? &type_bool : &type_i32;
        }
        Type *vector = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[1], vector->element);
        if (!same_type(expr->as.call.arguments.items[1]->type,
                       vector->element))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "List element expects `%s`, found `%s`",
                      vector->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (!list_element_has_default_equality(vector->element))
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` requires an element type with defined equality; `%s` does not yet provide it",
                      name, vector->element->name);
        if (strcmp(name, "List::Remove") == 0 &&
            expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return strcmp(name, "List::Contains") == 0 ||
               strcmp(name, "List::Remove") == 0
             ? &type_bool : &type_i32;
    }
    if (strcmp(name, "List::AddRange") == 0 ||
        strcmp(name, "List::InsertRange") == 0) {
        bool insert = strcmp(name, "List::InsertRange") == 0;
        size_t expected_count = insert ? 3U : 2U;
        size_t source_index = insert ? 2U : 1U;
        if (expr->as.call.arguments.count != expected_count ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC ||
            expr->as.call.arguments.items[source_index]->type->kind !=
                TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects compatible List values%s",
                      name, insert ? " and a nuint index" : "");
            return &type_unit;
        }
        Type *target = expr->as.call.arguments.items[0]->type;
        Type *source = expr->as.call.arguments.items[source_index]->type;
        if (!same_type(target, source))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[source_index]->span,
                      "`%s` expects `%s`, found `%s`",
                      name, target->name, source->name);
        if (insert) {
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
            if (!same_type(expr->as.call.arguments.items[1]->type,
                           &type_usize))
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[1]->span,
                          "List index expects `nuint`, found `%s`",
                          expr->as.call.arguments.items[1]->type->name);
        }
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::RemoveRange") == 0 ||
        strcmp(name, "List::GetRange") == 0) {
        bool get = strcmp(name, "List::GetRange") == 0;
        if (expr->as.call.arguments.count != 3U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects a List, a nuint index, and a nuint count",
                      name);
            return get ? &type_error : &type_unit;
        }
        for (size_t i = 1U; i < 3U; ++i) {
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[i], &type_usize);
            if (!same_type(expr->as.call.arguments.items[i]->type,
                           &type_usize))
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[i]->span,
                          "List range expects `nuint`, found `%s`",
                          expr->as.call.arguments.items[i]->type->name);
        }
        if (!get && expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return get ? expr->as.call.arguments.items[0]->type : &type_unit;
    }
    if (strcmp(name, "List::Reverse") == 0) {
        size_t count = expr->as.call.arguments.count;
        if ((count != 1U && count != 3U) ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Reverse` expects no arguments or a nuint index and count");
            return &type_unit;
        }
        if (count == 3U) {
            for (size_t i = 1U; i < 3U; ++i) {
                (void)coerce_literal(
                    checker, expr->as.call.arguments.items[i], &type_usize);
                if (!same_type(expr->as.call.arguments.items[i]->type,
                               &type_usize))
                    lang_diag(checker->diagnostics,
                              expr->as.call.arguments.items[i]->span,
                              "List range expects `nuint`, found `%s`",
                              expr->as.call.arguments.items[i]->type->name);
            }
        }
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::EnsureCapacity") == 0 ||
        strcmp(name, "List::SetCapacity") == 0) {
        bool ensure = strcmp(name, "List::EnsureCapacity") == 0;
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects a List and a nuint capacity", name);
            return ensure ? &type_usize : &type_unit;
        }
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[1], &type_usize);
        if (!same_type(expr->as.call.arguments.items[1]->type, &type_usize))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "List capacity expects `nuint`, found `%s`",
                      expr->as.call.arguments.items[1]->type->name);
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return ensure ? &type_usize : &type_unit;
    }
    if (strcmp(name, "List::TrimExcess") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.TrimExcess` expects one List value");
            return &type_unit;
        }
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::Exists") == 0 ||
        strcmp(name, "List::FindAll") == 0 ||
        strcmp(name, "List::FindIndex") == 0 ||
        strcmp(name, "List::FindLastIndex") == 0 ||
        strcmp(name, "List::RemoveAll") == 0 ||
        strcmp(name, "List::ForEach") == 0 ||
        strcmp(name, "List::TrueForAll") == 0) {
        bool indexed = strcmp(name, "List::FindIndex") == 0 ||
                       strcmp(name, "List::FindLastIndex") == 0;
        size_t count = expr->as.call.arguments.count;
        bool arity_ok = count == 2U;
        if (!arity_ok ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` received an invalid argument list", name);
            return strcmp(name, "List::FindAll") == 0
                 ? &type_error
                 : strcmp(name, "List::ForEach") == 0
                 ? &type_unit
                 : (indexed || strcmp(name, "List::RemoveAll") == 0)
                 ? &type_i32 : &type_bool;
        }
        Type *list = expr->as.call.arguments.items[0]->type;
        size_t callback_index = count - 1U;
        Type *callback =
            expr->as.call.arguments.items[callback_index]->type;
        bool action = strcmp(name, "List::ForEach") == 0;
        if (callback->kind != TYPE_FUNCTION ||
            callback->argument_count != 1U ||
            !same_type(callback->arguments[0], list->element) ||
            !same_type(callback->element,
                       action ? &type_unit : &type_bool) ||
            (callback->argument_count != 0U &&
             callback->parameter_modes[0] != PARAMETER_MODE_VALUE))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[callback_index]->span,
                      "`%s` expects a function taking `%s` and returning `%s`",
                      name, list->element->name,
                      action ? "void" : "bool");
        for (size_t i = 1U; i < callback_index; ++i) {
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[i], &type_usize);
            if (!same_type(expr->as.call.arguments.items[i]->type,
                           &type_usize))
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[i]->span,
                          "List range expects `nuint`, found `%s`",
                          expr->as.call.arguments.items[i]->type->name);
        }
        if (strcmp(name, "List::RemoveAll") == 0 &&
            expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        if (strcmp(name, "List::FindAll") == 0) return list;
        if (strcmp(name, "List::ForEach") == 0) return &type_unit;
        if (indexed || strcmp(name, "List::RemoveAll") == 0)
            return &type_i32;
        return &type_bool;
    }
    if (strcmp(name, "List::Get") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Get` expects a List and a nuint index");
            return &type_error;
        }
        Type *vector = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[1], &type_usize);
        if (!same_type(
                expr->as.call.arguments.items[1]->type, &type_usize))
            lang_diag(
                checker->diagnostics,
                expr->as.call.arguments.items[1]->span,
                "`List::Get` index expects `nuint`, found `%s`",
                expr->as.call.arguments.items[1]->type->name);
        if (vector->element->requires_cleanup)
            lang_diag(
                checker->diagnostics, expr->span,
                "`List::Get` cannot copy noncopyable element type `%s`",
                vector->element->name);
        return vector->element;
    }
    if (strcmp(name, "Buffer::allocate") == 0) return &type_buffer;
    if (strcmp(name, "BufferAsMutSlice") == 0) {
        if (checker->unsafe_depth == 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`buffer_as_mut_slice` requires an unsafe block");
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_BUFFER) {
            lang_diag(checker->diagnostics, expr->span,
                      "`buffer_as_mut_slice` expects one Buffer local");
        } else if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *buffer = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (buffer != NULL && !buffer->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot create a mutable slice from immutable Buffer `%s`",
                          buffer->name);
        }
        return &type_u8_slice;
    }
    if (strcmp(name, "Arena::new") == 0) {
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`Arena::new` expects no arguments");
        return &type_arena;
    }
    if (strcmp(name, "ArenaAlloc") == 0) {
        if (checker->unsafe_depth == 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`arena_alloc` requires an unsafe block");
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_ARENA ||
            expr->as.call.arguments.items[1]->type->kind != TYPE_I64)
            lang_diag(checker->diagnostics, expr->span,
                      "`arena_alloc` expects `(Arena, i64)`");
        if (expr->as.call.arguments.count >= 1U &&
            expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *arena = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (arena != NULL && !arena->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot allocate through immutable Arena `%s`",
                          arena->name);
        }
        if (checker->expected_type != NULL &&
            checker->expected_type->kind == TYPE_RAW_POINTER)
            return checker->expected_type;
        return &type_raw_pointer;
    }
    if (strcmp(name, "ArenaReset") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_ARENA)
            lang_diag(checker->diagnostics, expr->span,
                      "`arena_reset` expects an Arena");
        if (expr->as.call.arguments.count >= 1U &&
            expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *arena = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (arena != NULL && !arena->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot reset immutable Arena `%s`",
                          arena->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "raw_load_i64") == 0) {
        if (checker->unsafe_depth == 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`raw_load_i64` requires an unsafe block");
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_RAW_POINTER ||
            expr->as.call.arguments.items[0]->type->element == NULL ||
            expr->as.call.arguments.items[0]->type->element->kind != TYPE_I64)
            lang_diag(checker->diagnostics, expr->span,
                      "`raw_load_i64` expects `const long*` or `long*`");
        return &type_i64;
    }
    if (strcmp(name, "raw_store_i64") == 0) {
        if (checker->unsafe_depth == 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`raw_store_i64` requires an unsafe block");
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_RAW_POINTER ||
            !expr->as.call.arguments.items[0]->type->pointer_mutable ||
            expr->as.call.arguments.items[0]->type->element == NULL ||
            expr->as.call.arguments.items[0]->type->element->kind != TYPE_I64 ||
            expr->as.call.arguments.items[1]->type->kind != TYPE_I64)
            lang_diag(checker->diagnostics, expr->span,
                      "`raw_store_i64` expects `(long*, long)`");
        return &type_unit;
    }
    if (strcmp(name, "Option::Some") == 0 ||
        strcmp(name, "Option::None") == 0) {
        Type *option = checker->expected_type;
        if (option == NULL || option->kind != TYPE_OPTION) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` requires an expected `Option<T>` type", name);
            return &type_error;
        }
        bool some = strcmp(name, "Option::Some") == 0;
        size_t expected_count = some ? 1U : 0U;
        if (!some && !expr->as.call.implicit_enum_value &&
            expr->as.call.arguments.count == 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`Option.None` is a value and must not be called");
        if (expr->as.call.arguments.count != expected_count)
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects %zu payload value(s)",
                      name, expected_count);
        if (some && expr->as.call.arguments.count == 1U) {
            (void)coerce_literal(checker,
                                 expr->as.call.arguments.items[0],
                                 option->element);
            Type *actual = expr->as.call.arguments.items[0]->type;
            if (!same_type(option->element, actual))
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "`Option::Some` expects `%s`, found `%s`",
                          option->element->name, actual->name);
        }
        return option;
    }
    if (strcmp(name, "Result::Ok") == 0 || strcmp(name, "Result::Err") == 0) {
        Type *result = checker->expected_type != NULL &&
                       checker->expected_type->kind == TYPE_RESULT
                     ? checker->expected_type
                     : checker->function->checked_return_type;
        if (result == NULL || result->kind != TYPE_RESULT) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` needs a surrounding Result-returning function", name);
            return &type_error;
        }
        if (expr->as.call.arguments.count != 1U) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects one payload value", name);
            return result;
        }
        Type *expected = strcmp(name, "Result::Ok") == 0
                       ? result->element : result->error_type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[0],
                             expected);
        Type *actual = expr->as.call.arguments.items[0]->type;
        if (!type_assignable(expected, actual))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[0]->span,
                      "`%s` payload expects `%s`, found `%s`",
                      name, expected->name, actual->name);
        return result;
    }
    const char *separator = last_path_separator(name);
    if (separator != NULL) {
        size_t enum_length = (size_t)(separator - name);
        char *enum_name = lang_arena_strndup(
            &checker->module->arena, name, enum_length);
        Type *applied_enum = NULL;
        Decl *decl = NULL;
        if (checker->expected_type != NULL &&
            checker->expected_type->kind == TYPE_NAMED &&
            checker->expected_type->declaration != NULL &&
            checker->expected_type->declaration->kind == DECL_ENUM &&
            visible_declaration_path_matches(
                checker,
                enum_name,
                checker->expected_type->declaration->as.enumeration.name,
                checker->expected_type->declaration->module_name)) {
            applied_enum = checker->expected_type;
            decl = (Decl *)applied_enum->declaration;
        } else {
            decl = find_type_declaration(
                checker, enum_name, expr->span);
        }
        if (decl != NULL && decl->kind == DECL_ENUM) {
            expr->resolved_decl = decl;
            const char *variant = separator + 2;
            for (size_t v = 0U; v < decl->as.enumeration.variant_count; ++v) {
                FieldDecl *candidate = &decl->as.enumeration.variants[v];
                if (strcmp(candidate->name, variant) == 0) {
                    Type *result = applied_enum != NULL
                                 ? applied_enum
                                 : resolve_type(
                                       checker,
                                       decl->as.enumeration.name,
                                       expr->span);
                    bool payload = strcmp(candidate->type_name, "Unit") != 0;
                    if (!payload && !expr->as.call.implicit_enum_value &&
                        expr->as.call.arguments.count == 0U)
                        lang_diag(
                            checker->diagnostics, expr->span,
                            "`%s` is a value and must not be called", name);
                    if (expr->as.call.arguments.count != (payload ? 1U : 0U))
                        lang_diag(checker->diagnostics, expr->span,
                                  "%s alternative `%s` expects %u payload value(s)",
                                  decl->as.enumeration.is_union
                                      ? "union" : "enum",
                                  name, payload ? 1U : 0U);
                    if (payload && expr->as.call.arguments.count == 1U) {
                        Type *expected =
                            resolve_type_syntax_in_applied_declaration(
                            checker, result, candidate->type_syntax,
                            candidate->type_name, candidate->span);
                        (void)coerce_literal(
                            checker, expr->as.call.arguments.items[0],
                            expected);
                        Type *actual = expr->as.call.arguments.items[0]->type;
                        if (!type_assignable(expected, actual))
                            lang_diag(checker->diagnostics,
                                      expr->as.call.arguments.items[0]->span,
                                      "enum variant `%s` expects `%s`, found `%s`",
                                      name, expected->name, actual->name);
                    }
                    return result;
                }
            }
        }
    }
    Function *function = declared_function;
    if (function == NULL) {
        const char *constructor = last_path_separator(name);
        if (constructor != NULL &&
            strcmp(constructor + 2U, "new") == 0) {
            char *type_name = lang_arena_strndup(
                &checker->module->arena, name,
                (size_t)(constructor - name));
            Decl *type = find_type_declaration(
                checker, type_name, expr->span);
            if (type != NULL && type->kind == DECL_CLASS &&
                type->as.structure.is_interface) {
                lang_diag(checker->diagnostics, expr->span,
                          "cannot instantiate interface `%s`",
                          type->as.structure.name);
                return &type_error;
            }
        }
        lang_diag(checker->diagnostics, expr->span,
                  "unknown function `%s`", name);
        return &type_error;
    }
    if (function->is_constructor &&
        function->checked_return_type != NULL &&
        function->checked_return_type->declaration != NULL &&
        function->checked_return_type->declaration->kind == DECL_CLASS &&
        function->checked_return_type->declaration
            ->as.structure.is_abstract)
        lang_diag(checker->diagnostics, expr->span,
                  "cannot instantiate abstract class `%s`",
                  function->checked_return_type->declaration
                      ->as.structure.name);
    if (function->param_count != expr->as.call.arguments.count) {
        lang_diag(checker->diagnostics, expr->span,
                  "function `%s` expects %zu arguments, found %zu", name,
                  function->param_count, expr->as.call.arguments.count);
    }
    size_t count = function->param_count < expr->as.call.arguments.count
                 ? function->param_count : expr->as.call.arguments.count;
    for (size_t i = 0U; i < count; ++i) {
        Type *expected = resolve_declared_type_in_module(
            checker, function->params[i].type_syntax,
            function->params[i].type_name,
            function->params[i].span, declared_module);
        (void)coerce_literal(checker, expr->as.call.arguments.items[i],
                             expected);
        Type *actual = expr->as.call.arguments.items[i]->type;
        if (!type_assignable(expected, actual))
            lang_diag(checker->diagnostics, expr->as.call.arguments.items[i]->span,
                      "argument %zu to `%s` expects `%s`, found `%s`", i + 1U,
                      name, type_display_name(checker, expected),
                      type_display_name(checker, actual));
    }
    return resolve_declared_type_in_module(
        checker, function->return_type_syntax,
        function->return_type, function->span, declared_module);
}
