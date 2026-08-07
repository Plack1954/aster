#include "ir_bytecode_internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void lower_call(IrBytecodeBuilder *builder,
                       const IrInstruction *instruction) {
    if (instruction->opcode == IR_OP_CALL_INDIRECT) {
        for (size_t i = 1U; i < instruction->operand_count; ++i) {
            IrValueId value = instruction->operands[i];
            uint32_t local = value < builder->source->value_count
                ? builder->value_source_locals[value] : UINT32_MAX;
            if (i - 1U < instruction->argument_mode_count &&
                parameter_mode_is_reference(
                    instruction->argument_modes[i - 1U]) &&
                local != UINT32_MAX) {
                int32_t field = builder->value_source_fields[value];
                if (field >= 0) {
                    (void)emit_instruction(
                        builder, OP_REFERENCE_FIELD_LOCAL,
                        (int32_t)local, field, instruction->span);
                    (void)emit_instruction(
                        builder, OP_INVALIDATE_LOCAL,
                        (int32_t)value_slot(builder, value), 0,
                        instruction->span);
                } else {
                    (void)emit_instruction(
                        builder, OP_REFERENCE_LOCAL, (int32_t)local,
                        (int32_t)value_slot(builder, value),
                        instruction->span);
                }
            }
            else
                move_value(builder, value, instruction->span);
        }
        move_value(
            builder, instruction->operands[0], instruction->span);
        int32_t count;
        if (as_i32(builder, instruction->operand_count - 1U,
                   instruction->span, &count))
            (void)emit_instruction(
                builder, OP_CALL_INDIRECT, count, 0,
                instruction->span);
        store_result(builder, instruction);
        return;
    }
    for (size_t i = 0U; i < instruction->operand_count; ++i) {
        IrValueId value = instruction->operands[i];
        uint32_t local = value < builder->source->value_count
            ? builder->value_source_locals[value] : UINT32_MAX;
        ParameterMode mode = i < instruction->argument_mode_count
            ? instruction->argument_modes[i]
            : PARAMETER_MODE_VALUE;
        bool reference_argument =
            (instruction->opcode == IR_OP_CALL_DIRECT ||
             instruction->opcode == IR_OP_CALL_VIRTUAL)
                ? parameter_mode_is_reference(mode)
                : instruction->opcode == IR_OP_CALL_NATIVE
                ? parameter_mode_is_mutable(mode)
                : false;
        if (reference_argument && local != UINT32_MAX) {
            int32_t field = builder->value_source_fields[value];
            if (field >= 0) {
                (void)emit_instruction(
                    builder, OP_REFERENCE_FIELD_LOCAL,
                    (int32_t)local, field, instruction->span);
                (void)emit_instruction(
                    builder, OP_INVALIDATE_LOCAL,
                    (int32_t)value_slot(builder, value), 0,
                    instruction->span);
            } else {
                (void)emit_instruction(
                    builder, OP_REFERENCE_LOCAL, (int32_t)local,
                    (int32_t)value_slot(builder, value),
                    instruction->span);
            }
        }
        else
            move_value(builder, value, instruction->span);
    }
    int32_t count;
    if (!as_i32(builder, instruction->operand_count,
                instruction->span, &count))
        return;
    if (instruction->opcode == IR_OP_CALL_DIRECT ||
        instruction->opcode == IR_OP_CALL_VIRTUAL) {
        int32_t target = INT32_MIN;
        if (instruction->symbol != NULL) {
            if (strcmp(instruction->symbol, "string::StartsWith") == 0 &&
                instruction->operand_count == 2U)
                target = -92;
            else if (strcmp(instruction->symbol, "string::EndsWith") == 0 &&
                     instruction->operand_count == 2U)
                target = -93;
            else if (strcmp(instruction->symbol, "string::Contains") == 0 &&
                     instruction->operand_count == 2U)
                target = -94;
            else if (strcmp(instruction->symbol, "string::IndexOf") == 0 &&
                     (instruction->operand_count == 2U ||
                      instruction->operand_count == 3U))
                target = -91;
        }
        if (target == INT32_MIN &&
            !as_i32(builder, instruction->index,
                    instruction->span, &target))
            return;
        if (instruction->opcode == IR_OP_CALL_VIRTUAL && target >= 0) {
            IrFunctionId root = builder->ir->functions[target].virtual_root;
            if (root != IR_INVALID_ID) target = (int32_t)root;
        }
        size_t call_index = emit_instruction(
            builder,
            instruction->opcode == IR_OP_CALL_VIRTUAL
                ? OP_CALL_VIRTUAL : OP_CALL,
            target, count, instruction->span);
        if (target < 0) {
            BytecodeCallSite *call_site =
                &builder->function->call_sites[call_index];
            call_site->argument_count = instruction->operand_count;
            if (call_site->argument_count != 0U) {
                call_site->argument_modes = ir_bc_resize(
                    NULL, call_site->argument_count,
                    sizeof(*call_site->argument_modes));
                for (size_t i = 0U;
                     i < call_site->argument_count; ++i)
                    call_site->argument_modes[i] = PARAMETER_MODE_VALUE;
            }
        }
    } else {
        if (instruction->symbol != NULL &&
            strcmp(instruction->symbol, "Task::Delay") == 0 &&
            (instruction->operand_count == 1U ||
             instruction->operand_count == 2U)) {
            (void)emit_instruction(
                builder, OP_TASK_DELAY,
                instruction->operand_count == 2U ? 1 : 0,
                0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        if (instruction->symbol != NULL &&
            (strcmp(instruction->symbol, "Task::WhenAll") == 0 ||
             strcmp(instruction->symbol, "Task::WhenAny") == 0) &&
            instruction->operand_count == 1U) {
            (void)emit_instruction(
                builder,
                strcmp(instruction->symbol, "Task::WhenAll") == 0
                    ? OP_TASK_WHEN_ALL : OP_TASK_WHEN_ANY,
                strcmp(instruction->symbol, "Task::WhenAll") == 0 &&
                builder->ir->types[instruction->result_type].element_type !=
                    IR_INVALID_ID &&
                builder->ir->types[builder->ir->types[
                    instruction->result_type].element_type].shape !=
                    IR_TYPE_UNIT,
                0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        if (instruction->symbol != NULL) {
            OpCode cancellation_op = OP_TRAP;
            if (strcmp(instruction->symbol,
                       "CancellationTokenSource::New") == 0)
                cancellation_op = OP_CANCELLATION_SOURCE_NEW;
            else if (strcmp(instruction->symbol,
                            "CancellationToken::None") == 0)
                cancellation_op = OP_CANCELLATION_TOKEN_NONE;
            else if (strcmp(instruction->symbol,
                            "CancellationTokenSource::Token") == 0)
                cancellation_op = OP_CANCELLATION_TOKEN_GET;
            else if (strcmp(instruction->symbol,
                            "CancellationTokenSource::Cancel") == 0)
                cancellation_op = OP_CANCELLATION_CANCEL;
            else if (strcmp(instruction->symbol,
                            "CancellationToken::IsCancellationRequested") == 0)
                cancellation_op = OP_CANCELLATION_IS_REQUESTED;
            else if (strcmp(instruction->symbol,
                            "CancellationToken::ThrowIfCancellationRequested") == 0)
                cancellation_op = OP_CANCELLATION_THROW_IF_REQUESTED;
            if (cancellation_op != OP_TRAP) {
                (void)emit_instruction(
                    builder, cancellation_op, 0, 0, instruction->span);
                store_result(builder, instruction);
                return;
            }
        }
        int32_t builtin = builtin_index(instruction->symbol);
        if (builtin == -13 && instruction->operand_count == 2U &&
            builder->ir->types[builder->source->value_types[
                instruction->operands[1]]].shape == IR_TYPE_CHAR)
            builtin = -89;
        size_t call_index;
        if (builtin != INT32_MIN) {
            call_index = emit_instruction(
                builder, OP_CALL, builtin, count,
                instruction->span);
        } else {
            LangValue unit = {.tag = LANG_VALUE_UNIT};
            size_t constant = add_constant(
                builder, unit, instruction->symbol,
                instruction->symbol_length);
            int32_t name_index;
            if (!as_i32(builder, constant,
                        instruction->span, &name_index))
                return;
            call_index = emit_instruction(
                builder, OP_CALL_NATIVE, name_index,
                count, instruction->span);
        }
        BytecodeCallSite *call_site =
            &builder->function->call_sites[call_index];
        call_site->argument_count = instruction->argument_mode_count;
        if (call_site->argument_count != 0U) {
            call_site->argument_modes = ir_bc_resize(
                NULL, call_site->argument_count,
                sizeof(*call_site->argument_modes));
            memcpy(call_site->argument_modes, instruction->argument_modes,
                   call_site->argument_count *
                       sizeof(*call_site->argument_modes));
        }
    }
    store_result(builder, instruction);
}

void lower_instruction(IrBytecodeBuilder *builder,
                              const IrInstruction *instruction) {
    int32_t index;
    switch (instruction->opcode) {
        case IR_OP_PARAMETER:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            {
            bool borrowed =
                instruction->index < builder->source->parameter_count &&
                parameter_mode_is_reference(
                    builder->source->parameters[instruction->index].mode);
            /*
             * VM parameter slots are already the function's first locals.
             * A borrowed parameter must remain an alias to that slot: copying
             * it through a temporary would create false ownership.
             */
            if (borrowed) return;
            (void)emit_instruction(
                builder, OP_MOVE_LOCAL, index, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
            }
        case IR_OP_UNIT:
            (void)emit_instruction(
                builder, OP_UNIT, 0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_CONST_BOOL:
            (void)emit_instruction(
                builder, instruction->integer != 0U
                    ? OP_TRUE : OP_FALSE,
                0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_CONST_INT: {
            const IrType *type =
                &builder->ir->types[instruction->result_type];
            LangValue value;
            if (type->shape == IR_TYPE_UNSIGNED_INT ||
                type->shape == IR_TYPE_CHAR) {
                value.tag = LANG_VALUE_U64;
                value.as.u64 = instruction->integer;
            } else {
                value.tag = LANG_VALUE_I64;
                value.as.i64 = (int64_t)instruction->integer;
            }
            size_t constant = add_constant(
                builder, value, NULL, 0U);
            if (!as_i32(builder, constant,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_CONSTANT, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_CONST_FLOAT: {
            LangValue value = {
                .tag = LANG_VALUE_F64,
                .as.f64 = instruction->floating
            };
            size_t constant = add_constant(
                builder, value, NULL, 0U);
            if (!as_i32(builder, constant,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_CONSTANT, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_CONST_STRING: {
            LangValue unit = {.tag = LANG_VALUE_UNIT};
            size_t constant = add_constant(
                builder, unit, instruction->symbol,
                instruction->symbol_length);
            if (!as_i32(builder, constant,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_CONSTANT, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_CONST_NULL: {
            LangValue value = {
                .tag = LANG_VALUE_RAW_POINTER,
                .as.pointer = NULL
            };
            size_t constant = add_constant(
                builder, value, NULL, 0U);
            if (!as_i32(builder, constant,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_CONSTANT, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_LOAD:
        case IR_OP_LOCAL_MOVE:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder,
                instruction->opcode == IR_OP_LOCAL_LOAD
                    ? OP_LOAD_LOCAL : OP_MOVE_LOCAL,
                index, 0, instruction->span);
            if (instruction->opcode == IR_OP_LOCAL_LOAD &&
                instruction->result != IR_INVALID_ID)
                builder->value_source_locals[instruction->result] =
                    instruction->index;
            store_result(builder, instruction);
            return;
        case IR_OP_STATIC_FIELD_LOAD:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_LOAD_STATIC, index, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_STATIC_FIELD_STORE:
            move_value(
                builder, instruction->operands[0], instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_STORE_STATIC, index, 0,
                instruction->span);
            return;
        case IR_OP_LOCAL_STORE:
            {
            if (instruction->index < builder->source->parameter_count &&
                parameter_mode_is_reference(
                    builder->source->parameters[instruction->index].mode) &&
                instruction->operand_count == 1U &&
                instruction->operands[0] == instruction->index)
                return;
            }
            move_value(
                builder, instruction->operands[0], instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_SET_LOCAL, index, 0, instruction->span);
            (void)emit_instruction(
                builder, OP_POP, 0, 0, instruction->span);
            return;
        case IR_OP_LOCAL_DROP:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_DROP_LOCAL, index, -1,
                instruction->span);
            return;
        case IR_OP_LOCAL_DEFAULT:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_DEFAULT_LOCAL, index, 0,
                instruction->span);
            return;
        case IR_OP_VALUE_CLONE:
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_CLONE,
                (int32_t)instruction->auxiliary, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_VALUE_DISCARD:
            if (instruction->auxiliary == 1U) {
                int32_t slot;
                if (as_i32(builder,
                           value_slot(builder, instruction->operands[0]),
                           instruction->span, &slot))
                    (void)emit_instruction(
                        builder, OP_INVALIDATE_LOCAL, slot, 0,
                        instruction->span);
            } else {
                move_value(
                    builder, instruction->operands[0], instruction->span);
                (void)emit_instruction(
                    builder, OP_POP, 0, 0, instruction->span);
            }
            return;
        case IR_OP_ADD_CHECKED: case IR_OP_SUB_CHECKED:
        case IR_OP_MUL_CHECKED: case IR_OP_DIV_CHECKED:
        case IR_OP_REM_CHECKED: case IR_OP_SHIFT_LEFT_CHECKED:
        case IR_OP_SHIFT_RIGHT_CHECKED:
        case IR_OP_BIT_AND: case IR_OP_BIT_OR: case IR_OP_BIT_XOR:
        case IR_OP_ADD_FLOAT: case IR_OP_SUB_FLOAT:
        case IR_OP_MUL_FLOAT: case IR_OP_DIV_FLOAT:
        case IR_OP_EQUAL: case IR_OP_NOT_EQUAL:
        case IR_OP_LESS: case IR_OP_LESS_EQUAL:
        case IR_OP_GREATER: case IR_OP_GREATER_EQUAL:
            {
            IrTypeId operand_type = builder->source->value_types[
                instruction->operands[0]];
            move_value(
                builder, instruction->operands[0], instruction->span);
            move_value(
                builder, instruction->operands[1], instruction->span);
            (void)emit_instruction(
                builder, arithmetic_opcode(instruction->opcode),
                (int32_t)vm_type_kind(
                    &builder->ir->types[operand_type]),
                0, instruction->span);
            store_result(builder, instruction);
            return;
            }
        case IR_OP_NEGATE: {
            IrTypeId operand_type = builder->source->value_types[
                instruction->operands[0]];
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder,
                builder->ir->types[operand_type].shape ==
                    IR_TYPE_FLOAT ? OP_NEG_F64 : OP_NEG_I64,
                (int32_t)vm_type_kind(
                    &builder->ir->types[operand_type]),
                0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_NOT:
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_NOT, 0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_BIT_NOT: {
            IrTypeId operand_type = builder->source->value_types[
                instruction->operands[0]];
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_BIT_NOT,
                (int32_t)vm_type_kind(
                    &builder->ir->types[operand_type]),
                0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_CAST: {
            IrTypeId source_type = builder->source->value_types[
                instruction->operands[0]];
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_CAST,
                (int32_t)vm_type_kind(
                    &builder->ir->types[source_type]),
                (int32_t)vm_type_kind(
                    &builder->ir->types[instruction->result_type]),
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_FUNCTION_REF:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_FUNCTION, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_BOUND_METHOD_REF:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_BOUND_FUNCTION, index,
                builder->ir->functions[instruction->index].is_virtual
                    ? 1 : 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_CALL_DIRECT:
        case IR_OP_CALL_VIRTUAL:
        case IR_OP_CALL_INDIRECT:
        case IR_OP_CALL_NATIVE:
            lower_call(builder, instruction);
            return;
        case IR_OP_AWAIT:
            move_value(builder, instruction->operands[0],
                       instruction->span);
            (void)emit_instruction(builder, OP_AWAIT, 0, 0,
                                   instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_EXCEPTION_SET:
            move_value(builder, instruction->operands[0],
                       instruction->span);
            (void)emit_instruction(builder, OP_EXCEPTION_SET, 0, 0,
                                   instruction->span);
            return;
        case IR_OP_EXCEPTION_PENDING:
            (void)emit_instruction(builder, OP_EXCEPTION_PENDING, 0, 0,
                                   instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_EXCEPTION_MATCH:
            index = add_symbol_constant(
                builder, instruction->symbol,
                strlen(instruction->symbol), instruction->span);
            (void)emit_instruction(
                builder, OP_EXCEPTION_MATCH, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_EXCEPTION_TAKE:
            (void)emit_instruction(builder, OP_EXCEPTION_TAKE, 0, 0,
                                   instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_AGGREGATE_MAKE: {
            const IrType *type =
                &builder->ir->types[instruction->result_type];
            for (size_t i = 0U;
                 i < instruction->operand_count; ++i)
                move_value(
                    builder, instruction->operands[i],
                    instruction->span);
            int32_t count;
            if (!as_i32(builder, instruction->operand_count,
                        instruction->span, &count))
                return;
            if (type->shape == IR_TYPE_ARRAY) {
                (void)emit_instruction(
                    builder, OP_MAKE_ARRAY, count, 0,
                    instruction->span);
            } else if (type->shape == IR_TYPE_STRUCT ||
                       type->shape == IR_TYPE_CLASS_REFERENCE) {
                int32_t metadata =
                    add_struct_metadata(builder, instruction);
                if (builder->failed) return;
                (void)emit_instruction(
                    builder,
                    type->shape == IR_TYPE_CLASS_REFERENCE
                        ? OP_MAKE_CLASS : OP_MAKE_STRUCT,
                    metadata, count,
                    instruction->span);
            } else if (type->shape == IR_TYPE_ENUM) {
                LangValue value = {
                    .tag = LANG_VALUE_U64,
                    .as.u64 = instruction->index
                };
                size_t constant = add_constant(
                    builder, value, NULL, 0U);
                if (!as_i32(builder, constant,
                            instruction->span, &index))
                    return;
                (void)emit_instruction(
                    builder, OP_CONSTANT, index, 0,
                    instruction->span);
            } else if (type->shape == IR_TYPE_UNION) {
                int32_t metadata = add_enum_metadata(
                    builder, type, instruction->index,
                    true, instruction->span);
                if (builder->failed) return;
                (void)emit_instruction(
                    builder, OP_MAKE_STRUCT, metadata, count,
                    instruction->span);
            } else {
                unsupported_instruction(builder, instruction);
                return;
            }
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_ENUM_IS: {
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            const IrType *type = &builder->ir->types[
                builder->source->locals[instruction->index].type];
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, local, 0,
                instruction->span);
            if (type->shape == IR_TYPE_ENUM) {
                LangValue value = {
                    .tag = LANG_VALUE_U64,
                    .as.u64 = instruction->auxiliary
                };
                size_t constant = add_constant(
                    builder, value, NULL, 0U);
                if (!as_i32(builder, constant,
                            instruction->span, &index))
                    return;
                (void)emit_instruction(
                    builder, OP_CONSTANT, index, 0,
                    instruction->span);
                (void)emit_instruction(
                    builder, OP_EQ, (int32_t)TYPE_U32, 0,
                    instruction->span);
                store_result(builder, instruction);
                return;
            }
            (void)emit_instruction(
                builder, OP_GET_TAG, 0, 0, instruction->span);
            int32_t tag = add_enum_metadata(
                builder, type, instruction->auxiliary,
                false, instruction->span);
            if (builder->failed) return;
            (void)emit_instruction(
                builder, OP_CONSTANT, tag, 0, instruction->span);
            (void)emit_instruction(
                builder, OP_EQ, 0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_MOVE_LOCAL, index, 0,
                instruction->span);
            (void)emit_instruction(
                builder, OP_TAKE_PAYLOAD, 0, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_ENUM_IS: {
            int32_t source_slot;
            if (!as_i32(builder,
                        value_slot(builder, instruction->operands[0]),
                        instruction->span, &source_slot))
                return;
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, source_slot, 0,
                instruction->span);
            (void)emit_instruction(
                builder, OP_GET_TAG, 0, 0, instruction->span);
            const IrType *type = &builder->ir->types[
                builder->source->value_types[instruction->operands[0]]];
            int32_t tag = add_enum_metadata(
                builder, type, instruction->auxiliary,
                false, instruction->span);
            if (builder->failed) return;
            (void)emit_instruction(
                builder, OP_CONSTANT, tag, 0, instruction->span);
            (void)emit_instruction(
                builder, OP_EQ, 0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_ENUM_PAYLOAD_BORROW: {
            int32_t source_slot;
            if (!as_i32(builder,
                        value_slot(builder, instruction->operands[0]),
                        instruction->span, &source_slot))
                return;
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, source_slot, 0,
                instruction->span);
            (void)emit_instruction(
                builder, OP_BORROW_PAYLOAD, 0, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_COLLECTION_COUNT: {
            int32_t source_slot;
            if (!as_i32(builder,
                        value_slot(builder, instruction->operands[0]),
                        instruction->span, &source_slot))
                return;
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, source_slot, 0,
                instruction->span);
            (void)emit_instruction(
                builder, OP_COLLECTION_COUNT, 0, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LIST_ELEMENT_BORROW: {
            int32_t source_slot;
            if (!as_i32(builder,
                        value_slot(builder, instruction->operands[0]),
                        instruction->span, &source_slot))
                return;
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, source_slot, 0,
                instruction->span);
            move_value(builder, instruction->operands[1],
                       instruction->span);
            (void)emit_instruction(
                builder, OP_LIST_ELEMENT_BORROW, 0, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_QUEUE_FRONT_BORROW:
        case IR_OP_STACK_TOP_BORROW: {
            int32_t source_slot;
            if (!as_i32(builder,
                        value_slot(builder, instruction->operands[0]),
                        instruction->span, &source_slot))
                return;
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, source_slot, 0,
                instruction->span);
            (void)emit_instruction(
                builder,
                instruction->opcode == IR_OP_QUEUE_FRONT_BORROW
                    ? OP_QUEUE_FRONT_BORROW
                    : OP_STACK_TOP_BORROW,
                0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_DICTIONARY_GET_BORROW: {
            int32_t source_slot;
            if (!as_i32(builder,
                        value_slot(builder, instruction->operands[0]),
                        instruction->span, &source_slot))
                return;
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, source_slot, 0,
                instruction->span);
            move_value(builder, instruction->operands[1],
                       instruction->span);
            (void)emit_instruction(
                builder, OP_DICTIONARY_GET_BORROW, 0, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_DICTIONARY_FIND: {
            int32_t source_slot;
            if (!as_i32(builder,
                        value_slot(builder, instruction->operands[0]),
                        instruction->span, &source_slot))
                return;
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, source_slot, 0,
                instruction->span);
            move_value(builder, instruction->operands[1],
                       instruction->span);
            (void)emit_instruction(
                builder, OP_DICTIONARY_FIND, 0, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_DICTIONARY_KEY_BORROW:
        case IR_OP_DICTIONARY_VALUE_BORROW: {
            int32_t source_slot;
            if (!as_i32(builder,
                        value_slot(builder, instruction->operands[0]),
                        instruction->span, &source_slot))
                return;
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, source_slot, 0,
                instruction->span);
            move_value(builder, instruction->operands[1],
                       instruction->span);
            (void)emit_instruction(
                builder,
                instruction->opcode == IR_OP_DICTIONARY_KEY_BORROW
                    ? OP_DICTIONARY_KEY_BORROW
                    : OP_DICTIONARY_VALUE_BORROW,
                0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_ITERATOR_BEGIN:
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_ITER_INIT, 0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_BORROWED_ITERATOR_BEGIN:
            if (instruction->operand_count == 1U) {
                move_value(
                    builder, instruction->operands[0],
                    instruction->span);
                (void)emit_instruction(
                    builder, OP_ITER_INIT, 0, 1,
                    instruction->span);
                store_result(builder, instruction);
                return;
            }
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_ITER_BORROW_LOCAL, index, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_LOCAL_ITERATOR_HAS_NEXT:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_ITER_HAS_NEXT_LOCAL, index, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_LOCAL_ITERATOR_NEXT:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_ITER_TAKE_NEXT_LOCAL, index, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_CLASS_DELETE:
            move_value(builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_DELETE_CLASS, 0, 0, instruction->span);
            return;
        case IR_OP_RAW_ALLOC:
        case IR_OP_RAW_STORE:
            move_value(
                builder, instruction->operands[0], instruction->span);
            move_value(
                builder, instruction->operands[1], instruction->span);
            {
            size_t call_index = emit_instruction(
                builder, OP_CALL,
                instruction->opcode == IR_OP_RAW_ALLOC ? -6 : -9,
                2, instruction->span);
            BytecodeCallSite *call_site =
                &builder->function->call_sites[call_index];
            call_site->argument_count = 2U;
            call_site->argument_modes = ir_bc_resize(
                NULL, 2U, sizeof(*call_site->argument_modes));
            call_site->argument_modes[0] = PARAMETER_MODE_VALUE;
            call_site->argument_modes[1] = PARAMETER_MODE_VALUE;
            }
            store_result(builder, instruction);
            return;
        case IR_OP_RAW_LOAD:
            move_value(
                builder, instruction->operands[0], instruction->span);
            {
            size_t call_index = emit_instruction(
                builder, OP_CALL, -8, 1, instruction->span);
            BytecodeCallSite *call_site =
                &builder->function->call_sites[call_index];
            call_site->argument_count = 1U;
            call_site->argument_modes = ir_bc_resize(
                NULL, 1U, sizeof(*call_site->argument_modes));
            call_site->argument_modes[0] = PARAMETER_MODE_VALUE;
            }
            store_result(builder, instruction);
            return;
        case IR_OP_ELEMENT_BEGIN:
            {
            int32_t parent = 0;
            if (instruction->index != IR_INVALID_ID) {
                if (!as_i32(
                        builder, instruction->index,
                        instruction->span, &parent))
                    return;
                ++parent;
            }
            if (instruction->symbol_length == 9U &&
                memcmp(
                    instruction->symbol, "#fragment", 9U) == 0) {
                (void)emit_instruction(
                    builder, OP_HTML_FRAGMENT, 0, parent,
                    instruction->span);
            } else {
                int32_t tag = add_symbol_constant(
                    builder, instruction->symbol,
                    instruction->symbol_length, instruction->span);
                (void)emit_instruction(
                    builder, OP_HTML_BEGIN, tag, parent,
                    instruction->span);
            }
            store_result(builder, instruction);
            return;
            }
        case IR_OP_LOCAL_ELEMENT_PROPERTY: {
            move_value(
                builder, instruction->operands[0], instruction->span);
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            int32_t property = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_HTML_ATTR_LOCAL, local, property,
                instruction->span);
            return;
        }
        case IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN: {
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            int32_t property = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_HTML_ATTR_BEGIN_LOCAL,
                local, property, instruction->span);
            return;
        }
        case IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND:
            move_value(
                builder, instruction->operands[0],
                instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_ATTR_APPEND_LOCAL,
                index, 0, instruction->span);
            return;
        case IR_OP_LOCAL_ELEMENT_CSS_VALUE:
            move_value(
                builder, instruction->operands[0],
                instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_CSS_VALUE_LOCAL,
                index, 0, instruction->span);
            return;
        case IR_OP_LOCAL_ELEMENT_PROPERTY_END:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_ATTR_END_LOCAL,
                index, 0, instruction->span);
            return;
        case IR_OP_LOCAL_ELEMENT_APPEND:
            move_value(
                builder, instruction->operands[0], instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_APPEND_LOCAL, index, 0,
                instruction->span);
            return;
        case IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT: {
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            int32_t text = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, instruction->auxiliary != 0U
                    ? OP_HTML_APPEND_RAW_CONSTANT_LOCAL
                    : OP_HTML_APPEND_CONSTANT_LOCAL,
                index, text, instruction->span);
            return;
        }
        case IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED:
            move_value(
                builder, instruction->operands[0],
                instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_APPEND_FORMATTED_LOCAL,
                index, 0, instruction->span);
            return;
        case IR_OP_LOCAL_ELEMENT_FINISH:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_FINISH_LOCAL, index, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_FIELD_GET: {
            if (instruction->auxiliary == 1U) {
                int32_t source_slot;
                if (!as_i32(
                        builder,
                        value_slot(builder, instruction->operands[0]),
                        instruction->span, &source_slot))
                    return;
                (void)emit_instruction(
                    builder, OP_LOAD_LOCAL, source_slot, 0,
                    instruction->span);
            } else {
                move_value(
                    builder, instruction->operands[0],
                    instruction->span);
            }
            int32_t field = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder,
                (instruction->auxiliary == 1U ||
                 instruction->auxiliary == 2U)
                    ? OP_GET_FIELD_BORROW : OP_GET_FIELD,
                field, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_FIELD_GET: {
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            int32_t field = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_GET_FIELD_LOCAL, local, field,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_FIELD_MOVE: {
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            int32_t field = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_GET_FIELD_LOCAL_MOVE, local, field,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_FIELD_BORROW: {
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, local, 0,
                instruction->span);
            int32_t field = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            if (instruction->result != IR_INVALID_ID) {
                builder->value_source_locals[instruction->result] =
                    instruction->index;
                builder->value_source_fields[instruction->result] = field;
            }
            (void)emit_instruction(
                builder, OP_GET_FIELD_BORROW, field, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_FIELD_SET: {
            move_value(
                builder, instruction->operands[0], instruction->span);
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            int32_t field = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_SET_FIELD_LOCAL, local, field,
                instruction->span);
            (void)emit_instruction(
                builder, OP_POP, 0, 0, instruction->span);
            return;
        }
        case IR_OP_LOCAL_FIELD_DEFAULT: {
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            int32_t field = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_DEFAULT_FIELD_LOCAL, local, field,
                instruction->span);
            return;
        }
        case IR_OP_INDEX_GET:
            if (instruction->integer == 1U) {
                int32_t source_slot;
                if (!as_i32(
                        builder,
                        value_slot(builder, instruction->operands[0]),
                        instruction->span, &source_slot))
                    return;
                (void)emit_instruction(
                    builder, OP_LOAD_LOCAL, source_slot, 0,
                    instruction->span);
            } else {
                move_value(
                    builder, instruction->operands[0],
                    instruction->span);
            }
            move_value(
                builder, instruction->operands[1], instruction->span);
            (void)emit_instruction(
                builder, OP_GET_INDEX,
                instruction->auxiliary == 1U ? 1 : 0,
                instruction->integer != 0U ? 1 : 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_LOCAL_INDEX_GET:
            move_value(
                builder, instruction->operands[0], instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_GET_INDEX_LOCAL, index,
                instruction->auxiliary == 1U ? 1 : 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_LOCAL_INDEX_SET:
            move_value(
                builder, instruction->operands[0], instruction->span);
            move_value(
                builder, instruction->operands[1], instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_SET_INDEX_LOCAL, index,
                instruction->auxiliary == 1U ? 1 : 0,
                instruction->span);
            (void)emit_instruction(
                builder, OP_POP, 0, 0, instruction->span);
            return;
        default:
            unsupported_instruction(builder, instruction);
            return;
    }
}
