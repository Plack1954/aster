#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static unsigned type_width(const CEmitter *emitter,
                           const IrType *type) {
    if (type->shape == IR_TYPE_ENUM) return 32U;
    return type->bit_width != 0U
         ? type->bit_width
         : (unsigned)emitter->ir->target.pointer_size * 8U;
}

static void emit_signed_bounds(CEmitter *emitter,
                               const IrType *type) {
    unsigned width = type_width(emitter, type);
    if (width >= 64U) {
        fputs("INT64_MIN, INT64_MAX", emitter->output);
    } else {
        uint64_t magnitude = UINT64_C(1) << (width - 1U);
        fprintf(emitter->output,
                "-INT64_C(%" PRIu64 "), INT64_C(%" PRIu64 ")",
                magnitude, magnitude - 1U);
    }
}

static void emit_checked_integer(CEmitter *emitter,
                                 const IrInstruction *instruction,
                                 const char *signed_name,
                                 const char *unsigned_name) {
    const IrType *type =
        &emitter->ir->types[instruction->result_type];
    fprintf(emitter->output, "    v%" PRIu32 " = %s(",
            instruction->result,
            type->shape == IR_TYPE_SIGNED_INT
                ? signed_name : unsigned_name);
    emit_operands(emitter, instruction);
    if (type->shape == IR_TYPE_SIGNED_INT) {
        fputs(", ", emitter->output);
        emit_signed_bounds(emitter, type);
    } else {
        unsigned width = type_width(emitter, type);
        if (width >= 64U)
            fputs(", UINT64_MAX", emitter->output);
        else
            fprintf(emitter->output, ", UINT64_C(%" PRIu64 ")",
                    (UINT64_C(1) << width) - 1U);
    }
    fputs(");\n", emitter->output);
}

