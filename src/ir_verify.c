#include "ir_internal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

const char *ir_opcode_name(IrOpcode opcode) {
    static const char *names[] = {
        "parameter", "unit", "const_bool", "const_int", "const_float",
        "const_string", "const_null", "local_load", "local_move",
        "local_store", "local_drop", "static_field_load",
        "static_field_store", "value_clone", "value_discard",
        "add_checked", "sub_checked", "mul_checked", "div_checked",
        "rem_checked", "shift_left_checked", "shift_right_checked",
        "bit_and", "bit_or", "bit_xor", "bit_not",
        "add_float", "sub_float", "mul_float", "div_float", "negate",
        "not", "equal", "not_equal", "less", "less_equal", "greater",
        "greater_equal", "cast", "function_ref", "bound_method_ref",
        "call_direct", "call_virtual",
        "call_indirect", "call_native", "await", "exception_set",
        "exception_pending", "exception_match", "exception_take",
        "aggregate_make", "field_get",
        "field_set", "local_field_get", "local_field_move",
        "local_field_borrow",
        "local_field_set", "index_get",
        "index_set", "local_index_get", "local_index_set",
        "local_enum_is", "local_enum_payload_move", "iterator_begin",
        "borrowed_iterator_begin",
        "local_iterator_has_next", "local_iterator_next", "raw_alloc",
        "raw_load", "raw_store", "class_delete", "element_begin",
        "local_element_property",
        "local_element_property_begin",
        "local_element_property_append",
        "local_element_css_value",
        "local_element_property_end",
        "local_element_append",
        "local_element_append_static_text",
        "local_element_append_formatted",
        "local_element_finish"
    };
    _Static_assert(sizeof(names) / sizeof(names[0]) == IR_OP_COUNT,
                   "IR opcode names must cover every opcode");
    size_t index = (size_t)opcode;
    return index < sizeof(names) / sizeof(names[0])
         ? names[index] : "<invalid-opcode>";
}

static bool opcode_valid(IrOpcode opcode) {
    return (unsigned)opcode < (unsigned)IR_OP_COUNT;
}

static bool verify_type(const IrModule *ir, IrTypeId type) {
    return type != IR_INVALID_ID && (size_t)type < ir->type_count;
}

static bool valid_parameter_mode(ParameterMode mode) {
    return mode >= PARAMETER_MODE_VALUE && mode <= PARAMETER_MODE_OUT;
}

static bool valid_copy_policy(IrCopyPolicy policy) {
    return policy >= IR_COPY_TRIVIAL && policy <= IR_COPY_CUSTOM;
}

static bool valid_drop_policy(IrDropPolicy policy) {
    return policy >= IR_DROP_TRIVIAL && policy <= IR_DROP_CUSTOM;
}

static bool type_shape_valid(IrTypeShape shape) {
    switch (shape) {
        case IR_TYPE_ERROR: case IR_TYPE_UNIT: case IR_TYPE_NEVER:
        case IR_TYPE_BOOL: case IR_TYPE_SIGNED_INT:
        case IR_TYPE_UNSIGNED_INT: case IR_TYPE_FLOAT:
        case IR_TYPE_CHAR: case IR_TYPE_STRING_VIEW:
        case IR_TYPE_BUILTIN_OBJECT: case IR_TYPE_ARRAY:
        case IR_TYPE_RAW_POINTER: case IR_TYPE_SLICE:
        case IR_TYPE_ITERATOR: case IR_TYPE_ELEMENT_BUILDER:
        case IR_TYPE_FUNCTION: case IR_TYPE_STRUCT:
        case IR_TYPE_CLASS_REFERENCE:
        case IR_TYPE_ENUM: case IR_TYPE_UNION:
            return true;
    }
    return false;
}

static bool verify_value(const IrFunction *function, IrValueId value) {
    return value != IR_INVALID_ID && (size_t)value < function->value_count;
}

static bool ir_type_assignable_inner(
    const IrModule *ir, IrTypeId expected, IrTypeId actual, size_t depth
) {
    if (expected == actual) return true;
    if (!verify_type(ir, expected) || !verify_type(ir, actual) ||
        ir->types[expected].shape != IR_TYPE_CLASS_REFERENCE ||
        ir->types[actual].shape != IR_TYPE_CLASS_REFERENCE ||
        depth >= ir->type_count)
        return false;
    if (ir_type_assignable_inner(
            ir, expected, ir->types[actual].base_type, depth + 1U))
        return true;
    for (size_t interface = 0U;
         interface < ir->types[actual].interface_count; ++interface)
        if (ir_type_assignable_inner(
                ir, expected,
                ir->types[actual].interface_types[interface],
                depth + 1U))
            return true;
    return false;
}

static bool ir_type_assignable(
    const IrModule *ir, IrTypeId expected, IrTypeId actual
) {
    return ir_type_assignable_inner(ir, expected, actual, 0U);
}

static bool value_assignable(
    const IrModule *ir, const IrFunction *function,
    IrValueId value, IrTypeId expected
) {
    return verify_value(function, value) &&
        ir_type_assignable(
            ir, expected, function->value_types[value]);
}

static bool operand_count_valid(const IrInstruction *instruction) {
    switch (instruction->opcode) {
        case IR_OP_PARAMETER: case IR_OP_UNIT: case IR_OP_CONST_BOOL:
        case IR_OP_CONST_INT: case IR_OP_CONST_FLOAT:
        case IR_OP_CONST_STRING: case IR_OP_CONST_NULL:
        case IR_OP_LOCAL_LOAD: case IR_OP_LOCAL_MOVE:
        case IR_OP_LOCAL_DROP: case IR_OP_STATIC_FIELD_LOAD:
        case IR_OP_FUNCTION_REF:
        case IR_OP_LOCAL_FIELD_GET: case IR_OP_LOCAL_FIELD_MOVE:
        case IR_OP_LOCAL_FIELD_BORROW:
        case IR_OP_LOCAL_ENUM_IS:
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE:
        case IR_OP_LOCAL_ITERATOR_HAS_NEXT:
        case IR_OP_LOCAL_ITERATOR_NEXT:
        case IR_OP_ELEMENT_BEGIN:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_END:
        case IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT:
        case IR_OP_LOCAL_ELEMENT_FINISH:
        case IR_OP_EXCEPTION_PENDING:
        case IR_OP_EXCEPTION_MATCH:
        case IR_OP_EXCEPTION_TAKE:
            return instruction->operand_count == 0U;
        case IR_OP_BORROWED_ITERATOR_BEGIN:
            return instruction->operand_count <= 1U;
        case IR_OP_LOCAL_STORE: case IR_OP_STATIC_FIELD_STORE:
        case IR_OP_VALUE_CLONE:
        case IR_OP_VALUE_DISCARD: case IR_OP_NEGATE: case IR_OP_NOT:
        case IR_OP_BIT_NOT: case IR_OP_BOUND_METHOD_REF:
        case IR_OP_CAST: case IR_OP_FIELD_GET: case IR_OP_FIELD_SET:
        case IR_OP_LOCAL_FIELD_SET: case IR_OP_LOCAL_INDEX_GET:
        case IR_OP_ITERATOR_BEGIN:
        case IR_OP_RAW_LOAD:
        case IR_OP_CLASS_DELETE:
        case IR_OP_LOCAL_ELEMENT_PROPERTY:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND:
        case IR_OP_LOCAL_ELEMENT_CSS_VALUE:
        case IR_OP_LOCAL_ELEMENT_APPEND:
        case IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED:
        case IR_OP_EXCEPTION_SET: case IR_OP_AWAIT:
            return instruction->operand_count == 1U;
        case IR_OP_ADD_CHECKED: case IR_OP_SUB_CHECKED:
        case IR_OP_MUL_CHECKED: case IR_OP_DIV_CHECKED:
        case IR_OP_REM_CHECKED: case IR_OP_SHIFT_LEFT_CHECKED:
        case IR_OP_SHIFT_RIGHT_CHECKED:
        case IR_OP_BIT_AND: case IR_OP_BIT_OR: case IR_OP_BIT_XOR:
        case IR_OP_ADD_FLOAT:
        case IR_OP_SUB_FLOAT: case IR_OP_MUL_FLOAT: case IR_OP_DIV_FLOAT:
        case IR_OP_EQUAL: case IR_OP_NOT_EQUAL: case IR_OP_LESS:
        case IR_OP_LESS_EQUAL: case IR_OP_GREATER:
        case IR_OP_GREATER_EQUAL: case IR_OP_INDEX_GET:
        case IR_OP_INDEX_SET: case IR_OP_LOCAL_INDEX_SET:
        case IR_OP_RAW_ALLOC: case IR_OP_RAW_STORE:
            return instruction->operand_count == 2U;
        case IR_OP_CALL_DIRECT: case IR_OP_CALL_VIRTUAL:
        case IR_OP_CALL_INDIRECT:
        case IR_OP_CALL_NATIVE: case IR_OP_AGGREGATE_MAKE:
            return true;
        case IR_OP_COUNT:
            return false;
    }
    return false;
}

