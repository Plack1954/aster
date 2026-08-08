#include "ir_bytecode_internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool instruction_result_is_borrowed(
    const IrFunction *function,
    const IrInstruction *instruction
) {
    if (instruction->result == IR_INVALID_ID) return false;
    switch (instruction->opcode) {
        case IR_OP_LOCAL_LOAD:
        case IR_OP_LOCAL_FIELD_BORROW:
        case IR_OP_ENUM_PAYLOAD_BORROW:
        case IR_OP_LIST_ELEMENT_BORROW:
        case IR_OP_QUEUE_FRONT_BORROW:
        case IR_OP_STACK_TOP_BORROW:
        case IR_OP_DICTIONARY_GET_BORROW:
        case IR_OP_DICTIONARY_KEY_BORROW:
        case IR_OP_DICTIONARY_VALUE_BORROW:
        case IR_OP_LOCAL_ITERATOR_NEXT:
            return true;
        case IR_OP_FIELD_GET:
            return instruction->auxiliary == 1U ||
                   instruction->auxiliary == 2U;
        case IR_OP_INDEX_GET:
            return instruction->integer != 0U;
        case IR_OP_PARAMETER:
            return instruction->index < function->parameter_count &&
                parameter_mode_is_reference(
                    function->parameters[instruction->index].mode);
        default:
            return false;
    }
}

static void lower_terminator(IrBytecodeBuilder *builder,
                             const IrTerminator *terminator) {
    switch (terminator->kind) {
        case IR_TERM_JUMP: {
            if (terminator->target == builder->current_block + 1U)
                break;
            size_t jump = emit_instruction(
                builder, OP_JUMP, 0, 0, terminator->span);
            add_patch(builder, jump, terminator->target);
            break;
        }
        case IR_TERM_BRANCH: {
            move_value(builder, terminator->value, terminator->span);
            size_t false_jump = emit_instruction(
                builder, OP_JUMP_IF_FALSE, 0, 0, terminator->span);
            add_patch(builder, false_jump, terminator->alternate);
            if (terminator->target != builder->current_block + 1U) {
                size_t true_jump = emit_instruction(
                    builder, OP_JUMP, 0, 0, terminator->span);
                add_patch(builder, true_jump, terminator->target);
            }
            break;
        }
        case IR_TERM_RETURN:
            move_value(builder, terminator->value, terminator->span);
            (void)emit_instruction(
                builder, OP_RETURN, 0, 0, terminator->span);
            break;
        case IR_TERM_PROPAGATE_EXCEPTION:
            (void)emit_instruction(
                builder, OP_PROPAGATE_EXCEPTION, 0, 0,
                terminator->span);
            break;
        case IR_TERM_TRAP:
            (void)emit_instruction(
                builder, OP_TRAP, 0, 0, terminator->span);
            break;
        case IR_TERM_NONE:
            lang_diag(builder->diagnostics, terminator->span,
                      "IR bytecode backend received an unterminated block");
            builder->failed = true;
            break;
    }
}

