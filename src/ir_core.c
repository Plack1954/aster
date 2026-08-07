#include "internal.h"
#include "ir_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const Type ir_bool_type = {
    .kind = TYPE_BOOL,
    .name = "bool"
};
const Type ir_str_type = {
    .kind = TYPE_STR,
    .name = "string"
};
const Type ir_unit_type = {
    .kind = TYPE_UNIT,
    .name = "unit"
};
const Type ir_usize_type = {
    .kind = TYPE_USIZE,
    .name = "nuint"
};
const Type ir_string_type = {
    .kind = TYPE_STRING,
    .name = "string",
    .managed = true
};

bool ir_style_name(const char *name) {
    const char *part = name;
    for (const char *cursor = strstr(name, "::"); cursor != NULL;
         cursor = strstr(cursor + 2U, "::"))
        part = cursor + 2U;
    return strcmp(part, "style") == 0;
}
const Type ir_string_builder_type = {
    .kind = TYPE_STRING_BUILDER,
    .name = "StringBuilder",
    .requires_cleanup = true
};

void *ir_resize(void *pointer, size_t count, size_t size) {
    if (size != 0U && count > SIZE_MAX / size) {
        fputs("fatal: IR allocation is too large\n", stderr);
        exit(2);
    }
    void *result = realloc(pointer, count * size);
    if (result == NULL && count != 0U) {
        fputs("fatal: out of memory while building IR\n", stderr);
        exit(2);
    }
    return result;
}

static IrTypeShape type_shape(const Type *type) {
    if (type == NULL || type->kind == TYPE_ERROR) return IR_TYPE_ERROR;
    switch (type->kind) {
        case TYPE_UNIT: return IR_TYPE_UNIT;
        case TYPE_NEVER: return IR_TYPE_NEVER;
        case TYPE_BOOL: return IR_TYPE_BOOL;
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
        case TYPE_ISIZE:
            return IR_TYPE_SIGNED_INT;
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
        case TYPE_USIZE:
            return IR_TYPE_UNSIGNED_INT;
        case TYPE_F32: case TYPE_F64:
            return IR_TYPE_FLOAT;
        case TYPE_CHAR: return IR_TYPE_CHAR;
        case TYPE_STR: return IR_TYPE_STRING_VIEW;
        case TYPE_ARRAY: return IR_TYPE_ARRAY;
        case TYPE_RAW_POINTER: return IR_TYPE_RAW_POINTER;
        case TYPE_SLICE: case TYPE_READONLY_SPAN: return IR_TYPE_SLICE;
        case TYPE_FUNCTION: return IR_TYPE_FUNCTION;
        case TYPE_CLASS: return IR_TYPE_CLASS_REFERENCE;
        case TYPE_OPTION: case TYPE_RESULT:
            return IR_TYPE_UNION;
        case TYPE_TASK:
        case TYPE_CANCELLATION_TOKEN:
        case TYPE_CANCELLATION_TOKEN_SOURCE:
            return IR_TYPE_BUILTIN_OBJECT;
        case TYPE_NAMED:
            if (type->declaration != NULL &&
                type->declaration->kind == DECL_ENUM)
                return type->declaration->as.enumeration.is_union
                    ? IR_TYPE_UNION : IR_TYPE_ENUM;
            return IR_TYPE_STRUCT;
        default:
            return IR_TYPE_BUILTIN_OBJECT;
    }
}

static uint8_t type_bit_width(const Type *type) {
    if (type == NULL) return 0U;
    switch (type->kind) {
        case TYPE_I8: case TYPE_U8: return 8U;
        case TYPE_I16: case TYPE_U16: return 16U;
        case TYPE_I32: case TYPE_U32: case TYPE_F32:
        case TYPE_CHAR:
            return 32U;
        case TYPE_I64: case TYPE_U64: case TYPE_F64:
            return 64U;
        default:
            return 0U;
    }
}