static bool result_type_valid(const IrModule *ir,
                              const IrFunction *function,
                              const IrInstruction *instruction) {
    if (instruction->opcode != IR_OP_CALL_NATIVE &&
        instruction->native_call != NULL)
        return false;
    bool produces_result;
    switch (instruction->opcode) {
        case IR_OP_LOCAL_STORE: case IR_OP_LOCAL_DROP:
        case IR_OP_STATIC_FIELD_STORE:
        case IR_OP_VALUE_DISCARD: case IR_OP_EXCEPTION_SET:
        case IR_OP_FIELD_SET: case IR_OP_LOCAL_FIELD_SET:
        case IR_OP_INDEX_SET: case IR_OP_LOCAL_INDEX_SET:
        case IR_OP_LOCAL_ELEMENT_PROPERTY:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND:
        case IR_OP_LOCAL_ELEMENT_CSS_VALUE:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_END:
        case IR_OP_LOCAL_ELEMENT_APPEND:
        case IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT:
        case IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED:
        case IR_OP_CLASS_DELETE:
            produces_result = false;
            break;
        case IR_OP_PARAMETER: case IR_OP_UNIT: case IR_OP_CONST_BOOL:
        case IR_OP_CONST_INT: case IR_OP_CONST_FLOAT:
        case IR_OP_CONST_STRING: case IR_OP_CONST_NULL:
        case IR_OP_LOCAL_LOAD: case IR_OP_LOCAL_MOVE:
        case IR_OP_STATIC_FIELD_LOAD:
        case IR_OP_VALUE_CLONE:
        case IR_OP_ADD_CHECKED: case IR_OP_SUB_CHECKED:
        case IR_OP_MUL_CHECKED: case IR_OP_DIV_CHECKED:
        case IR_OP_REM_CHECKED: case IR_OP_SHIFT_LEFT_CHECKED:
        case IR_OP_SHIFT_RIGHT_CHECKED: case IR_OP_BIT_AND:
        case IR_OP_BIT_OR: case IR_OP_BIT_XOR: case IR_OP_BIT_NOT:
        case IR_OP_ADD_FLOAT: case IR_OP_SUB_FLOAT:
        case IR_OP_MUL_FLOAT: case IR_OP_DIV_FLOAT:
        case IR_OP_NEGATE: case IR_OP_NOT: case IR_OP_EQUAL:
        case IR_OP_NOT_EQUAL: case IR_OP_LESS: case IR_OP_LESS_EQUAL:
        case IR_OP_GREATER: case IR_OP_GREATER_EQUAL: case IR_OP_CAST:
        case IR_OP_FUNCTION_REF: case IR_OP_BOUND_METHOD_REF:
        case IR_OP_CALL_DIRECT: case IR_OP_CALL_VIRTUAL:
        case IR_OP_CALL_INDIRECT: case IR_OP_CALL_NATIVE:
        case IR_OP_AWAIT: case IR_OP_EXCEPTION_PENDING:
        case IR_OP_EXCEPTION_MATCH: case IR_OP_EXCEPTION_TAKE:
        case IR_OP_AGGREGATE_MAKE: case IR_OP_FIELD_GET:
        case IR_OP_LOCAL_FIELD_GET: case IR_OP_LOCAL_FIELD_MOVE:
        case IR_OP_LOCAL_FIELD_BORROW: case IR_OP_INDEX_GET:
        case IR_OP_LOCAL_INDEX_GET: case IR_OP_LOCAL_ENUM_IS:
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE:
        case IR_OP_ITERATOR_BEGIN: case IR_OP_BORROWED_ITERATOR_BEGIN:
        case IR_OP_LOCAL_ITERATOR_HAS_NEXT:
        case IR_OP_LOCAL_ITERATOR_NEXT: case IR_OP_RAW_ALLOC:
        case IR_OP_RAW_LOAD: case IR_OP_RAW_STORE:
        case IR_OP_ELEMENT_BEGIN:
        case IR_OP_LOCAL_ELEMENT_FINISH:
            produces_result = true;
            break;
        case IR_OP_COUNT:
        default:
            return false;
    }
    if (produces_result != (instruction->result != IR_INVALID_ID) ||
        produces_result !=
            (instruction->result_type != IR_INVALID_ID))
        return false;
    if (!produces_result) return true;
    if (!verify_type(ir, instruction->result_type)) return false;
    IrTypeShape shape = ir->types[instruction->result_type].shape;
    switch (instruction->opcode) {
        case IR_OP_UNIT:
            return shape == IR_TYPE_UNIT;
        case IR_OP_CONST_BOOL:
        case IR_OP_NOT:
        case IR_OP_EQUAL:
        case IR_OP_NOT_EQUAL:
        case IR_OP_LESS:
        case IR_OP_LESS_EQUAL:
        case IR_OP_GREATER:
        case IR_OP_GREATER_EQUAL:
        case IR_OP_LOCAL_ENUM_IS:
        case IR_OP_LOCAL_ITERATOR_HAS_NEXT:
        case IR_OP_EXCEPTION_PENDING:
        case IR_OP_EXCEPTION_MATCH:
            return shape == IR_TYPE_BOOL;
        case IR_OP_CONST_INT:
            return shape == IR_TYPE_SIGNED_INT ||
                   shape == IR_TYPE_UNSIGNED_INT ||
                   shape == IR_TYPE_CHAR;
        case IR_OP_CONST_FLOAT:
            return shape == IR_TYPE_FLOAT;
        case IR_OP_CONST_STRING:
            return shape == IR_TYPE_STRING_VIEW;
        case IR_OP_CONST_NULL:
            return shape == IR_TYPE_RAW_POINTER ||
                   shape == IR_TYPE_CLASS_REFERENCE;
        case IR_OP_RAW_ALLOC:
            return shape == IR_TYPE_RAW_POINTER;
        case IR_OP_RAW_STORE:
            return shape == IR_TYPE_UNIT;
        case IR_OP_PARAMETER:
            return instruction->index < function->parameter_count &&
                   instruction->result_type ==
                       function->parameters[instruction->index].type;
        case IR_OP_AGGREGATE_MAKE:
            return shape == IR_TYPE_ARRAY || shape == IR_TYPE_STRUCT ||
                   shape == IR_TYPE_CLASS_REFERENCE ||
                   shape == IR_TYPE_ENUM ||
                   shape == IR_TYPE_UNION ||
                   shape == IR_TYPE_BUILTIN_OBJECT;
        case IR_OP_ELEMENT_BEGIN:
            return shape == IR_TYPE_ELEMENT_BUILDER;
        default:
            return true;
    }
}

static bool value_is_type(const IrFunction *function,
                          IrValueId value, IrTypeId type) {
    return verify_value(function, value) &&
           function->value_types[value] == type;
}

static bool value_type(const IrModule *ir, const IrFunction *function,
                       IrValueId value, IrTypeId *type) {
    if (!verify_value(function, value)) return false;
    IrTypeId found = function->value_types[value];
    if (!verify_type(ir, found)) return false;
    if (type != NULL) *type = found;
    return true;
}

static bool shape_is_integer(IrTypeShape shape) {
    return shape == IR_TYPE_SIGNED_INT ||
           shape == IR_TYPE_UNSIGNED_INT;
}

static bool shape_is_numeric(IrTypeShape shape) {
    return shape_is_integer(shape) || shape == IR_TYPE_FLOAT;
}

static bool values_match_result(const IrModule *ir,
                                const IrFunction *function,
                                const IrInstruction *instruction) {
    if (!verify_type(ir, instruction->result_type)) return false;
    for (size_t i = 0U; i < instruction->operand_count; ++i)
        if (!value_is_type(function, instruction->operands[i],
                           instruction->result_type))
            return false;
    return true;
}

static bool verify_element_child_type(
    const IrModule *ir, IrTypeId type_id) {
    if (!verify_type(ir, type_id)) return false;
    const IrType *type = &ir->types[type_id];
    if (type->shape == IR_TYPE_STRING_VIEW ||
        (type->name != NULL &&
         (strcmp(type->name, "string") == 0 ||
          strcmp(type->name, "Html") == 0)))
        return true;
    if (type->element_child_collection &&
        type->element_type != IR_INVALID_ID)
        return verify_element_child_type(
            ir, type->element_type);
    return false;
}

