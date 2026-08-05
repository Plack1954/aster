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
static const Type ir_unit_type = {
    .kind = TYPE_UNIT,
    .name = "unit"
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

static const Type ir_string_builder_type = {
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

static bool is_float_type(const Type *type) {
    return type != NULL &&
           (type->kind == TYPE_F32 || type->kind == TYPE_F64);
}

static bool load_requires_clone(const Type *type) {
    return type != NULL &&
           (type->managed || type->kind == TYPE_ARRAY || type->kind == TYPE_OPTION ||
            type->kind == TYPE_RESULT || type->kind == TYPE_NAMED);
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
        case TYPE_SLICE: return IR_TYPE_SLICE;
        case TYPE_FUNCTION: return IR_TYPE_FUNCTION;
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
    size_t offset = 0U;
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
    module->types[id].member_layout_known = known;
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

static bool same_ir_type_identity(const Type *left, const Type *right) {
    if (left == right) return true;
    if (left == NULL || right == NULL || left->kind != right->kind)
        return false;
    if (left->kind == TYPE_NAMED) {
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
    if (type != NULL && type->kind == TYPE_NAMED &&
        type->declaration != NULL &&
        type->declaration->kind == DECL_STRUCT) {
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

uint32_t ir_field_index(const Type *object_type, const char *name) {
    if (object_type == NULL || object_type->declaration == NULL ||
        object_type->declaration->kind != DECL_STRUCT)
        return UINT32_MAX;
    const Decl *decl = object_type->declaration;
    for (size_t i = 0U; i < decl->as.structure.field_count; ++i)
        if (strcmp(decl->as.structure.fields[i].name, name) == 0)
            return (uint32_t)i;
    return UINT32_MAX;
}

static uint32_t variant_index(const Decl *decl, const char *name) {
    if (decl == NULL || decl->kind != DECL_ENUM)
        return UINT32_MAX;
    for (size_t i = 0U; i < decl->as.enumeration.variant_count; ++i)
        if (strcmp(decl->as.enumeration.variants[i].name, name) == 0)
            return (uint32_t)i;
    return UINT32_MAX;
}

static const char *unqualified_variant(const char *name) {
    const char *result = name;
    for (const char *cursor = strstr(name, "::");
         cursor != NULL; cursor = strstr(cursor + 2U, "::"))
        result = cursor + 2U;
    return result;
}

static uint32_t type_variant_index(const Type *type, const char *name) {
    const char *variant = unqualified_variant(name);
    if (type != NULL && type->kind == TYPE_OPTION)
        return strcmp(variant, "Some") == 0 ? 1U : 0U;
    if (type != NULL && type->kind == TYPE_RESULT)
        return strcmp(variant, "Err") == 0 ? 1U : 0U;
    return type != NULL
        ? variant_index(type->declaration, variant)
        : UINT32_MAX;
}

IrValueId ir_lower_expr(IrBuilder *builder, const Expr *expr);
void ir_lower_stmt(IrBuilder *builder, const Stmt *stmt);
IrValueId ir_emit_synthetic_native_call(
    IrBuilder *builder, const char *name,
    const Type *result_type, const IrValueId *operands,
    size_t operand_count, bool borrow_first,
    LangSpan span);

static void ir_set_native_call_descriptor(
    IrBuilder *builder, IrInstruction *call, bool compiler_generated
) {
    call->native_call = ir_resize(NULL, 1U, sizeof(*call->native_call));
    memset(call->native_call, 0, sizeof(*call->native_call));
    call->native_call->name = call->symbol;
    call->native_call->return_type = call->result_type;
    call->native_call->parameter_count = call->operand_count;
    call->native_call->calling_convention = IR_CALLING_CONVENTION_NATIVE;
    call->native_call->may_propagate_exception = true;
    call->native_call->compiler_generated = compiler_generated;
    if (call->operand_count == 0U) return;
    call->native_call->parameter_types = ir_resize(
        NULL, call->operand_count,
        sizeof(*call->native_call->parameter_types));
    call->native_call->parameter_modes = ir_resize(
        NULL, call->operand_count,
        sizeof(*call->native_call->parameter_modes));
    for (size_t i = 0U; i < call->operand_count; ++i) {
        IrValueId operand = call->operands[i];
        call->native_call->parameter_types[i] =
            operand < builder->function->value_count
                ? builder->function->value_types[operand]
                : IR_INVALID_ID;
        call->native_call->parameter_modes[i] =
            call->argument_modes[i];
    }
}

static bool builtin_borrows_first_place(const char *name) {
    return name != NULL &&
           (strcmp(name, "Html::ToHtmlString") == 0 ||
            strcmp(name, "ArenaAlloc") == 0 ||
            strcmp(name, "ArenaReset") == 0 ||
            strcmp(name, "StringBuilder::Append") == 0 ||
            strcmp(name, "StringBuilder::AppendByte") == 0 ||
            strcmp(name, "StringBuilder::ToString") == 0 ||
            strcmp(name, "StringBuilder::Length") == 0 ||
            strcmp(name, "StringBuilder::Clear") == 0 ||
            strcmp(name, "List::Add") == 0 ||
            strcmp(name, "List::Count") == 0 ||
            strcmp(name, "List::Get") == 0 ||
            strcmp(name, "List::Capacity") == 0 ||
            strcmp(name, "List::Clear") == 0 ||
            strcmp(name, "List::Insert") == 0 ||
            strcmp(name, "List::RemoveAt") == 0 ||
            strcmp(name, "List::Set") == 0 ||
            strcmp(name, "List::Contains") == 0 ||
            strcmp(name, "List::IndexOf") == 0 ||
            strcmp(name, "List::LastIndexOf") == 0 ||
            strcmp(name, "List::Remove") == 0 ||
            strcmp(name, "List::AddRange") == 0 ||
            strcmp(name, "List::InsertRange") == 0 ||
            strcmp(name, "List::RemoveRange") == 0 ||
            strcmp(name, "List::GetRange") == 0 ||
            strcmp(name, "List::Reverse") == 0 ||
            strcmp(name, "List::EnsureCapacity") == 0 ||
            strcmp(name, "List::TrimExcess") == 0 ||
            strcmp(name, "List::SetCapacity") == 0 ||
            strcmp(name, "List::Exists") == 0 ||
            strcmp(name, "List::FindAll") == 0 ||
            strcmp(name, "List::FindIndex") == 0 ||
            strcmp(name, "List::FindLastIndex") == 0 ||
            strcmp(name, "List::RemoveAll") == 0 ||
            strcmp(name, "List::ForEach") == 0 ||
            strcmp(name, "List::TrueForAll") == 0 ||
            strcmp(name, "Dictionary::Add") == 0 ||
            strcmp(name, "Dictionary::Count") == 0 ||
            strcmp(name, "Dictionary::ContainsKey") == 0 ||
            strcmp(name, "Dictionary::Remove") == 0 ||
            strcmp(name, "Dictionary::Clear") == 0 ||
            strcmp(name, "Dictionary::Get") == 0 ||
            strcmp(name, "Dictionary::Set") == 0 ||
            strcmp(name, "Dictionary::TryAdd") == 0 ||
            strcmp(name, "Dictionary::TryGetValue") == 0 ||
            strcmp(name, "Dictionary::ContainsValue") == 0 ||
            strcmp(name, "Dictionary::EnsureCapacity") == 0 ||
            strcmp(name, "Dictionary::TrimExcess") == 0 ||
            strcmp(name, "Dictionary::Capacity") == 0 ||
            strcmp(name, "Queue::Enqueue") == 0 ||
            strcmp(name, "Queue::Dequeue") == 0 ||
            strcmp(name, "Queue::Peek") == 0 ||
            strcmp(name, "Queue::TryDequeue") == 0 ||
            strcmp(name, "Queue::TryPeek") == 0 ||
            strcmp(name, "Queue::Count") == 0 ||
            strcmp(name, "Queue::Clear") == 0 ||
            strcmp(name, "Queue::EnsureCapacity") == 0 ||
            strcmp(name, "Queue::TrimExcess") == 0 ||
            strcmp(name, "Queue::Capacity") == 0 ||
            strcmp(name, "Stack::Push") == 0 ||
            strcmp(name, "Stack::Pop") == 0 ||
            strcmp(name, "Stack::Peek") == 0 ||
            strcmp(name, "Stack::TryPop") == 0 ||
            strcmp(name, "Stack::TryPeek") == 0 ||
            strcmp(name, "Stack::Count") == 0 ||
            strcmp(name, "Stack::Clear") == 0 ||
            strcmp(name, "Stack::EnsureCapacity") == 0 ||
            strcmp(name, "Stack::TrimExcess") == 0 ||
            strcmp(name, "Stack::Capacity") == 0 ||
            strcmp(name, "BufferAsMutSlice") == 0);
}

static bool builtin_borrows_named_first(const char *name) {
    return name != NULL &&
           (strcmp(name, "Console::WriteLine") == 0 ||
            strcmp(name, "Console::Write") == 0 ||
            strcmp(name, "Console::Error::WriteLine") == 0 ||
            strcmp(name, "Console::Error::Write") == 0 ||
            strcmp(name, "TextLen") == 0);
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

static IrOpcode binary_opcode(TokenKind token, bool floating) {
    switch (token) {
        case TOK_PLUS: return floating ? IR_OP_ADD_FLOAT : IR_OP_ADD_CHECKED;
        case TOK_MINUS: return floating ? IR_OP_SUB_FLOAT : IR_OP_SUB_CHECKED;
        case TOK_STAR: return floating ? IR_OP_MUL_FLOAT : IR_OP_MUL_CHECKED;
        case TOK_SLASH: return floating ? IR_OP_DIV_FLOAT : IR_OP_DIV_CHECKED;
        case TOK_PERCENT: return IR_OP_REM_CHECKED;
        case TOK_SHIFT_LEFT: return IR_OP_SHIFT_LEFT_CHECKED;
        case TOK_SHIFT_RIGHT: return IR_OP_SHIFT_RIGHT_CHECKED;
        case TOK_AMP: return IR_OP_BIT_AND;
        case TOK_PIPE: return IR_OP_BIT_OR;
        case TOK_CARET: return IR_OP_BIT_XOR;
        case TOK_EQUAL_EQUAL: return IR_OP_EQUAL;
        case TOK_BANG_EQUAL: return IR_OP_NOT_EQUAL;
        case TOK_LESS: return IR_OP_LESS;
        case TOK_LESS_EQUAL: return IR_OP_LESS_EQUAL;
        case TOK_GREATER: return IR_OP_GREATER;
        case TOK_GREATER_EQUAL: return IR_OP_GREATER_EQUAL;
        default: return IR_OP_VALUE_DISCARD;
    }
}

static IrValueId lower_enum_constructor(IrBuilder *builder,
                                        const Expr *expr,
                                        const char *name) {
    size_t argument_count = expr->as.call.arguments.count;
    IrValueId *operands = ir_resize(
        NULL, argument_count, sizeof(*operands));
    for (size_t i = 0U; i < argument_count; ++i)
        operands[i] = ir_lower_expr(
            builder, expr->as.call.arguments.items[i]);
    const char *separator = strrchr(name, ':');
    const char *variant =
        separator != NULL ? separator + 1U : name;
    IrInstruction *make = ir_append_instruction(
        builder, IR_OP_AGGREGATE_MAKE,
        ir_intern_type(builder->module, expr->type),
        operands, argument_count, expr->span);
    free(operands);
    if (make == NULL) return IR_INVALID_ID;
    make->symbol = variant;
    make->symbol_length = strlen(variant);
    if (expr->resolved_decl != NULL &&
        expr->resolved_decl->kind == DECL_ENUM)
        make->index = variant_index(expr->resolved_decl, variant);
    else if (strcmp(variant, "Some") == 0 ||
             strcmp(variant, "Err") == 0)
        make->index = 1U;
    else
        make->index = 0U;
    return make->result;
}

IrInstruction *ir_emit_local_enum_operation(
    IrBuilder *builder, IrOpcode opcode, IrTypeId result_type,
    uint32_t local, const Type *enum_type, const char *variant,
    LangSpan span) {
    IrInstruction *instruction = ir_append_instruction(
        builder, opcode, result_type, NULL, 0U, span);
    if (instruction != NULL) {
        instruction->index = local;
        instruction->auxiliary =
            type_variant_index(enum_type, variant);
        instruction->symbol = unqualified_variant(variant);
        instruction->symbol_length = strlen(instruction->symbol);
    }
    return instruction;
}

static IrValueId lower_try(IrBuilder *builder, const Expr *expr) {
    const Expr *operand_expr = expr->as.try_.value;
    const Type *result_type = operand_expr->type;
    IrValueId result = ir_lower_expr(builder, operand_expr);
    uint32_t local = ir_add_local(
        builder, "<try-result>", 0U, result_type, false);
    IrInstruction *store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &result, 1U, expr->span);
    if (store != NULL) store->index = local;

    IrInstruction *is_ok = ir_emit_local_enum_operation(
        builder, IR_OP_LOCAL_ENUM_IS,
        ir_intern_type(builder->module, &ir_bool_type),
        local, result_type, "Result::Ok", expr->span);
    if (is_ok == NULL) return IR_INVALID_ID;
    IrBlockId success = ir_add_block(builder->function);
    IrBlockId error = ir_add_block(builder->function);
    ir_set_terminator(builder, IR_TERM_BRANCH, is_ok->result,
                   success, error, expr->span);

    builder->current = error;
    IrInstruction *error_payload = ir_emit_local_enum_operation(
        builder, IR_OP_LOCAL_ENUM_PAYLOAD_MOVE,
        ir_intern_type(builder->module, result_type->error_type),
        local, result_type, "Result::Err", expr->span);
    if (error_payload == NULL) return IR_INVALID_ID;
    IrValueId payload = error_payload->result;
    const Type *function_result =
        builder->function->declaration->as.function.checked_return_type;
    IrInstruction *propagated = ir_append_instruction(
        builder, IR_OP_AGGREGATE_MAKE,
        ir_intern_type(builder->module, function_result),
        &payload, 1U, expr->span);
    if (propagated == NULL) return IR_INVALID_ID;
    propagated->symbol = "Err";
    propagated->symbol_length = 3U;
    propagated->index = 1U;
    IrValueId propagated_result = propagated->result;
    ir_emit_function_cleanup(builder, expr->span);
    ir_set_terminator(builder, IR_TERM_RETURN, propagated_result,
                   IR_INVALID_ID, IR_INVALID_ID, expr->span);

    builder->current = success;
    IrInstruction *success_payload = ir_emit_local_enum_operation(
        builder, IR_OP_LOCAL_ENUM_PAYLOAD_MOVE,
        ir_intern_type(builder->module, expr->type),
        local, result_type, "Result::Ok", expr->span);
    return success_payload != NULL
         ? success_payload->result : IR_INVALID_ID;
}

static IrValueId lower_call(IrBuilder *builder, const Expr *expr) {
    const char *callee_name =
        expr->as.call.callee->kind == EXPR_NAME
        ? expr->as.call.callee->as.name : NULL;
    if (callee_name != NULL &&
        strcmp(callee_name, "ArenaAlloc") == 0) {
        IrValueId operands[2] = {
            ir_lower_expr(builder, expr->as.call.arguments.items[0]),
            ir_lower_expr(builder, expr->as.call.arguments.items[1])
        };
        IrInstruction *allocation = ir_append_instruction(
            builder, IR_OP_RAW_ALLOC,
            ir_intern_type(builder->module, expr->type),
            operands, 2U, expr->span);
        return allocation != NULL
             ? allocation->result : IR_INVALID_ID;
    }
    if (callee_name != NULL &&
        strcmp(callee_name, "raw_load_i64") == 0) {
        IrValueId pointer = ir_lower_expr(
            builder, expr->as.call.arguments.items[0]);
        IrInstruction *load = ir_append_instruction(
            builder, IR_OP_RAW_LOAD,
            ir_intern_type(builder->module, expr->type),
            &pointer, 1U, expr->span);
        return load != NULL ? load->result : IR_INVALID_ID;
    }
    if (callee_name != NULL &&
        strcmp(callee_name, "raw_store_i64") == 0) {
        IrValueId operands[2] = {
            ir_lower_expr(builder, expr->as.call.arguments.items[0]),
            ir_lower_expr(builder, expr->as.call.arguments.items[1])
        };
        IrInstruction *store = ir_append_instruction(
            builder, IR_OP_RAW_STORE,
            ir_intern_type(builder->module, expr->type),
            operands, 2U, expr->span);
        return store != NULL ? store->result : IR_INVALID_ID;
    }
    if (callee_name != NULL &&
        ((expr->resolved_decl != NULL &&
          expr->resolved_decl->kind == DECL_ENUM) ||
         strcmp(callee_name, "Option::Some") == 0 ||
         strcmp(callee_name, "Option::None") == 0 ||
         strcmp(callee_name, "Result::Ok") == 0 ||
         strcmp(callee_name, "Result::Err") == 0))
        return lower_enum_constructor(builder, expr, callee_name);
    size_t argument_count = expr->as.call.arguments.count;
    bool indirect = expr->as.call.callee->resolved_local_id != 0U;
    size_t operand_count = argument_count + (indirect ? 1U : 0U);
    IrValueId *operands = ir_resize(
        NULL, operand_count, sizeof(*operands));
    uint32_t *borrowed_temporary_locals = ir_resize(
        NULL, argument_count, sizeof(*borrowed_temporary_locals));
    size_t borrowed_temporary_count = 0U;
    size_t offset = 0U;
    const Type *indirect_function_type = indirect
        ? expr->as.call.callee->type : NULL;
    if (indirect)
        operands[offset++] = ir_lower_expr(builder, expr->as.call.callee);
    const Decl *target = expr->resolved_decl;
    for (size_t i = 0U; i < argument_count; ++i) {
        Expr *argument = expr->as.call.arguments.items[i];
        bool builtin_borrow =
            i == 0U &&
            (builtin_borrows_first_place(callee_name) ||
             (builtin_borrows_named_first(callee_name) &&
              argument->kind == EXPR_NAME));
        bool indirect_borrow = indirect_function_type != NULL &&
            parameter_mode_is_reference(
                indirect_function_type->parameter_modes[i]);
        bool declared_borrow =
            target != NULL && target->kind == DECL_FUNCTION &&
            i < target->as.function.param_count &&
            target->as.function.params[i].borrowed;
        ParameterMode explicit_mode =
            expr->as.call.argument_modes != NULL
                ? expr->as.call.argument_modes[i]
                : PARAMETER_MODE_VALUE;
        bool explicit_borrow =
            parameter_mode_is_reference(explicit_mode);
        bool borrowed =
            indirect_borrow || declared_borrow || builtin_borrow ||
            explicit_borrow;
        bool borrowed_local = borrowed &&
            (argument->kind == EXPR_NAME ||
             (argument->kind == EXPR_FIELD &&
              argument->as.field.object->kind == EXPR_NAME));
        if (borrowed_local) {
            const Expr *place = argument->kind == EXPR_FIELD
                              ? argument->as.field.object : argument;
            uint32_t local = ir_find_local(
                builder, place->resolved_local_id, place->span);
            IrInstruction *load = ir_append_instruction(
                builder,
                argument->kind == EXPR_FIELD
                    ? IR_OP_LOCAL_FIELD_BORROW
                    : IR_OP_LOCAL_LOAD,
                ir_intern_type(builder->module, argument->type),
                NULL, 0U, argument->span);
            if (load != NULL) {
                load->index = local;
                if (argument->kind == EXPR_FIELD) {
                    load->auxiliary = ir_field_index(
                        place->type, argument->as.field.field);
                    load->symbol = argument->as.field.field;
                    load->symbol_length =
                        strlen(argument->as.field.field);
                }
            }
            operands[offset + i] =
                load != NULL ? load->result : IR_INVALID_ID;
        } else if (borrowed) {
            IrValueId owner = ir_lower_expr(builder, argument);
            IrTypeId owner_type = ir_intern_type(
                builder->module, argument->type);
            uint32_t owner_local = ir_add_synthetic_local(
                builder, "<borrowed-temporary>", owner_type);
            IrInstruction *store = ir_append_instruction(
                builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                &owner, 1U, argument->span);
            if (store != NULL) store->index = owner_local;
            IrInstruction *load = ir_append_instruction(
                builder, IR_OP_LOCAL_LOAD, owner_type,
                NULL, 0U, argument->span);
            if (load != NULL) load->index = owner_local;
            operands[offset + i] = load != NULL
                ? load->result : IR_INVALID_ID;
            borrowed_temporary_locals[borrowed_temporary_count++] =
                owner_local;
        } else {
            operands[offset + i] =
                ir_lower_expr(builder, argument);
        }
    }
    bool native = target != NULL && target->kind == DECL_FUNCTION &&
                  target->as.function.is_extern;
    if (!indirect && target == NULL)
        native = true;
    IrOpcode opcode = indirect ? IR_OP_CALL_INDIRECT :
                        native ? IR_OP_CALL_NATIVE : IR_OP_CALL_DIRECT;
    IrInstruction *call = ir_append_instruction(
        builder, opcode, ir_intern_type(builder->module, expr->type),
        operands, operand_count, expr->span);
    free(operands);
    if (call == NULL) {
        free(borrowed_temporary_locals);
        return IR_INVALID_ID;
    }
    call->argument_mode_count = argument_count;
    if (argument_count != 0U)
        call->argument_modes = ir_resize(
            NULL, argument_count, sizeof(*call->argument_modes));
    for (size_t i = 0U; i < argument_count; ++i) {
        ParameterMode mode = expr->as.call.argument_modes != NULL
            ? expr->as.call.argument_modes[i]
            : PARAMETER_MODE_VALUE;
        if (indirect && indirect_function_type != NULL)
            mode = indirect_function_type->parameter_modes[i];
        else if (target != NULL && target->kind == DECL_FUNCTION &&
                 i < target->as.function.param_count)
            mode = parameter_mode_from_param(
                &target->as.function.params[i]);
        else if (mode == PARAMETER_MODE_VALUE && i == 0U &&
                 (builtin_borrows_first_place(callee_name) ||
                  (builtin_borrows_named_first(callee_name) &&
                   expr->as.call.arguments.items[0]->kind == EXPR_NAME)))
            mode = PARAMETER_MODE_IMMUTABLE_REFERENCE;
        call->argument_modes[i] = mode;
    }
    if (target != NULL && target->kind == DECL_FUNCTION) {
        call->symbol = target->as.function.name;
        call->symbol_length = strlen(call->symbol);
        if (!native) call->index = ir_find_function(builder->module, target);
    } else if (!indirect && expr->as.call.callee->kind == EXPR_NAME) {
        call->symbol = expr->as.call.callee->as.name;
        call->symbol_length = strlen(call->symbol);
    }
    if (native)
        ir_set_native_call_descriptor(builder, call, false);
    IrValueId result = call->result;
    for (size_t i = borrowed_temporary_count; i > 0U; --i) {
        IrInstruction *drop = ir_append_instruction(
            builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
            NULL, 0U, expr->span);
        if (drop != NULL)
            drop->index = borrowed_temporary_locals[i - 1U];
    }
    free(borrowed_temporary_locals);
    bool registered_native = target != NULL &&
        target->kind == DECL_FUNCTION && target->as.function.is_extern;
    bool builtin_may_throw = callee_name != NULL &&
        strcmp(callee_name,
               "CancellationToken::ThrowIfCancellationRequested") == 0;
    if (!native || registered_native || builtin_may_throw) {
        IrInstruction *pending = ir_append_instruction(
            builder, IR_OP_EXCEPTION_PENDING,
            ir_intern_type(builder->module, &ir_bool_type),
            NULL, 0U, expr->span);
        if (pending == NULL) return IR_INVALID_ID;
        IrBlockId exceptional = ir_add_block(builder->function);
        IrBlockId continuation = ir_add_block(builder->function);
        ir_set_terminator(builder, IR_TERM_BRANCH, pending->result,
                          exceptional, continuation, expr->span);
        builder->current = exceptional;
        ir_emit_cleanup(builder, &expr->error_cleanup, expr->span);
        if (builder->exception_count != 0U)
            ir_set_terminator(
                builder, IR_TERM_JUMP, IR_INVALID_ID,
                builder->exceptions[builder->exception_count - 1U].handler,
                IR_INVALID_ID, expr->span);
        else
            ir_set_terminator(
                builder, IR_TERM_PROPAGATE_EXCEPTION, IR_INVALID_ID,
                IR_INVALID_ID, IR_INVALID_ID, expr->span);
        builder->current = continuation;
    }
    return result;
}

static IrValueId lower_logical_expr(IrBuilder *builder,
                                    const Expr *expr,
                                    IrTypeId type) {
    bool conjunction = expr->as.binary.op == TOK_AND_AND;
    IrValueId left = ir_lower_expr(builder, expr->as.binary.left);
    uint32_t result_local = ir_add_synthetic_local(
        builder, "<logical-result>", type);
    IrBlockId right_block = ir_add_block(builder->function);
    IrBlockId constant_block = ir_add_block(builder->function);
    IrBlockId merge_block = ir_add_block(builder->function);
    ir_set_terminator(
        builder, IR_TERM_BRANCH, left,
        conjunction ? right_block : constant_block,
        conjunction ? constant_block : right_block,
        expr->span);

    builder->current = right_block;
    IrValueId right = ir_lower_expr(builder, expr->as.binary.right);
    IrInstruction *right_store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &right, 1U, expr->as.binary.right->span);
    if (right_store != NULL) right_store->index = result_local;
    ir_set_terminator(
        builder, IR_TERM_JUMP, IR_INVALID_ID,
        merge_block, IR_INVALID_ID, expr->span);

    builder->current = constant_block;
    IrInstruction *constant = ir_append_instruction(
        builder, IR_OP_CONST_BOOL, type, NULL, 0U, expr->span);
    if (constant != NULL)
        constant->integer = conjunction ? 0U : 1U;
    IrValueId constant_value =
        constant != NULL ? constant->result : IR_INVALID_ID;
    IrInstruction *constant_store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &constant_value, 1U, expr->span);
    if (constant_store != NULL) constant_store->index = result_local;
    ir_set_terminator(
        builder, IR_TERM_JUMP, IR_INVALID_ID,
        merge_block, IR_INVALID_ID, expr->span);

    builder->current = merge_block;
    IrInstruction *load = ir_append_instruction(
        builder, IR_OP_LOCAL_LOAD, type, NULL, 0U, expr->span);
    if (load != NULL) load->index = result_local;
    return load != NULL ? load->result : IR_INVALID_ID;
}

static bool lower_value_block_to_local(IrBuilder *builder,
                                       const Stmt *block,
                                       uint32_t result_local) {
    size_t count = block->as.block.count;
    for (size_t i = 0U; i + 1U < count; ++i) {
        ir_lower_stmt(builder, block->as.block.items[i]);
        if (ir_current_terminated(builder)) return false;
    }
    if (count == 0U) return false;
    const Stmt *tail = block->as.block.items[count - 1U];
    if (tail->kind != STMT_EXPR) return false;
    IrValueId value = ir_lower_expr(builder, tail->as.expression);
    if (ir_current_terminated(builder)) return false;
    IrInstruction *store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &value, 1U, tail->span);
    if (store != NULL) store->index = result_local;
    ir_emit_cleanup(builder, &block->exit_cleanup, block->span);
    return true;
}

static IrValueId load_conditional_result(IrBuilder *builder,
                                         uint32_t result_local,
                                         IrTypeId type,
                                         LangSpan span) {
    IrOpcode opcode = ir_type_needs_cleanup(builder->module, type)
                    ? IR_OP_LOCAL_MOVE : IR_OP_LOCAL_LOAD;
    IrInstruction *load = ir_append_instruction(
        builder, opcode, type, NULL, 0U, span);
    if (load != NULL) load->index = result_local;
    return load != NULL ? load->result : IR_INVALID_ID;
}

static IrValueId lower_if_expression(IrBuilder *builder, const Expr *expr,
                                     IrTypeId type) {
    IrValueId condition = ir_lower_expr(builder, expr->as.if_.condition);
    uint32_t result_local = ir_add_synthetic_local(
        builder, "<if-result>", type);
    IrBlockId then_block = ir_add_block(builder->function);
    IrBlockId else_block = ir_add_block(builder->function);
    IrBlockId merge_block = ir_add_block(builder->function);
    ir_set_terminator(builder, IR_TERM_BRANCH, condition,
                   then_block, else_block, expr->span);

    bool reaches_merge = false;
    builder->current = then_block;
    if (lower_value_block_to_local(
            builder, expr->as.if_.then_branch, result_local) &&
        !ir_current_terminated(builder)) {
        ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                       merge_block, IR_INVALID_ID, expr->span);
        reaches_merge = true;
    }

    builder->current = else_block;
    if (lower_value_block_to_local(
            builder, expr->as.if_.else_branch, result_local) &&
        !ir_current_terminated(builder)) {
        ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                       merge_block, IR_INVALID_ID, expr->span);
        reaches_merge = true;
    }

    builder->current = merge_block;
    if (!reaches_merge) {
        ir_set_terminator(builder, IR_TERM_TRAP, IR_INVALID_ID,
                       IR_INVALID_ID, IR_INVALID_ID, expr->span);
        return IR_INVALID_ID;
    }
    return load_conditional_result(
        builder, result_local, type, expr->span);
}

static IrValueId lower_match_expression(IrBuilder *builder,
                                        const Expr *expr,
                                        IrTypeId type) {
    const Expr *matched_expr = expr->as.match_.value;
    const Type *matched_type = matched_expr->type;
    IrValueId matched = ir_lower_expr(builder, matched_expr);
    uint32_t matched_local = ir_add_local(
        builder, "<match>", 0U, matched_type, false);
    IrInstruction *store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &matched, 1U, matched_expr->span);
    if (store != NULL) store->index = matched_local;
    uint32_t result_local = ir_add_synthetic_local(
        builder, "<match-result>", type);

    IrBlockId merge = ir_add_block(builder->function);
    bool reaches_merge = false;
    for (size_t a = 0U; a < expr->as.match_.arm_count; ++a) {
        const MatchArm *arm = &expr->as.match_.arms[a];
        IrInstruction *matches = ir_emit_local_enum_operation(
            builder, IR_OP_LOCAL_ENUM_IS,
            ir_intern_type(builder->module, &ir_bool_type),
            matched_local, matched_type, arm->variant, arm->span);
        if (matches == NULL) return IR_INVALID_ID;
        IrBlockId arm_block = ir_add_block(builder->function);
        IrBlockId next_arm = ir_add_block(builder->function);
        ir_set_terminator(builder, IR_TERM_BRANCH, matches->result,
                       arm_block, next_arm, arm->span);

        builder->current = arm_block;
        if (arm->binding != NULL && arm->binding_type != NULL) {
            IrInstruction *payload = ir_emit_local_enum_operation(
                builder, IR_OP_LOCAL_ENUM_PAYLOAD_MOVE,
                ir_intern_type(builder->module, arm->binding_type),
                matched_local, matched_type, arm->variant, arm->span);
            if (payload == NULL) return IR_INVALID_ID;
            uint32_t binding = ir_add_local(
                builder, arm->binding, arm->binding_id,
                arm->binding_type, false);
            IrValueId value = payload->result;
            IrInstruction *binding_store = ir_append_instruction(
                builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                &value, 1U, arm->span);
            if (binding_store != NULL) binding_store->index = binding;
        } else {
            IrInstruction *drop = ir_append_instruction(
                builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                NULL, 0U, arm->span);
            if (drop != NULL) drop->index = matched_local;
        }
        if (lower_value_block_to_local(
                builder, arm->body, result_local) &&
            !ir_current_terminated(builder)) {
            ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                           merge, IR_INVALID_ID, arm->span);
            reaches_merge = true;
        }
        builder->current = next_arm;
    }
    IrInstruction *drop = ir_append_instruction(
        builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
        NULL, 0U, expr->span);
    if (drop != NULL) drop->index = matched_local;
    ir_set_terminator(builder, IR_TERM_TRAP, IR_INVALID_ID,
                   IR_INVALID_ID, IR_INVALID_ID, expr->span);

    builder->current = merge;
    if (!reaches_merge) {
        ir_set_terminator(builder, IR_TERM_TRAP, IR_INVALID_ID,
                       IR_INVALID_ID, IR_INVALID_ID, expr->span);
        return IR_INVALID_ID;
    }
    return load_conditional_result(
        builder, result_local, type, expr->span);
}

