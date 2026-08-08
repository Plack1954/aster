#include "internal.h"
#include "ir_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool same_assignment_place(const Expr *left, const Expr *right) {
    if (left == NULL || right == NULL || left->kind != right->kind)
        return false;
    switch (left->kind) {
        case EXPR_NAME:
            return left->resolved_local_id != 0U &&
                left->resolved_local_id == right->resolved_local_id;
        case EXPR_FIELD:
            return !left->as.field.static_field &&
                !right->as.field.static_field &&
                strcmp(left->as.field.field, right->as.field.field) == 0 &&
                same_assignment_place(
                    left->as.field.object, right->as.field.object);
        case EXPR_INDEX:
            return left->as.index.index->kind == EXPR_INT &&
                right->as.index.index->kind == EXPR_INT &&
                left->as.index.index->as.integer ==
                    right->as.index.index->as.integer &&
                same_assignment_place(
                    left->as.index.object, right->as.index.object);
        default:
            return false;
    }
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
            bool move = expr->type != NULL &&
                expr->type->kind != TYPE_CLASS &&
                expr->type->kind != TYPE_RAW_POINTER &&
                (expr->type->requires_cleanup || expr->type->managed ||
                 ir_type_requires_custom_copy(builder, expr->type));
            bool transfer = move &&
                builder->module->types[type].copy_policy !=
                    IR_COPY_NONCOPYABLE;
            instruction = ir_append_instruction(
                builder, transfer ? IR_OP_LOCAL_TRANSFER
                                  : move ? IR_OP_LOCAL_MOVE
                                         : IR_OP_LOCAL_LOAD,
                type, NULL, 0U, expr->span);
            if (instruction != NULL) instruction->index = local;
            if (transfer)
                ir_set_transfer_exception_context(
                    builder, instruction, &expr->error_cleanup);
            if (!move && instruction != NULL &&
                load_requires_clone(expr->type)) {
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
            IrValueId borrowed_values[2][64];
            size_t borrowed_counts[2] = {0U, 0U};
            IrValueId operands[2];
            operands[0] = expr->as.binary.borrow_left
                ? lower_borrowed_expr(
                      builder, expr->as.binary.left,
                      borrowed_values[0], &borrowed_counts[0])
                : ir_lower_expr(builder, expr->as.binary.left);
            operands[1] = expr->as.binary.borrow_right
                ? lower_borrowed_expr(
                      builder, expr->as.binary.right,
                      borrowed_values[1], &borrowed_counts[1])
                : ir_lower_expr(builder, expr->as.binary.right);
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
            IrValueId binary_result = instruction != NULL
                ? instruction->result : IR_INVALID_ID;
            if (instruction != NULL)
                instruction->auxiliary =
                    (expr->as.binary.borrow_left ? 1U : 0U) |
                    (expr->as.binary.borrow_right ? 2U : 0U);
            if (expr->as.binary.borrow_left)
                discard_local_place_borrows(
                    builder, borrowed_values[0],
                    borrowed_counts[0], expr->span);
            if (expr->as.binary.borrow_right)
                discard_local_place_borrows(
                    builder, borrowed_values[1],
                    borrowed_counts[1], expr->span);
            return binary_result;
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
            if (compound == TOK_ERROR &&
                same_assignment_place(target, expr->as.assign.value))
                return ir_emit_unit(builder, expr->span, expr->type);
            if (target->kind == EXPR_FIELD &&
                target->as.field.static_field) {
                uint32_t field = ir_static_field_index(
                    builder->module, target->resolved_decl,
                    target->as.field.field);
                IrValueId value;
                if (compound == TOK_ERROR) {
                    value = ir_lower_expr(builder, expr->as.assign.value);
                } else {
                    IrInstruction *old = ir_append_instruction(
                        builder, IR_OP_STATIC_FIELD_LOAD,
                        ir_intern_type(builder->module, target->type),
                        NULL, 0U, target->span);
                    if (old != NULL) old->index = field;
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
                    builder, IR_OP_STATIC_FIELD_STORE,
                    IR_INVALID_ID, &value, 1U, expr->span);
                if (instruction != NULL) instruction->index = field;
            } else if (target->kind == EXPR_UNARY &&
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
                if (strcmp(target->as.name, "_") == 0) {
                    IrValueId value = ir_lower_expr(
                        builder, expr->as.assign.value);
                    (void)ir_append_instruction(
                        builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
                        &value, 1U, expr->span);
                    instruction = ir_append_instruction(
                        builder, IR_OP_UNIT,
                        ir_intern_type(builder->module, &ir_unit_type),
                        NULL, 0U, expr->span);
                    break;
                }
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
        case EXPR_COPY: {
            IrValueId operand;
            const Expr *source = expr->as.copy.value;
            bool local_place = expression_is_local_place(source);
            IrValueId borrowed_values[64];
            size_t borrowed_count = 0U;
            if (local_place)
                operand = lower_local_place_borrow(
                    builder, source,
                    borrowed_values, &borrowed_count);
            else
                operand = ir_lower_expr(builder, source);
            if (!local_place &&
                (source->kind == EXPR_FIELD ||
                 source->kind == EXPR_INDEX ||
                 (source->kind == EXPR_CALL &&
                 source->as.call.callee->kind == EXPR_NAME &&
                 (strcmp(source->as.call.callee->as.name,
                         "List::Get") == 0 ||
                  strcmp(source->as.call.callee->as.name,
                         "Queue::Peek") == 0 ||
                  strcmp(source->as.call.callee->as.name,
                         "Stack::Peek") == 0 ||
                  strcmp(source->as.call.callee->as.name,
                         "Dictionary::Get") == 0 ||
                  strcmp(source->as.call.callee->as.name,
                         "Dictionary::KeyAt") == 0 ||
                  strcmp(source->as.call.callee->as.name,
                         "Dictionary::ValueAt") == 0))))
                return operand;
            IrValueId copied;
            if (ir_type_requires_custom_copy(builder, expr->type)) {
                copied = ir_emit_recursive_copy(
                    builder, expr->type, operand, expr->span, true,
                    &expr->error_cleanup);
            } else {
                instruction = ir_append_instruction(
                    builder, IR_OP_VALUE_CLONE, type,
                    &operand, 1U, expr->span);
                if (instruction != NULL)
                    instruction->auxiliary = local_place ? 0U : 1U;
                copied = instruction != NULL
                    ? instruction->result : IR_INVALID_ID;
            }
            if (local_place) {
                discard_local_place_borrows(
                    builder, borrowed_values,
                    borrowed_count, expr->span);
            } else if (ir_type_requires_custom_copy(
                           builder, expr->type)) {
                IrInstruction *discard = ir_append_instruction(
                    builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
                    &operand, 1U, expr->span);
                (void)discard;
            }
            return copied;
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
            pending->index = result;
            IrBlockId exceptional = ir_add_block(builder->function);
            IrBlockId continuation = ir_add_block(builder->function);
            ir_set_terminator(
                builder, IR_TERM_BRANCH, pending->result,
                exceptional, continuation, expr->span);
            builder->current = exceptional;
            if (builder->exception_count != 0U) {
                ir_emit_temporary_cleanups(builder, expr->span);
                ir_emit_cleanup(
                    builder, &expr->error_cleanup, expr->span);
                ir_set_terminator(
                    builder, IR_TERM_JUMP, IR_INVALID_ID,
                    builder->exceptions[
                        builder->exception_count - 1U].handler,
                    IR_INVALID_ID, expr->span);
            } else {
                ir_emit_function_cleanup(builder, expr->span);
                ir_set_terminator(
                    builder, IR_TERM_PROPAGATE_EXCEPTION,
                    IR_INVALID_ID, IR_INVALID_ID,
                    IR_INVALID_ID, expr->span);
            }
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
            if (expression_is_local_place(expr)) {
                IrValueId borrowed_values[64];
                size_t borrowed_count = 0U;
                IrValueId borrowed = lower_local_place_borrow(
                    builder, expr, borrowed_values, &borrowed_count);
                if (borrowed == IR_INVALID_ID) return IR_INVALID_ID;
                IrValueId copied;
                if (ir_type_requires_custom_copy(builder, expr->type))
                    copied = ir_emit_recursive_copy(
                        builder, expr->type, borrowed,
                        expr->span, true, &expr->error_cleanup);
                else
                    copied = emit_plain_clone(
                        builder, expr->type, borrowed, expr->span);
                discard_local_place_borrows(
                    builder, borrowed_values,
                    borrowed_count, expr->span);
                return copied;
            }
            if (ir_type_requires_custom_copy(builder, expr->type)) {
                IrValueId index = ir_lower_expr(
                    builder, expr->as.index.index);
                IrValueId aggregate = ir_lower_expr(
                    builder, expr->as.index.object);
                IrValueId operands[2] = {aggregate, index};
                IrInstruction *borrow = ir_append_instruction(
                    builder, IR_OP_INDEX_GET, type,
                    operands, 2U, expr->span);
                if (borrow == NULL) return IR_INVALID_ID;
                borrow->auxiliary =
                    expr->as.index.unchecked ? 1U : 0U;
                borrow->integer = 1U;
                IrValueId copied = ir_emit_recursive_copy(
                    builder, expr->type, borrow->result,
                    expr->span, false, &expr->error_cleanup);
                IrInstruction *discard = ir_append_instruction(
                    builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
                    &aggregate, 1U, expr->span);
                (void)discard;
                return copied;
            }
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
            if (expr->as.field.static_field) {
                instruction = ir_append_instruction(
                    builder, IR_OP_STATIC_FIELD_LOAD, type,
                    NULL, 0U, expr->span);
                if (instruction != NULL)
                    instruction->index = ir_static_field_index(
                        builder->module, expr->resolved_decl,
                        expr->as.field.field);
                break;
            }
            const Expr *object = expr->as.field.object;
            if (expr->as.field.bound_method &&
                expr->resolved_decl != NULL &&
                expr->resolved_decl->kind == DECL_FUNCTION) {
                IrValueId receiver = ir_lower_expr(builder, object);
                instruction = ir_append_instruction(
                    builder, IR_OP_BOUND_METHOD_REF, type,
                    &receiver, 1U, expr->span);
                if (instruction != NULL) {
                    instruction->symbol =
                        expr->resolved_decl->as.function.name;
                    instruction->symbol_length = strlen(instruction->symbol);
                    instruction->index = ir_find_function(
                        builder->module, expr->resolved_decl);
                }
                break;
            }
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
            if (expr->as.field.move_out &&
                object->kind == EXPR_NAME) {
                uint32_t local = ir_find_local(
                    builder, object->resolved_local_id, object->span);
                instruction = ir_append_instruction(
                    builder,
                    builder->module->types[type].copy_policy !=
                            IR_COPY_NONCOPYABLE
                        ? IR_OP_LOCAL_FIELD_TRANSFER
                        : IR_OP_LOCAL_FIELD_MOVE,
                    type, NULL, 0U, expr->span);
                if (instruction == NULL) return IR_INVALID_ID;
                instruction->index = local;
                instruction->auxiliary = ir_field_index(
                    object->type, expr->as.field.field);
                instruction->symbol = expr->as.field.field;
                instruction->symbol_length = strlen(instruction->symbol);
                if (instruction->opcode == IR_OP_LOCAL_FIELD_TRANSFER)
                    ir_set_transfer_exception_context(
                        builder, instruction, &expr->error_cleanup);
                if (instruction->opcode == IR_OP_LOCAL_FIELD_MOVE) {
                    IrInstruction *drop = ir_append_instruction(
                        builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                        NULL, 0U, expr->span);
                    if (drop != NULL) drop->index = local;
                }
                return instruction->result;
            }
            if (expression_is_local_place(expr)) {
                IrValueId borrowed_values[64];
                size_t borrowed_count = 0U;
                IrValueId borrowed = lower_local_place_borrow(
                    builder, expr, borrowed_values, &borrowed_count);
                if (borrowed == IR_INVALID_ID) return IR_INVALID_ID;
                IrValueId copied;
                if (ir_type_requires_custom_copy(builder, expr->type))
                    copied = ir_emit_recursive_copy(
                        builder, expr->type, borrowed,
                        expr->span, true, &expr->error_cleanup);
                else
                    copied = emit_plain_clone(
                        builder, expr->type, borrowed, expr->span);
                discard_local_place_borrows(
                    builder, borrowed_values,
                    borrowed_count, expr->span);
                return copied;
            }
            uint32_t field = ir_field_index(
                object->type, expr->as.field.field);
            if (ir_type_requires_custom_copy(builder, expr->type)) {
                IrValueId borrowed;
                IrValueId owner = IR_INVALID_ID;
                bool owns_owner = false;
                if (object->kind == EXPR_NAME) {
                    IrInstruction *projection = ir_append_instruction(
                        builder, IR_OP_LOCAL_FIELD_BORROW, type,
                        NULL, 0U, expr->span);
                    if (projection == NULL) return IR_INVALID_ID;
                    projection->index = ir_find_local(
                        builder, object->resolved_local_id,
                        object->span);
                    projection->auxiliary = field;
                    projection->symbol = expr->as.field.field;
                    projection->symbol_length =
                        strlen(projection->symbol);
                    borrowed = projection->result;
                } else {
                    owner = ir_lower_expr(builder, object);
                    IrInstruction *projection = ir_append_instruction(
                        builder, IR_OP_FIELD_GET, type,
                        &owner, 1U, expr->span);
                    if (projection == NULL) return IR_INVALID_ID;
                    projection->index = field;
                    projection->auxiliary = 1U;
                    projection->symbol = expr->as.field.field;
                    projection->symbol_length =
                        strlen(projection->symbol);
                    borrowed = projection->result;
                    owns_owner = true;
                }
                IrValueId copied = ir_emit_recursive_copy(
                    builder, expr->type, borrowed,
                    expr->span, false, &expr->error_cleanup);
                if (owns_owner) {
                    IrInstruction *discard = ir_append_instruction(
                        builder, IR_OP_VALUE_DISCARD,
                        IR_INVALID_ID, &owner, 1U, expr->span);
                    (void)discard;
                }
                return copied;
            }
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