static bool instruction_signature_valid(
    const IrModule *ir, const IrFunction *function,
    const IrInstruction *instruction) {
    uint32_t local_index = instruction->index;
    const IrLocal *local =
        local_index < function->local_count
        ? &function->locals[local_index] : NULL;
    switch (instruction->opcode) {
        case IR_OP_PARAMETER:
            return instruction->index < function->parameter_count &&
                   instruction->result_type ==
                       function->parameters[instruction->index].type;
        case IR_OP_UNIT:
        case IR_OP_CONST_BOOL:
        case IR_OP_CONST_INT:
        case IR_OP_CONST_FLOAT:
        case IR_OP_CONST_NULL:
            return true;
        case IR_OP_CONST_STRING:
            return instruction->symbol != NULL;
        case IR_OP_LOCAL_DROP:
            return local != NULL;
        case IR_OP_VALUE_DISCARD:
            return value_type(ir, function, instruction->operands[0], NULL);
        case IR_OP_ADD_CHECKED:
        case IR_OP_SUB_CHECKED:
        case IR_OP_MUL_CHECKED:
        case IR_OP_DIV_CHECKED:
        case IR_OP_REM_CHECKED:
        case IR_OP_SHIFT_LEFT_CHECKED:
        case IR_OP_SHIFT_RIGHT_CHECKED: {
            if (!values_match_result(ir, function, instruction))
                return false;
            IrTypeShape shape =
                ir->types[instruction->result_type].shape;
            return shape_is_integer(shape);
        }
        case IR_OP_BIT_AND:
        case IR_OP_BIT_OR:
        case IR_OP_BIT_XOR: {
            if (!values_match_result(ir, function, instruction))
                return false;
            IrTypeShape shape =
                ir->types[instruction->result_type].shape;
            return shape_is_integer(shape) || shape == IR_TYPE_ENUM;
        }
        case IR_OP_BIT_NOT: {
            if (!values_match_result(ir, function, instruction))
                return false;
            IrTypeShape shape =
                ir->types[instruction->result_type].shape;
            return shape_is_integer(shape) || shape == IR_TYPE_ENUM;
        }
        case IR_OP_ADD_FLOAT:
        case IR_OP_SUB_FLOAT:
        case IR_OP_MUL_FLOAT:
        case IR_OP_DIV_FLOAT:
            return values_match_result(ir, function, instruction) &&
                   ir->types[instruction->result_type].shape ==
                       IR_TYPE_FLOAT;
        case IR_OP_NEGATE:
            return values_match_result(ir, function, instruction) &&
                   (ir->types[instruction->result_type].shape ==
                        IR_TYPE_SIGNED_INT ||
                    ir->types[instruction->result_type].shape ==
                        IR_TYPE_FLOAT);
        case IR_OP_NOT:
            return values_match_result(ir, function, instruction) &&
                   ir->types[instruction->result_type].shape ==
                       IR_TYPE_BOOL;
        case IR_OP_EQUAL:
        case IR_OP_NOT_EQUAL: {
            IrTypeId left;
            return value_type(ir, function, instruction->operands[0],
                              &left) &&
                   value_is_type(function, instruction->operands[1], left) &&
                   ir->types[instruction->result_type].shape == IR_TYPE_BOOL;
        }
        case IR_OP_LESS:
        case IR_OP_LESS_EQUAL:
        case IR_OP_GREATER:
        case IR_OP_GREATER_EQUAL: {
            IrTypeId left;
            return value_type(ir, function, instruction->operands[0],
                              &left) &&
                   value_is_type(function, instruction->operands[1], left) &&
                   shape_is_numeric(ir->types[left].shape) &&
                   ir->types[instruction->result_type].shape == IR_TYPE_BOOL;
        }
        case IR_OP_CAST: {
            IrTypeId source;
            return value_type(ir, function, instruction->operands[0],
                              &source) &&
                   (shape_is_numeric(ir->types[source].shape) ||
                    ir->types[source].shape == IR_TYPE_CHAR) &&
                   (shape_is_numeric(
                        ir->types[instruction->result_type].shape) ||
                    ir->types[instruction->result_type].shape == IR_TYPE_CHAR);
        }
        case IR_OP_FUNCTION_REF: {
            if (instruction->index >= ir->function_count ||
                !verify_type(ir, instruction->result_type))
                return false;
            const IrType *type = &ir->types[instruction->result_type];
            const IrFunction *target =
                &ir->functions[instruction->index];
            if (type->shape != IR_TYPE_FUNCTION ||
                type->argument_count != target->parameter_count ||
                (type->argument_count != 0U &&
                 (type->argument_types == NULL ||
                  type->parameter_modes == NULL)) ||
                type->element_type != target->return_type)
                return false;
            for (size_t i = 0U; i < type->argument_count; ++i)
                if (type->argument_types[i] != target->parameters[i].type ||
                    type->parameter_modes[i] != target->parameters[i].mode)
                    return false;
            return true;
        }
        case IR_OP_BOUND_METHOD_REF: {
            if (instruction->index >= ir->function_count ||
                !verify_type(ir, instruction->result_type))
                return false;
            IrTypeId receiver_type;
            if (!value_type(ir, function, instruction->operands[0],
                            &receiver_type))
                return false;
            const IrType *type = &ir->types[instruction->result_type];
            const IrFunction *target =
                &ir->functions[instruction->index];
            if (ir->types[receiver_type].shape !=
                    IR_TYPE_CLASS_REFERENCE ||
                type->shape != IR_TYPE_FUNCTION ||
                target->parameter_count == 0U ||
                !ir_type_assignable(
                    ir, target->parameters[0].type, receiver_type) ||
                type->argument_count + 1U != target->parameter_count ||
                type->element_type != target->return_type)
                return false;
            for (size_t i = 0U; i < type->argument_count; ++i)
                if (type->argument_types[i] !=
                        target->parameters[i + 1U].type ||
                    type->parameter_modes[i] !=
                        target->parameters[i + 1U].mode)
                    return false;
            return true;
        }
        case IR_OP_CALL_INDIRECT: {
            if (instruction->operand_count == 0U) return false;
            IrTypeId callee_type;
            if (!value_type(ir, function, instruction->operands[0],
                            &callee_type))
                return false;
            const IrType *callee = &ir->types[callee_type];
            if (callee->shape != IR_TYPE_FUNCTION ||
                (callee->argument_count != 0U &&
                 (callee->argument_types == NULL ||
                  callee->parameter_modes == NULL)) ||
                instruction->operand_count != callee->argument_count + 1U ||
                instruction->argument_mode_count != callee->argument_count ||
                (instruction->argument_mode_count != 0U &&
                 instruction->argument_modes == NULL) ||
                instruction->result_type != callee->element_type)
                return false;
            for (size_t i = 0U; i < callee->argument_count; ++i)
                if (!value_is_type(function, instruction->operands[i + 1U],
                                   callee->argument_types[i]) ||
                    instruction->argument_modes[i] !=
                        callee->parameter_modes[i])
                    return false;
            return true;
        }
        case IR_OP_CALL_NATIVE:
            if (instruction->symbol == NULL ||
                !verify_type(ir, instruction->result_type) ||
                instruction->native_call == NULL ||
                instruction->argument_mode_count !=
                    instruction->operand_count ||
                (instruction->argument_mode_count != 0U &&
                 instruction->argument_modes == NULL))
                return false;
            if (instruction->native_call->name != instruction->symbol ||
                instruction->native_call->return_type !=
                    instruction->result_type ||
                instruction->native_call->parameter_count !=
                    instruction->operand_count ||
                instruction->native_call->calling_convention !=
                    IR_CALLING_CONVENTION_NATIVE ||
                (instruction->operand_count != 0U &&
                 (instruction->native_call->parameter_types == NULL ||
                  instruction->native_call->parameter_modes == NULL)))
                return false;
            for (size_t i = 0U; i < instruction->operand_count; ++i)
                if (!value_is_type(
                        function, instruction->operands[i],
                        instruction->native_call->parameter_types[i]) ||
                    !valid_parameter_mode(instruction->argument_modes[i]) ||
                    instruction->native_call->parameter_modes[i] !=
                        instruction->argument_modes[i])
                    return false;
            return true;
        case IR_OP_EXCEPTION_SET: {
            IrTypeId exception;
            return value_type(ir, function, instruction->operands[0],
                              &exception) &&
                   ir->types[exception].shape == IR_TYPE_STRUCT;
        }
        case IR_OP_EXCEPTION_PENDING:
            return ir->types[instruction->result_type].shape == IR_TYPE_BOOL;
        case IR_OP_EXCEPTION_MATCH:
            return instruction->symbol != NULL &&
                   ir->types[instruction->result_type].shape == IR_TYPE_BOOL;
        case IR_OP_EXCEPTION_TAKE:
            return ir->types[instruction->result_type].shape == IR_TYPE_STRUCT;
        case IR_OP_ELEMENT_BEGIN:
            if (instruction->symbol == NULL ||
                !verify_type(ir, instruction->result_type) ||
                ir->types[instruction->result_type].shape !=
                    IR_TYPE_ELEMENT_BUILDER)
                return false;
            if (instruction->index == IR_INVALID_ID)
                return true;
            return local != NULL &&
                   verify_type(ir, local->type) &&
                   ir->types[local->type].shape ==
                       IR_TYPE_ELEMENT_BUILDER;
        case IR_OP_LOCAL_LOAD:
        case IR_OP_LOCAL_MOVE:
            return local != NULL &&
                   instruction->result_type == local->type;
        case IR_OP_LOCAL_STORE:
            return local != NULL &&
                   instruction->operand_count == 1U &&
                   value_assignable(
                       ir, function, instruction->operands[0], local->type);
        case IR_OP_STATIC_FIELD_LOAD:
            return instruction->index < ir->static_field_count &&
                   instruction->result_type ==
                       ir->static_fields[instruction->index].type;
        case IR_OP_STATIC_FIELD_STORE:
            return instruction->index < ir->static_field_count &&
                   value_is_type(
                       function, instruction->operands[0],
                       ir->static_fields[instruction->index].type);
        case IR_OP_LOCAL_ENUM_IS:
            return local != NULL &&
                   instruction->operand_count == 0U &&
                   verify_type(ir, local->type) &&
                   (ir->types[local->type].shape == IR_TYPE_ENUM ||
                    ir->types[local->type].shape == IR_TYPE_UNION) &&
                   instruction->auxiliary <
                       ir->types[local->type].variant_count;
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE: {
            if (local == NULL || !verify_type(ir, local->type) ||
                instruction->auxiliary == UINT32_MAX)
                return false;
            const IrType *source = &ir->types[local->type];
            if (source->shape != IR_TYPE_ENUM &&
                source->shape != IR_TYPE_UNION) return false;
            if (instruction->auxiliary >= source->variant_count)
                return false;
            IrTypeId expected =
                source->variant_payload_types[
                    instruction->auxiliary];
            return instruction->operand_count == 0U &&
                   expected != IR_INVALID_ID &&
                   instruction->result_type == expected;
        }
        case IR_OP_ITERATOR_BEGIN: {
            if (!verify_type(ir, instruction->result_type))
                return false;
            const IrType *iterator =
                &ir->types[instruction->result_type];
            return iterator->shape == IR_TYPE_ITERATOR &&
                   iterator->argument_count == 1U &&
                   iterator->argument_types != NULL &&
                   instruction->operand_count == 1U &&
                   value_is_type(
                       function, instruction->operands[0],
                       iterator->argument_types[0]);
        }
        case IR_OP_BORROWED_ITERATOR_BEGIN: {
            if (!verify_type(ir, instruction->result_type))
                return false;
            const IrType *iterator =
                &ir->types[instruction->result_type];
            if (iterator->shape != IR_TYPE_ITERATOR ||
                iterator->argument_count != 1U ||
                iterator->argument_types == NULL)
                return false;
            if (instruction->operand_count == 1U)
                return value_is_type(
                    function, instruction->operands[0],
                    iterator->argument_types[0]);
            return instruction->operand_count == 0U &&
                   local != NULL &&
                   iterator->argument_types[0] == local->type;
        }
        case IR_OP_VALUE_CLONE:
            return instruction->auxiliary <= 1U &&
                   instruction->operand_count == 1U &&
                   ir->types[instruction->result_type].copy_policy !=
                       IR_COPY_NONCOPYABLE &&
                   value_is_type(
                       function, instruction->operands[0],
                       instruction->result_type);
        case IR_OP_LOCAL_ITERATOR_HAS_NEXT:
        case IR_OP_LOCAL_ITERATOR_NEXT: {
            if (local == NULL || !verify_type(ir, local->type))
                return false;
            const IrType *iterator = &ir->types[local->type];
            if (iterator->shape != IR_TYPE_ITERATOR)
                return false;
            return instruction->opcode ==
                       IR_OP_LOCAL_ITERATOR_HAS_NEXT ||
                   instruction->result_type ==
                       iterator->element_type;
        }
        case IR_OP_LOCAL_ELEMENT_PROPERTY:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND:
        case IR_OP_LOCAL_ELEMENT_CSS_VALUE:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_END:
        case IR_OP_LOCAL_ELEMENT_APPEND:
        case IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT:
        case IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED:
        case IR_OP_LOCAL_ELEMENT_FINISH: {
            if (local == NULL || !verify_type(ir, local->type))
                return false;
            const IrType *element_builder =
                &ir->types[local->type];
            if (element_builder->shape !=
                IR_TYPE_ELEMENT_BUILDER)
                return false;
            if (instruction->opcode ==
                IR_OP_LOCAL_ELEMENT_FINISH)
                return instruction->result_type ==
                       element_builder->element_type;
            if (instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN)
                return instruction->operand_count == 0U &&
                       instruction->symbol != NULL;
            if (instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_PROPERTY_END)
                return instruction->operand_count == 0U;
            if (instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT)
                return instruction->operand_count == 0U &&
                       instruction->symbol != NULL;
            if (instruction->operand_count != 1U ||
                !verify_value(
                    function, instruction->operands[0]))
                return false;
            IrTypeId value_type_id;
            if (!value_type(ir, function, instruction->operands[0],
                            &value_type_id))
                return false;
            if (instruction->opcode ==
                IR_OP_LOCAL_ELEMENT_APPEND)
                return verify_element_child_type(
                    ir, value_type_id);
            if (instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND ||
                instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_CSS_VALUE ||
                instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED) {
                const IrType *value = &ir->types[value_type_id];
                return value->shape == IR_TYPE_STRING_VIEW ||
                       value->shape == IR_TYPE_BOOL ||
                       value->shape == IR_TYPE_SIGNED_INT ||
                       value->shape == IR_TYPE_UNSIGNED_INT ||
                       value->shape == IR_TYPE_FLOAT ||
                       value->shape == IR_TYPE_CHAR ||
                       (value->name != NULL && value->shape ==
                            IR_TYPE_BUILTIN_OBJECT &&
                        strcmp(value->name, "string") == 0);
            }
            return instruction->symbol != NULL;
        }
        case IR_OP_RAW_ALLOC:
            if (instruction->operand_count != 2U ||
                !verify_type(ir, instruction->result_type) ||
                ir->types[instruction->result_type].shape !=
                    IR_TYPE_RAW_POINTER ||
                !verify_value(function, instruction->operands[0]) ||
                !verify_value(function, instruction->operands[1]))
                return false;
            {
                IrTypeId owner =
                    function->value_types[instruction->operands[0]];
                IrTypeId size =
                    function->value_types[instruction->operands[1]];
                return verify_type(ir, owner) &&
                       verify_type(ir, size) &&
                       ir->types[owner].name != NULL &&
                       strcmp(ir->types[owner].name, "Arena") == 0 &&
                       (ir->types[size].shape == IR_TYPE_SIGNED_INT ||
                        ir->types[size].shape == IR_TYPE_UNSIGNED_INT);
            }
        case IR_OP_RAW_LOAD: {
            if (instruction->operand_count != 1U ||
                !verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId pointer_type =
                function->value_types[instruction->operands[0]];
            return verify_type(ir, pointer_type) &&
                   ir->types[pointer_type].shape ==
                       IR_TYPE_RAW_POINTER &&
                   instruction->result_type ==
                       ir->types[pointer_type].element_type;
        }
        case IR_OP_RAW_STORE: {
            if (instruction->operand_count != 2U ||
                !verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId pointer_type =
                function->value_types[instruction->operands[0]];
            return verify_type(ir, pointer_type) &&
                   ir->types[pointer_type].shape ==
                       IR_TYPE_RAW_POINTER &&
                   ir->types[pointer_type].pointer_mutable &&
                   value_is_type(
                       function, instruction->operands[1],
                       ir->types[pointer_type].element_type);
        }
        case IR_OP_LOCAL_FIELD_GET:
        case IR_OP_LOCAL_FIELD_MOVE:
        case IR_OP_LOCAL_FIELD_BORROW:
        case IR_OP_LOCAL_FIELD_SET: {
            if (local == NULL || !verify_type(ir, local->type))
                return false;
            const IrType *aggregate = &ir->types[local->type];
            if (aggregate->shape != IR_TYPE_STRUCT &&
                aggregate->shape != IR_TYPE_CLASS_REFERENCE)
                return false;
            if (instruction->auxiliary >= aggregate->field_count)
                return false;
            IrTypeId field_type =
                aggregate->field_types[instruction->auxiliary];
            if (instruction->opcode == IR_OP_LOCAL_FIELD_GET ||
                instruction->opcode == IR_OP_LOCAL_FIELD_MOVE ||
                instruction->opcode == IR_OP_LOCAL_FIELD_BORROW)
                return instruction->operand_count == 0U &&
                       instruction->result_type == field_type;
            return instruction->operand_count == 1U &&
                   value_is_type(
                       function, instruction->operands[0],
                       field_type);
        }
        case IR_OP_FIELD_GET: {
            if (instruction->operand_count != 1U ||
                !verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId aggregate_type =
                function->value_types[instruction->operands[0]];
            if (!verify_type(ir, aggregate_type))
                return false;
            const IrType *aggregate =
                &ir->types[aggregate_type];
            if (aggregate->name != NULL &&
                aggregate->shape == IR_TYPE_BUILTIN_OBJECT &&
                strcmp(aggregate->name, "Buffer") == 0)
                return instruction->index == IR_INVALID_ID &&
                       instruction->symbol != NULL &&
                       strcmp(instruction->symbol, "len") == 0 &&
                       ir->types[instruction->result_type].shape ==
                           IR_TYPE_SIGNED_INT &&
                       ir->types[instruction->result_type].bit_width == 64U;
            if (aggregate->shape != IR_TYPE_STRUCT &&
                aggregate->shape != IR_TYPE_CLASS_REFERENCE)
                return false;
            return instruction->index < aggregate->field_count &&
                   instruction->result_type ==
                       aggregate->field_types[instruction->index];
        }
        case IR_OP_LOCAL_INDEX_GET:
        case IR_OP_LOCAL_INDEX_SET: {
            if (local == NULL || !verify_type(ir, local->type))
                return false;
            const IrType *aggregate = &ir->types[local->type];
            if (aggregate->shape != IR_TYPE_ARRAY ||
                aggregate->element_type == IR_INVALID_ID ||
                instruction->operand_count !=
                    (instruction->opcode ==
                         IR_OP_LOCAL_INDEX_GET ? 1U : 2U) ||
                !verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId index_type;
            if (!value_type(ir, function, instruction->operands[0],
                            &index_type))
                return false;
            IrTypeShape index_shape = ir->types[index_type].shape;
            if (index_shape != IR_TYPE_SIGNED_INT &&
                index_shape != IR_TYPE_UNSIGNED_INT)
                return false;
            if (instruction->opcode == IR_OP_LOCAL_INDEX_GET)
                return instruction->result_type ==
                       aggregate->element_type;
            return instruction->operand_count == 2U &&
                   value_is_type(
                       function, instruction->operands[1],
                       aggregate->element_type);
        }
        case IR_OP_INDEX_GET: {
            if (instruction->operand_count != 2U ||
                !verify_value(function, instruction->operands[0]) ||
                !verify_value(function, instruction->operands[1]))
                return false;
            IrTypeId aggregate_type =
                function->value_types[instruction->operands[0]];
            if (!verify_type(ir, aggregate_type))
                return false;
            const IrType *aggregate =
                &ir->types[aggregate_type];
            IrTypeId index_type;
            if (!value_type(ir, function, instruction->operands[1],
                            &index_type))
                return false;
            IrTypeShape index_shape = ir->types[index_type].shape;
            return aggregate->shape == IR_TYPE_ARRAY &&
                   (index_shape == IR_TYPE_SIGNED_INT ||
                    index_shape == IR_TYPE_UNSIGNED_INT) &&
                   instruction->result_type ==
                       aggregate->element_type;
        }
        case IR_OP_AGGREGATE_MAKE: {
            if (!verify_type(ir, instruction->result_type))
                return false;
            const IrType *aggregate =
                &ir->types[instruction->result_type];
            if (aggregate->shape == IR_TYPE_ARRAY) {
                if (instruction->operand_count !=
                    aggregate->array_length)
                    return false;
                for (size_t i = 0U;
                     i < instruction->operand_count; ++i)
                    if (!value_is_type(
                            function, instruction->operands[i],
                            aggregate->element_type))
                        return false;
                return true;
            } else if (aggregate->shape == IR_TYPE_STRUCT ||
                       aggregate->shape == IR_TYPE_CLASS_REFERENCE) {
                if (aggregate->field_count !=
                        instruction->operand_count ||
                    instruction->label_count !=
                        instruction->operand_count)
                    return false;
                bool seen[256] = {false};
                if (aggregate->field_count > 256U)
                    return false;
                for (size_t i = 0U;
                     i < instruction->operand_count; ++i) {
                    uint32_t field = instruction->labels[i];
                    if (field >= aggregate->field_count ||
                        seen[field] ||
                        !value_is_type(
                            function, instruction->operands[i],
                            aggregate->field_types[field]))
                        return false;
                    seen[field] = true;
                }
                return true;
            } else if (aggregate->shape == IR_TYPE_ENUM ||
                       aggregate->shape == IR_TYPE_UNION) {
                if (instruction->index >=
                    aggregate->variant_count)
                    return false;
                IrTypeId payload =
                    aggregate->variant_payload_types[
                        instruction->index];
                if (payload == IR_INVALID_ID)
                    return instruction->operand_count == 0U;
                return instruction->operand_count == 1U &&
                       value_is_type(
                           function, instruction->operands[0],
                           payload);
            }
            return false;
        }
        case IR_OP_CALL_DIRECT:
        case IR_OP_CALL_VIRTUAL: {
            if (instruction->index >= ir->function_count)
                return false;
            const IrFunction *target =
                &ir->functions[instruction->index];
            if (instruction->render_destination !=
                    IR_INVALID_ID) {
                uint32_t destination =
                    instruction->render_destination;
                if (!target->has_render_root ||
                    destination >= function->local_count ||
                    !verify_type(
                        ir, function->locals[destination].type) ||
                    ir->types[
                        function->locals[destination].type].shape !=
                        IR_TYPE_ELEMENT_BUILDER)
                    return false;
            }
            if (instruction->operand_count != target->parameter_count ||
                instruction->argument_mode_count != target->parameter_count ||
                (instruction->argument_mode_count != 0U &&
                 instruction->argument_modes == NULL) ||
                instruction->result_type != target->return_type)
                return false;
            for (size_t i = 0U; i < instruction->operand_count; ++i)
                if (!value_assignable(
                        ir, function, instruction->operands[i],
                        target->parameters[i].type) ||
                    instruction->argument_modes[i] !=
                        target->parameters[i].mode)
                    return false;
            return true;
        }
        case IR_OP_CLASS_DELETE: {
            if (instruction->operand_count != 1U ||
                !verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId type = function->value_types[instruction->operands[0]];
            return verify_type(ir, type) &&
                   ir->types[type].shape == IR_TYPE_CLASS_REFERENCE;
        }
        case IR_OP_FIELD_SET:
        case IR_OP_INDEX_SET:
            /* Reserved opcodes have no lowering/backend contract yet. */
            return false;
        case IR_OP_AWAIT: {
            if (!function->is_async ||
                instruction->operand_count != 1U ||
                !verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId task_type = function->value_types[
                instruction->operands[0]];
            if (!verify_type(ir, task_type)) return false;
            const IrType *task = &ir->types[task_type];
            return task->name != NULL &&
                   task->shape == IR_TYPE_BUILTIN_OBJECT &&
                   strncmp(task->name, "Task", 4U) == 0 &&
                   task->element_type != IR_INVALID_ID &&
                   instruction->result_type == task->element_type;
        }
        case IR_OP_COUNT:
        default:
            return false;
    }
}

static size_t terminator_successor_count(const IrTerminator *term,
                                         size_t block_count) {
    if (term->kind == IR_TERM_JUMP)
        return term->target < block_count ? 1U : 0U;
    if (term->kind == IR_TERM_BRANCH)
        return term->target < block_count &&
               term->alternate < block_count ? 2U : 0U;
    return 0U;
}

static IrBlockId terminator_successor(const IrTerminator *term,
                                      size_t successor) {
    return successor == 0U ? term->target : term->alternate;
}

static void compute_dominators(const IrFunction *function,
                               bool *reachable, bool *dominators) {
    size_t count = function->block_count;
    IrBlockId *work = ir_resize(NULL, count, sizeof(*work));
    size_t work_count = 0U;
    reachable[function->entry_block] = true;
    work[work_count++] = function->entry_block;
    while (work_count != 0U) {
        IrBlockId block_id = work[--work_count];
        const IrTerminator *term =
            &function->blocks[block_id].terminator;
        size_t successors = terminator_successor_count(term, count);
        for (size_t i = 0U; i < successors; ++i) {
            IrBlockId next = terminator_successor(term, i);
            if (!reachable[next]) {
                reachable[next] = true;
                work[work_count++] = next;
            }
        }
    }
    free(work);

    for (size_t block = 0U; block < count; ++block) {
        if (!reachable[block]) {
            dominators[block * count + block] = true;
            continue;
        }
        if (block == function->entry_block) {
            dominators[block * count + block] = true;
            continue;
        }
        for (size_t candidate = 0U; candidate < count; ++candidate)
            dominators[block * count + candidate] = reachable[candidate];
    }

    bool changed;
    do {
        changed = false;
        for (size_t block = 0U; block < count; ++block) {
            if (!reachable[block] || block == function->entry_block)
                continue;
            for (size_t candidate = 0U; candidate < count; ++candidate) {
                bool dominates = true;
                bool has_predecessor = false;
                for (size_t predecessor = 0U;
                     predecessor < count; ++predecessor) {
                    if (!reachable[predecessor]) continue;
                    const IrTerminator *term =
                        &function->blocks[predecessor].terminator;
                    size_t successors =
                        terminator_successor_count(term, count);
                    bool is_predecessor = false;
                    for (size_t edge = 0U; edge < successors; ++edge)
                        if (terminator_successor(term, edge) == block)
                            is_predecessor = true;
                    if (!is_predecessor) continue;
                    has_predecessor = true;
                    if (!dominators[predecessor * count + candidate])
                        dominates = false;
                }
                if (!has_predecessor) dominates = false;
                if (candidate == block) dominates = true;
                size_t slot = block * count + candidate;
                if (dominators[slot] != dominates) {
                    dominators[slot] = dominates;
                    changed = true;
                }
            }
        }
    } while (changed);
}

static bool value_available(const IrFunction *function,
                            const bool *defined,
                            const size_t *definition_blocks,
                            const size_t *definition_instructions,
                            const bool *reachable,
                            const bool *dominators,
                            IrValueId value, size_t use_block,
                            size_t use_instruction) {
    if (!verify_value(function, value) || !defined[value]) return false;
    size_t definition_block = definition_blocks[value];
    if (definition_block == use_block)
        return definition_instructions[value] < use_instruction;
    /* Dominance is only meaningful in the entry-reachable CFG. */
    if (!reachable[use_block]) return true;
    return reachable[definition_block] &&
           dominators[use_block * function->block_count +
                      definition_block];
}

bool lang_ir_verify_module(const IrModule *ir,
                           LangDiagnostics *diagnostics) {
    if (ir == NULL ||
        (ir->type_count != 0U && ir->types == NULL) ||
        (ir->function_count != 0U && ir->functions == NULL) ||
        (ir->static_field_count != 0U && ir->static_fields == NULL)) {
        lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                  "IR module has invalid top-level storage");
        return false;
    }
    bool ok = true;
    for (size_t field = 0U; field < ir->static_field_count; ++field) {
        const IrStaticField *value = &ir->static_fields[field];
        if (value->owner_declaration != NULL || value->name == NULL ||
            value->owner_name == NULL || !verify_type(ir, value->type)) {
            lang_diag(diagnostics, value->span,
                      "IR static field %zu is malformed", field);
            ok = false;
        }
    }
    for (size_t t = 0U; t < ir->type_count; ++t) {
        const IrType *type = &ir->types[t];
        if (type->checked_type != NULL || type->name == NULL ||
            !type_shape_valid(type->shape) ||
            !valid_copy_policy(type->copy_policy) ||
            !valid_drop_policy(type->drop_policy) ||
            (type->copy_policy == IR_COPY_TRIVIAL &&
             (type->requires_cleanup || type->managed)) ||
            (type->drop_policy == IR_DROP_TRIVIAL &&
             (type->requires_cleanup || type->managed)) ||
            (type->copy_function != IR_INVALID_ID &&
             type->copy_policy != IR_COPY_CUSTOM) ||
            (type->destructor_function != IR_INVALID_ID &&
             type->drop_policy != IR_DROP_CUSTOM &&
             !(type->shape == IR_TYPE_CLASS_REFERENCE &&
               type->drop_policy == IR_DROP_TRIVIAL))) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu has invalid identity or copy/drop policy", t);
            ok = false;
        }
        if (type->copy_function != IR_INVALID_ID &&
            type->copy_function >= ir->function_count) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu has an invalid copy function", t);
            ok = false;
        }
        if (type->copy_function != IR_INVALID_ID &&
            type->copy_function < ir->function_count) {
            const IrFunction *copy =
                &ir->functions[type->copy_function];
            if (copy->return_type != (IrTypeId)t ||
                copy->parameter_count != 1U ||
                copy->parameters[0].type != (IrTypeId)t ||
                copy->parameters[0].mode !=
                    PARAMETER_MODE_IMMUTABLE_REFERENCE) {
                lang_diag(
                    diagnostics, copy->span,
                    "IR type t%zu has an invalid custom copy function signature",
                    t);
                ok = false;
            }
        }
        if (type->target_layout_known &&
            type->target_alignment == 0U) {
            lang_diag(
                diagnostics, (LangSpan){NULL, 0U, 0U},
                "IR type t%zu has an invalid target alignment",
                t);
            ok = false;
        }
        if (type->element_type != IR_INVALID_ID &&
            !verify_type(ir, type->element_type)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu has an invalid element type", t);
            ok = false;
        }
        if (type->base_type != IR_INVALID_ID &&
            (!verify_type(ir, type->base_type) ||
             type->shape != IR_TYPE_CLASS_REFERENCE ||
             ir->types[type->base_type].shape !=
                 IR_TYPE_CLASS_REFERENCE)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu (`%s`) has invalid base t%" PRIu32
                      " (`%s`, shape %d)",
                      t, type->name, type->base_type,
                      verify_type(ir, type->base_type)
                          ? ir->types[type->base_type].name : "<invalid>",
                      verify_type(ir, type->base_type)
                          ? (int)ir->types[type->base_type].shape : -1);
            ok = false;
        }
        if (type->interface_count != 0U &&
            type->interface_types == NULL) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu is missing interface metadata", t);
            ok = false;
        }
        for (size_t interface = 0U;
             type->interface_types != NULL &&
             interface < type->interface_count; ++interface)
            if (!verify_type(ir, type->interface_types[interface]) ||
                type->shape != IR_TYPE_CLASS_REFERENCE ||
                ir->types[type->interface_types[interface]].shape !=
                    IR_TYPE_CLASS_REFERENCE) {
                lang_diag(
                    diagnostics, (LangSpan){NULL, 0U, 0U},
                    "IR type t%zu has invalid interface metadata", t);
                ok = false;
            }
        if (type->base_type != IR_INVALID_ID &&
            verify_type(ir, type->base_type)) {
            IrTypeId base = type->base_type;
            size_t depth = 0U;
            while (base != IR_INVALID_ID && verify_type(ir, base) &&
                   depth++ < ir->type_count) {
                if (base == (IrTypeId)t) {
                    lang_diag(
                        diagnostics, (LangSpan){NULL, 0U, 0U},
                        "IR class type t%zu has an inheritance cycle", t);
                    ok = false;
                    break;
                }
                base = ir->types[base].base_type;
            }
            if (base != IR_INVALID_ID && depth > ir->type_count) {
                lang_diag(
                    diagnostics, (LangSpan){NULL, 0U, 0U},
                    "IR class type t%zu has an invalid inheritance chain", t);
                ok = false;
            }
        }
        if (type->error_type != IR_INVALID_ID &&
            !verify_type(ir, type->error_type)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu has an invalid error type", t);
            ok = false;
        }
        if (type->destructor_function != IR_INVALID_ID) {
            if (type->destructor_function >=
                ir->function_count) {
                lang_diag(
                    diagnostics, (LangSpan){NULL, 0U, 0U},
                    "IR type t%zu has an invalid destructor function",
                    t);
                ok = false;
            } else {
                const IrFunction *destructor =
                    &ir->functions[
                        type->destructor_function];
                if (!destructor->is_destructor ||
                    destructor->parameter_count != 1U ||
                    destructor->parameters == NULL ||
                    !ir_type_assignable(
                        ir, destructor->parameters[0].type,
                        (IrTypeId)t)) {
                    lang_diag(
                        diagnostics,
                        (LangSpan){NULL, 0U, 0U},
                        "IR type t%zu `%s` has incompatible destructor f%" PRIu32,
                        t, type->name != NULL ? type->name : "<invalid>",
                        type->destructor_function);
                    ok = false;
                }
            }
        }
        if (type->argument_count != 0U &&
            type->argument_types == NULL) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu is missing argument metadata", t);
            ok = false;
        }
        for (size_t a = 0U;
             type->argument_types != NULL && a < type->argument_count; ++a)
            if (!verify_type(ir, type->argument_types[a])) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR type t%zu has an invalid argument type", t);
                ok = false;
            }
        if (type->shape == IR_TYPE_FUNCTION)
            for (size_t a = 0U; a < type->argument_count; ++a)
                if (type->parameter_modes == NULL ||
                    !valid_parameter_mode(type->parameter_modes[a])) {
                    lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                              "IR function type t%zu has invalid parameter metadata",
                              t);
                    ok = false;
                    break;
                }
        if ((type->shape == IR_TYPE_ARRAY ||
            type->shape == IR_TYPE_RAW_POINTER ||
             type->shape == IR_TYPE_SLICE ||
             type->shape == IR_TYPE_ITERATOR ||
             type->shape == IR_TYPE_ELEMENT_BUILDER) &&
            !verify_type(ir, type->element_type)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR aggregate type t%zu is missing its element type",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_ITERATOR &&
            (type->argument_count != 1U ||
             type->argument_types == NULL ||
             !verify_type(ir, type->argument_types[0]))) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR iterator type t%zu has invalid source metadata",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_FUNCTION &&
            (!verify_type(ir, type->element_type) ||
             (type->argument_count != 0U &&
              type->parameter_modes == NULL))) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR function type t%zu has invalid result metadata",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_ELEMENT_BUILDER &&
            (!verify_type(ir, type->element_type) ||
             ir->types[type->element_type].name == NULL ||
             strcmp(ir->types[type->element_type].name, "Html") != 0)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR element builder type t%zu has invalid result metadata",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_STRUCT ||
            type->shape == IR_TYPE_CLASS_REFERENCE) {
            if (type->field_count != 0U &&
                (type->field_names == NULL ||
                 type->field_types == NULL ||
                 type->field_spans == NULL ||
                 type->field_offsets == NULL)) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR aggregate type t%zu has incomplete field metadata",
                          t);
                ok = false;
            } else if (type->field_count != 0U) {
                for (size_t field = 0U;
                     field < type->field_count; ++field) {
                    if (type->field_names[field] == NULL) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR aggregate type t%zu has an invalid field name",
                            t);
                        ok = false;
                    }
                    if (!verify_type(ir, type->field_types[field])) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR struct type t%zu has an invalid field type",
                            t);
                        ok = false;
                    } else {
                        size_t storage_size =
                            type->shape == IR_TYPE_CLASS_REFERENCE
                            ? type->object_size : type->target_size;
                        if (type->member_layout_known &&
                        (type->field_offsets[field] %
                             ir->types[type->field_types[field]].target_alignment !=
                             0U ||
                         type->field_offsets[field] > storage_size ||
                         ir->types[type->field_types[field]].target_size >
                             storage_size - type->field_offsets[field])) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR aggregate type t%zu has an invalid field offset",
                            t);
                        ok = false;
                        }
                    }
                }
            }
            if (type->target_layout_known && !type->member_layout_known) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR struct type t%zu is missing member layout", t);
                ok = false;
            }
            if (type->shape == IR_TYPE_CLASS_REFERENCE &&
                (!type->object_layout_known || type->object_size == 0U ||
                 type->object_alignment == 0U)) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR class type t%zu is missing object layout", t);
                ok = false;
            }
        }
        if (type->shape == IR_TYPE_ENUM ||
            type->shape == IR_TYPE_UNION) {
            if (type->variant_count == 0U ||
                type->variant_names == NULL ||
                type->variant_payload_types == NULL ||
                type->variant_spans == NULL ||
                type->variant_discriminants == NULL ||
                type->variant_payload_offsets == NULL) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR %s type t%zu has incomplete variant metadata",
                          type->shape == IR_TYPE_UNION ? "union" : "enum",
                          t);
                ok = false;
            } else {
                for (size_t variant = 0U;
                     variant < type->variant_count; ++variant) {
                    if (type->variant_names[variant] == NULL) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR enum type t%zu has an invalid variant name",
                            t);
                        ok = false;
                    }
                    if (type->variant_discriminants[variant] !=
                        (uint32_t)variant) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR enum type t%zu has an invalid discriminant",
                            t);
                        ok = false;
                    }
                    if (type->variant_payload_types[variant] !=
                            IR_INVALID_ID &&
                        !verify_type(
                            ir,
                            type->variant_payload_types[variant])) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR enum type t%zu has an invalid payload type",
                            t);
                        ok = false;
                    }
                    if (type->member_layout_known &&
                        type->variant_payload_types[variant] !=
                            IR_INVALID_ID &&
                        verify_type(
                            ir, type->variant_payload_types[variant])) {
                        const IrType *payload = &ir->types[
                            type->variant_payload_types[variant]];
                        if (type->variant_payload_offsets[variant] <
                                ir->target.enum_tag_size ||
                            type->variant_payload_offsets[variant] %
                                payload->target_alignment != 0U) {
                            lang_diag(
                                diagnostics, (LangSpan){NULL, 0U, 0U},
                                "IR union type t%zu has an invalid payload offset",
                                t);
                            ok = false;
                        }
                    }
                }
            }
            if (type->target_layout_known && !type->member_layout_known) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR enum type t%zu is missing member layout", t);
                ok = false;
            }
        }
    }
    if (ir->interface_dispatch_count != 0U &&
        ir->interface_dispatches == NULL) {
        lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                  "IR module is missing interface dispatch metadata");
        ok = false;
    }
    for (size_t entry = 0U;
         ir->interface_dispatches != NULL &&
         entry < ir->interface_dispatch_count; ++entry) {
        const IrInterfaceDispatch *dispatch =
            &ir->interface_dispatches[entry];
        if (!verify_type(ir, dispatch->interface_type) ||
            !verify_type(ir, dispatch->runtime_type) ||
            ir->types[dispatch->interface_type].shape !=
                IR_TYPE_CLASS_REFERENCE ||
            ir->types[dispatch->runtime_type].shape !=
                IR_TYPE_CLASS_REFERENCE ||
            dispatch->interface_function >= ir->function_count ||
            dispatch->target_function >= ir->function_count) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR interface dispatch entry %zu is invalid", entry);
            ok = false;
            continue;
        }
        const IrFunction *contract =
            &ir->functions[dispatch->interface_function];
        const IrFunction *target =
            &ir->functions[dispatch->target_function];
        bool signature = contract->is_abstract && contract->is_virtual &&
            contract->parameter_count != 0U &&
            contract->parameter_count == target->parameter_count &&
            contract->return_type == target->return_type &&
            contract->parameters[0].type == dispatch->interface_type &&
            ir_type_assignable(
                ir, dispatch->interface_type, dispatch->runtime_type) &&
            ir_type_assignable(
                ir, target->parameters[0].type, dispatch->runtime_type);
        for (size_t parameter = 1U;
             signature && parameter < contract->parameter_count;
             ++parameter)
            signature = contract->parameters[parameter].type ==
                    target->parameters[parameter].type &&
                contract->parameters[parameter].mode ==
                    target->parameters[parameter].mode;
        if (!signature) {
            lang_diag(
                diagnostics, (LangSpan){NULL, 0U, 0U},
                "IR interface dispatch entry %zu has an incompatible signature",
                entry);
            ok = false;
        }
    }
    for (size_t f = 0U; f < ir->function_count; ++f) {
        const IrFunction *function = &ir->functions[f];
        LangSpan function_span = function->span;
        if (function->declaration != NULL || function->name == NULL ||
            !verify_type(ir, function->return_type) ||
            !verify_type(ir, function->async_result_type) ||
            function->blocks == NULL ||
            function->abi.calling_convention !=
                IR_CALLING_CONVENTION_ASTER ||
            function->abi.returns_async_task != function->is_async ||
            (function->parameter_count != 0U &&
             function->parameters == NULL) ||
            (function->local_count != 0U &&
             function->locals == NULL) ||
            (function->value_count != 0U &&
             function->value_types == NULL) ||
            (function->is_async &&
             (ir->types[function->return_type].shape !=
                  IR_TYPE_BUILTIN_OBJECT ||
              ir->types[function->return_type].name == NULL ||
              strncmp(ir->types[function->return_type].name,
                      "Task", 4U) != 0 ||
              ir->types[function->return_type].element_type !=
                  function->async_result_type)) ||
            function->block_count == 0U ||
            function->entry_block >= function->block_count) {
            lang_diag(diagnostics, function_span,
                      "malformed IR function `%s`",
                      function->name != NULL
                          ? function->name : "<invalid>");
            ok = false;
            continue;
        }
        if ((function->static_css_count != 0U &&
             function->static_css == NULL) ||
            function->static_css_count > function->static_css_capacity) {
            lang_diag(diagnostics, function_span,
                      "IR function `%s` has invalid static CSS metadata",
                      function->name);
            ok = false;
        }
        for (size_t css = 0U;
             function->static_css != NULL &&
             css < function->static_css_count; ++css)
            if (function->static_css[css].scope_attribute == NULL ||
                (function->static_css[css].text == NULL &&
                 function->static_css[css].text_length != 0U)) {
                lang_diag(diagnostics, function_span,
                          "IR function `%s` has invalid static CSS entry",
                          function->name);
                ok = false;
            }
        size_t suspension_count = 0U;
        for (size_t b = 0U; b < function->block_count; ++b)
            for (size_t i = 0U;
                 i < function->blocks[b].instruction_count; ++i)
                if (function->blocks[b].instructions[i].opcode == IR_OP_AWAIT)
                    ++suspension_count;
        if (suspension_count != function->async_suspension_count ||
            (!function->is_async && suspension_count != 0U)) {
            lang_diag(diagnostics, function_span,
                      "IR function `%s` has invalid async metadata",
                      function->name);
            ok = false;
        }
        for (size_t p = 0U; p < function->parameter_count; ++p)
            if (function->parameters[p].name == NULL ||
                !verify_type(ir, function->parameters[p].type) ||
                !valid_parameter_mode(function->parameters[p].mode)) {
                lang_diag(diagnostics, function_span,
                          "IR function `%s` has an invalid parameter type",
                          function->name);
                ok = false;
            }
        for (size_t l = 0U; l < function->local_count; ++l)
            if (!verify_type(ir, function->locals[l].type)) {
                lang_diag(diagnostics, function_span,
                          "IR function `%s` has an invalid local type",
                          function->name);
                ok = false;
            }
        size_t value_slots = function->value_count == 0U
                           ? 1U : function->value_count;
        bool *defined = ir_resize(NULL, value_slots, sizeof(*defined));
        size_t *definition_blocks = ir_resize(
            NULL, value_slots, sizeof(*definition_blocks));
        size_t *definition_instructions = ir_resize(
            NULL, value_slots, sizeof(*definition_instructions));
        memset(defined, 0, value_slots * sizeof(*defined));
        for (size_t value = 0U; value < value_slots; ++value) {
            definition_blocks[value] = function->block_count;
            definition_instructions[value] = SIZE_MAX;
        }
        for (size_t b = 0U; b < function->block_count; ++b) {
            const IrBlock *block = &function->blocks[b];
            if (block->instruction_count != 0U &&
                block->instructions == NULL) {
                lang_diag(diagnostics, function_span,
                          "IR block b%zu in `%s` has no instruction storage",
                          b, function->name);
                ok = false;
                continue;
            }
            const IrTerminator *term = &block->terminator;
            bool valid_terminator = true;
            switch (term->kind) {
                case IR_TERM_JUMP:
                    valid_terminator =
                        term->target < function->block_count &&
                        term->value == IR_INVALID_ID &&
                        term->alternate == IR_INVALID_ID;
                    break;
                case IR_TERM_BRANCH:
                    valid_terminator =
                        term->target < function->block_count &&
                        term->alternate < function->block_count;
                    break;
                case IR_TERM_RETURN:
                    valid_terminator =
                        term->target == IR_INVALID_ID &&
                        term->alternate == IR_INVALID_ID;
                    break;
                case IR_TERM_PROPAGATE_EXCEPTION:
                case IR_TERM_TRAP:
                    valid_terminator =
                        term->value == IR_INVALID_ID &&
                        term->target == IR_INVALID_ID &&
                        term->alternate == IR_INVALID_ID;
                    break;
                case IR_TERM_NONE:
                default:
                    valid_terminator = false;
                    break;
            }
            if (!valid_terminator) {
                lang_diag(diagnostics, function_span,
                          "IR block b%zu in `%s` has an invalid terminator",
                          b, function->name);
                ok = false;
            }
            for (size_t i = 0U; i < block->instruction_count; ++i) {
                const IrInstruction *instruction =
                    &block->instructions[i];
                bool known_opcode = opcode_valid(instruction->opcode);
                bool valid_operands = known_opcode &&
                    operand_count_valid(instruction) &&
                    (instruction->operand_count == 0U ||
                     instruction->operands != NULL);
                bool valid_labels = instruction->label_count == 0U ||
                                    instruction->labels != NULL;
                bool valid_result = known_opcode &&
                    result_type_valid(ir, function, instruction);
                if (!known_opcode) {
                    lang_diag(diagnostics, instruction->span,
                              "IR instruction has unknown opcode %u",
                              (unsigned)instruction->opcode);
                    ok = false;
                } else if (!valid_operands) {
                    lang_diag(diagnostics, instruction->span,
                              "IR `%s` has an invalid operand count",
                              ir_opcode_name(instruction->opcode));
                    ok = false;
                }
                if (known_opcode && !valid_result) {
                    lang_diag(diagnostics, instruction->span,
                              "IR `%s` has an incompatible result type",
                              ir_opcode_name(instruction->opcode));
                    ok = false;
                }
                if (known_opcode && valid_operands && valid_labels &&
                    valid_result &&
                    !instruction_signature_valid(
                        ir, function, instruction)) {
                    lang_diag(diagnostics, instruction->span,
                              "IR `%s` has an invalid typed signature",
                              ir_opcode_name(instruction->opcode));
                    ok = false;
                }
                if (instruction->result == IR_INVALID_ID) {
                    if (instruction->result_type != IR_INVALID_ID) {
                        lang_diag(diagnostics, instruction->span,
                                  "IR effect instruction has a result type");
                        ok = false;
                    }
                } else if (!verify_value(function, instruction->result) ||
                           !verify_type(ir, instruction->result_type) ||
                           defined[instruction->result] ||
                           function->value_types[instruction->result] !=
                           instruction->result_type) {
                    lang_diag(diagnostics, instruction->span,
                              "IR instruction has an invalid result");
                    ok = false;
                } else {
                    defined[instruction->result] = true;
                    definition_blocks[instruction->result] = b;
                    definition_instructions[instruction->result] = i;
                }
                if (((instruction->opcode == IR_OP_ELEMENT_BEGIN &&
                      instruction->index != IR_INVALID_ID) ||
                     instruction->opcode == IR_OP_LOCAL_LOAD ||
                     instruction->opcode == IR_OP_LOCAL_MOVE ||
                     instruction->opcode == IR_OP_LOCAL_STORE ||
                     instruction->opcode == IR_OP_LOCAL_DROP ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_GET ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_MOVE ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_BORROW ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_SET ||
                     instruction->opcode == IR_OP_LOCAL_INDEX_GET ||
                     instruction->opcode == IR_OP_LOCAL_INDEX_SET ||
                     instruction->opcode == IR_OP_LOCAL_ENUM_IS ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ENUM_PAYLOAD_MOVE ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ITERATOR_HAS_NEXT ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ITERATOR_NEXT ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_PROPERTY ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_CSS_VALUE ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_PROPERTY_END ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_APPEND ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_FINISH) &&
                    instruction->index >= function->local_count) {
                    lang_diag(diagnostics, instruction->span,
                              "IR instruction references invalid local %u",
                              instruction->index);
                    ok = false;
                }
                if (instruction->opcode == IR_OP_PARAMETER &&
                    instruction->index >= function->parameter_count) {
                    lang_diag(diagnostics, instruction->span,
                              "IR parameter index is out of range");
                    ok = false;
                }
                if ((instruction->opcode == IR_OP_CALL_DIRECT ||
                     instruction->opcode == IR_OP_CALL_VIRTUAL) &&
                    instruction->index >= ir->function_count) {
                    lang_diag(diagnostics, instruction->span,
                              "IR direct call target is invalid");
                    ok = false;
                }
                if (instruction->label_count != 0U &&
                    instruction->labels == NULL) {
                    lang_diag(diagnostics, instruction->span,
                              "IR aggregate field mapping is missing");
                    ok = false;
                } else if (instruction->label_count != 0U &&
                    (instruction->opcode != IR_OP_AGGREGATE_MAKE ||
                     instruction->label_count !=
                         instruction->operand_count)) {
                    lang_diag(diagnostics, instruction->span,
                              "IR aggregate field mapping is malformed");
                    ok = false;
                }
                if (instruction->label_count != 0U &&
                    instruction->labels != NULL) {
                    bool *seen = ir_resize(
                        NULL, instruction->label_count,
                        sizeof(*seen));
                    memset(seen, 0,
                           instruction->label_count * sizeof(*seen));
                    for (size_t label = 0U;
                         label < instruction->label_count; ++label) {
                        uint32_t field = instruction->labels[label];
                        if (field >= instruction->label_count ||
                            seen[field]) {
                            lang_diag(
                                diagnostics, instruction->span,
                                "IR aggregate field mapping is not a permutation");
                            ok = false;
                            break;
                        }
                        seen[field] = true;
                    }
                    free(seen);
                }
            }
        }
        if (function->block_count > SIZE_MAX / function->block_count) {
            lang_diag(diagnostics, function_span,
                      "IR function `%s` has too many CFG blocks",
                      function->name);
            free(definition_instructions);
            free(definition_blocks);
            free(defined);
            ok = false;
            continue;
        }
        size_t dominator_slots =
            function->block_count * function->block_count;
        bool *reachable = ir_resize(
            NULL, function->block_count, sizeof(*reachable));
        bool *dominators = ir_resize(
            NULL, dominator_slots, sizeof(*dominators));
        memset(reachable, 0,
               function->block_count * sizeof(*reachable));
        memset(dominators, 0,
               dominator_slots * sizeof(*dominators));
        compute_dominators(function, reachable, dominators);
        for (size_t b = 0U; b < function->block_count; ++b) {
            const IrBlock *block = &function->blocks[b];
            if (block->instruction_count != 0U &&
                block->instructions == NULL)
                continue;
            for (size_t i = 0U; i < block->instruction_count; ++i) {
                const IrInstruction *instruction =
                    &block->instructions[i];
                if (instruction->operand_count != 0U &&
                    instruction->operands == NULL)
                    continue;
                for (size_t o = 0U; o < instruction->operand_count; ++o)
                    if (!value_available(
                            function, defined, definition_blocks,
                            definition_instructions, reachable,
                            dominators, instruction->operands[o], b, i)) {
                        lang_diag(diagnostics, instruction->span,
                                  "IR instruction operand is undefined or does not dominate its use");
                        ok = false;
                    }
            }
            const IrTerminator *term = &block->terminator;
            switch (term->kind) {
                case IR_TERM_BRANCH: {
                    IrTypeId condition_type;
                    if (!value_available(
                            function, defined, definition_blocks,
                            definition_instructions, reachable,
                            dominators, term->value, b,
                            block->instruction_count) ||
                        !value_type(ir, function, term->value,
                                    &condition_type) ||
                        ir->types[condition_type].shape != IR_TYPE_BOOL) {
                        lang_diag(diagnostics, term->span,
                                  "IR branch has invalid or non-dominating condition");
                        ok = false;
                    }
                    break;
                }
                case IR_TERM_RETURN:
                    if (!value_available(
                            function, defined, definition_blocks,
                            definition_instructions, reachable,
                            dominators, term->value, b,
                            block->instruction_count) ||
                        !value_is_type(function, term->value,
                                       function->async_result_type)) {
                        lang_diag(diagnostics, term->span,
                                  "IR return has invalid or non-dominating value");
                        ok = false;
                    }
                    break;
                case IR_TERM_JUMP:
                case IR_TERM_PROPAGATE_EXCEPTION:
                case IR_TERM_TRAP:
                case IR_TERM_NONE:
                default:
                    break;
            }
        }
        free(dominators);
        free(reachable);
        free(definition_instructions);
        free(definition_blocks);
        free(defined);
    }
    return ok;
}