IrValueId ir_emit_synthetic_native_call(
    IrBuilder *builder, const char *name, const Type *result_type,
    const IrValueId *operands, size_t operand_count,
    bool borrow_first, LangSpan span) {
    IrInstruction *call = ir_append_instruction(
        builder, IR_OP_CALL_NATIVE,
        ir_intern_type(builder->module, result_type),
        operands, operand_count, span);
    if (call == NULL) return IR_INVALID_ID;
    call->symbol = name;
    call->symbol_length = strlen(name);
    call->argument_mode_count = operand_count;
    if (operand_count != 0U) {
        call->argument_modes = ir_resize(
            NULL, operand_count, sizeof(*call->argument_modes));
        for (size_t i = 0U; i < operand_count; ++i)
            call->argument_modes[i] = i == 0U && borrow_first
                ? PARAMETER_MODE_IMMUTABLE_REFERENCE
                : PARAMETER_MODE_VALUE;
    }
    ir_set_native_call_descriptor(builder, call, true);
    return call->result;
}

static IrValueId emit_interpolation_literal(
    IrBuilder *builder, const InterpolationPart *part) {
    IrInstruction *literal = ir_append_instruction(
        builder, IR_OP_CONST_STRING,
        ir_intern_type(builder->module, &ir_str_type),
        NULL, 0U, part->span);
    if (literal == NULL) return IR_INVALID_ID;
    literal->symbol = part->text;
    literal->symbol_length = part->text_length;
    return literal->result;
}

