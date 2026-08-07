#include "checker_internal.h"

#include <stdlib.h>
#include <string.h>

bool same_type(const Type *a, const Type *b) {
    if (a == &type_error || b == &type_error) return true;
    if (a->kind != b->kind) return false;
    if (a->kind == TYPE_ARRAY)
        return a->array_length == b->array_length &&
               same_type(a->element, b->element);
    if (a->kind == TYPE_RESULT || a->kind == TYPE_DICTIONARY)
        return same_type(a->element, b->element) &&
               same_type(a->error_type, b->error_type);
    if (a->kind == TYPE_HASH_SET)
        return same_type(a->element, b->element);
    if (a->kind == TYPE_OPTION || a->kind == TYPE_SLICE ||
        a->kind == TYPE_READONLY_SPAN ||
        a->kind == TYPE_VEC || a->kind == TYPE_QUEUE)
        return same_type(a->element, b->element);
    if (a->kind == TYPE_TASK)
        return same_type(a->element, b->element);
    if (a->kind == TYPE_STACK)
        return same_type(a->element, b->element);
    if (a->kind == TYPE_RAW_POINTER)
        return a->pointer_mutable == b->pointer_mutable &&
               same_type(a->element, b->element);
    if (a->kind == TYPE_FUNCTION) {
        if (a->argument_count != b->argument_count ||
            !same_type(a->element, b->element))
            return false;
        for (size_t i = 0U; i < a->argument_count; ++i)
            if (a->parameter_modes[i] != b->parameter_modes[i] ||
                !same_type(a->arguments[i], b->arguments[i]))
                return false;
        return true;
    }
    if (a->kind == TYPE_NAMED || a->kind == TYPE_CLASS) {
        if (a->declaration != NULL && b->declaration != NULL) {
            if (a->declaration != b->declaration ||
                a->argument_count != b->argument_count)
                return false;
            for (size_t i = 0U; i < a->argument_count; ++i)
                if (!same_type(a->arguments[i], b->arguments[i]))
                    return false;
            return true;
        } else {
            return strcmp(a->name, b->name) == 0;
        }
    }
    return true;
}

static bool class_or_interface_assignable(
    const Decl *expected, const Decl *actual, size_t depth
) {
    if (expected == actual) return true;
    if (expected == NULL || actual == NULL || depth >= 256U)
        return false;
    if (class_or_interface_assignable(
            expected, actual->as.structure.base_class, depth + 1U))
        return true;
    for (size_t interface = 0U;
         interface < actual->as.structure.interface_count; ++interface)
        if (class_or_interface_assignable(
                expected, actual->as.structure.interfaces[interface],
                depth + 1U))
            return true;
    return false;
}

bool type_assignable(const Type *expected, const Type *actual) {
    if (same_type(expected, actual)) return true;
    if (expected != NULL && actual != NULL &&
        expected->kind == TYPE_CLASS && actual->kind == TYPE_CLASS &&
        expected->declaration != NULL && actual->declaration != NULL) {
        if (class_or_interface_assignable(
                expected->declaration, actual->declaration, 0U))
            return true;
    }
    return expected != NULL && actual != NULL &&
           expected->kind == TYPE_READONLY_SPAN &&
           actual->kind == TYPE_SLICE &&
           same_type(expected->element, actual->element);
}

const char *type_display_name(Checker *checker, const Type *type) {
    if (type == NULL) return "<unknown>";
    if ((type->kind != TYPE_NAMED && type->kind != TYPE_CLASS) ||
        type->declaration == NULL ||
        type->declaration->module_name == NULL)
        return type->name;
    size_t length = strlen(type->declaration->module_name) +
                    strlen(type->name) + 3U;
    char *display = lang_arena_alloc(&checker->module->arena, length);
    (void)snprintf(display, length, "%s::%s",
                   type->declaration->module_name, type->name);
    return display;
}

static unsigned integer_width(const Type *type) {
    switch (type->kind) {
        case TYPE_I8: case TYPE_U8: return 8U;
        case TYPE_I16: case TYPE_U16: return 16U;
        case TYPE_I32: case TYPE_U32: return 32U;
        case TYPE_I64: case TYPE_U64:
        case TYPE_ISIZE: case TYPE_USIZE: return 64U;
        default: return 0U;
    }
}

