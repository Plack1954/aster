#include "ir_internal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

const char *ir_opcode_name(IrOpcode opcode) {
    static const char *names[] = {
        "parameter", "unit", "const_bool", "const_int", "const_float",
        "const_string", "const_null", "local_load", "local_move",
        "local_store", "local_drop", "value_clone", "value_discard",
        "add_checked", "sub_checked", "mul_checked", "div_checked",
        "rem_checked", "shift_left_checked", "shift_right_checked",
        "bit_and", "bit_or", "bit_xor", "bit_not",
        "add_float", "sub_float", "mul_float", "div_float", "negate",
        "not", "equal", "not_equal", "less", "less_equal", "greater",
        "greater_equal", "cast", "function_ref", "call_direct",
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
        "raw_load", "raw_store", "element_begin",
        "local_element_property",
        "local_element_property_begin",
        "local_element_property_append",
        "local_element_css_value",
        "local_element_property_end",
        "local_element_append",
        "local_element_append_raw_text",
        "local_element_append_formatted",
        "local_element_finish"
    };
    size_t index = (size_t)opcode;
    return index < sizeof(names) / sizeof(names[0])
         ? names[index] : "<invalid-opcode>";
}

static bool verify_type(const IrModule *ir, IrTypeId type) {
    return type != IR_INVALID_ID && (size_t)type < ir->type_count;
}

static bool verify_value(const IrFunction *function, IrValueId value) {
    return value != IR_INVALID_ID && (size_t)value < function->value_count;
}

static bool operand_count_valid(const IrInstruction *instruction) {
    switch (instruction->opcode) {
        case IR_OP_PARAMETER: case IR_OP_UNIT: case IR_OP_CONST_BOOL:
        case IR_OP_CONST_INT: case IR_OP_CONST_FLOAT:
        case IR_OP_CONST_STRING: case IR_OP_CONST_NULL:
        case IR_OP_LOCAL_LOAD: case IR_OP_LOCAL_MOVE:
        case IR_OP_LOCAL_DROP: case IR_OP_FUNCTION_REF:
        case IR_OP_LOCAL_FIELD_GET: case IR_OP_LOCAL_FIELD_MOVE:
        case IR_OP_LOCAL_FIELD_BORROW:
        case IR_OP_LOCAL_ENUM_IS:
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE:
        case IR_OP_LOCAL_ITERATOR_HAS_NEXT:
        case IR_OP_LOCAL_ITERATOR_NEXT:
        case IR_OP_ELEMENT_BEGIN:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_END:
        case IR_OP_LOCAL_ELEMENT_APPEND_RAW_TEXT:
        case IR_OP_LOCAL_ELEMENT_FINISH:
        case IR_OP_EXCEPTION_PENDING:
        case IR_OP_EXCEPTION_MATCH:
        case IR_OP_EXCEPTION_TAKE:
            return instruction->operand_count == 0U;
        case IR_OP_BORROWED_ITERATOR_BEGIN:
            return instruction->operand_count <= 1U;
        case IR_OP_LOCAL_STORE: case IR_OP_VALUE_CLONE:
        case IR_OP_VALUE_DISCARD: case IR_OP_NEGATE: case IR_OP_NOT:
        case IR_OP_BIT_NOT:
        case IR_OP_CAST: case IR_OP_FIELD_GET: case IR_OP_FIELD_SET:
        case IR_OP_LOCAL_FIELD_SET: case IR_OP_LOCAL_INDEX_GET:
        case IR_OP_ITERATOR_BEGIN:
        case IR_OP_RAW_LOAD:
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
        case IR_OP_CALL_DIRECT: case IR_OP_CALL_INDIRECT:
        case IR_OP_CALL_NATIVE: case IR_OP_AGGREGATE_MAKE:
            return true;
    }
    return false;
}