static IrValueId lower_owned_interpolation(
    IrBuilder *builder, const Expr *expr) {
    IrValueId builder_value = ir_emit_synthetic_native_call(
        builder, "StringBuilder::New",
        &ir_string_builder_type, NULL, 0U, 0U, expr->span);
    IrTypeId builder_type = ir_intern_type(
        builder->module, &ir_string_builder_type);
    uint32_t local = ir_add_synthetic_local(
        builder, "<interpolation-builder>", builder_type);
    IrInstruction *store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &builder_value, 1U, expr->span);
    if (store != NULL) store->index = local;

    for (size_t i = 0U;
         i < expr->as.interpolation.part_count; ++i) {
        const InterpolationPart *part =
            &expr->as.interpolation.parts[i];
        IrValueId value = part->expression != NULL
            ? ir_lower_expr(builder, part->expression)
            : emit_interpolation_literal(builder, part);
        if (value == IR_INVALID_ID ||
            ir_current_terminated(builder))
            return IR_INVALID_ID;

        const Type *value_type = part->expression != NULL
            ? part->expression->type : &ir_str_type;
        if (value_type->kind != TYPE_STR &&
            value_type->kind != TYPE_STRING) {
            IrInstruction *load = ir_append_instruction(
                builder, IR_OP_LOCAL_LOAD, builder_type,
                NULL, 0U, part->span);
            if (load != NULL) load->index = local;
            IrValueId arguments[2] = {
                load != NULL
                    ? load->result : IR_INVALID_ID,
                value
            };
            IrValueId appended = ir_emit_synthetic_native_call(
                builder,
                "__interpolation_builder_append_formatted",
                &ir_unit_type, arguments, 2U, 1U,
                part->span);
            IrInstruction *discard_result =
                ir_append_instruction(
                    builder, IR_OP_VALUE_DISCARD,
                    IR_INVALID_ID, &appended, 1U,
                    part->span);
            (void)discard_result;
            continue;
        }
        IrValueId owned_text = IR_INVALID_ID;
        uint32_t owned_text_local = IR_INVALID_ID;
        IrValueId text = value;
        if (value_type->kind == TYPE_STRING) {
            owned_text = value;
        }
        if (owned_text != IR_INVALID_ID) {
            IrTypeId owned_text_type = ir_intern_type(
                builder->module, &ir_string_type);
            owned_text_local = ir_add_synthetic_local(
                builder, "<interpolation-text>",
                owned_text_type);
            IrInstruction *store_text = ir_append_instruction(
                builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                &owned_text, 1U, part->span);
            if (store_text != NULL)
                store_text->index = owned_text_local;
            IrInstruction *load_text = ir_append_instruction(
                builder, IR_OP_LOCAL_LOAD, owned_text_type,
                NULL, 0U, part->span);
            if (load_text != NULL)
                load_text->index = owned_text_local;
            IrValueId borrowed_text =
                load_text != NULL
                    ? load_text->result : IR_INVALID_ID;
            text = ir_emit_synthetic_native_call(
                builder, "StringView", &ir_str_type,
                &borrowed_text, 1U, 1U, part->span);
        }

        IrInstruction *load = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, builder_type,
            NULL, 0U, part->span);
        if (load != NULL) load->index = local;
        IrValueId arguments[2] = {
            load != NULL ? load->result : IR_INVALID_ID,
            text
        };
        IrValueId appended = ir_emit_synthetic_native_call(
            builder, "StringBuilder::Append", &ir_unit_type,
            arguments, 2U, 1U, part->span);
        IrInstruction *discard_result = ir_append_instruction(
            builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
            &appended, 1U, part->span);
        (void)discard_result;
        if (owned_text_local != IR_INVALID_ID) {
            IrInstruction *drop_text = ir_append_instruction(
                builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                NULL, 0U, part->span);
            if (drop_text != NULL)
                drop_text->index = owned_text_local;
        }
    }

    IrInstruction *move = ir_append_instruction(
        builder, IR_OP_LOCAL_MOVE, builder_type,
        NULL, 0U, expr->span);
    if (move != NULL) move->index = local;
    IrValueId moved_builder = move != NULL
        ? move->result : IR_INVALID_ID;
    IrValueId result = ir_emit_synthetic_native_call(
        builder, "StringBuilder::Finish", expr->type,
        move != NULL ? &moved_builder : NULL,
        move != NULL ? 1U : 0U, 0U, expr->span);
    IrInstruction *drop = ir_append_instruction(
        builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
        NULL, 0U, expr->span);
    if (drop != NULL) drop->index = local;
    return result;
}