static IrCopyPolicy type_copy_policy(const Type *type) {
    if (type != NULL && type->kind == TYPE_NAMED) {
        const Decl *copy = type_copy_constructor(type);
        if (copy != NULL)
            return copy->as.function.is_deleted
                ? IR_COPY_NONCOPYABLE : IR_COPY_CUSTOM;
    }
    if (type == NULL || (!type->requires_cleanup && !type->managed))
        return IR_COPY_TRIVIAL;
    if (type->kind == TYPE_ARENA)
        return IR_COPY_NONCOPYABLE;
    switch (type->kind) {
        case TYPE_STRING:
        case TYPE_NATIVE_HANDLE:
        case TYPE_TASK:
        case TYPE_CANCELLATION_TOKEN:
        case TYPE_CANCELLATION_TOKEN_SOURCE:
            return IR_COPY_SHARED_RETAIN;
        case TYPE_ARRAY:
        case TYPE_OPTION:
        case TYPE_RESULT:
        case TYPE_VEC:
        case TYPE_DICTIONARY:
        case TYPE_HASH_SET:
        case TYPE_QUEUE:
        case TYPE_STACK:
        case TYPE_NAMED:
            return IR_COPY_DEEP;
        default:
            return IR_COPY_CUSTOM;
    }
}

static IrDropPolicy type_drop_policy(const Type *type) {
    if (type == NULL || (!type->requires_cleanup && !type->managed))
        return IR_DROP_TRIVIAL;
    switch (type->kind) {
        case TYPE_ARRAY:
        case TYPE_OPTION:
        case TYPE_RESULT:
        case TYPE_NAMED:
            return IR_DROP_RECURSIVE;
        default:
            return IR_DROP_CUSTOM;
    }
}

static bool ir_align_up(size_t value, size_t alignment, size_t *result) {
    if (alignment == 0U) return false;
    size_t remainder = value % alignment;
    size_t padding = remainder == 0U ? 0U : alignment - remainder;
    if (value > SIZE_MAX - padding) return false;
    *result = value + padding;
    return true;
}

static void resolve_struct_member_layout(IrModule *module, IrTypeId id) {
    IrType *type = &module->types[id];
    size_t offset = type->shape == IR_TYPE_CLASS_REFERENCE
        ? module->target.enum_tag_size : 0U;
    bool known = true;
    for (size_t field = 0U; field < type->field_count; ++field) {
        IrTypeId field_id = type->field_types[field];
        if (field_id >= module->type_count ||
            !module->types[field_id].target_layout_known ||
            !ir_align_up(offset, module->types[field_id].target_alignment,
                         &offset)) {
            known = false;
            break;
        }
        type = &module->types[id];
        type->field_offsets[field] = offset;
        if (module->types[field_id].target_size > SIZE_MAX - offset) {
            known = false;
            break;
        }
        offset += module->types[field_id].target_size;
    }
    type = &module->types[id];
    type->member_layout_known = known;
    if (type->shape == IR_TYPE_CLASS_REFERENCE) {
        size_t alignment = module->target.enum_tag_alignment;
        if (known) {
            for (size_t field = 0U; field < type->field_count; ++field) {
                IrTypeId field_id = type->field_types[field];
                if (module->types[field_id].target_alignment > alignment)
                    alignment = module->types[field_id].target_alignment;
            }
            known = ir_align_up(offset, alignment, &offset);
        }
        type->object_layout_known = known;
        type->object_size = known ? offset : 0U;
        type->object_alignment = known ? alignment : 0U;
    }
}

static void resolve_variant_member_layout(IrModule *module, IrTypeId id) {
    IrType *type = &module->types[id];
    size_t payload_alignment = 1U;
    bool known = true;
    for (size_t variant = 0U; variant < type->variant_count; ++variant) {
        IrTypeId payload = type->variant_payload_types[variant];
        if (payload == IR_INVALID_ID) continue;
        if (payload >= module->type_count ||
            !module->types[payload].target_layout_known) {
            known = false;
            break;
        }
        if (module->types[payload].target_alignment > payload_alignment)
            payload_alignment = module->types[payload].target_alignment;
    }
    size_t payload_offset = 0U;
    if (known)
        known = ir_align_up(module->target.enum_tag_size,
                            payload_alignment, &payload_offset);
    if (known)
        for (size_t variant = 0U; variant < type->variant_count; ++variant)
            type->variant_payload_offsets[variant] = payload_offset;
    type->member_layout_known = known;
}