static bool lower_function(IrBytecodeBuilder *builder) {
    const IrFunction *source = builder->source;
    BytecodeFunction *function = builder->function;
    size_t local_count = source->local_count + source->value_count;
    if (local_count > IR_BC_MAX_LOCALS) {
        lang_diag(builder->diagnostics, source->span,
                  "IR bytecode function requires %zu locals; limit is %u",
                  local_count, (unsigned)IR_BC_MAX_LOCALS);
        return false;
    }
    for (size_t l = 0U; l < source->local_count; ++l)
        if (!runtime_type_supported(
                &builder->ir->types[source->locals[l].type])) {
            lang_diag(builder->diagnostics, source->span,
                      "IR bytecode backend does not yet support this local type");
            return false;
        }
    function->object_local_mask_valid = local_count <= 64U;
    for (size_t l = 0U; l < source->local_count; ++l)
        if (runtime_type_may_be_object(
                &builder->ir->types[source->locals[l].type])) {
            function->may_have_object_locals = true;
            if (l < 64U)
                function->object_local_mask |=
                    UINT64_C(1) << (unsigned)l;
        }
    for (size_t value = 0U; value < source->value_count; ++value)
        if (runtime_type_may_be_object(
                &builder->ir->types[source->value_types[value]])) {
            function->may_have_object_locals = true;
            size_t slot = builder->value_base + value;
            if (slot < 64U)
                function->object_local_mask |=
                    UINT64_C(1) << (unsigned)slot;
        }
    function->local_count = local_count;
    function->local_destructors = ir_bc_resize(
        NULL, local_count, sizeof(*function->local_destructors));
    for (size_t i = 0U; i < local_count; ++i)
        function->local_destructors[i] =
            i < source->local_count && source->locals[i].borrowed
                ? -2 : -1;
    for (size_t block = 0U; block < source->block_count; ++block)
        for (size_t index = 0U;
             index < source->blocks[block].instruction_count; ++index) {
            const IrInstruction *instruction =
                &source->blocks[block].instructions[index];
            if (instruction_result_is_borrowed(source, instruction))
                function->local_destructors[
                    source->local_count + instruction->result] = -2;
        }
    builder->value_base = source->local_count;
    builder->value_source_locals = ir_bc_resize(
        NULL, source->value_count, sizeof(*builder->value_source_locals));
    for (size_t i = 0U; i < source->value_count; ++i)
        builder->value_source_locals[i] = UINT32_MAX;
    builder->value_source_fields = ir_bc_resize(
        NULL, source->value_count, sizeof(*builder->value_source_fields));
    for (size_t i = 0U; i < source->value_count; ++i)
        builder->value_source_fields[i] = -1;
    builder->block_offsets = ir_bc_resize(
        NULL, source->block_count, sizeof(*builder->block_offsets));
    for (size_t b = 0U; b < source->block_count; ++b) {
        builder->block_offsets[b] = function->code_count;
        builder->block_start = function->code_count;
        builder->current_block = (IrBlockId)b;
        const IrBlock *block = &source->blocks[b];
        for (size_t i = 0U; i < block->instruction_count; ++i) {
            lower_instruction(builder, &block->instructions[i]);
            if (builder->failed) return false;
        }
        lower_terminator(builder, &block->terminator);
        if (builder->failed) return false;
    }
    for (size_t p = 0U; p < builder->patch_count; ++p) {
        IrBytecodePatch patch = builder->patches[p];
        if (patch.target >= source->block_count) return false;
        int32_t target;
        if (!as_i32(builder, builder->block_offsets[patch.target],
                    source->span, &target))
            return false;
        function->code[patch.instruction].a = target;
    }
    function->fast_scalar_loop_start = SIZE_MAX;
    function->fast_scalar_loop_end = SIZE_MAX;
    if (!function->may_have_object_locals &&
        function->local_count <= 64U) {
        for (size_t i = 0U; i < function->code_count; ++i) {
            Instruction instruction = function->code[i];
            if (instruction.op == OP_JUMP && instruction.a >= 0 &&
                (size_t)instruction.a < i) {
                function->fast_scalar_loop_start =
                    (size_t)instruction.a;
                function->fast_scalar_loop_end = i;
                break;
            }
        }
    }
    bool fast_signed_scalar_types = true;
    for (size_t l = 0U;
         l < source->local_count && fast_signed_scalar_types; ++l) {
        const IrType *type = &builder->ir->types[source->locals[l].type];
        fast_signed_scalar_types =
            type->shape == IR_TYPE_SIGNED_INT ||
            type->shape == IR_TYPE_BOOL || type->shape == IR_TYPE_UNIT ||
            type->shape == IR_TYPE_NEVER;
    }
    for (size_t value = 0U;
         value < source->value_count && fast_signed_scalar_types;
         ++value) {
        const IrType *type =
            &builder->ir->types[source->value_types[value]];
        fast_signed_scalar_types =
            type->shape == IR_TYPE_SIGNED_INT ||
            type->shape == IR_TYPE_BOOL || type->shape == IR_TYPE_UNIT ||
            type->shape == IR_TYPE_NEVER;
    }
    bool no_reference_parameters = true;
    for (size_t parameter = 0U; parameter < function->arity; ++parameter)
        if (parameter_mode_is_reference(
                function->parameter_modes[parameter]))
            no_reference_parameters = false;
    function->fast_scalar_leaf =
        !function->is_async && !function->may_have_object_locals &&
        no_reference_parameters &&
        function->local_count <= 64U &&
        fast_signed_scalar_types;
    for (size_t i = 0U;
         i < function->code_count && function->fast_scalar_leaf; ++i) {
        Instruction instruction = function->code[i];
        switch (instruction.op) {
            case OP_CONSTANT_LOCAL:
            case OP_COPY_LOCAL_TO:
            case OP_BINARY_LOCALS:
            case OP_BINARY_LOCAL_IMMEDIATE:
            case OP_BINARY_LOCALS_IMMEDIATE:
            case OP_RETURN_LOCAL:
                break;
            case OP_COMPARE_LOCAL_CONSTANT_BRANCH:
            case OP_JUMP:
                if (instruction.a <= (int32_t)i)
                    function->fast_scalar_leaf = false;
                break;
            default:
                function->fast_scalar_leaf = false;
                break;
        }
    }
    if (function->fast_scalar_leaf && function->arity == 2U &&
        function->code_count == 8U) {
        const Instruction *sum = &function->code[0];
        const Instruction *offset = &function->code[1];
        const Instruction *test = &function->code[2];
        const Instruction *copy = &function->code[3];
        const Instruction *constant = &function->code[4];
        const Instruction *subtract = &function->code[5];
        const Instruction *wrapped_return = &function->code[6];
        const Instruction *plain_return = &function->code[7];
        uint32_t sum_slots = (uint32_t)sum->b;
        uint32_t offset_slots = (uint32_t)offset->b;
        uint32_t test_packed = (uint32_t)test->b;
        uint32_t subtract_slots = (uint32_t)subtract->b;
        size_t sum_destination = (size_t)(
            (sum_slots >> 20U) & UINT32_C(0x3ff));
        size_t result_slot = (size_t)(
            (offset_slots >> 10U) & UINT32_C(0x3ff));
        size_t test_constant = (size_t)(
            (test_packed >> 10U) & UINT32_C(0xffff));
        bool shape =
            sum->op == OP_BINARY_LOCALS &&
            offset->op == OP_BINARY_LOCAL_IMMEDIATE &&
            test->op == OP_COMPARE_LOCAL_CONSTANT_BRANCH &&
            copy->op == OP_COPY_LOCAL_TO &&
            constant->op == OP_CONSTANT_LOCAL &&
            subtract->op == OP_BINARY_LOCALS &&
            wrapped_return->op == OP_RETURN_LOCAL &&
            plain_return->op == OP_RETURN_LOCAL &&
            ((OpCode)((uint32_t)sum->a & UINT32_C(0xff))) ==
                OP_ADD_I64 &&
            (sum_slots & UINT32_C(0x3ff)) == 0U &&
            ((sum_slots >> 10U) & UINT32_C(0x3ff)) == 1U &&
            ((OpCode)((uint32_t)offset->a & UINT32_C(0xff))) ==
                OP_ADD_I64 &&
            (offset_slots & UINT32_C(0x3ff)) == sum_destination &&
            (OpCode)(test_packed >> 26U) == OP_GT_I64 &&
            (test_packed & UINT32_C(0x3ff)) == result_slot &&
            test->a == 7 && copy->a == (int32_t)result_slot &&
            ((OpCode)((uint32_t)subtract->a & UINT32_C(0xff))) ==
                OP_SUB_I64 &&
            (subtract_slots & UINT32_C(0x3ff)) ==
                (uint32_t)copy->b &&
            ((subtract_slots >> 10U) & UINT32_C(0x3ff)) ==
                (uint32_t)constant->b &&
            wrapped_return->a == (int32_t)(
                (subtract_slots >> 20U) & UINT32_C(0x3ff)) &&
            plain_return->a == (int32_t)result_slot &&
            constant->a >= 0 && test_constant <
                builder->module->constant_count &&
            (size_t)constant->a < builder->module->constant_count;
        if (shape) {
            LangValue test_value = builder->module->constants[
                test_constant].value;
            LangValue subtract_value = builder->module->constants[
                (size_t)constant->a].value;
            if (test_value.tag == LANG_VALUE_I64 &&
                subtract_value.tag == LANG_VALUE_I64 &&
                test_value.as.i64 == subtract_value.as.i64) {
                function->fast_affine_wrap_leaf = true;
                function->fast_affine_addend =
                    (int64_t)((int32_t)offset_slots >> 20U);
                function->fast_affine_limit = test_value.as.i64;
            }
        }
    }
    return true;
}