IrValueId ir_lower_expr(IrBuilder *builder, const Expr *expr) {
    IrTypeId type = ir_intern_type(builder->module, expr->type);
    IrInstruction *instruction = NULL;
    switch (expr->kind) {
        case EXPR_INT:
            instruction = ir_append_instruction(
                builder, IR_OP_CONST_INT, type, NULL, 0U, expr->span);
            if (instruction != NULL) instruction->integer = expr->as.integer;
            break;
        case EXPR_FLOAT:
            instruction = ir_append_instruction(
                builder, IR_OP_CONST_FLOAT, type, NULL, 0U, expr->span);
            if (instruction != NULL)
                instruction->floating = expr->as.floating;
            break;
        case EXPR_STRING:
            instruction = ir_append_instruction(
                builder, IR_OP_CONST_STRING,
                expr->type->kind == TYPE_STRING
                    ? ir_intern_type(builder->module, &ir_str_type)
                    : type,
                NULL, 0U, expr->span);
            if (instruction != NULL) {
                instruction->symbol = expr->as.string.data;
                instruction->symbol_length = expr->as.string.length;
            }
            if (instruction != NULL && expr->type->kind == TYPE_STRING) {
                IrValueId view = instruction->result;
                return ir_emit_synthetic_native_call(
                    builder, "String::from", &ir_string_type,
                    &view, 1U, 0U, expr->span);
            }
            break;
        case EXPR_INTERPOLATION:
            return lower_owned_interpolation(builder, expr);
        case EXPR_BOOL:
            instruction = ir_append_instruction(
                builder, IR_OP_CONST_BOOL, type, NULL, 0U, expr->span);
            if (instruction != NULL)
                instruction->integer = expr->as.boolean ? 1U : 0U;
            break;
        case EXPR_NULL:
            instruction = ir_append_instruction(
                builder, IR_OP_CONST_NULL, type, NULL, 0U, expr->span);
            break;
        case EXPR_NAME: {
            if (expr->resolved_local_id == 0U &&
                expr->resolved_decl != NULL &&
                expr->resolved_decl->kind == DECL_FUNCTION) {
                instruction = ir_append_instruction(
                    builder, IR_OP_FUNCTION_REF, type, NULL, 0U,
                    expr->span);
                if (instruction != NULL) {
                    instruction->symbol =
                        expr->resolved_decl->as.function.name;
                    instruction->symbol_length =
                        strlen(instruction->symbol);
                    instruction->index = ir_find_function(
                        builder->module, expr->resolved_decl);
                }
                break;
            }
            uint32_t local = ir_find_local(
                builder, expr->resolved_local_id, expr->span);
            instruction = ir_append_instruction(
                builder, IR_OP_LOCAL_LOAD, type, NULL, 0U, expr->span);
            if (instruction != NULL) instruction->index = local;
            if (instruction != NULL && load_requires_clone(expr->type)) {
                IrValueId operand = instruction->result;
                instruction = ir_append_instruction(
                    builder, IR_OP_VALUE_CLONE, type, &operand, 1U,
                    expr->span);
                if (instruction != NULL)
                    instruction->auxiliary = 0U;
            }
            break;
        }
        case EXPR_BINARY: {
            if (expr->as.binary.op == TOK_AND_AND ||
                expr->as.binary.op == TOK_OR_OR)
                return lower_logical_expr(builder, expr, type);
            const Expr *nullable = NULL;
            const Expr *null_value = NULL;
            if (expr->as.binary.left->type != NULL &&
                expr->as.binary.left->type->kind == TYPE_OPTION &&
                (expr->as.binary.op == TOK_EQUAL_EQUAL ||
                 expr->as.binary.op == TOK_BANG_EQUAL)) {
                const Expr *left = expr->as.binary.left;
                const Expr *right = expr->as.binary.right;
                bool left_null = left->kind == EXPR_CALL &&
                    left->as.call.callee->kind == EXPR_NAME &&
                    strcmp(left->as.call.callee->as.name,
                           "Option::None") == 0;
                bool right_null = right->kind == EXPR_CALL &&
                    right->as.call.callee->kind == EXPR_NAME &&
                    strcmp(right->as.call.callee->as.name,
                           "Option::None") == 0;
                nullable = left_null ? right : left;
                null_value = left_null ? left : (right_null ? right : NULL);
            }
            if (null_value != NULL && nullable->kind == EXPR_NAME) {
                instruction = ir_emit_local_enum_operation(
                    builder, IR_OP_LOCAL_ENUM_IS, type,
                    ir_find_local(builder, nullable->resolved_local_id,
                                  nullable->span),
                    nullable->type, "Option::None", expr->span);
                if (instruction != NULL &&
                    expr->as.binary.op == TOK_BANG_EQUAL) {
                    IrValueId operand = instruction->result;
                    instruction = ir_append_instruction(
                        builder, IR_OP_NOT, type, &operand, 1U,
                        expr->span);
                }
                break;
            }
            IrValueId operands[2] = {
                ir_lower_expr(builder, expr->as.binary.left),
                ir_lower_expr(builder, expr->as.binary.right)
            };
            IrOpcode opcode = binary_opcode(
                expr->as.binary.op,
                is_float_type(expr->as.binary.left->type));
            if (opcode == IR_OP_VALUE_DISCARD) {
                lang_diag(builder->diagnostics, expr->span,
                          "IR lowering does not support this binary operator");
                builder->failed = true;
                return IR_INVALID_ID;
            }
            instruction = ir_append_instruction(
                builder, opcode, type, operands, 2U, expr->span);
            break;
        }
        case EXPR_UNARY: {
            if (expr->as.unary.op == TOK_MINUS &&
                expr->as.unary.operand->kind == EXPR_INT &&
                expr->type != NULL &&
                (expr->type->kind == TYPE_I64 ||
                 expr->type->kind == TYPE_ISIZE) &&
                expr->as.unary.operand->as.integer ==
                    (UINT64_C(1) << 63U)) {
                instruction = ir_append_instruction(
                    builder, IR_OP_CONST_INT, type,
                    NULL, 0U, expr->span);
                if (instruction != NULL)
                    instruction->integer = UINT64_C(1) << 63U;
                break;
            }
            IrValueId operand = ir_lower_expr(
                builder, expr->as.unary.operand);
            instruction = ir_append_instruction(
                builder,
                expr->as.unary.op == TOK_STAR
                    ? IR_OP_RAW_LOAD :
                expr->as.unary.op == TOK_MINUS
                    ? IR_OP_NEGATE :
                expr->as.unary.op == TOK_TILDE
                    ? IR_OP_BIT_NOT : IR_OP_NOT,
                type, &operand, 1U, expr->span);
            break;
        }
        case EXPR_CALL:
            return lower_call(builder, expr);
        case EXPR_ASSIGN: {
            const Expr *target = expr->as.assign.target;
            TokenKind compound = expr->as.assign.compound_op;
            if (target->kind == EXPR_UNARY &&
                target->as.unary.op == TOK_STAR) {
                IrValueId pointer = ir_lower_expr(
                    builder, target->as.unary.operand);
                IrValueId store_pointer = pointer;
                IrValueId value;
                if (compound == TOK_ERROR) {
                    value = ir_lower_expr(
                        builder, expr->as.assign.value);
                } else {
                    IrTypeId pointer_type =
                        pointer < builder->function->value_count
                            ? builder->function->value_types[pointer]
                            : IR_INVALID_ID;
                    uint32_t pointer_local = ir_add_synthetic_local(
                        builder, "<compound-pointer>", pointer_type);
                    IrInstruction *save = ir_append_instruction(
                        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                        &pointer, 1U, target->span);
                    if (save != NULL) save->index = pointer_local;
                    IrInstruction *read_pointer = ir_append_instruction(
                        builder, IR_OP_LOCAL_LOAD, pointer_type,
                        NULL, 0U, target->span);
                    if (read_pointer != NULL)
                        read_pointer->index = pointer_local;
                    IrValueId read = read_pointer != NULL
                        ? read_pointer->result : IR_INVALID_ID;
                    IrInstruction *old = ir_append_instruction(
                        builder, IR_OP_RAW_LOAD,
                        ir_intern_type(builder->module, target->type),
                        &read, 1U, target->span);
                    IrValueId operands[2] = {
                        old != NULL ? old->result : IR_INVALID_ID,
                        ir_lower_expr(builder, expr->as.assign.value)
                    };
                    IrInstruction *updated = ir_append_instruction(
                        builder,
                        binary_opcode(
                            compound, is_float_type(target->type)),
                        ir_intern_type(builder->module, target->type),
                        operands, 2U, expr->span);
                    value = updated != NULL
                        ? updated->result : IR_INVALID_ID;
                    IrInstruction *write_pointer = ir_append_instruction(
                        builder, IR_OP_LOCAL_LOAD, pointer_type,
                        NULL, 0U, target->span);
                    if (write_pointer != NULL)
                        write_pointer->index = pointer_local;
                    store_pointer = write_pointer != NULL
                        ? write_pointer->result : IR_INVALID_ID;
                }
                IrValueId operands[2] = {store_pointer, value};
                instruction = ir_append_instruction(
                    builder, IR_OP_RAW_STORE,
                    ir_intern_type(builder->module, expr->type),
                    operands, 2U, expr->span);
            } else if (target->kind == EXPR_NAME) {
                uint32_t local = ir_find_local(
                    builder, target->resolved_local_id,
                    target->span);
                IrValueId value;
                if (compound == TOK_ERROR) {
                    value = ir_lower_expr(builder, expr->as.assign.value);
                } else {
                    IrInstruction *old = ir_append_instruction(
                        builder, IR_OP_LOCAL_LOAD,
                        ir_intern_type(builder->module, target->type),
                        NULL, 0U, target->span);
                    if (old != NULL) old->index = local;
                    IrValueId operands[2] = {
                        old != NULL ? old->result : IR_INVALID_ID,
                        ir_lower_expr(builder, expr->as.assign.value)
                    };
                    IrInstruction *updated = ir_append_instruction(
                        builder,
                        binary_opcode(
                            compound, is_float_type(target->type)),
                        ir_intern_type(builder->module, target->type),
                        operands, 2U, expr->span);
                    value = updated != NULL
                          ? updated->result : IR_INVALID_ID;
                }
                instruction = ir_append_instruction(
                    builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                    &value, 1U, expr->span);
                if (instruction != NULL)
                    instruction->index = local;
            } else if (target->kind == EXPR_FIELD &&
                       target->as.field.object->kind == EXPR_NAME) {
                uint32_t local = ir_find_local(
                    builder,
                    target->as.field.object->resolved_local_id,
                    target->as.field.object->span);
                uint32_t field = ir_field_index(
                    target->as.field.object->type,
                    target->as.field.field);
                IrValueId value;
                if (compound == TOK_ERROR) {
                    value = ir_lower_expr(builder, expr->as.assign.value);
                } else {
                    IrInstruction *old = ir_append_instruction(
                        builder, IR_OP_LOCAL_FIELD_GET,
                        ir_intern_type(builder->module, target->type),
                        NULL, 0U, target->span);
                    if (old != NULL) {
                        old->index = local;
                        old->auxiliary = field;
                        old->symbol = target->as.field.field;
                        old->symbol_length =
                            strlen(target->as.field.field);
                    }
                    IrValueId operands[2] = {
                        old != NULL ? old->result : IR_INVALID_ID,
                        ir_lower_expr(builder, expr->as.assign.value)
                    };
                    IrInstruction *updated = ir_append_instruction(
                        builder,
                        binary_opcode(
                            compound, is_float_type(target->type)),
                        ir_intern_type(builder->module, target->type),
                        operands, 2U, expr->span);
                    value = updated != NULL
                          ? updated->result : IR_INVALID_ID;
                }
                instruction = ir_append_instruction(
                    builder, IR_OP_LOCAL_FIELD_SET, IR_INVALID_ID,
                    &value, 1U, expr->span);
                if (instruction != NULL) {
                    instruction->index = local;
                    instruction->auxiliary = field;
                    instruction->symbol = target->as.field.field;
                    instruction->symbol_length =
                        strlen(target->as.field.field);
                }
            } else if (target->kind == EXPR_INDEX &&
                       target->as.index.object->kind == EXPR_NAME) {
                IrValueId index = ir_lower_expr(
                    builder, target->as.index.index);
                IrValueId set_index = index;
                IrValueId value;
                if (compound == TOK_ERROR) {
                    value = ir_lower_expr(builder, expr->as.assign.value);
                } else {
                    IrTypeId index_type =
                        index < builder->function->value_count
                            ? builder->function->value_types[index]
                            : IR_INVALID_ID;
                    uint32_t index_local = ir_add_synthetic_local(
                        builder, "<compound-index>", index_type);
                    IrInstruction *save_index = ir_append_instruction(
                        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                        &index, 1U, target->as.index.index->span);
                    if (save_index != NULL)
                        save_index->index = index_local;
                    IrInstruction *read_index = ir_append_instruction(
                        builder, IR_OP_LOCAL_LOAD, index_type,
                        NULL, 0U, target->as.index.index->span);
                    if (read_index != NULL)
                        read_index->index = index_local;
                    IrValueId read_index_value = read_index != NULL
                        ? read_index->result : IR_INVALID_ID;
                    IrInstruction *old = ir_append_instruction(
                        builder, IR_OP_LOCAL_INDEX_GET,
                        ir_intern_type(builder->module, target->type),
                        &read_index_value, 1U, target->span);
                    if (old != NULL) {
                        old->index = ir_find_local(
                            builder,
                            target->as.index.object->resolved_local_id,
                            target->as.index.object->span);
                        old->auxiliary =
                            target->as.index.unchecked ? 1U : 0U;
                    }
                    IrValueId binary_operands[2] = {
                        old != NULL ? old->result : IR_INVALID_ID,
                        ir_lower_expr(builder, expr->as.assign.value)
                    };
                    IrInstruction *updated = ir_append_instruction(
                        builder,
                        binary_opcode(
                            compound, is_float_type(target->type)),
                        ir_intern_type(builder->module, target->type),
                        binary_operands, 2U, expr->span);
                    value = updated != NULL
                          ? updated->result : IR_INVALID_ID;
                    IrInstruction *write_index = ir_append_instruction(
                        builder, IR_OP_LOCAL_LOAD, index_type,
                        NULL, 0U, target->as.index.index->span);
                    if (write_index != NULL)
                        write_index->index = index_local;
                    set_index = write_index != NULL
                        ? write_index->result : IR_INVALID_ID;
                }
                IrValueId operands[2] = {set_index, value};
                instruction = ir_append_instruction(
                    builder, IR_OP_LOCAL_INDEX_SET, IR_INVALID_ID,
                    operands, 2U, expr->span);
                if (instruction != NULL) {
                    instruction->index = ir_find_local(
                        builder,
                        target->as.index.object->resolved_local_id,
                        target->as.index.object->span);
                    instruction->auxiliary =
                        target->as.index.unchecked ? 1U : 0U;
                }
            } else {
                lang_diag(builder->diagnostics, expr->span,
                          "IR aggregate assignment requires a direct local");
                builder->failed = true;
                return IR_INVALID_ID;
            }
            return ir_emit_unit(builder, expr->span, expr->type);
        }
        case EXPR_CLONE: {
            IrValueId operand;
            const Expr *source = expr->as.clone.value;
            if (source->kind == EXPR_NAME) {
                uint32_t local = ir_find_local(
                    builder, source->resolved_local_id, source->span);
                IrInstruction *load = ir_append_instruction(
                    builder, IR_OP_LOCAL_LOAD,
                    ir_intern_type(builder->module, source->type),
                    NULL, 0U, source->span);
                if (load == NULL) return IR_INVALID_ID;
                load->index = local;
                operand = load->result;
            } else {
                operand = ir_lower_expr(builder, source);
            }
            instruction = ir_append_instruction(
                builder, IR_OP_VALUE_CLONE, type,
                &operand, 1U, expr->span);
            if (instruction != NULL)
                instruction->auxiliary =
                    source->kind == EXPR_NAME ? 0U : 1U;
            break;
        }
        case EXPR_TRY:
            return lower_try(builder, expr);
        case EXPR_AWAIT:
        {
            IrValueId task = ir_lower_expr(
                builder, expr->as.try_.value);
            IrInstruction *awaited = ir_append_instruction(
                builder, IR_OP_AWAIT, type, &task, 1U,
                expr->span);
            if (awaited == NULL) return IR_INVALID_ID;
            IrValueId result = awaited->result;
            IrInstruction *pending = ir_append_instruction(
                builder, IR_OP_EXCEPTION_PENDING,
                ir_intern_type(builder->module, &ir_bool_type),
                NULL, 0U, expr->span);
            if (pending == NULL) return IR_INVALID_ID;
            IrBlockId exceptional = ir_add_block(builder->function);
            IrBlockId continuation = ir_add_block(builder->function);
            ir_set_terminator(
                builder, IR_TERM_BRANCH, pending->result,
                exceptional, continuation, expr->span);
            builder->current = exceptional;
            ir_emit_cleanup(
                builder, &expr->error_cleanup, expr->span);
            if (builder->exception_count != 0U)
                ir_set_terminator(
                    builder, IR_TERM_JUMP, IR_INVALID_ID,
                    builder->exceptions[
                        builder->exception_count - 1U].handler,
                    IR_INVALID_ID, expr->span);
            else
                ir_set_terminator(
                    builder, IR_TERM_PROPAGATE_EXCEPTION,
                    IR_INVALID_ID, IR_INVALID_ID,
                    IR_INVALID_ID, expr->span);
            builder->current = continuation;
            return result;
        }
        case EXPR_CAST: {
            IrValueId operand = ir_lower_expr(builder, expr->as.cast.value);
            instruction = ir_append_instruction(
                builder, IR_OP_CAST, type, &operand, 1U, expr->span);
            break;
        }
        case EXPR_ARRAY: {
            size_t count = expr->as.array.count;
            IrValueId *operands = ir_resize(
                NULL, count, sizeof(*operands));
            for (size_t i = 0U; i < count; ++i)
                operands[i] = ir_lower_expr(
                    builder, expr->as.array.items[i]);
            instruction = ir_append_instruction(
                builder, IR_OP_AGGREGATE_MAKE, type,
                operands, count, expr->span);
            free(operands);
            if (instruction != NULL) {
                instruction->symbol = "array";
                instruction->symbol_length = 5U;
                instruction->index = (uint32_t)count;
            }
            break;
        }
        case EXPR_INDEX: {
            if (expr->as.index.object->kind == EXPR_NAME) {
                IrValueId index = ir_lower_expr(
                    builder, expr->as.index.index);
                instruction = ir_append_instruction(
                    builder, IR_OP_LOCAL_INDEX_GET, type,
                    &index, 1U, expr->span);
                if (instruction != NULL) {
                    instruction->index = ir_find_local(
                        builder,
                        expr->as.index.object->resolved_local_id,
                        expr->as.index.object->span);
                    instruction->auxiliary =
                        expr->as.index.unchecked ? 1U : 0U;
                }
            } else {
                IrValueId operands[2] = {
                    ir_lower_expr(builder, expr->as.index.object),
                    ir_lower_expr(builder, expr->as.index.index)
                };
                instruction = ir_append_instruction(
                    builder, IR_OP_INDEX_GET, type,
                    operands, 2U, expr->span);
                if (instruction != NULL)
                    instruction->auxiliary =
                        expr->as.index.unchecked ? 1U : 0U;
            }
            break;
        }
        case EXPR_FIELD: {
            const Expr *object = expr->as.field.object;
            if (object->type != NULL &&
                object->type->kind == TYPE_OPTION &&
                strcmp(expr->as.field.field, "Value") == 0) {
                IrValueId option = ir_lower_expr(builder, object);
                uint32_t local = ir_add_synthetic_local(
                    builder, "<nullable-value>",
                    ir_intern_type(builder->module, object->type));
                IrInstruction *store = ir_append_instruction(
                    builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                    &option, 1U, expr->span);
                if (store != NULL) store->index = local;
                IrInstruction *some = ir_emit_local_enum_operation(
                    builder, IR_OP_LOCAL_ENUM_IS,
                    ir_intern_type(builder->module, &ir_bool_type),
                    local, object->type, "Option::Some", expr->span);
                if (some == NULL) return IR_INVALID_ID;
                IrBlockId present = ir_add_block(builder->function);
                IrBlockId absent = ir_add_block(builder->function);
                ir_set_terminator(builder, IR_TERM_BRANCH, some->result,
                                  present, absent, expr->span);
                builder->current = absent;
                IrInstruction *drop = ir_append_instruction(
                    builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                    NULL, 0U, expr->span);
                if (drop != NULL) drop->index = local;
                ir_set_terminator(builder, IR_TERM_TRAP, IR_INVALID_ID,
                                  IR_INVALID_ID, IR_INVALID_ID, expr->span);
                builder->current = present;
                instruction = ir_emit_local_enum_operation(
                    builder, IR_OP_LOCAL_ENUM_PAYLOAD_MOVE, type,
                    local, object->type, "Option::Some", expr->span);
                break;
            }
            uint32_t field = ir_field_index(
                object->type, expr->as.field.field);
            if (object->kind == EXPR_NAME) {
                instruction = ir_append_instruction(
                    builder, IR_OP_LOCAL_FIELD_GET, type,
                    NULL, 0U, expr->span);
                if (instruction != NULL) {
                    instruction->index = ir_find_local(
                        builder, object->resolved_local_id,
                        object->span);
                    instruction->auxiliary = field;
                }
            } else {
                IrValueId value = ir_lower_expr(builder, object);
                instruction = ir_append_instruction(
                    builder, IR_OP_FIELD_GET, type,
                    &value, 1U, expr->span);
                if (instruction != NULL)
                    instruction->index = field;
            }
            if (instruction != NULL) {
                instruction->symbol = expr->as.field.field;
                instruction->symbol_length =
                    strlen(expr->as.field.field);
            }
            break;
        }
        case EXPR_STRUCT: {
            size_t count = expr->as.structure.field_count;
            IrValueId *operands = ir_resize(
                NULL, count, sizeof(*operands));
            uint32_t *labels = ir_resize(
                NULL, count, sizeof(*labels));
            const Decl *decl = expr->resolved_decl;
            for (size_t supplied = 0U; supplied < count; ++supplied) {
                operands[supplied] = ir_lower_expr(
                    builder,
                    expr->as.structure.fields[supplied].value);
                labels[supplied] = UINT32_MAX;
                if (decl != NULL && decl->kind == DECL_STRUCT)
                    for (size_t field = 0U;
                         field < decl->as.structure.field_count; ++field)
                        if (strcmp(
                                expr->as.structure.fields[supplied].name,
                                decl->as.structure.fields[field].name) == 0)
                            labels[supplied] = (uint32_t)field;
            }
            instruction = ir_append_instruction(
                builder, IR_OP_AGGREGATE_MAKE, type,
                operands, count, expr->span);
            free(operands);
            if (instruction != NULL) {
                instruction->labels = labels;
                instruction->label_count = count;
                instruction->symbol =
                    decl != NULL && decl->kind == DECL_STRUCT
                    ? decl->as.structure.name
                    : expr->as.structure.name;
                instruction->symbol_length =
                    strlen(instruction->symbol);
                instruction->index = (uint32_t)count;
            } else free(labels);
            break;
        }
        case EXPR_ELEMENT:
            return ir_lower_element(builder, expr);
        case EXPR_IF:
            return lower_if_expression(builder, expr, type);
        case EXPR_MATCH:
            return lower_match_expression(builder, expr, type);
        default:
            lang_diag(builder->diagnostics, expr->span,
                      "IR foundation does not yet lower this expression");
            builder->failed = true;
            return IR_INVALID_ID;
    }
    return instruction != NULL ? instruction->result : IR_INVALID_ID;
}