bool same_ir_type_identity(const Type *left, const Type *right) {
    if (left == right) return true;
    if (left == NULL || right == NULL)
        return false;
    bool span_pair =
        (left->kind == TYPE_SLICE || left->kind == TYPE_READONLY_SPAN) &&
        (right->kind == TYPE_SLICE || right->kind == TYPE_READONLY_SPAN);
    if (left->kind != right->kind && !span_pair) return false;
    if (left->kind == TYPE_NAMED || left->kind == TYPE_CLASS) {
        if (left->declaration != right->declaration ||
            left->argument_count != right->argument_count)
            return false;
        for (size_t i = 0U; i < left->argument_count; ++i)
            if (!same_ir_type_identity(
                    left->arguments[i], right->arguments[i]))
                return false;
        return true;
    }
    if (left->kind == TYPE_ARRAY &&
        left->array_length != right->array_length)
        return false;
    if (left->kind == TYPE_TASK)
        return same_ir_type_identity(left->element, right->element);
    if (left->kind == TYPE_RAW_POINTER &&
        left->pointer_mutable != right->pointer_mutable)
        return false;
    if (!same_ir_type_identity(left->element, right->element))
        return false;
    if (!same_ir_type_identity(left->error_type, right->error_type))
        return false;
    if (left->argument_count != right->argument_count)
        return false;
    for (size_t i = 0U; i < left->argument_count; ++i)
        if ((left->kind == TYPE_FUNCTION &&
             left->parameter_modes[i] != right->parameter_modes[i]) ||
            !same_ir_type_identity(
                left->arguments[i], right->arguments[i]))
            return false;
    return true;
}

