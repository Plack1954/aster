#include "checker_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void dump_expr_types(const Expr *expr, int depth);
static void dump_stmt_types(const Stmt *stmt, int depth) {
    if (stmt->kind == STMT_BLOCK)
        for (size_t i = 0U; i < stmt->as.block.count; ++i)
            dump_stmt_types(stmt->as.block.items[i], depth);
    else if (stmt->kind == STMT_LET) dump_expr_types(stmt->as.let.value, depth);
    else if (stmt->kind == STMT_EXPR) dump_expr_types(stmt->as.expression, depth);
    else if (stmt->kind == STMT_RETURN && stmt->as.return_value)
        dump_expr_types(stmt->as.return_value, depth);
}
static void dump_expr_types(const Expr *expr, int depth) {
    for (int i = 0; i < depth; ++i) fputs("  ", stdout);
    printf("%zu..%zu : %s\n", expr->span.start, expr->span.end,
           expr->type != NULL ? expr->type->name : "<unchecked>");
}
void lang_dump_types(const Module *module) {
    for (size_t i = 0U; i < module->count; ++i)
        if (module->decls[i]->kind == DECL_FUNCTION) {
            const Decl *decl = module->decls[i];
            const Function *function = &decl->as.function;
            bool is_main = decl->type_param_count == 0U &&
                !function->is_extern &&
                strcmp(function->name, "main") == 0;
            const char *visibility =
                function->is_drop || is_main
                    ? ""
                    : decl->is_public ? "public " : "private ";
            if (module->decls[i]->type_param_count != 0U) {
                printf("%s%s %s<template>\n", visibility,
                       module->decls[i]->as.function.return_type,
                       module->decls[i]->as.function.name);
                continue;
            }
            printf("%s%s %s\n", visibility,
                   module->decls[i]->as.function.return_type,
                   module->decls[i]->as.function.name);
            if (module->decls[i]->as.function.body != NULL)
                dump_stmt_types(module->decls[i]->as.function.body, 1);
        }
}

typedef struct ComputedLayout {
    size_t size;
    size_t alignment;
    bool valid;
} ComputedLayout;

static bool layout_align(size_t value, size_t alignment,
                         size_t *out_value) {
    if (alignment == 0U) return false;
    size_t remainder = value % alignment;
    size_t extra = remainder == 0U ? 0U : alignment - remainder;
    if (value > SIZE_MAX - extra) return false;
    *out_value = value + extra;
    return true;
}

static ComputedLayout type_layout(
    Checker *checker, Type *type, const LangTargetInfo *target,
    const Type **stack, size_t stack_count);

static ComputedLayout tagged_layout(ComputedLayout payload,
                                    const LangTargetInfo *target) {
    if (!payload.valid) return payload;
    size_t offset;
    if (!layout_align((size_t)target->enum_tag_size,
                      payload.alignment, &offset) ||
        payload.size > SIZE_MAX - offset)
        return (ComputedLayout){0U, 0U, false};
    size_t alignment = payload.alignment >
                       (size_t)target->enum_tag_alignment
                     ? payload.alignment
                     : (size_t)target->enum_tag_alignment;
    size_t size;
    if (!layout_align(offset + payload.size, alignment, &size))
        return (ComputedLayout){0U, 0U, false};
    return (ComputedLayout){size, alignment, true};
}