static IrTypeId bytecode_class_type_for_function(
    const IrModule *ir, const IrFunction *function
) {
    const char *separator = strstr(function->name, "::");
    if (separator == NULL) return IR_INVALID_ID;
    size_t name_length = (size_t)(separator - function->name);
    for (size_t type = 0U; type < ir->type_count; ++type) {
        const IrType *candidate = &ir->types[type];
        if (candidate->shape != IR_TYPE_CLASS_REFERENCE ||
            candidate->name == NULL || strlen(candidate->name) != name_length ||
            strncmp(candidate->name, function->name, name_length) != 0)
            continue;
        if (candidate->module_name != NULL && function->module_name != NULL &&
            strcmp(candidate->module_name, function->module_name) != 0)
            continue;
        return (IrTypeId)type;
    }
    return IR_INVALID_ID;
}

static IrFunctionId bytecode_virtual_target(
    const IrModule *ir, IrFunctionId root, IrTypeId runtime_type
) {
    for (IrTypeId owner = runtime_type;
         owner != IR_INVALID_ID && owner < ir->type_count;
         owner = ir->types[owner].base_type)
        for (size_t function = 0U; function < ir->function_count; ++function) {
            const IrFunction *candidate = &ir->functions[function];
            if (candidate->is_abstract || candidate->virtual_root != root)
                continue;
            if (bytecode_class_type_for_function(ir, candidate) == owner)
                return (IrFunctionId)function;
        }
    return IR_INVALID_ID;
}