bool coerce_literal(Checker *checker, Expr *expr, Type *expected) {
    if (expr->kind == EXPR_INT && is_integer(expected)) {
        uint64_t value = expr->as.integer;
        unsigned width = integer_width(expected);
        uint64_t maximum;
        if (is_signed_integer(expected))
            maximum = width == 64U ? (uint64_t)INT64_MAX
                                   : (UINT64_C(1) << (width - 1U)) - 1U;
        else
            maximum = width == 64U ? UINT64_MAX
                                   : (UINT64_C(1) << width) - 1U;
        if (value > maximum) {
            lang_diag(checker->diagnostics, expr->span,
                      "integer literal does not fit `%s`", expected->name);
            expr->type = expected;
            return true;
        }
        expr->type = expected;
        return true;
    }
    if (expr->kind == EXPR_FLOAT && is_float(expected)) {
        expr->type = expected;
        return true;
    }
    if (expr->kind == EXPR_UNARY && expr->as.unary.op == TOK_MINUS &&
        expr->as.unary.operand->kind == EXPR_INT &&
        is_signed_integer(expected)) {
        uint64_t magnitude =
            expr->as.unary.operand->as.integer;
        unsigned width = integer_width(expected);
        uint64_t maximum_magnitude =
            width == 64U ? UINT64_C(1) << 63U
                         : UINT64_C(1) << (width - 1U);
        if (magnitude > maximum_magnitude) {
            lang_diag(checker->diagnostics, expr->span,
                      "integer literal does not fit `%s`", expected->name);
            return false;
        }
        expr->as.unary.operand->type = expected;
        expr->type = expected;
        return true;
    }
    if (expr->kind == EXPR_ARRAY && expected->kind == TYPE_ARRAY &&
        expr->as.array.count == expected->array_length) {
        bool ok = true;
        for (size_t i = 0U; i < expr->as.array.count; ++i)
            if (!coerce_literal(checker, expr->as.array.items[i],
                                expected->element) &&
                !same_type(expr->as.array.items[i]->type, expected->element))
                ok = false;
        if (ok) expr->type = expected;
        return ok;
    }
    if (expected->kind == TYPE_OPTION && expr->type != NULL &&
        same_type(expr->type, expected->element)) {
        Expr *value = lang_arena_alloc(
            &checker->module->arena, sizeof(*value));
        *value = *expr;
        Expr *callee = lang_arena_alloc(
            &checker->module->arena, sizeof(*callee));
        memset(callee, 0, sizeof(*callee));
        callee->kind = EXPR_NAME;
        callee->span = expr->span;
        callee->as.name = "Option::Some";
        memset(expr, 0, sizeof(*expr));
        expr->kind = EXPR_CALL;
        expr->span = value->span;
        expr->as.call.callee = callee;
        expr->as.call.arguments.items = lang_arena_alloc(
            &checker->module->arena, sizeof(Expr *));
        expr->as.call.arguments.items[0] = value;
        expr->as.call.arguments.count = 1U;
        Type *previous_expected = checker->expected_type;
        checker->expected_type = expected;
        Type *wrapped = check_expr(checker, expr);
        checker->expected_type = previous_expected;
        return same_type(wrapped, expected);
    }
    if (expected->kind == TYPE_READONLY_SPAN && expr->type != NULL &&
        expr->type->kind == TYPE_SLICE &&
        same_type(expected->element, expr->type->element)) {
        return true;
    }
    return false;
}

const Decl *type_copy_constructor(const Type *type) {
    if (type == NULL || type->declaration == NULL ||
        type->declaration->kind != DECL_STRUCT)
        return NULL;
    const Decl *declaration = type->declaration;
    for (size_t member = 0U;
         member < declaration->as.structure.member_count; ++member) {
        const Decl *candidate = declaration->as.structure.members[member];
        if (candidate->kind == DECL_FUNCTION &&
            candidate->as.function.is_copy_constructor)
            return candidate;
    }
    return NULL;
}

static bool type_is_copyable_inner(Checker *checker, Type *type,
                                   const Type **seen, size_t seen_count,
                                   bool allow_custom_copy) {
    if (type->kind == TYPE_ARENA)
        return false;
    if (type->kind == TYPE_ARRAY)
        return type_is_copyable_inner(
            checker, type->element, seen, seen_count,
            allow_custom_copy);
    if (type->kind == TYPE_OPTION)
        return type_is_copyable_inner(
            checker, type->element, seen, seen_count,
            allow_custom_copy);
    if (type->kind == TYPE_VEC || type->kind == TYPE_QUEUE ||
        type->kind == TYPE_STACK)
        return type_is_copyable_inner(
            checker, type->element, seen, seen_count,
            allow_custom_copy);
    if (type->kind == TYPE_DICTIONARY)
        return type_is_copyable_inner(
                   checker, type->element, seen, seen_count,
                   allow_custom_copy) &&
               type_is_copyable_inner(
                   checker, type->error_type, seen, seen_count,
                   allow_custom_copy);
    if (type->kind == TYPE_HASH_SET)
        return type_is_copyable_inner(
            checker, type->element, seen, seen_count,
            allow_custom_copy);
    if (type->kind == TYPE_RESULT)
        return type_is_copyable_inner(
                   checker, type->element, seen, seen_count,
                   allow_custom_copy) &&
               type_is_copyable_inner(
                   checker, type->error_type, seen, seen_count,
                   allow_custom_copy);
    if (type->kind == TYPE_CLASS) return true;
    if (type->kind != TYPE_NAMED) return true;
    const Decl *type_decl = type->declaration;
    if (type_decl == NULL) return false;
    const Decl *copy = type_copy_constructor(type);
    if (copy != NULL)
        return allow_custom_copy && !copy->as.function.is_deleted;
    for (size_t i = 0U; i < seen_count; ++i)
        if (same_type(seen[i], type)) return false;
    if (seen_count >= 64U) return false;
    const Type *next_seen[64];
    for (size_t i = 0U; i < seen_count; ++i) next_seen[i] = seen[i];
    next_seen[seen_count++] = type;
    FieldDecl *fields = NULL;
    size_t field_count = 0U;
    if (type_decl->kind == DECL_STRUCT) {
        fields = type_decl->as.structure.fields;
        field_count = type_decl->as.structure.field_count;
    } else if (type_decl->kind == DECL_ENUM) {
        fields = type_decl->as.enumeration.variants;
        field_count = type_decl->as.enumeration.variant_count;
    }
    if (fields == NULL) return false;
    bool copyable = true;
    bool allow_member_custom = allow_custom_copy &&
        (type_decl->kind == DECL_STRUCT ||
         (type_decl->kind == DECL_ENUM &&
          type_decl->as.enumeration.is_union));
    for (size_t field = 0U; field < field_count; ++field) {
        Type *field_type = resolve_type_syntax_in_applied_declaration(
            checker, type, fields[field].type_syntax,
            fields[field].type_name,
            fields[field].span);
        if (!type_is_copyable_inner(
                checker, field_type, next_seen, seen_count,
                allow_member_custom)) {
            copyable = false;
            break;
        }
    }
    return copyable;
}

