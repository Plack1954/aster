#include "ir_verify_internal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

const char *ir_opcode_name(IrOpcode opcode) {
    static const char *names[] = {
        "parameter", "unit", "const_bool", "const_int", "const_float",
        "const_string", "const_null", "local_load", "local_move",
        "local_transfer",
        "local_store", "local_drop", "local_default", "local_invalidate",
        "static_field_load",
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
        "local_field_transfer",
        "local_field_borrow",
        "local_field_set", "local_field_default", "index_get",
        "index_set", "local_index_get", "local_index_set",
        "local_enum_is", "local_enum_payload_move", "enum_is",
        "enum_payload_borrow", "collection_count", "list_element_borrow",
        "queue_front_borrow", "stack_top_borrow",
        "dictionary_get_borrow", "dictionary_find",
        "dictionary_key_borrow", "dictionary_value_borrow",
        "iterator_begin",
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
bool ir_verify_opcode_valid(IrOpcode opcode) {
    return (unsigned)opcode < (unsigned)IR_OP_COUNT;
}

bool ir_verify_type(const IrModule *ir, IrTypeId type) {
    return type != IR_INVALID_ID && (size_t)type < ir->type_count;
}

bool ir_verify_parameter_mode(ParameterMode mode) {
    return mode >= PARAMETER_MODE_VALUE && mode <= PARAMETER_MODE_OUT;
}

bool ir_verify_copy_policy(IrCopyPolicy policy) {
    return policy >= IR_COPY_TRIVIAL && policy <= IR_COPY_CUSTOM;
}

bool ir_verify_drop_policy(IrDropPolicy policy) {
    return policy >= IR_DROP_TRIVIAL && policy <= IR_DROP_CUSTOM;
}

bool ir_verify_type_shape(IrTypeShape shape) {
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

bool ir_verify_value(const IrFunction *function, IrValueId value) {
    return value != IR_INVALID_ID && (size_t)value < function->value_count;
}

static bool ir_type_assignable_inner(
    const IrModule *ir, IrTypeId expected, IrTypeId actual, size_t depth
) {
    if (expected == actual) return true;
    if (!ir_verify_type(ir, expected) || !ir_verify_type(ir, actual) ||
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

bool ir_verify_type_assignable(
    const IrModule *ir, IrTypeId expected, IrTypeId actual
) {
    return ir_type_assignable_inner(ir, expected, actual, 0U);
}

static bool value_assignable(
    const IrModule *ir, const IrFunction *function,
    IrValueId value, IrTypeId expected
) {
    return ir_verify_value(function, value) &&
        ir_verify_type_assignable(
            ir, expected, function->value_types[value]);
}

bool ir_verify_operand_count(const IrInstruction *instruction) {
    switch (instruction->opcode) {
        case IR_OP_PARAMETER: case IR_OP_UNIT: case IR_OP_CONST_BOOL:
        case IR_OP_CONST_INT: case IR_OP_CONST_FLOAT:
        case IR_OP_CONST_STRING: case IR_OP_CONST_NULL:
        case IR_OP_LOCAL_LOAD: case IR_OP_LOCAL_MOVE:
        case IR_OP_LOCAL_TRANSFER:
        case IR_OP_LOCAL_DROP: case IR_OP_LOCAL_DEFAULT:
        case IR_OP_LOCAL_INVALIDATE:
        case IR_OP_STATIC_FIELD_LOAD:
        case IR_OP_FUNCTION_REF:
        case IR_OP_LOCAL_FIELD_GET: case IR_OP_LOCAL_FIELD_MOVE:
        case IR_OP_LOCAL_FIELD_TRANSFER:
        case IR_OP_LOCAL_FIELD_BORROW:
        case IR_OP_LOCAL_FIELD_DEFAULT:
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
        case IR_OP_ENUM_IS: case IR_OP_ENUM_PAYLOAD_BORROW:
        case IR_OP_COLLECTION_COUNT:
        case IR_OP_QUEUE_FRONT_BORROW:
        case IR_OP_STACK_TOP_BORROW:
            return instruction->operand_count == 1U;
        case IR_OP_LIST_ELEMENT_BORROW:
        case IR_OP_DICTIONARY_GET_BORROW:
        case IR_OP_DICTIONARY_FIND:
        case IR_OP_DICTIONARY_KEY_BORROW:
        case IR_OP_DICTIONARY_VALUE_BORROW:
            return instruction->operand_count == 2U;
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

bool ir_verify_result_type(const IrModule *ir,
                              const IrFunction *function,
                              const IrInstruction *instruction) {
    if (instruction->opcode != IR_OP_CALL_NATIVE &&
        instruction->native_call != NULL)
        return false;
    bool produces_result;
    switch (instruction->opcode) {
        case IR_OP_LOCAL_STORE: case IR_OP_LOCAL_DROP:
        case IR_OP_LOCAL_DEFAULT: case IR_OP_LOCAL_INVALIDATE:
        case IR_OP_STATIC_FIELD_STORE:
        case IR_OP_VALUE_DISCARD: case IR_OP_EXCEPTION_SET:
        case IR_OP_FIELD_SET: case IR_OP_LOCAL_FIELD_SET:
        case IR_OP_LOCAL_FIELD_DEFAULT:
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
        case IR_OP_LOCAL_TRANSFER:
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
        case IR_OP_LOCAL_FIELD_TRANSFER:
        case IR_OP_LOCAL_FIELD_BORROW: case IR_OP_INDEX_GET:
        case IR_OP_LOCAL_INDEX_GET: case IR_OP_LOCAL_ENUM_IS:
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE:
        case IR_OP_ENUM_IS: case IR_OP_ENUM_PAYLOAD_BORROW:
        case IR_OP_COLLECTION_COUNT:
        case IR_OP_LIST_ELEMENT_BORROW:
        case IR_OP_QUEUE_FRONT_BORROW:
        case IR_OP_STACK_TOP_BORROW:
        case IR_OP_DICTIONARY_GET_BORROW:
        case IR_OP_DICTIONARY_FIND:
        case IR_OP_DICTIONARY_KEY_BORROW:
        case IR_OP_DICTIONARY_VALUE_BORROW:
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
    if (!ir_verify_type(ir, instruction->result_type)) return false;
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

bool ir_verify_value_is_type(const IrFunction *function,
                          IrValueId value, IrTypeId type) {
    return ir_verify_value(function, value) &&
           function->value_types[value] == type;
}

bool ir_verify_value_type(const IrModule *ir, const IrFunction *function,
                       IrValueId value, IrTypeId *type) {
    if (!ir_verify_value(function, value)) return false;
    IrTypeId found = function->value_types[value];
    if (!ir_verify_type(ir, found)) return false;
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
    if (!ir_verify_type(ir, instruction->result_type)) return false;
    for (size_t i = 0U; i < instruction->operand_count; ++i)
        if (!ir_verify_value_is_type(function, instruction->operands[i],
                           instruction->result_type))
            return false;
    return true;
}

static bool verify_element_child_type(
    const IrModule *ir, IrTypeId type_id) {
    if (!ir_verify_type(ir, type_id)) return false;
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

bool ir_verify_instruction_signature(
    const IrModule *ir, const IrFunction *function,
    const IrInstruction *instruction) {
    uint32_t local_index = instruction->index;
    const IrLocal *local =
        local_index < function->local_count
        ? &function->locals[local_index] : NULL;
    switch (instruction->opcode) {
        case IR_OP_LOCAL_TRANSFER:
        case IR_OP_LOCAL_FIELD_TRANSFER:
            /* Frontend-only opcodes must be gone before verification. */
            return false;
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
        case IR_OP_LOCAL_DEFAULT:
        case IR_OP_LOCAL_INVALIDATE:
            return local != NULL;
        case IR_OP_VALUE_DISCARD:
            return ir_verify_value_type(ir, function, instruction->operands[0], NULL);
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
            return ir_verify_value_type(ir, function, instruction->operands[0],
                              &left) &&
                   ir_verify_value_is_type(function, instruction->operands[1], left) &&
                   ir->types[instruction->result_type].shape == IR_TYPE_BOOL;
        }
        case IR_OP_LESS:
        case IR_OP_LESS_EQUAL:
        case IR_OP_GREATER:
        case IR_OP_GREATER_EQUAL: {
            IrTypeId left;
            return ir_verify_value_type(ir, function, instruction->operands[0],
                              &left) &&
                   ir_verify_value_is_type(function, instruction->operands[1], left) &&
                   shape_is_numeric(ir->types[left].shape) &&
                   ir->types[instruction->result_type].shape == IR_TYPE_BOOL;
        }
        case IR_OP_CAST: {
            IrTypeId source;
            return ir_verify_value_type(ir, function, instruction->operands[0],
                              &source) &&
                   (shape_is_numeric(ir->types[source].shape) ||
                    ir->types[source].shape == IR_TYPE_CHAR) &&
                   (shape_is_numeric(
                        ir->types[instruction->result_type].shape) ||
                    ir->types[instruction->result_type].shape == IR_TYPE_CHAR);
        }
        case IR_OP_FUNCTION_REF: {
            if (instruction->index >= ir->function_count ||
                !ir_verify_type(ir, instruction->result_type))
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
                !ir_verify_type(ir, instruction->result_type))
                return false;
            IrTypeId receiver_type;
            if (!ir_verify_value_type(ir, function, instruction->operands[0],
                            &receiver_type))
                return false;
            const IrType *type = &ir->types[instruction->result_type];
            const IrFunction *target =
                &ir->functions[instruction->index];
            if (ir->types[receiver_type].shape !=
                    IR_TYPE_CLASS_REFERENCE ||
                type->shape != IR_TYPE_FUNCTION ||
                target->parameter_count == 0U ||
                !ir_verify_type_assignable(
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
            if (!ir_verify_value_type(ir, function, instruction->operands[0],
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
                if (!ir_verify_value_is_type(function, instruction->operands[i + 1U],
                                   callee->argument_types[i]) ||
                    instruction->argument_modes[i] !=
                        callee->parameter_modes[i])
                    return false;
            return true;
        }
        case IR_OP_CALL_NATIVE:
            if (instruction->symbol == NULL ||
                !ir_verify_type(ir, instruction->result_type) ||
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
                if (!ir_verify_value_is_type(
                        function, instruction->operands[i],
                        instruction->native_call->parameter_types[i]) ||
                    !ir_verify_parameter_mode(instruction->argument_modes[i]) ||
                    instruction->native_call->parameter_modes[i] !=
                        instruction->argument_modes[i])
                    return false;
            return true;
        case IR_OP_EXCEPTION_SET: {
            IrTypeId exception;
            return ir_verify_value_type(ir, function, instruction->operands[0],
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
                !ir_verify_type(ir, instruction->result_type) ||
                ir->types[instruction->result_type].shape !=
                    IR_TYPE_ELEMENT_BUILDER)
                return false;
            if (instruction->index == IR_INVALID_ID)
                return true;
            return local != NULL &&
                   ir_verify_type(ir, local->type) &&
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
                   ir_verify_value_is_type(
                       function, instruction->operands[0],
                       ir->static_fields[instruction->index].type);
        case IR_OP_LOCAL_ENUM_IS:
            return local != NULL &&
                   instruction->operand_count == 0U &&
                   ir_verify_type(ir, local->type) &&
                   (ir->types[local->type].shape == IR_TYPE_ENUM ||
                    ir->types[local->type].shape == IR_TYPE_UNION) &&
                   instruction->auxiliary <
                       ir->types[local->type].variant_count;
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE: {
            if (local == NULL || !ir_verify_type(ir, local->type) ||
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
        case IR_OP_ENUM_IS: {
            if (!ir_verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId source_id =
                function->value_types[instruction->operands[0]];
            if (!ir_verify_type(ir, source_id)) return false;
            const IrType *source = &ir->types[source_id];
            return source->shape == IR_TYPE_UNION &&
                   instruction->auxiliary < source->variant_count &&
                   ir->types[instruction->result_type].shape == IR_TYPE_BOOL;
        }
        case IR_OP_ENUM_PAYLOAD_BORROW: {
            if (!ir_verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId source_id =
                function->value_types[instruction->operands[0]];
            if (!ir_verify_type(ir, source_id)) return false;
            const IrType *source = &ir->types[source_id];
            return source->shape == IR_TYPE_UNION &&
                   instruction->auxiliary < source->variant_count &&
                   source->variant_payload_types[instruction->auxiliary] !=
                       IR_INVALID_ID &&
                   instruction->result_type ==
                       source->variant_payload_types[instruction->auxiliary];
        }
        case IR_OP_COLLECTION_COUNT: {
            if (!ir_verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId source_id =
                function->value_types[instruction->operands[0]];
            return ir_verify_type(ir, source_id) &&
                   ir->types[source_id].shape == IR_TYPE_BUILTIN_OBJECT &&
                   ir->types[instruction->result_type].shape ==
                       IR_TYPE_UNSIGNED_INT;
        }
        case IR_OP_DICTIONARY_KEY_BORROW:
        case IR_OP_DICTIONARY_VALUE_BORROW: {
            if (!ir_verify_value(function, instruction->operands[0]) ||
                !ir_verify_value(function, instruction->operands[1]))
                return false;
            IrTypeId source_id =
                function->value_types[instruction->operands[0]];
            if (!ir_verify_type(ir, source_id)) return false;
            const IrType *source = &ir->types[source_id];
            IrTypeId expected = instruction->opcode ==
                    IR_OP_DICTIONARY_KEY_BORROW
                ? source->element_type : source->error_type;
            return source->shape == IR_TYPE_BUILTIN_OBJECT &&
                   expected != IR_INVALID_ID &&
                   instruction->result_type == expected &&
                   ir->types[function->value_types[
                       instruction->operands[1]]].shape ==
                       IR_TYPE_UNSIGNED_INT;
        }
        case IR_OP_LIST_ELEMENT_BORROW: {
            if (!ir_verify_value(function, instruction->operands[0]) ||
                !ir_verify_value(function, instruction->operands[1]))
                return false;
            IrTypeId source_id =
                function->value_types[instruction->operands[0]];
            if (!ir_verify_type(ir, source_id)) return false;
            const IrType *source = &ir->types[source_id];
            return source->shape == IR_TYPE_BUILTIN_OBJECT &&
                   source->element_type != IR_INVALID_ID &&
                   instruction->result_type == source->element_type &&
                   ir->types[function->value_types[
                       instruction->operands[1]]].shape ==
                       IR_TYPE_UNSIGNED_INT;
        }
        case IR_OP_QUEUE_FRONT_BORROW:
        case IR_OP_STACK_TOP_BORROW: {
            if (!ir_verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId source_id =
                function->value_types[instruction->operands[0]];
            if (!ir_verify_type(ir, source_id)) return false;
            const IrType *source = &ir->types[source_id];
            return source->shape == IR_TYPE_BUILTIN_OBJECT &&
                   source->element_type != IR_INVALID_ID &&
                   instruction->result_type == source->element_type;
        }
        case IR_OP_DICTIONARY_GET_BORROW: {
            if (!ir_verify_value(function, instruction->operands[0]) ||
                !ir_verify_value(function, instruction->operands[1]))
                return false;
            IrTypeId source_id =
                function->value_types[instruction->operands[0]];
            if (!ir_verify_type(ir, source_id)) return false;
            const IrType *source = &ir->types[source_id];
            return source->shape == IR_TYPE_BUILTIN_OBJECT &&
                   source->element_type != IR_INVALID_ID &&
                   source->error_type != IR_INVALID_ID &&
                   instruction->result_type == source->error_type &&
                   ir_verify_value_is_type(function, instruction->operands[1],
                                 source->element_type);
        }
        case IR_OP_DICTIONARY_FIND: {
            if (!ir_verify_value(function, instruction->operands[0]) ||
                !ir_verify_value(function, instruction->operands[1]))
                return false;
            IrTypeId source_id =
                function->value_types[instruction->operands[0]];
            if (!ir_verify_type(ir, source_id)) return false;
            const IrType *source = &ir->types[source_id];
            return source->shape == IR_TYPE_BUILTIN_OBJECT &&
                   source->element_type != IR_INVALID_ID &&
                   source->error_type != IR_INVALID_ID &&
                   ir->types[instruction->result_type].shape ==
                       IR_TYPE_UNSIGNED_INT &&
                   ir_verify_value_is_type(function, instruction->operands[1],
                                 source->element_type);
        }
        case IR_OP_ITERATOR_BEGIN: {
            if (!ir_verify_type(ir, instruction->result_type))
                return false;
            const IrType *iterator =
                &ir->types[instruction->result_type];
            return iterator->shape == IR_TYPE_ITERATOR &&
                   iterator->argument_count == 1U &&
                   iterator->argument_types != NULL &&
                   instruction->operand_count == 1U &&
                   ir_verify_value_is_type(
                       function, instruction->operands[0],
                       iterator->argument_types[0]);
        }
        case IR_OP_BORROWED_ITERATOR_BEGIN: {
            if (!ir_verify_type(ir, instruction->result_type))
                return false;
            const IrType *iterator =
                &ir->types[instruction->result_type];
            if (iterator->shape != IR_TYPE_ITERATOR ||
                iterator->argument_count != 1U ||
                iterator->argument_types == NULL)
                return false;
            if (instruction->operand_count == 1U)
                return ir_verify_value_is_type(
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
                   ir_verify_value_is_type(
                       function, instruction->operands[0],
                       instruction->result_type);
        case IR_OP_LOCAL_ITERATOR_HAS_NEXT:
        case IR_OP_LOCAL_ITERATOR_NEXT: {
            if (local == NULL || !ir_verify_type(ir, local->type))
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
            if (local == NULL || !ir_verify_type(ir, local->type))
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
                !ir_verify_value(
                    function, instruction->operands[0]))
                return false;
            IrTypeId value_type_id;
            if (!ir_verify_value_type(ir, function, instruction->operands[0],
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
                !ir_verify_type(ir, instruction->result_type) ||
                ir->types[instruction->result_type].shape !=
                    IR_TYPE_RAW_POINTER ||
                !ir_verify_value(function, instruction->operands[0]) ||
                !ir_verify_value(function, instruction->operands[1]))
                return false;
            {
                IrTypeId owner =
                    function->value_types[instruction->operands[0]];
                IrTypeId size =
                    function->value_types[instruction->operands[1]];
                return ir_verify_type(ir, owner) &&
                       ir_verify_type(ir, size) &&
                       ir->types[owner].name != NULL &&
                       strcmp(ir->types[owner].name, "Arena") == 0 &&
                       (ir->types[size].shape == IR_TYPE_SIGNED_INT ||
                        ir->types[size].shape == IR_TYPE_UNSIGNED_INT);
            }
        case IR_OP_RAW_LOAD: {
            if (instruction->operand_count != 1U ||
                !ir_verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId pointer_type =
                function->value_types[instruction->operands[0]];
            return ir_verify_type(ir, pointer_type) &&
                   ir->types[pointer_type].shape ==
                       IR_TYPE_RAW_POINTER &&
                   instruction->result_type ==
                       ir->types[pointer_type].element_type;
        }
        case IR_OP_RAW_STORE: {
            if (instruction->operand_count != 2U ||
                !ir_verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId pointer_type =
                function->value_types[instruction->operands[0]];
            return ir_verify_type(ir, pointer_type) &&
                   ir->types[pointer_type].shape ==
                       IR_TYPE_RAW_POINTER &&
                   ir->types[pointer_type].pointer_mutable &&
                   ir_verify_value_is_type(
                       function, instruction->operands[1],
                       ir->types[pointer_type].element_type);
        }
        case IR_OP_LOCAL_FIELD_GET:
        case IR_OP_LOCAL_FIELD_MOVE:
        case IR_OP_LOCAL_FIELD_BORROW:
        case IR_OP_LOCAL_FIELD_SET:
        case IR_OP_LOCAL_FIELD_DEFAULT: {
            if (local == NULL || !ir_verify_type(ir, local->type))
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
            if (instruction->opcode == IR_OP_LOCAL_FIELD_DEFAULT)
                return instruction->operand_count == 0U;
            return instruction->operand_count == 1U &&
                   ir_verify_value_is_type(
                       function, instruction->operands[0],
                       field_type);
        }
        case IR_OP_FIELD_GET: {
            if (instruction->operand_count != 1U ||
                !ir_verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId aggregate_type =
                function->value_types[instruction->operands[0]];
            if (!ir_verify_type(ir, aggregate_type))
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
            if (local == NULL || !ir_verify_type(ir, local->type))
                return false;
            const IrType *aggregate = &ir->types[local->type];
            if (aggregate->shape != IR_TYPE_ARRAY ||
                aggregate->element_type == IR_INVALID_ID ||
                instruction->operand_count !=
                    (instruction->opcode ==
                         IR_OP_LOCAL_INDEX_GET ? 1U : 2U) ||
                !ir_verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId index_type;
            if (!ir_verify_value_type(ir, function, instruction->operands[0],
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
                   ir_verify_value_is_type(
                       function, instruction->operands[1],
                       aggregate->element_type);
        }
        case IR_OP_INDEX_GET: {
            if (instruction->operand_count != 2U ||
                !ir_verify_value(function, instruction->operands[0]) ||
                !ir_verify_value(function, instruction->operands[1]))
                return false;
            IrTypeId aggregate_type =
                function->value_types[instruction->operands[0]];
            if (!ir_verify_type(ir, aggregate_type))
                return false;
            const IrType *aggregate =
                &ir->types[aggregate_type];
            IrTypeId index_type;
            if (!ir_verify_value_type(ir, function, instruction->operands[1],
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
            if (!ir_verify_type(ir, instruction->result_type))
                return false;
            const IrType *aggregate =
                &ir->types[instruction->result_type];
            if (aggregate->shape == IR_TYPE_ARRAY) {
                if (instruction->operand_count !=
                    aggregate->array_length)
                    return false;
                for (size_t i = 0U;
                     i < instruction->operand_count; ++i)
                    if (!ir_verify_value_is_type(
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
                        !ir_verify_value_is_type(
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
                       ir_verify_value_is_type(
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
                    !ir_verify_type(
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
                !ir_verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId type = function->value_types[instruction->operands[0]];
            return ir_verify_type(ir, type) &&
                   ir->types[type].shape == IR_TYPE_CLASS_REFERENCE;
        }
        case IR_OP_FIELD_SET:
        case IR_OP_INDEX_SET:
            /* Reserved opcodes have no lowering/backend contract yet. */
            return false;
        case IR_OP_AWAIT: {
            if (!function->is_async ||
                instruction->operand_count != 1U ||
                !ir_verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId task_type = function->value_types[
                instruction->operands[0]];
            if (!ir_verify_type(ir, task_type)) return false;
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