static bool result_type_valid(const IrModule *ir,
                              const IrFunction *function,
                              const IrInstruction *instruction) {
    if (instruction->result == IR_INVALID_ID ||
        !verify_type(ir, instruction->result_type))
        return true;
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
        case IR_OP_RAW_ALLOC:
            return shape == IR_TYPE_RAW_POINTER;
        case IR_OP_RAW_STORE:
            return shape == IR_TYPE_UNIT;
        case IR_OP_PARAMETER:
            return instruction->index < function->parameter_count &&
                   instruction->result_type ==
                       function->parameter_types[instruction->index];
        case IR_OP_AGGREGATE_MAKE:
            return shape == IR_TYPE_ARRAY || shape == IR_TYPE_STRUCT ||
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

static bool verify_element_child_type(
    const IrModule *ir, IrTypeId type_id) {
    if (!verify_type(ir, type_id)) return false;
    const IrType *type = &ir->types[type_id];
    if (type->shape == IR_TYPE_STRING_VIEW ||
        strcmp(type->name, "string") == 0 ||
        strcmp(type->name, "Html") == 0)
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
                   value_is_type(
                       function, instruction->operands[0], local->type);
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
                iterator->argument_count != 1U)
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
        case IR_OP_LOCAL_ELEMENT_APPEND_RAW_TEXT:
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
                    IR_OP_LOCAL_ELEMENT_APPEND_RAW_TEXT)
                return instruction->operand_count == 0U &&
                       instruction->symbol != NULL;
            if (instruction->operand_count != 1U ||
                !verify_value(
                    function, instruction->operands[0]))
                return false;
            if (instruction->opcode ==
                IR_OP_LOCAL_ELEMENT_APPEND)
                return verify_element_child_type(
                    ir, function->value_types[
                        instruction->operands[0]]);
            if (instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND ||
                instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_CSS_VALUE ||
                instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED) {
                const IrType *value = &ir->types[
                    function->value_types[
                        instruction->operands[0]]];
                return value->shape == IR_TYPE_STRING_VIEW ||
                       value->shape == IR_TYPE_BOOL ||
                       value->shape == IR_TYPE_SIGNED_INT ||
                       value->shape == IR_TYPE_UNSIGNED_INT ||
                       value->shape == IR_TYPE_FLOAT ||
                       value->shape == IR_TYPE_CHAR ||
                       (value->shape ==
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
            if (aggregate->shape != IR_TYPE_STRUCT)
                return true;
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
            if (aggregate->shape != IR_TYPE_STRUCT)
                return true;
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
            IrTypeShape index_shape =
                ir->types[function->value_types[
                    instruction->operands[0]]].shape;
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
            IrTypeShape index_shape =
                ir->types[function->value_types[
                    instruction->operands[1]]].shape;
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
            } else if (aggregate->shape == IR_TYPE_STRUCT) {
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
            return true;
        }
        case IR_OP_CALL_DIRECT: {
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
                instruction->result_type != target->return_type)
                return false;
            for (size_t i = 0U; i < instruction->operand_count; ++i)
                if (!value_is_type(
                        function, instruction->operands[i],
                        target->parameter_types[i]))
                    return false;
            return true;
        }
        case IR_OP_AWAIT: {
            if (!function->is_async ||
                instruction->operand_count != 1U ||
                !verify_value(function, instruction->operands[0]))
                return false;
            IrTypeId task_type = function->value_types[
                instruction->operands[0]];
            if (!verify_type(ir, task_type)) return false;
            const IrType *task = &ir->types[task_type];
            return task->shape == IR_TYPE_BUILTIN_OBJECT &&
                   strncmp(task->name, "Task", 4U) == 0 &&
                   task->element_type != IR_INVALID_ID &&
                   instruction->result_type == task->element_type;
        }
        default:
            return true;
    }
}