static ComputedLayout declaration_layout(
    Checker *checker, Type *applied, const LangTargetInfo *target,
    const Type **stack, size_t stack_count) {
    const Decl *decl = applied->declaration;
    for (size_t i = 0U; i < stack_count; ++i)
        if (same_type(stack[i], applied))
            return (ComputedLayout){0U, 0U, false};
    if (stack_count >= 64U)
        return (ComputedLayout){0U, 0U, false};
    const Type *next_stack[64];
    for (size_t i = 0U; i < stack_count; ++i)
        next_stack[i] = stack[i];
    next_stack[stack_count++] = applied;
    const char *previous_module = checker->current_module;
    checker->current_module = decl->module_name;
    if (decl->kind == DECL_ALIAS) {
        Type *target_type = resolve_declared_type(
            checker, decl->as.alias.target_syntax,
            decl->as.alias.target, decl->span);
        ComputedLayout result = type_layout(
            checker, target_type, target, next_stack, stack_count);
        checker->current_module = previous_module;
        return result;
    }
    FieldDecl *fields = decl->kind == DECL_STRUCT
                      ? decl->as.structure.fields
                      : decl->as.enumeration.variants;
    size_t field_count = decl->kind == DECL_STRUCT
                       ? decl->as.structure.field_count
                       : decl->as.enumeration.variant_count;
    size_t aggregate_size = 0U;
    size_t aggregate_alignment = 1U;
    if (decl->kind == DECL_STRUCT) {
        for (size_t i = 0U; i < field_count; ++i) {
            Type *field_type = resolve_type_syntax_in_applied_declaration(
                checker, applied, fields[i].type_syntax,
                fields[i].type_name,
                fields[i].span);
            ComputedLayout field = type_layout(
                checker, field_type, target, next_stack, stack_count);
            if (!field.valid ||
                !layout_align(aggregate_size, field.alignment,
                              &aggregate_size) ||
                field.size > SIZE_MAX - aggregate_size) {
                checker->current_module = previous_module;
                return (ComputedLayout){0U, 0U, false};
            }
            aggregate_size += field.size;
            if (field.alignment > aggregate_alignment)
                aggregate_alignment = field.alignment;
        }
        bool valid = layout_align(
            aggregate_size, aggregate_alignment, &aggregate_size);
        checker->current_module = previous_module;
        return (ComputedLayout){
            aggregate_size, aggregate_alignment, valid
        };
    }
    for (size_t i = 0U; i < field_count; ++i) {
        Type *payload_type = resolve_type_syntax_in_applied_declaration(
            checker, applied, fields[i].type_syntax,
            fields[i].type_name,
            fields[i].span);
        ComputedLayout payload = type_layout(
            checker, payload_type, target, next_stack, stack_count);
        if (!payload.valid) {
            checker->current_module = previous_module;
            return payload;
        }
        if (payload.size > aggregate_size) aggregate_size = payload.size;
        if (payload.alignment > aggregate_alignment)
            aggregate_alignment = payload.alignment;
    }
    checker->current_module = previous_module;
    return tagged_layout(
        (ComputedLayout){
            aggregate_size, aggregate_alignment, true
        }, target);
}

static ComputedLayout type_layout(
    Checker *checker, Type *type, const LangTargetInfo *target,
    const Type **stack, size_t stack_count) {
    if (type == NULL) return (ComputedLayout){0U, 0U, false};
    switch (type->kind) {
        case TYPE_UNIT:
            return (ComputedLayout){0U, 1U, true};
        case TYPE_BOOL: case TYPE_I8: case TYPE_U8:
            return (ComputedLayout){1U, 1U, true};
        case TYPE_I16: case TYPE_U16:
            return (ComputedLayout){2U, 2U, true};
        case TYPE_I32: case TYPE_U32: case TYPE_F32: case TYPE_CHAR:
            return (ComputedLayout){4U, 4U, true};
        case TYPE_I64: case TYPE_U64: case TYPE_F64:
            return (ComputedLayout){8U, 8U, true};
        case TYPE_ISIZE: case TYPE_USIZE:
        case TYPE_RAW_POINTER:
        case TYPE_FUNCTION:
        case TYPE_STRING: case TYPE_STRING_BUILDER: case TYPE_URL:
        case TYPE_HTML: case TYPE_BUFFER: case TYPE_ARENA:
        case TYPE_NATIVE_HANDLE:
        case TYPE_CANCELLATION_TOKEN:
        case TYPE_CANCELLATION_TOKEN_SOURCE:
        case TYPE_TASK:
            return (ComputedLayout){
                target->pointer_size, target->pointer_alignment, true
            };
        case TYPE_STR: case TYPE_SLICE: case TYPE_READONLY_SPAN:
            return (ComputedLayout){
                (size_t)target->pointer_size * 2U,
                target->pointer_alignment, true
            };
        case TYPE_VEC:
        case TYPE_DICTIONARY:
        case TYPE_HASH_SET:
        case TYPE_QUEUE:
        case TYPE_STACK:
            return (ComputedLayout){
                target->pointer_size, target->pointer_alignment, true
            };
        case TYPE_ARRAY: {
            ComputedLayout element = type_layout(
                checker, type->element, target, stack, stack_count);
            if (!element.valid ||
                (type->array_length != 0U &&
                 element.size > SIZE_MAX / type->array_length))
                return (ComputedLayout){0U, 0U, false};
            return (ComputedLayout){
                element.size * type->array_length,
                element.alignment, true
            };
        }
        case TYPE_OPTION:
            return tagged_layout(
                type_layout(checker, type->element, target,
                            stack, stack_count),
                target);
        case TYPE_RESULT: {
            ComputedLayout success = type_layout(
                checker, type->element, target, stack, stack_count);
            ComputedLayout error = type_layout(
                checker, type->error_type, target, stack, stack_count);
            if (!success.valid || !error.valid)
                return (ComputedLayout){0U, 0U, false};
            return tagged_layout(
                (ComputedLayout){
                    success.size >= error.size
                        ? success.size : error.size,
                    success.alignment >= error.alignment
                        ? success.alignment : error.alignment,
                    true
                }, target);
        }
        case TYPE_NAMED:
            if (type->declaration == NULL)
                return (ComputedLayout){0U, 0U, false};
            return declaration_layout(
                checker, type, target, stack, stack_count);
        case TYPE_ERROR: case TYPE_NEVER:
            return (ComputedLayout){0U, 0U, false};
    }
    return (ComputedLayout){0U, 0U, false};
}