static bool expression_contains_return(const Expr *expr);

static bool statement_contains_return(const Stmt *stmt) {
    if (stmt == NULL) return false;
    switch (stmt->kind) {
        case STMT_RETURN:
            return true;
        case STMT_LET:
            return expression_contains_return(stmt->as.let.value);
        case STMT_DESTRUCTURE:
            return expression_contains_return(
                stmt->as.destructure.value);
        case STMT_EXPR:
            return expression_contains_return(stmt->as.expression);
        case STMT_IF:
            return expression_contains_return(stmt->as.if_.condition) ||
                   statement_contains_return(stmt->as.if_.then_branch) ||
                   statement_contains_return(stmt->as.if_.else_branch);
        case STMT_WHILE:
            return expression_contains_return(stmt->as.while_.condition) ||
                   statement_contains_return(stmt->as.while_.body);
        case STMT_FOR:
            return expression_contains_return(stmt->as.for_.iterable) ||
                   expression_contains_return(stmt->as.for_.range_end) ||
                   statement_contains_return(stmt->as.for_.body);
        case STMT_C_FOR:
            return statement_contains_return(
                       stmt->as.c_for.initializer) ||
                   expression_contains_return(
                       stmt->as.c_for.condition) ||
                   expression_contains_return(
                       stmt->as.c_for.increment) ||
                   statement_contains_return(stmt->as.c_for.body);
        case STMT_MATCH:
            if (expression_contains_return(stmt->as.match_.value))
                return true;
            for (size_t i = 0U;
                 i < stmt->as.match_.arm_count; ++i)
                if (statement_contains_return(
                        stmt->as.match_.arms[i].body))
                    return true;
            return false;
        case STMT_THROW:
            return expression_contains_return(stmt->as.throw_value);
        case STMT_TRY:
            return statement_contains_return(stmt->as.try_.body) ||
                   statement_contains_return(stmt->as.try_.catch_body) ||
                   statement_contains_return(stmt->as.try_.finally_body);
        case STMT_BLOCK:
            for (size_t i = 0U; i < stmt->as.block.count; ++i)
                if (statement_contains_return(
                        stmt->as.block.items[i]))
                    return true;
            return false;
        case STMT_UNSAFE:
            return statement_contains_return(
                stmt->as.unsafe_body);
        case STMT_BREAK:
        case STMT_CONTINUE:
            return false;
    }
    return false;
}