void c_backend_emit_operation_instruction(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction
) {
    FILE *output = emitter->output;
    switch (instruction->opcode) {
        case IR_OP_ADD_CHECKED:
            emit_checked_integer(
                emitter, instruction,
                "aster_add_s", "aster_add_u");
            return;
        case IR_OP_SUB_CHECKED:
            emit_checked_integer(
                emitter, instruction,
                "aster_sub_s", "aster_sub_u");
            return;
        case IR_OP_MUL_CHECKED:
            emit_checked_integer(
                emitter, instruction,
                "aster_mul_s", "aster_mul_u");
            return;
        case IR_OP_DIV_CHECKED:
        case IR_OP_REM_CHECKED: {
            const IrType *type =
                &emitter->ir->types[instruction->result_type];
            const char *name =
                instruction->opcode == IR_OP_DIV_CHECKED
                    ? "aster_div" : "aster_rem";
            fprintf(output, "    v%" PRIu32 " = %s_%c(",
                    instruction->result, name,
                    type->shape == IR_TYPE_SIGNED_INT ? 's' : 'u');
            emit_operands(emitter, instruction);
            if (type->shape == IR_TYPE_SIGNED_INT) {
                fputs(", ", output);
                emit_signed_bounds(emitter, type);
            }
            fputs(");\n", output);
            return;
        }
        case IR_OP_SHIFT_LEFT_CHECKED:
        case IR_OP_SHIFT_RIGHT_CHECKED: {
            const IrType *type =
                &emitter->ir->types[instruction->result_type];
            fprintf(output, "    v%" PRIu32 " = aster_shift_%c(",
                    instruction->result,
                    type->shape == IR_TYPE_SIGNED_INT ? 's' : 'u');
            fprintf(output, "v%" PRIu32 ", (uint64_t)v%" PRIu32,
                    instruction->operands[0],
                    instruction->operands[1]);
            fprintf(output, ", %uU, %s",
                    type_width(emitter, type),
                    instruction->opcode == IR_OP_SHIFT_RIGHT_CHECKED
                        ? "true" : "false");
            if (type->shape == IR_TYPE_SIGNED_INT) {
                fputs(", ", output);
                emit_signed_bounds(emitter, type);
            } else {
                unsigned width = type_width(emitter, type);
                if (width >= 64U)
                    fputs(", UINT64_MAX", output);
                else
                    fprintf(output, ", UINT64_C(%" PRIu64 ")",
                            (UINT64_C(1) << width) - 1U);
            }
            fputs(");\n", output);
            return;
        }
        case IR_OP_BIT_AND:
        case IR_OP_BIT_OR:
        case IR_OP_BIT_XOR: {
            static const char *operators[] = {"&", "|", "^"};
            size_t index =
                (size_t)instruction->opcode -
                (size_t)IR_OP_BIT_AND;
            const IrType *type =
                &emitter->ir->types[instruction->result_type];
            fprintf(output, "    v%" PRIu32 " = (",
                    instruction->result);
            c_backend_emit_type(emitter, instruction->result_type);
            fprintf(output,
                    ")aster_bits_%c("
                    "(uint64_t)v%" PRIu32 " %s "
                    "(uint64_t)v%" PRIu32 ", %uU);\n",
                    type->shape == IR_TYPE_SIGNED_INT ? 's' : 'u',
                    instruction->operands[0], operators[index],
                    instruction->operands[1],
                    type_width(emitter, type));
            return;
        }
        case IR_OP_ADD_FLOAT:
        case IR_OP_SUB_FLOAT:
        case IR_OP_MUL_FLOAT:
        case IR_OP_DIV_FLOAT: {
            static const char *operators[] = {"+", "-", "*", "/"};
            size_t index =
                (size_t)instruction->opcode -
                (size_t)IR_OP_ADD_FLOAT;
            fprintf(output,
                    "    v%" PRIu32 " = v%" PRIu32 " %s v%" PRIu32 ";\n",
                    instruction->result, instruction->operands[0],
                    operators[index], instruction->operands[1]);
            return;
        }
        case IR_OP_EQUAL:
        case IR_OP_NOT_EQUAL:
        case IR_OP_LESS:
        case IR_OP_LESS_EQUAL:
        case IR_OP_GREATER:
        case IR_OP_GREATER_EQUAL: {
            static const char *operators[] = {
                "==", "!=", "<", "<=", ">", ">="
            };
            size_t index =
                (size_t)instruction->opcode -
                (size_t)IR_OP_EQUAL;
            const IrType *operand_type =
                &emitter->ir->types[
                    function->value_types[instruction->operands[0]]];
            if (operand_type->shape == IR_TYPE_STRING_VIEW &&
                (instruction->opcode == IR_OP_EQUAL ||
                 instruction->opcode == IR_OP_NOT_EQUAL)) {
                fprintf(output,
                        "    v%" PRIu32 " = %saster_str_equal("
                        "v%" PRIu32 ", v%" PRIu32 ");\n",
                        instruction->result,
                        instruction->opcode == IR_OP_NOT_EQUAL ? "!" : "",
                        instruction->operands[0],
                        instruction->operands[1]);
            } else if (operand_type->shape == IR_TYPE_UNION &&
                       (instruction->opcode == IR_OP_EQUAL ||
                        instruction->opcode == IR_OP_NOT_EQUAL)) {
                const IrInstruction *left =
                    c_backend_find_value_producer(
                        function, instruction->operands[0]);
                const IrInstruction *right =
                    c_backend_find_value_producer(
                        function, instruction->operands[1]);
                bool left_payloadless =
                    left != NULL &&
                    left->opcode == IR_OP_AGGREGATE_MAKE &&
                    left->index < operand_type->variant_count &&
                    operand_type->variant_payload_types[left->index] ==
                        IR_INVALID_ID;
                bool right_payloadless =
                    right != NULL &&
                    right->opcode == IR_OP_AGGREGATE_MAKE &&
                    right->index < operand_type->variant_count &&
                    operand_type->variant_payload_types[right->index] ==
                        IR_INVALID_ID;
                if (!left_payloadless && !right_payloadless) {
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "union equality without a payloadless variant");
                    return;
                }
                fprintf(output,
                        "    v%" PRIu32 " = v%" PRIu32
                        ".tag %s v%" PRIu32 ".tag;\n",
                        instruction->result,
                        instruction->operands[0],
                        instruction->opcode == IR_OP_NOT_EQUAL
                            ? "!=" : "==",
                        instruction->operands[1]);
                if (c_backend_type_needs_drop(
                        emitter,
                        function->value_types[
                            instruction->operands[0]])) {
                    c_backend_emit_drop_call(
                        emitter,
                        function->value_types[
                            instruction->operands[0]],
                        "v", instruction->operands[0]);
                    fputs(";\n", output);
                    c_backend_emit_drop_call(
                        emitter,
                        function->value_types[
                            instruction->operands[1]],
                        "v", instruction->operands[1]);
                    fputs(";\n", output);
                }
            } else if (operand_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                       strcmp(operand_type->name, "string") == 0 &&
                       (instruction->opcode == IR_OP_EQUAL ||
                        instruction->opcode == IR_OP_NOT_EQUAL)) {
                fprintf(output,
                        "    v%" PRIu32 " = %saster_str_equal("
                        "aster_string_as_str(v%" PRIu32 "), "
                        "aster_string_as_str(v%" PRIu32 "));\n"
                        "    aster_string_drop(v%" PRIu32 ");\n"
                        "    aster_string_drop(v%" PRIu32 ");\n",
                        instruction->result,
                        instruction->opcode == IR_OP_NOT_EQUAL ? "!" : "",
                        instruction->operands[0],
                        instruction->operands[1],
                        instruction->operands[0],
                        instruction->operands[1]);
            } else {
                fprintf(output,
                        "    v%" PRIu32 " = v%" PRIu32
                        " %s v%" PRIu32 ";\n",
                        instruction->result, instruction->operands[0],
                        operators[index], instruction->operands[1]);
            }
            return;
        }
        case IR_OP_NEGATE:
            if (emitter->ir->types[
                    instruction->result_type].shape == IR_TYPE_FLOAT) {
                fprintf(output,
                        "    v%" PRIu32 " = -v%" PRIu32 ";\n",
                        instruction->result,
                        instruction->operands[0]);
            } else {
                fprintf(output,
                        "    v%" PRIu32 " = aster_neg_s(v%" PRIu32 ", ",
                        instruction->result,
                        instruction->operands[0]);
                emit_signed_bounds(
                    emitter,
                    &emitter->ir->types[instruction->result_type]);
                fputs(");\n", output);
            }
            return;
        case IR_OP_NOT:
            fprintf(output, "    v%" PRIu32 " = !v%" PRIu32 ";\n",
                    instruction->result, instruction->operands[0]);
            return;
        case IR_OP_CAST: {
            const IrType *source =
                &emitter->ir->types[
                    function->value_types[instruction->operands[0]]];
            const IrType *target =
                &emitter->ir->types[instruction->result_type];
            bool source_integer =
                source->shape == IR_TYPE_SIGNED_INT ||
                source->shape == IR_TYPE_UNSIGNED_INT ||
                source->shape == IR_TYPE_CHAR;
            bool target_integer =
                target->shape == IR_TYPE_SIGNED_INT ||
                target->shape == IR_TYPE_UNSIGNED_INT ||
                target->shape == IR_TYPE_CHAR;
            if (target->shape == IR_TYPE_FLOAT &&
                (source_integer || source->shape == IR_TYPE_FLOAT)) {
                const char *maximum = type_width(emitter, target) == 32U
                    ? "FLT_MAX" : "DBL_MAX";
                fprintf(output,
                        "    ;\n"
                        "    double cast_value_%" PRIu32
                        " = (double)v%" PRIu32 ";\n"
                        "    if (cast_value_%" PRIu32
                        " != cast_value_%" PRIu32
                        " || cast_value_%" PRIu32 " > %s"
                        " || cast_value_%" PRIu32 " < -%s)\n"
                        "        aster_trap(\"numeric cast is out of range\");\n"
                        "    v%" PRIu32 " = (",
                        instruction->result,
                        instruction->operands[0],
                        instruction->result,
                        instruction->result,
                        instruction->result, maximum,
                        instruction->result, maximum,
                        instruction->result);
                c_backend_emit_type(emitter, instruction->result_type);
                fprintf(output,
                        ")cast_value_%" PRIu32 ";\n",
                        instruction->result);
                if (target->shape == IR_TYPE_CHAR)
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " >= UINT64_C(0xd800) && "
                            "(uint64_t)v%" PRIu32
                            " <= UINT64_C(0xdfff))\n"
                            "        aster_trap(\"numeric cast is out of range\");\n",
                            instruction->result,
                            instruction->result);
                return;
            }
            if (source->shape == IR_TYPE_FLOAT && target_integer) {
                unsigned width = type_width(emitter, target);
                fprintf(output,
                        "    ;\n"
                        "    double cast_value_%" PRIu32
                        " = (double)v%" PRIu32 ";\n"
                        "    if (cast_value_%" PRIu32
                        " != cast_value_%" PRIu32,
                        instruction->result,
                        instruction->operands[0],
                        instruction->result,
                        instruction->result);
                if (target->shape == IR_TYPE_SIGNED_INT) {
                    if (width >= 64U) {
                        fprintf(output,
                                " || cast_value_%" PRIu32
                                " < -0x1p63 || cast_value_%" PRIu32
                                " >= 0x1p63",
                                instruction->result,
                                instruction->result);
                    } else {
                        int64_t minimum =
                            -(int64_t)(UINT64_C(1) << (width - 1U));
                        uint64_t upper =
                            UINT64_C(1) << (width - 1U);
                        fprintf(output,
                                " || cast_value_%" PRIu32
                                " < %.1f || cast_value_%" PRIu32
                                " >= %.1f",
                                instruction->result, (double)minimum,
                                instruction->result, (double)upper);
                    }
                } else {
                    uint64_t upper = target->shape == IR_TYPE_CHAR
                        ? UINT64_C(0x110000)
                        : width >= 64U ? UINT64_MAX
                        : UINT64_C(1) << width;
                    fprintf(output,
                            " || cast_value_%" PRIu32
                            " < 0.0 || cast_value_%" PRIu32 " >= ",
                            instruction->result,
                            instruction->result);
                    if (width >= 64U && target->shape != IR_TYPE_CHAR)
                        fputs("0x1p64", output);
                    else
                        fprintf(output, "%.1f", (double)upper);
                }
                fputs(")\n"
                      "        aster_trap(\"numeric cast is out of range\");\n",
                      output);
                fprintf(output,
                        "    v%" PRIu32 " = (",
                        instruction->result);
                c_backend_emit_type(emitter, instruction->result_type);
                fprintf(output,
                        ")cast_value_%" PRIu32 ";\n",
                        instruction->result);
                return;
            }
            if (!source_integer || !target_integer) {
                c_backend_unsupported(emitter, instruction->span,
                            "this numeric cast");
                return;
            }
            unsigned width = type_width(emitter, target);
            if (target->shape == IR_TYPE_SIGNED_INT) {
                uint64_t maximum = width >= 64U
                    ? (uint64_t)INT64_MAX
                    : (UINT64_C(1) << (width - 1U)) - 1U;
                int64_t minimum = width >= 64U
                    ? INT64_MIN
                    : -(int64_t)(UINT64_C(1) << (width - 1U));
                if (source->shape == IR_TYPE_SIGNED_INT) {
                    fprintf(output, "    if (v%" PRIu32 " < ",
                            instruction->operands[0]);
                    if (width >= 64U)
                        fputs("INT64_MIN", output);
                    else
                        fprintf(output, "INT64_C(%" PRId64 ")", minimum);
                    fprintf(output, " || v%" PRIu32 " > ",
                            instruction->operands[0]);
                    if (width >= 64U)
                        fputs("INT64_MAX", output);
                    else
                        fprintf(output, "INT64_C(%" PRIu64 ")", maximum);
                    fputs(")\n"
                          "        aster_trap(\"numeric cast is out of range\");\n",
                          output);
                }
                else
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " > UINT64_C(%" PRIu64 "))\n"
                            "        aster_trap(\"numeric cast is out of range\");\n",
                            instruction->operands[0], maximum);
            } else {
                uint64_t maximum = target->shape == IR_TYPE_CHAR
                    ? UINT64_C(0x10ffff)
                    : width >= 64U
                        ? UINT64_MAX
                        : (UINT64_C(1) << width) - 1U;
                if (source->shape == IR_TYPE_SIGNED_INT)
                    fprintf(output,
                            "    if (v%" PRIu32 " < 0 || "
                            "(uint64_t)v%" PRIu32
                            " > UINT64_C(%" PRIu64 "))\n"
                            "        aster_trap(\"numeric cast is out of range\");\n",
                            instruction->operands[0],
                            instruction->operands[0], maximum);
                else
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " > UINT64_C(%" PRIu64 "))\n"
                            "        aster_trap(\"numeric cast is out of range\");\n",
                            instruction->operands[0], maximum);
                if (target->shape == IR_TYPE_CHAR)
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " >= UINT64_C(0xd800) && "
                            "(uint64_t)v%" PRIu32
                            " <= UINT64_C(0xdfff))\n"
                            "        aster_trap(\"numeric cast is out of range\");\n",
                            instruction->operands[0],
                            instruction->operands[0]);
            }
            fprintf(output,
                    "    v%" PRIu32 " = (",
                    instruction->result);
            c_backend_emit_type(emitter, instruction->result_type);
            fprintf(output, ")v%" PRIu32 ";\n",
                    instruction->operands[0]);
            return;
        }
        case IR_OP_BIT_NOT:
            {
            const IrType *type =
                &emitter->ir->types[instruction->result_type];
            fprintf(output, "    v%" PRIu32 " = (",
                    instruction->result);
            c_backend_emit_type(emitter, instruction->result_type);
            fprintf(output,
                    ")aster_bits_%c("
                    "~(uint64_t)v%" PRIu32 ", %uU);\n",
                    type->shape == IR_TYPE_SIGNED_INT ? 's' : 'u',
                    instruction->operands[0],
                    type_width(emitter, type));
            }
            return;
        case IR_OP_EXCEPTION_SET:
            {
            const IrType *exception_type = &emitter->ir->types[
                function->value_types[instruction->operands[0]]];
            fprintf(output,
                    "    if (aster_exception_pending) "
                    "aster_string_drop(aster_exception_message);\n"
                    "    aster_exception_message = v%" PRIu32 ".f0;\n"
                    "    v%" PRIu32 ".f0 = NULL;\n"
                    "    aster_exception_type = \"%s\";\n"
                    "    aster_exception_pending = true;\n",
                    instruction->operands[0], instruction->operands[0],
                    exception_type->name);
            }
            return;
        case IR_OP_EXCEPTION_PENDING:
            fprintf(output, "    v%" PRIu32
                    " = aster_exception_pending;\n", instruction->result);
            return;
        case IR_OP_EXCEPTION_MATCH:
            fprintf(output,
                    "    v%" PRIu32 " = strcmp(\"%s\", \"Exception\") == 0 "
                    "|| (aster_exception_type != NULL && "
                    "(strcmp(aster_exception_type, \"%s\") == 0 || "
                    "(strcmp(\"%s\", \"OperationCanceledException\") == 0 && "
                    "strcmp(aster_exception_type, \"TaskCanceledException\") == 0)));\n",
                    instruction->result, instruction->symbol,
                    instruction->symbol, instruction->symbol);
            return;
        case IR_OP_EXCEPTION_TAKE:
            fprintf(output,
                    "    if (!aster_exception_pending) "
                    "aster_trap(\"catch entered without an exception\");\n"
                    "    v%" PRIu32 ".f0 = aster_exception_message;\n"
                    "    aster_exception_message = NULL;\n"
                    "    aster_exception_type = NULL;\n"
                    "    aster_exception_pending = false;\n",
                    instruction->result);
            return;
        default:
            c_backend_unsupported(
                emitter, instruction->span,
                "this operation IR instruction");
            return;
    }
}