IrTypeId ir_intern_type(IrModule *module, const Type *type) {
    for (size_t i = 0U; i < module->type_count; ++i)
        if (same_ir_type_identity(
                module->types[i].checked_type, type))
            return (IrTypeId)i;
    if (module->type_count == module->type_capacity) {
        size_t next = module->type_capacity == 0U
                    ? 16U : module->type_capacity * 2U;
        module->types = ir_resize(
            module->types, next, sizeof(*module->types));
        module->type_capacity = next;
    }
    if (module->type_count >= (size_t)IR_INVALID_ID) {
        fputs("fatal: too many IR types\n", stderr);
        exit(2);
    }
    IrType *entry = &module->types[module->type_count];
    memset(entry, 0, sizeof(*entry));
    entry->name = type != NULL && type->name != NULL
                ? type->name : "<unknown>";
    entry->checked_type = type;
    entry->module_name =
        type != NULL && type->declaration != NULL
        ? type->declaration->module_name : NULL;
    entry->shape = type_shape(type);
    entry->element_type = IR_INVALID_ID;
    entry->error_type = IR_INVALID_ID;
    entry->base_type = IR_INVALID_ID;
    entry->copy_function = IR_INVALID_ID;
    entry->destructor_function = IR_INVALID_ID;
    entry->copy_policy = type_copy_policy(type);
    entry->drop_policy = type_drop_policy(type);
    entry->array_length =
        type != NULL ? type->array_length : 0U;
    entry->bit_width = type_bit_width(type);
    entry->pointer_mutable =
        type != NULL && type->kind == TYPE_RAW_POINTER &&
        type->pointer_mutable;
    entry->requires_cleanup = type != NULL && type->requires_cleanup;
    entry->managed = type != NULL && type->managed;
    if (type != NULL && module->lowering_module != NULL)
        entry->target_layout_known =
            lang_checker_resolve_type_layout(
                module->lowering_module,
                module->lowering_diagnostics,
                type, &module->target,
                &entry->target_size,
                &entry->target_alignment);
    entry->element_child_collection =
        type != NULL &&
        (type->kind == TYPE_ARRAY || type->kind == TYPE_OPTION ||
         type->kind == TYPE_VEC);
    IrTypeId id = (IrTypeId)module->type_count++;
    if (type != NULL && type->kind == TYPE_CLASS &&
        type->declaration != NULL &&
        type->declaration->as.structure.base_class != NULL) {
        Type *base = lang_arena_alloc(
            &module->lowering_module->arena, sizeof(*base));
        memset(base, 0, sizeof(*base));
        base->kind = TYPE_CLASS;
        base->name = type->declaration->as.structure
            .base_class->as.structure.name;
        base->declaration =
            type->declaration->as.structure.base_class;
        module->types[id].base_type = ir_intern_type(module, base);
    }
    if (type != NULL && type->kind == TYPE_CLASS &&
        type->declaration != NULL &&
        type->declaration->as.structure.interface_count != 0U) {
        size_t count = type->declaration->as.structure.interface_count;
        module->types[id].interface_types = ir_resize(
            NULL, count, sizeof(*module->types[id].interface_types));
        module->types[id].interface_count = count;
        for (size_t interface = 0U; interface < count; ++interface) {
            const Decl *declaration = type->declaration->as.structure
                .interfaces[interface];
            Type *interface_type = lang_arena_alloc(
                &module->lowering_module->arena,
                sizeof(*interface_type));
            memset(interface_type, 0, sizeof(*interface_type));
            interface_type->kind = TYPE_CLASS;
            interface_type->name = declaration->as.structure.name;
            interface_type->declaration = declaration;
            module->types[id].interface_types[interface] =
                ir_intern_type(module, interface_type);
        }
    }
    if (type != NULL && type->element != NULL) {
        IrTypeId element = ir_intern_type(module, type->element);
        module->types[id].element_type = element;
    }
    if (type != NULL && type->error_type != NULL) {
        IrTypeId error = ir_intern_type(module, type->error_type);
        module->types[id].error_type = error;
    }
    if (type != NULL && type->argument_count != 0U) {
        module->types[id].argument_types = ir_resize(
            NULL, type->argument_count,
            sizeof(*module->types[id].argument_types));
        module->types[id].argument_count = type->argument_count;
        if (type->kind == TYPE_FUNCTION) {
            module->types[id].parameter_modes = ir_resize(
                NULL, type->argument_count,
                sizeof(*module->types[id].parameter_modes));
            memcpy(module->types[id].parameter_modes,
                   type->parameter_modes,
                   type->argument_count *
                       sizeof(*module->types[id].parameter_modes));
        }
        for (size_t i = 0U; i < type->argument_count; ++i) {
            IrTypeId argument =
                ir_intern_type(module, type->arguments[i]);
            module->types[id].argument_types[i] = argument;
        }
    }
    if (type != NULL &&
        (type->kind == TYPE_NAMED || type->kind == TYPE_CLASS) &&
        type->declaration != NULL &&
        (type->declaration->kind == DECL_STRUCT ||
         type->declaration->kind == DECL_CLASS)) {
        size_t count =
            type->declaration->as.structure.field_count;
        module->types[id].field_names = ir_resize(
            NULL, count, sizeof(*module->types[id].field_names));
        module->types[id].field_types = ir_resize(
            NULL, count, sizeof(*module->types[id].field_types));
        module->types[id].field_spans = ir_resize(
            NULL, count, sizeof(*module->types[id].field_spans));
        module->types[id].field_offsets = ir_resize(
            NULL, count, sizeof(*module->types[id].field_offsets));
        module->types[id].field_count = count;
        for (size_t i = 0U; i < count; ++i) {
            module->types[id].field_names[i] =
                type->declaration->as.structure.fields[i].name;
            module->types[id].field_spans[i] =
                type->declaration->as.structure.fields[i].span;
            Type *field_type = lang_checker_resolve_aggregate_member(
                module->lowering_module, module->lowering_diagnostics,
                type, i);
            module->types[id].field_types[i] =
                field_type != NULL
                ? ir_intern_type(module, field_type) : IR_INVALID_ID;
        }
        resolve_struct_member_layout(module, id);
    }
    if (type != NULL && type->kind == TYPE_NAMED &&
        type->declaration != NULL &&
        type->declaration->kind == DECL_ENUM) {
        size_t count =
            type->declaration->as.enumeration.variant_count;
        module->types[id].variant_names = ir_resize(
            NULL, count, sizeof(*module->types[id].variant_names));
        module->types[id].variant_payload_types = ir_resize(
            NULL, count,
            sizeof(*module->types[id].variant_payload_types));
        module->types[id].variant_spans = ir_resize(
            NULL, count, sizeof(*module->types[id].variant_spans));
        module->types[id].variant_discriminants = ir_resize(
            NULL, count, sizeof(*module->types[id].variant_discriminants));
        module->types[id].variant_payload_offsets = ir_resize(
            NULL, count,
            sizeof(*module->types[id].variant_payload_offsets));
        module->types[id].variant_count = count;
        for (size_t i = 0U; i < count; ++i) {
            module->types[id].variant_names[i] =
                type->declaration->as.enumeration.variants[i].name;
            module->types[id].variant_spans[i] =
                type->declaration->as.enumeration.variants[i].span;
            module->types[id].variant_discriminants[i] = (uint32_t)i;
            Type *payload_type = lang_checker_resolve_aggregate_member(
                module->lowering_module, module->lowering_diagnostics,
                type, i);
            module->types[id].variant_payload_types[i] =
                payload_type != NULL
                ? ir_intern_type(module, payload_type) : IR_INVALID_ID;
        }
        resolve_variant_member_layout(module, id);
    } else if (type != NULL &&
               (type->kind == TYPE_OPTION ||
                type->kind == TYPE_RESULT)) {
        size_t count = 2U;
        module->types[id].variant_names = ir_resize(
            NULL, count, sizeof(*module->types[id].variant_names));
        module->types[id].variant_payload_types = ir_resize(
            NULL, count,
            sizeof(*module->types[id].variant_payload_types));
        module->types[id].variant_spans = ir_resize(
            NULL, count, sizeof(*module->types[id].variant_spans));
        module->types[id].variant_discriminants = ir_resize(
            NULL, count, sizeof(*module->types[id].variant_discriminants));
        module->types[id].variant_payload_offsets = ir_resize(
            NULL, count,
            sizeof(*module->types[id].variant_payload_offsets));
        module->types[id].variant_count = count;
        memset(module->types[id].variant_spans, 0,
               count * sizeof(*module->types[id].variant_spans));
        module->types[id].variant_discriminants[0] = 0U;
        module->types[id].variant_discriminants[1] = 1U;
        if (type->kind == TYPE_OPTION) {
            module->types[id].variant_names[0] = "None";
            module->types[id].variant_names[1] = "Some";
            module->types[id].variant_payload_types[0] = IR_INVALID_ID;
            module->types[id].variant_payload_types[1] =
                module->types[id].element_type;
        } else {
            module->types[id].variant_names[0] = "Ok";
            module->types[id].variant_names[1] = "Err";
            module->types[id].variant_payload_types[0] =
                module->types[id].element_type;
            module->types[id].variant_payload_types[1] =
                module->types[id].error_type;
        }
        resolve_variant_member_layout(module, id);
    }
    return id;
}