static bool expression_contains_return(const Expr *expr) {
    if (expr == NULL) return false;
    switch (expr->kind) {
        case EXPR_BINARY:
            return expression_contains_return(expr->as.binary.left) ||
                   expression_contains_return(expr->as.binary.right);
        case EXPR_UNARY:
            return expression_contains_return(expr->as.unary.operand);
        case EXPR_CALL:
            if (expression_contains_return(expr->as.call.callee))
                return true;
            for (size_t i = 0U;
                 i < expr->as.call.arguments.count; ++i)
                if (expression_contains_return(
                        expr->as.call.arguments.items[i]))
                    return true;
            return false;
        case EXPR_ASSIGN:
            return expression_contains_return(expr->as.assign.target) ||
                   expression_contains_return(expr->as.assign.value);
        case EXPR_CLONE:
            return expression_contains_return(expr->as.clone.value);
        case EXPR_TRY:
            return expression_contains_return(expr->as.try_.value);
        case EXPR_AWAIT:
            return expression_contains_return(expr->as.try_.value);
        case EXPR_CAST:
            return expression_contains_return(expr->as.cast.value);
        case EXPR_ARRAY:
            for (size_t i = 0U; i < expr->as.array.count; ++i)
                if (expression_contains_return(
                        expr->as.array.items[i]))
                    return true;
            return false;
        case EXPR_INTERPOLATION:
            for (size_t i = 0U;
                 i < expr->as.interpolation.part_count; ++i)
                if (expr->as.interpolation.parts[i].expression !=
                        NULL &&
                    expression_contains_return(
                        expr->as.interpolation.parts[i].expression))
                    return true;
            return false;
        case EXPR_INDEX:
            return expression_contains_return(expr->as.index.object) ||
                   expression_contains_return(expr->as.index.index);
        case EXPR_FIELD:
            return expression_contains_return(expr->as.field.object);
        case EXPR_STRUCT:
            for (size_t i = 0U;
                 i < expr->as.structure.field_count; ++i)
                if (expression_contains_return(
                        expr->as.structure.fields[i].value))
                    return true;
            return false;
        case EXPR_ELEMENT:
            for (size_t i = 0U;
                 i < expr->as.element.property_count; ++i)
                if (expression_contains_return(
                        expr->as.element.properties[i].value))
                    return true;
            for (size_t i = 0U;
                 i < expr->as.element.body_count; ++i) {
                const ElementBodyItem *item =
                    &expr->as.element.body[i];
                if ((item->is_statement &&
                     statement_contains_return(
                         item->as.statement)) ||
                    (!item->is_statement &&
                     expression_contains_return(
                         item->as.expression)))
                    return true;
            }
            return false;
        case EXPR_IF:
            return expression_contains_return(expr->as.if_.condition) ||
                   statement_contains_return(expr->as.if_.then_branch) ||
                   statement_contains_return(expr->as.if_.else_branch);
        case EXPR_MATCH:
            if (expression_contains_return(expr->as.match_.value))
                return true;
            for (size_t i = 0U;
                 i < expr->as.match_.arm_count; ++i)
                if (statement_contains_return(
                        expr->as.match_.arms[i].body))
                    return true;
            return false;
        case EXPR_INT:
        case EXPR_FLOAT:
        case EXPR_STRING:
        case EXPR_BOOL:
        case EXPR_NULL:
        case EXPR_NAME:
            return false;
    }
    return false;
}