bool lang_checker_resolve_type_layout(
    Module *module, LangDiagnostics *diagnostics,
    const Type *type, const LangTargetInfo *target,
    size_t *out_size, size_t *out_alignment) {
    if (module == NULL || type == NULL || target == NULL ||
        out_size == NULL || out_alignment == NULL)
        return false;
    Checker checker;
    memset(&checker, 0, sizeof(checker));
    checker.module = module;
    checker.diagnostics = diagnostics;
    if (type->declaration != NULL)
        checker.current_module =
            type->declaration->module_name;
    ComputedLayout layout = type_layout(
        &checker, (Type *)type, target, NULL, 0U);
    if (!layout.valid) return false;
    *out_size = layout.size;
    *out_alignment = layout.alignment;
    return true;
}

static void dump_named_type_layout(
    Checker *checker, Type *type, const LangTargetInfo *target) {
    const Decl *decl = type->declaration;
    ComputedLayout layout = type_layout(
        checker, type, target, NULL, 0U);
    printf("type %s::%s size=",
           decl->module_name, type->name);
    if (layout.valid)
        printf("%zu align=%zu%s\n", layout.size, layout.alignment,
               decl->kind == DECL_STRUCT &&
               decl->as.structure.is_extern
                   ? " abi=c" : "");
    else
        puts("<invalid> align=<invalid>");
    if (layout.valid && decl->kind == DECL_STRUCT) {
        size_t offset = 0U;
        for (size_t field = 0U;
             field < decl->as.structure.field_count; ++field) {
            FieldDecl *field_decl =
                &decl->as.structure.fields[field];
            Type *field_type = resolve_type_syntax_in_applied_declaration(
                checker, type, field_decl->type_syntax,
                field_decl->type_name,
                field_decl->span);
            ComputedLayout field_layout = type_layout(
                checker, field_type, target, NULL, 0U);
            if (!field_layout.valid ||
                !layout_align(offset, field_layout.alignment,
                              &offset))
                break;
            printf("  field %s offset=%zu size=%zu align=%zu\n",
                   field_decl->name, offset,
                   field_layout.size, field_layout.alignment);
            offset += field_layout.size;
        }
    } else if (layout.valid && decl->kind == DECL_ENUM) {
        size_t payload_offset = 0U;
        size_t payload_alignment =
            layout.alignment >
            (size_t)target->enum_tag_alignment
            ? layout.alignment
            : (size_t)target->enum_tag_alignment;
        (void)layout_align(
            target->enum_tag_size, payload_alignment,
            &payload_offset);
        printf("  tag offset=0 size=%u align=%u\n"
               "  payload offset=%zu\n",
               (unsigned)target->enum_tag_size,
               (unsigned)target->enum_tag_alignment,
               payload_offset);
    }
}

void lang_dump_layout(Module *module, const LangTargetInfo *target) {
    if (module == NULL || target == NULL) return;
    printf("target pointer=%u align=%u endian=%s enum-tag=%u/%u c-abi=%s\n",
           (unsigned)target->pointer_size,
           (unsigned)target->pointer_alignment,
           target->endianness == LANG_ENDIAN_LITTLE ? "little" : "big",
           (unsigned)target->enum_tag_size,
           (unsigned)target->enum_tag_alignment,
           target->c_abi_supported ? "supported" : "unsupported");
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Checker checker;
    memset(&checker, 0, sizeof(checker));
    checker.module = module;
    checker.diagnostics = &diagnostics;
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *decl = module->decls[i];
        const char *name = type_declaration_name(decl);
        if (name == NULL) continue;
        if (decl->type_param_count != 0U) {
            printf("type %s::%s<", decl->module_name, name);
            for (size_t parameter = 0U;
                 parameter < decl->type_param_count; ++parameter)
                printf("%s%s", parameter == 0U ? "" : ",",
                       decl->type_params[parameter]);
            puts("> template");
            continue;
        }
        checker.current_module = decl->module_name;
        Type *type = resolve_type(&checker, name, decl->span);
        dump_named_type_layout(&checker, type, target);
    }
    for (size_t i = 0U; i < module->type_instantiation_count; ++i) {
        Type *type = module->type_instantiations[i];
        checker.current_module = type->declaration->module_name;
        dump_named_type_layout(&checker, type, target);
    }
    lang_diagnostics_free(&diagnostics);
}