IrTypeId ir_intern_iterator_type(IrModule *module,
                                     IrTypeId source_type,
                                     IrTypeId element_type) {
    for (size_t i = 0U; i < module->type_count; ++i)
        if (module->types[i].shape == IR_TYPE_ITERATOR &&
            module->types[i].argument_count == 1U &&
            module->types[i].argument_types[0] == source_type &&
            module->types[i].element_type == element_type)
            return (IrTypeId)i;
    if (module->type_count == module->type_capacity) {
        size_t next = module->type_capacity == 0U
                    ? 16U : module->type_capacity * 2U;
        module->types = ir_resize(
            module->types, next, sizeof(*module->types));
        module->type_capacity = next;
    }
    if (module->type_count >= (size_t)IR_INVALID_ID) {
        fputs("fatal: too many IR types\n", stderr);
        exit(2);
    }
    IrTypeId id = (IrTypeId)module->type_count++;
    IrType *type = &module->types[id];
    memset(type, 0, sizeof(*type));
    type->name = "iterator";
    type->shape = IR_TYPE_ITERATOR;
    type->element_type = element_type;
    type->error_type = IR_INVALID_ID;
    type->base_type = IR_INVALID_ID;
    type->copy_function = IR_INVALID_ID;
    type->destructor_function = IR_INVALID_ID;
    type->copy_policy = IR_COPY_DEEP;
    type->drop_policy = IR_DROP_CUSTOM;
    type->argument_types = ir_resize(
        NULL, 1U, sizeof(*type->argument_types));
    type->argument_types[0] = source_type;
    type->argument_count = 1U;
    type->requires_cleanup = true;
    type->target_layout_known = true;
    type->target_size = module->target.pointer_size;
    type->target_alignment = module->target.pointer_alignment;
    return id;
}