bool type_is_copyable(Checker *checker, Type *type) {
    return type_is_copyable_inner(checker, type, NULL, 0U, true);
}

static bool type_uses_custom_copy_inner(
    Checker *checker, Type *type, const Type **seen, size_t seen_count
) {
    if (type == NULL) return false;
    if (type_copy_constructor(type) != NULL) return true;
    if (type->kind == TYPE_ARRAY || type->kind == TYPE_OPTION)
        return type_uses_custom_copy_inner(
            checker, type->element, seen, seen_count);
    if (type->kind == TYPE_VEC || type->kind == TYPE_QUEUE ||
        type->kind == TYPE_STACK)
        return type_uses_custom_copy_inner(
            checker, type->element, seen, seen_count);
    if (type->kind == TYPE_RESULT)
        return type_uses_custom_copy_inner(
                   checker, type->element, seen, seen_count) ||
               type_uses_custom_copy_inner(
                   checker, type->error_type, seen, seen_count);
    if (type->kind == TYPE_DICTIONARY)
        return type_uses_custom_copy_inner(
                   checker, type->element, seen, seen_count) ||
               type_uses_custom_copy_inner(
                   checker, type->error_type, seen, seen_count);
    if (type->kind == TYPE_HASH_SET)
        return type_uses_custom_copy_inner(
            checker, type->element, seen, seen_count);
    if (type->kind != TYPE_NAMED || type->declaration == NULL ||
        (type->declaration->kind != DECL_STRUCT &&
         (type->declaration->kind != DECL_ENUM ||
          !type->declaration->as.enumeration.is_union)))
        return false;
    for (size_t i = 0U; i < seen_count; ++i)
        if (same_type(seen[i], type)) return false;
    if (seen_count >= 64U) return false;
    const Type *next_seen[64];
    for (size_t i = 0U; i < seen_count; ++i) next_seen[i] = seen[i];
    next_seen[seen_count++] = type;
    FieldDecl *members = type->declaration->kind == DECL_STRUCT
        ? type->declaration->as.structure.fields
        : type->declaration->as.enumeration.variants;
    size_t member_count = type->declaration->kind == DECL_STRUCT
        ? type->declaration->as.structure.field_count
        : type->declaration->as.enumeration.variant_count;
    for (size_t member = 0U; member < member_count; ++member) {
        Type *member_type = resolve_type_syntax_in_applied_declaration(
            checker, type, members[member].type_syntax,
            members[member].type_name, members[member].span);
        if (type_uses_custom_copy_inner(
                checker, member_type, next_seen, seen_count))
            return true;
    }
    return false;
}

bool type_uses_custom_copy(Checker *checker, Type *type) {
    return type_uses_custom_copy_inner(checker, type, NULL, 0U);
}

bool checker_require_copyable(
    Checker *checker, Type *type, LangSpan copy_span
) {
    if (type_is_copyable(checker, type)) return true;
    const Decl *copy_constructor = type_copy_constructor(type);
    if (copy_constructor != NULL &&
        copy_constructor->as.function.is_deleted) {
        LangDiagnostic *diagnostic = lang_diag(
            checker->diagnostics, copy_span,
            "copy constructor for `%s` is deleted", type->name);
        lang_diag_secondary(
            diagnostic, copy_constructor->span,
            "copy constructor deleted here");
    } else {
        lang_diag(
            checker->diagnostics, copy_span,
            "type `%s` is not copyable", type->name);
    }
    return false;
}