static bool bytecode_class_derives_from(
    const IrModule *ir, IrTypeId actual, IrTypeId expected
) {
    for (IrTypeId type = actual;
         type != IR_INVALID_ID && type < ir->type_count;
         type = ir->types[type].base_type)
        if (type == expected) return true;
    return false;
}

bool lang_ir_compile_bytecode(const IrModule *ir,
                              LangDiagnostics *diagnostics,
                              BytecodeModule *bytecode) {
    memset(bytecode, 0, sizeof(*bytecode));
    bytecode->static_count = ir->static_field_count;
    bytecode->static_defaults = ir_bc_resize(
        NULL, bytecode->static_count,
        sizeof(*bytecode->static_defaults));
    for (size_t field = 0U; field < bytecode->static_count; ++field) {
        const IrType *type = &ir->types[ir->static_fields[field].type];
        LangValue value = {.tag=LANG_VALUE_UNIT};
        switch (type->shape) {
            case IR_TYPE_BOOL:
                value.tag = LANG_VALUE_BOOL;
                value.as.boolean =
                    ir->static_fields[field].initial_integer != 0U;
                break;
            case IR_TYPE_UNSIGNED_INT:
            case IR_TYPE_CHAR:
                value.tag = LANG_VALUE_U64;
                value.as.u64 =
                    ir->static_fields[field].initial_integer;
                break;
            case IR_TYPE_FLOAT:
                value.tag = LANG_VALUE_F64;
                value.as.f64 =
                    ir->static_fields[field].initial_floating;
                break;
            case IR_TYPE_CLASS_REFERENCE:
            case IR_TYPE_RAW_POINTER:
                value.tag = LANG_VALUE_RAW_POINTER;
                value.as.pointer = NULL;
                break;
            case IR_TYPE_SIGNED_INT:
            case IR_TYPE_ENUM:
            default:
                value.tag = LANG_VALUE_I64;
                value.as.i64 = (int64_t)
                    ir->static_fields[field].initial_integer;
                break;
        }
        bytecode->static_defaults[field] = value;
    }
    bytecode->function_count = ir->function_count;
    bytecode->functions = ir_bc_resize(
        NULL, ir->function_count, sizeof(*bytecode->functions));
    memset(bytecode->functions, 0,
           ir->function_count * sizeof(*bytecode->functions));
    for (size_t f = 0U; f < ir->function_count; ++f) {
        const IrFunction *source = &ir->functions[f];
        BytecodeFunction *output = &bytecode->functions[f];
        output->name = source->name;
        output->module_name = source->module_name;
        output->arity = source->parameter_count;
        if (output->arity != 0U) {
            output->parameter_modes = ir_bc_resize(
                NULL, output->arity, sizeof(*output->parameter_modes));
            for (size_t parameter = 0U;
                 parameter < output->arity; ++parameter)
                output->parameter_modes[parameter] =
                    source->parameters[parameter].mode;
        }
        output->is_entry = source->is_entry;
        output->is_async = source->is_async;
        output->is_public = source->is_public;
    }
    for (size_t root = 0U; root < ir->function_count; ++root) {
        if (!ir->functions[root].is_virtual ||
            ir->functions[root].virtual_root != root)
            continue;
        IrTypeId root_type = bytecode_class_type_for_function(
            ir, &ir->functions[root]);
        for (size_t type = 0U; type < ir->type_count; ++type)
            if (ir->types[type].shape == IR_TYPE_CLASS_REFERENCE &&
                bytecode_class_derives_from(
                    ir, (IrTypeId)type, root_type) &&
                bytecode_virtual_target(
                    ir, (IrFunctionId)root, (IrTypeId)type) != IR_INVALID_ID)
                ++bytecode->virtual_entry_count;
    }
    bytecode->virtual_entry_count += ir->interface_dispatch_count;
    bytecode->virtual_entries = ir_bc_resize(
        NULL, bytecode->virtual_entry_count,
        sizeof(*bytecode->virtual_entries));
    size_t virtual_output = 0U;
    for (size_t root = 0U; root < ir->function_count; ++root) {
        const IrFunction *root_function = &ir->functions[root];
        if (!root_function->is_virtual ||
            root_function->virtual_root != root)
            continue;
        IrTypeId root_type = bytecode_class_type_for_function(
            ir, root_function);
        for (size_t type = 0U; type < ir->type_count; ++type) {
            const IrType *runtime = &ir->types[type];
            if (runtime->shape != IR_TYPE_CLASS_REFERENCE ||
                !bytecode_class_derives_from(
                    ir, (IrTypeId)type, root_type))
                continue;
            IrFunctionId target = bytecode_virtual_target(
                ir, (IrFunctionId)root, (IrTypeId)type);
            if (target == IR_INVALID_ID) continue;
            bytecode->virtual_entries[virtual_output++] =
                (BytecodeVirtualEntry){
                    .runtime_module=runtime->module_name,
                    .runtime_module_length=runtime->module_name != NULL
                        ? strlen(runtime->module_name) : 0U,
                    .runtime_type=runtime->name,
                    .runtime_type_length=strlen(runtime->name),
                    .root_function=root,
                    .target_function=target
                };
        }
    }
    for (size_t entry = 0U;
         entry < ir->interface_dispatch_count; ++entry) {
        const IrInterfaceDispatch *dispatch =
            &ir->interface_dispatches[entry];
        const IrType *runtime = &ir->types[dispatch->runtime_type];
        bytecode->virtual_entries[virtual_output++] =
            (BytecodeVirtualEntry){
                .runtime_module=runtime->module_name,
                .runtime_module_length=runtime->module_name != NULL
                    ? strlen(runtime->module_name) : 0U,
                .runtime_type=runtime->name,
                .runtime_type_length=strlen(runtime->name),
                .root_function=dispatch->interface_function,
                .target_function=dispatch->target_function
            };
    }
    for (size_t type = 0U; type < ir->type_count; ++type)
        if (ir->types[type].shape == IR_TYPE_CLASS_REFERENCE &&
            ir->types[type].destructor_function != IR_INVALID_ID)
            ++bytecode->class_destructor_count;
    bytecode->class_destructors = ir_bc_resize(
        NULL, bytecode->class_destructor_count,
        sizeof(*bytecode->class_destructors));
    size_t destructor_output = 0U;
    for (size_t type = 0U; type < ir->type_count; ++type) {
        const IrType *runtime = &ir->types[type];
        if (runtime->shape != IR_TYPE_CLASS_REFERENCE ||
            runtime->destructor_function == IR_INVALID_ID)
            continue;
        bytecode->class_destructors[destructor_output++] =
            (BytecodeClassDestructor){
                .runtime_module=runtime->module_name,
                .runtime_module_length=runtime->module_name != NULL
                    ? strlen(runtime->module_name) : 0U,
                .runtime_type=runtime->name,
                .runtime_type_length=strlen(runtime->name),
                .destructor_function=runtime->destructor_function
            };
    }
    for (size_t f = 0U; f < ir->function_count; ++f) {
        IrBytecodeBuilder builder;
        memset(&builder, 0, sizeof(builder));
        builder.ir = ir;
        builder.diagnostics = diagnostics;
        builder.module = bytecode;
        builder.source = &ir->functions[f];
        builder.function = &bytecode->functions[f];
        bool ok = lower_function(&builder);
        free(builder.block_offsets);
        free(builder.patches);
        free(builder.value_source_locals);
        free(builder.value_source_fields);
        if (!ok || builder.failed) {
            lang_bytecode_free(bytecode);
            return false;
        }
    }
    return diagnostics->count == 0U;
}