IrTypeId ir_intern_element_builder_type(IrModule *module,
                                            IrTypeId result_type) {
    for (size_t i = 0U; i < module->type_count; ++i)
        if (module->types[i].shape == IR_TYPE_ELEMENT_BUILDER &&
            module->types[i].element_type == result_type)
            return (IrTypeId)i;
    if (module->type_count == module->type_capacity) {
        size_t next = module->type_capacity == 0U
                    ? 16U : module->type_capacity * 2U;
        module->types = ir_resize(
            module->types, next, sizeof(*module->types));
        module->type_capacity = next;
    }
    if (module->type_count >= (size_t)IR_INVALID_ID) {
        fputs("fatal: too many IR types\n", stderr);
        exit(2);
    }
    IrTypeId id = (IrTypeId)module->type_count++;
    IrType *type = &module->types[id];
    memset(type, 0, sizeof(*type));
    type->name = "element-builder";
    type->shape = IR_TYPE_ELEMENT_BUILDER;
    type->element_type = result_type;
    type->error_type = IR_INVALID_ID;
    type->base_type = IR_INVALID_ID;
    type->copy_function = IR_INVALID_ID;
    type->destructor_function = IR_INVALID_ID;
    type->copy_policy = IR_COPY_NONCOPYABLE;
    type->drop_policy = IR_DROP_CUSTOM;
    type->requires_cleanup = true;
    /*
     * The builder is an opaque owning runtime handle, just like Html.
     * Keeping its target representation explicit lets every backend use
     * normal local-storage and cleanup machinery while the typed IR still
     * distinguishes an unfinished builder from a finished Html value.
     */
    type->target_layout_known = true;
    type->target_size = module->target.pointer_size;
    type->target_alignment = module->target.pointer_alignment;
    return id;
}

IrBlockId ir_add_block(IrFunction *function) {
    if (function->block_count == function->block_capacity) {
        size_t next = function->block_capacity == 0U
                    ? 8U : function->block_capacity * 2U;
        function->blocks = ir_resize(
            function->blocks, next, sizeof(*function->blocks));
        memset(function->blocks + function->block_capacity, 0,
               (next - function->block_capacity) *
               sizeof(*function->blocks));
        function->block_capacity = next;
    }
    if (function->block_count >= (size_t)IR_INVALID_ID) {
        fputs("fatal: too many IR blocks\n", stderr);
        exit(2);
    }
    IrBlockId id = (IrBlockId)function->block_count++;
    function->blocks[id].terminator = (IrTerminator){
        IR_TERM_NONE, IR_INVALID_ID, IR_INVALID_ID, IR_INVALID_ID,
        {NULL, 0U, 0U}
    };
    return id;
}

static IrValueId add_value(IrFunction *function, IrTypeId type) {
    if (function->value_count == function->value_capacity) {
        size_t next = function->value_capacity == 0U
                    ? 32U : function->value_capacity * 2U;
        function->value_types = ir_resize(
            function->value_types, next,
            sizeof(*function->value_types));
        function->value_capacity = next;
    }
    if (function->value_count >= (size_t)IR_INVALID_ID) {
        fputs("fatal: too many IR values\n", stderr);
        exit(2);
    }
    function->value_types[function->value_count] = type;
    return (IrValueId)function->value_count++;
}