static const Expr *direct_render_root(const Function *function) {
    if (function == NULL || function->checked_return_type == NULL ||
        function->checked_return_type->kind != TYPE_HTML ||
        function->body == NULL ||
        function->body->kind != STMT_BLOCK ||
        function->body->as.block.count == 0U)
        return NULL;
    const Stmt *tail = function->body->as.block.items[
        function->body->as.block.count - 1U];
    if (tail == NULL) return NULL;
    const Expr *root = NULL;
    if (tail->kind == STMT_RETURN)
        root = tail->as.return_value;
    else if (tail->kind == STMT_EXPR &&
             !tail->expression_terminated)
        root = tail->as.expression;
    bool intrinsic_root =
        root != NULL && root->kind == EXPR_ELEMENT &&
        ((root->resolved_decl != NULL &&
          root->resolved_decl->kind == DECL_ELEMENT) ||
         strcmp(root->as.element.name, "#fragment") == 0);
    if (!intrinsic_root || expression_contains_return(root))
        return NULL;
    for (size_t i = 0U;
         i + 1U < function->body->as.block.count; ++i)
        if (statement_contains_return(
                function->body->as.block.items[i]))
            return NULL;
    return root;
}

static void initialize_functions(const Module *module, IrModule *ir) {
    for (size_t i = 0U; i < module->count; ++i) {
        const Decl *decl = module->decls[i];
        if (decl->kind == DECL_FUNCTION &&
            decl->type_param_count == 0U &&
            !decl->as.function.is_extern)
            ++ir->function_count;
    }
    ir->functions = ir_resize(
        NULL, ir->function_count, sizeof(*ir->functions));
    memset(ir->functions, 0,
           ir->function_count * sizeof(*ir->functions));
    size_t output = 0U;
    for (size_t i = 0U; i < module->count; ++i) {
        const Decl *decl = module->decls[i];
        if (decl->kind != DECL_FUNCTION ||
            decl->type_param_count != 0U ||
            decl->as.function.is_extern)
            continue;
        IrFunction *function = &ir->functions[output++];
        function->name = decl->as.function.name;
        function->module_name = decl->module_name;
        function->declaration = decl;
        function->span = decl->span;
        function->is_public = decl->is_public;
        function->abi.calling_convention = IR_CALLING_CONVENTION_ASTER;
        function->abi.may_propagate_exception = true;
        function->is_destructor =
            decl->as.function.is_drop;
        function->is_web_export =
            decl->as.function.is_web_handler;
        const Expr *render_root =
            direct_render_root(&decl->as.function);
        if (render_root != NULL) {
            function->has_render_root = true;
            function->render_root_span =
                render_root->as.element.open_span;
        }
        function->return_type =
            ir_intern_type(ir, decl->as.function.checked_return_type);
        function->is_async = decl->as.function.is_async;
        function->abi.returns_async_task = function->is_async;
        function->async_result_type = function->is_async
            ? ir_intern_type(
                  ir, decl->as.function.checked_return_type->element)
            : function->return_type;
        function->parameter_count = decl->as.function.param_count;
        function->parameters = ir_resize(
            NULL, function->parameter_count,
            sizeof(*function->parameters));
        for (size_t p = 0U; p < function->parameter_count; ++p) {
            function->parameters[p].name =
                decl->as.function.params[p].name;
            function->parameters[p].type = ir_intern_type(
                ir, decl->as.function.params[p].checked_type);
            function->parameters[p].mode = parameter_mode_from_param(
                &decl->as.function.params[p]);
            function->parameters[p].span =
                decl->as.function.params[p].span;
        }
        function->css_scope_attribute =
            decl->as.function.css_scope_attribute;
        function->is_entry =
            strcmp(function->name, "main") == 0 &&
            module->entry_module != NULL &&
            function->module_name != NULL &&
            strcmp(module->entry_module, function->module_name) == 0;
    }
}