bool lang_ir_verify_module(const IrModule *ir,
                           LangDiagnostics *diagnostics) {
    bool ok = true;
    for (size_t t = 0U; t < ir->type_count; ++t) {
        const IrType *type = &ir->types[t];
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
                    destructor->parameter_types[0] !=
                        (IrTypeId)t) {
                    lang_diag(
                        diagnostics,
                        (LangSpan){NULL, 0U, 0U},
                        "IR type t%zu `%s` has incompatible destructor f%" PRIu32,
                        t, type->name,
                        type->destructor_function);
                    ok = false;
                }
            }
        }
        for (size_t a = 0U; a < type->argument_count; ++a)
            if (!verify_type(ir, type->argument_types[a])) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR type t%zu has an invalid argument type", t);
                ok = false;
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
             !verify_type(ir, type->argument_types[0]))) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR iterator type t%zu has invalid source metadata",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_ELEMENT_BUILDER &&
            (!verify_type(ir, type->element_type) ||
             strcmp(ir->types[type->element_type].name, "Html") != 0)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR element builder type t%zu has invalid result metadata",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_STRUCT) {
            if (type->field_count != 0U &&
                (type->field_names == NULL ||
                 type->field_types == NULL)) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR struct type t%zu has incomplete field metadata",
                          t);
                ok = false;
            } else if (type->field_count != 0U) {
                for (size_t field = 0U;
                     field < type->field_count; ++field) {
                    if (type->field_names[field] == NULL) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR struct type t%zu has an invalid field name",
                            t);
                        ok = false;
                    }
                    if (!verify_type(ir, type->field_types[field])) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR struct type t%zu has an invalid field type",
                            t);
                        ok = false;
                    }
                }
            }
        }
        if (type->shape == IR_TYPE_ENUM ||
            type->shape == IR_TYPE_UNION) {
            if (type->variant_count == 0U ||
                type->variant_names == NULL ||
                type->variant_payload_types == NULL) {
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
                }
            }
        }
    }
    for (size_t f = 0U; f < ir->function_count; ++f) {
        const IrFunction *function = &ir->functions[f];
        LangSpan function_span = function->declaration != NULL
                               ? function->declaration->span
                               : (LangSpan){NULL, 0U, 0U};
        if (!verify_type(ir, function->return_type) ||
            !verify_type(ir, function->async_result_type) ||
            (function->is_async &&
             (ir->types[function->return_type].shape !=
                  IR_TYPE_BUILTIN_OBJECT ||
              strncmp(ir->types[function->return_type].name,
                      "Task", 4U) != 0 ||
              ir->types[function->return_type].element_type !=
                  function->async_result_type)) ||
            function->block_count == 0U ||
            function->entry_block >= function->block_count) {
            lang_diag(diagnostics, function_span,
                      "malformed IR function `%s`", function->name);
            ok = false;
            continue;
        }
        for (size_t p = 0U; p < function->parameter_count; ++p)
            if (!verify_type(ir, function->parameter_types[p])) {
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
        bool *defined = ir_resize(
            NULL, function->value_count, sizeof(*defined));
        memset(defined, 0, function->value_count * sizeof(*defined));
        for (size_t b = 0U; b < function->block_count; ++b) {
            const IrBlock *block = &function->blocks[b];
            if (block->terminator.kind == IR_TERM_NONE) {
                lang_diag(diagnostics, function_span,
                          "IR block b%zu in `%s` has no terminator",
                          b, function->name);
                ok = false;
            }
            for (size_t i = 0U; i < block->instruction_count; ++i) {
                const IrInstruction *instruction =
                    &block->instructions[i];
                if (!operand_count_valid(instruction)) {
                    lang_diag(diagnostics, instruction->span,
                              "IR `%s` has an invalid operand count",
                              ir_opcode_name(instruction->opcode));
                    ok = false;
                }
                if (!result_type_valid(ir, function, instruction)) {
                    lang_diag(diagnostics, instruction->span,
                              "IR `%s` has an incompatible result type",
                              ir_opcode_name(instruction->opcode));
                    ok = false;
                }
                if (!instruction_signature_valid(
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
                }
                if (((instruction->opcode == IR_OP_ELEMENT_BEGIN &&
                      instruction->index != IR_INVALID_ID) ||
                     instruction->opcode == IR_OP_LOCAL_LOAD ||
                     instruction->opcode == IR_OP_LOCAL_MOVE ||
                     instruction->opcode == IR_OP_LOCAL_STORE ||
                     instruction->opcode == IR_OP_LOCAL_DROP ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_GET ||
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
                         IR_OP_LOCAL_ELEMENT_APPEND ||
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
                if (instruction->opcode == IR_OP_CALL_DIRECT &&
                    instruction->index >= ir->function_count) {
                    lang_diag(diagnostics, instruction->span,
                              "IR direct call target is invalid");
                    ok = false;
                }
                if (instruction->label_count != 0U &&
                    (instruction->opcode != IR_OP_AGGREGATE_MAKE ||
                     instruction->label_count !=
                         instruction->operand_count)) {
                    lang_diag(diagnostics, instruction->span,
                              "IR aggregate field mapping is malformed");
                    ok = false;
                }
                if (instruction->label_count != 0U) {
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
        for (size_t b = 0U; b < function->block_count; ++b) {
            const IrBlock *block = &function->blocks[b];
            for (size_t i = 0U; i < block->instruction_count; ++i) {
                const IrInstruction *instruction =
                    &block->instructions[i];
                for (size_t o = 0U; o < instruction->operand_count; ++o)
                    if (!verify_value(function,
                                      instruction->operands[o]) ||
                        !defined[instruction->operands[o]]) {
                        lang_diag(diagnostics, instruction->span,
                                  "IR instruction references undefined value");
                        ok = false;
                    }
            }
            const IrTerminator *term = &block->terminator;
            if ((term->kind == IR_TERM_JUMP ||
                 term->kind == IR_TERM_BRANCH) &&
                term->target >= function->block_count) {
                lang_diag(diagnostics, term->span,
                          "IR terminator references invalid block");
                ok = false;
            }
            if (term->kind == IR_TERM_BRANCH &&
                (term->alternate >= function->block_count ||
                 !verify_value(function, term->value) ||
                 !defined[term->value] ||
                 (verify_value(function, term->value) &&
                  (!verify_type(
                       ir, function->value_types[term->value]) ||
                   ir->types[
                       function->value_types[term->value]].shape !=
                       IR_TYPE_BOOL)))) {
                lang_diag(diagnostics, term->span,
                          "IR branch has invalid operands");
                ok = false;
            }
            if (term->kind == IR_TERM_RETURN &&
                (!verify_value(function, term->value) ||
                 !defined[term->value] ||
                 (verify_value(function, term->value) &&
                  function->value_types[term->value] !=
                      function->async_result_type))) {
                lang_diag(diagnostics, term->span,
                          "IR return has invalid value");
                ok = false;
            }
        }
        free(defined);
    }
    return ok;
}