IrInstruction *ir_append_instruction(IrBuilder *builder,
                                         IrOpcode opcode,
                                         IrTypeId result_type,
                                         const IrValueId *operands,
                                         size_t operand_count,
                                         LangSpan span) {
    IrBlock *block = &builder->function->blocks[builder->current];
    if (block->terminator.kind != IR_TERM_NONE) {
        lang_diag(builder->diagnostics, span,
                  "internal IR error: instruction after terminator");
        builder->failed = true;
        return NULL;
    }
    if (block->instruction_count == block->instruction_capacity) {
        size_t next = block->instruction_capacity == 0U
                    ? 8U : block->instruction_capacity * 2U;
        block->instructions = ir_resize(
            block->instructions, next, sizeof(*block->instructions));
        block->instruction_capacity = next;
    }
    IrInstruction *instruction =
        &block->instructions[block->instruction_count++];
    memset(instruction, 0, sizeof(*instruction));
    instruction->opcode = opcode;
    instruction->result_type = result_type;
    instruction->result = result_type == IR_INVALID_ID
                        ? IR_INVALID_ID
                        : add_value(builder->function, result_type);
    instruction->span = span;
    instruction->index = UINT32_MAX;
    instruction->auxiliary = UINT32_MAX;
    instruction->render_destination = UINT32_MAX;
    if (operand_count != 0U) {
        instruction->operands = ir_resize(
            NULL, operand_count, sizeof(*instruction->operands));
        memcpy(instruction->operands, operands,
               operand_count * sizeof(*instruction->operands));
        instruction->operand_count = operand_count;
    }
    return instruction;
}

void ir_set_terminator(IrBuilder *builder, IrTerminatorKind kind,
                           IrValueId value, IrBlockId target,
                           IrBlockId alternate, LangSpan span) {
    IrBlock *block = &builder->function->blocks[builder->current];
    if (block->terminator.kind != IR_TERM_NONE) {
        lang_diag(builder->diagnostics, span,
                  "internal IR error: block has multiple terminators");
        builder->failed = true;
        return;
    }
    block->terminator =
        (IrTerminator){kind, value, target, alternate, span};
}

bool ir_current_terminated(const IrBuilder *builder) {
    return builder->function->blocks[builder->current].terminator.kind !=
           IR_TERM_NONE;
}

uint32_t ir_add_local(IrBuilder *builder, const char *name,
                          size_t binding_id, const Type *type,
                          bool mutable_) {
    IrFunction *function = builder->function;
    for (size_t i = 0U; i < function->local_count; ++i)
        if (binding_id != 0U &&
            function->locals[i].binding_id == binding_id)
            return (uint32_t)i;
    if (function->local_count == function->local_capacity) {
        size_t next = function->local_capacity == 0U
                    ? 16U : function->local_capacity * 2U;
        function->locals = ir_resize(
            function->locals, next, sizeof(*function->locals));
        function->local_capacity = next;
    }
    if (function->local_count >= (size_t)UINT32_MAX) {
        fputs("fatal: too many IR locals\n", stderr);
        exit(2);
    }
    uint32_t slot = (uint32_t)function->local_count++;
    function->locals[slot] = (IrLocal){
        name, binding_id, ir_intern_type(builder->module, type), mutable_, false
    };
    return slot;
}

uint32_t ir_add_synthetic_local(IrBuilder *builder,
                                    const char *name,
                                    IrTypeId type) {
    IrFunction *function = builder->function;
    if (function->local_count == function->local_capacity) {
        size_t next = function->local_capacity == 0U
                    ? 16U : function->local_capacity * 2U;
        function->locals = ir_resize(
            function->locals, next, sizeof(*function->locals));
        function->local_capacity = next;
    }
    if (function->local_count >= (size_t)UINT32_MAX) {
        fputs("fatal: too many IR locals\n", stderr);
        exit(2);
    }
    uint32_t slot = (uint32_t)function->local_count++;
    function->locals[slot] =
        (IrLocal){name, 0U, type, false, false};
    return slot;
}