static void resolve_type_destructors(IrModule *ir) {
    for (size_t type_index = 0U;
         type_index < ir->type_count; ++type_index) {
        IrType *type = &ir->types[type_index];
        if (type->shape != IR_TYPE_STRUCT &&
            type->shape != IR_TYPE_ENUM &&
            type->shape != IR_TYPE_UNION)
            continue;
        for (size_t function_index = 0U;
             function_index < ir->function_count;
             ++function_index) {
            const IrFunction *function =
                &ir->functions[function_index];
            if (!function->is_destructor ||
                function->parameter_count != 1U ||
                function->parameters[0].type !=
                    (IrTypeId)type_index)
                continue;
            type->destructor_function =
                (IrFunctionId)function_index;
            type->drop_policy = IR_DROP_CUSTOM;
            break;
        }
    }
}

bool lang_ir_lower_module(Module *module,
                          const LangTargetInfo *target,
                          LangDiagnostics *diagnostics,
                          IrModule *ir) {
    memset(ir, 0, sizeof(*ir));
    ir->target = *target;
    ir->lowering_module = module;
    ir->lowering_diagnostics = diagnostics;
    initialize_functions(module, ir);
    for (size_t i = 0U; i < ir->function_count; ++i) {
        IrFunction *output = &ir->functions[i];
        const Function *source = &output->declaration->as.function;
        IrBuilder builder;
        memset(&builder, 0, sizeof(builder));
        builder.source = module;
        builder.diagnostics = diagnostics;
        builder.module = ir;
        builder.function = output;
        output->entry_block = ir_add_block(output);
        builder.current = output->entry_block;
        for (size_t p = 0U; p < source->param_count; ++p) {
            uint32_t local = ir_add_local(
                &builder, source->params[p].name,
                source->params[p].binding_id,
                source->params[p].checked_type,
                source->params[p].mutable_);
            output->locals[local].borrowed =
                parameter_mode_is_reference(output->parameters[p].mode);
            IrInstruction *parameter = ir_append_instruction(
                &builder, IR_OP_PARAMETER,
                output->parameters[p].type, NULL, 0U,
                source->params[p].span);
            if (parameter == NULL) continue;
            parameter->index = (uint32_t)p;
            if (!parameter_mode_is_reference(
                    output->parameters[p].mode)) {
                IrValueId value = parameter->result;
                IrInstruction *store = ir_append_instruction(
                    &builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                    &value, 1U, source->params[p].span);
                if (store != NULL) store->index = local;
            }
        }
        ir_lower_stmt(&builder, source->body);
        if (!ir_current_terminated(&builder)) {
            /*
             * Parameters live outside the function body's lexical block.
             * Body cleanup plans therefore do not contain them. A local drop
             * is defined to destroy the slot only when it still contains an
             * owning value, so a parameter moved on the fallthrough path is
             * already empty.
             */
            for (size_t p = source->param_count; p > 0U; --p) {
                const Type *parameter_type =
                    source->params[p - 1U].checked_type;
                if ((source->is_drop && p == 1U) ||
                    source->params[p - 1U].borrowed ||
                    parameter_type == NULL ||
                    !parameter_type->requires_cleanup)
                    continue;
                uint32_t local = ir_find_local(
                    &builder, source->params[p - 1U].binding_id,
                    source->params[p - 1U].span);
                IrInstruction *drop = ir_append_instruction(
                    &builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                    NULL, 0U, source->params[p - 1U].span);
                if (drop != NULL) drop->index = local;
            }
            const Type *logical_return =
                source->is_async &&
                source->checked_return_type != NULL &&
                source->checked_return_type->kind == TYPE_TASK
                    ? source->checked_return_type->element
                    : source->checked_return_type;
            if (logical_return != NULL &&
                logical_return->kind == TYPE_UNIT) {
                IrValueId unit = ir_emit_unit(
                    &builder, source->span,
                    logical_return);
                ir_set_terminator(
                    &builder, IR_TERM_RETURN, unit,
                    IR_INVALID_ID, IR_INVALID_ID, source->span);
            } else {
                /*
                 * The checker has already required a value-returning function
                 * to return on every reachable path. A merge block whose
                 * predecessors all returned is structurally present but
                 * unreachable; terminate it without inventing a typed value.
                 */
                ir_set_terminator(
                    &builder, IR_TERM_TRAP, IR_INVALID_ID,
                    IR_INVALID_ID, IR_INVALID_ID, source->span);
            }
        }
        if (builder.failed) {
            ir->lowering_module = NULL;
            ir->lowering_diagnostics = NULL;
            return false;
        }
        for (size_t b = 0U; b < output->block_count; ++b)
            for (size_t instruction = 0U;
                 instruction < output->blocks[b].instruction_count;
                 ++instruction)
                if (output->blocks[b].instructions[instruction].opcode ==
                    IR_OP_AWAIT)
                    ++output->async_suspension_count;
    }
    resolve_type_destructors(ir);
    /* Frontend links are lowering-only. A completed IR module is closed
     * backend input; keeping these null makes accidental semantic fallback
     * fail immediately during development. */
    for (size_t i = 0U; i < ir->function_count; ++i)
        ir->functions[i].declaration = NULL;
    for (size_t i = 0U; i < ir->type_count; ++i)
        ir->types[i].checked_type = NULL;
    ir->lowering_module = NULL;
    ir->lowering_diagnostics = NULL;
    return diagnostics->count == 0U;
}