uint32_t ir_find_local(IrBuilder *builder, size_t binding_id,
                           LangSpan span) {
    for (size_t i = builder->function->local_count; i > 0U; --i)
        if (builder->function->locals[i - 1U].binding_id == binding_id)
            return (uint32_t)(i - 1U);
    lang_diag(builder->diagnostics, span,
              "internal IR error: unresolved local binding %zu",
              binding_id);
    builder->failed = true;
    return UINT32_MAX;
}

uint32_t ir_find_function(const IrModule *ir, const Decl *decl) {
    for (size_t i = 0U; i < ir->function_count; ++i)
        if (ir->functions[i].declaration == decl)
            return (uint32_t)i;
    return UINT32_MAX;
}

bool ir_type_produces_element_child(const Type *type) {
    if (type == NULL) return false;
    if (type->kind == TYPE_STR || type->kind == TYPE_STRING ||
        type->kind == TYPE_HTML)
        return true;
    if (type->kind == TYPE_OPTION || type->kind == TYPE_VEC ||
        type->kind == TYPE_ARRAY)
        return ir_type_produces_element_child(type->element);
    return false;
}

IrValueId ir_emit_unit(IrBuilder *builder, LangSpan span,
                           const Type *type) {
    IrInstruction *instruction = ir_append_instruction(
        builder, IR_OP_UNIT, ir_intern_type(builder->module, type),
        NULL, 0U, span);
    return instruction != NULL ? instruction->result : IR_INVALID_ID;
}

void ir_emit_cleanup(IrBuilder *builder, const CleanupPlan *plan,
                         LangSpan span) {
    for (size_t i = 0U; i < plan->count; ++i) {
        uint32_t local = ir_find_local(
            builder, plan->binding_ids[i], span);
        if (local == UINT32_MAX) continue;
        IrInstruction *drop = ir_append_instruction(
            builder, IR_OP_LOCAL_DROP, IR_INVALID_ID, NULL, 0U, span);
        if (drop != NULL) drop->index = local;
    }
}

bool ir_type_needs_cleanup(const IrModule *module, IrTypeId id) {
    if (id == IR_INVALID_ID || (size_t)id >= module->type_count)
        return false;
    const IrType *type = &module->types[id];
    return type->requires_cleanup || type->managed || type->shape == IR_TYPE_ARRAY ||
           type->shape == IR_TYPE_STRUCT || type->shape == IR_TYPE_ENUM ||
           type->shape == IR_TYPE_UNION ||
           type->shape == IR_TYPE_BUILTIN_OBJECT ||
           type->shape == IR_TYPE_ITERATOR ||
           type->shape == IR_TYPE_ELEMENT_BUILDER;
}

void ir_emit_element_exit_cleanup(IrBuilder *builder,
                                      size_t loop_depth,
                                      LangSpan span) {
    for (size_t i = builder->element_count; i > 0U; --i) {
        const IrElementContext *element = &builder->elements[i - 1U];
        if (element->loop_depth < loop_depth) continue;
        IrInstruction *drop = ir_append_instruction(
            builder, IR_OP_LOCAL_DROP, IR_INVALID_ID, NULL, 0U, span);
        if (drop != NULL) drop->index = element->local;
    }
}

void ir_emit_function_cleanup_except(
    IrBuilder *builder, LangSpan span, uint32_t excluded_local) {
    const Function *source =
        &builder->function->declaration->as.function;
    for (size_t i = builder->function->local_count; i > 0U; --i) {
        size_t local_index = i - 1U;
        if (local_index == excluded_local) continue;
        if (source->is_drop && local_index == 0U) continue;
        if (local_index < source->param_count &&
            source->params[local_index].borrowed)
            continue;
        if (!ir_type_needs_cleanup(
                builder->module,
                builder->function->locals[local_index].type))
            continue;
        IrInstruction *drop = ir_append_instruction(
            builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
            NULL, 0U, span);
        if (drop != NULL) drop->index = (uint32_t)local_index;
    }
}

void ir_emit_function_cleanup(IrBuilder *builder, LangSpan span) {
    ir_emit_function_cleanup_except(builder, span, IR_INVALID_ID);
}
