#include "internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static bool contains_opcode(const IrModule *ir, IrOpcode opcode) {
    for (size_t f = 0U; f < ir->function_count; ++f)
        for (size_t b = 0U; b < ir->functions[f].block_count; ++b)
            for (size_t i = 0U;
                 i < ir->functions[f].blocks[b].instruction_count; ++i)
                if (ir->functions[f].blocks[b].instructions[i].opcode ==
                    opcode)
                    return true;
    return false;
}

static bool contains_reordered_fields(const IrModule *ir) {
    for (size_t f = 0U; f < ir->function_count; ++f)
        for (size_t b = 0U; b < ir->functions[f].block_count; ++b)
            for (size_t i = 0U;
                 i < ir->functions[f].blocks[b].instruction_count; ++i) {
                const IrInstruction *instruction =
                    &ir->functions[f].blocks[b].instructions[i];
                if (instruction->opcode == IR_OP_AGGREGATE_MAKE &&
                    instruction->symbol != NULL &&
                    instruction->symbol_length == 4U &&
                    memcmp(instruction->symbol, "Pair", 4U) == 0 &&
                    instruction->label_count == 2U &&
                    instruction->labels[0] == 1U &&
                    instruction->labels[1] == 0U)
                    return true;
            }
    return false;
}

static bool contains_try_cleanup_edge(const IrModule *ir) {
    for (size_t f = 0U; f < ir->function_count; ++f)
        for (size_t b = 0U; b < ir->functions[f].block_count; ++b) {
            const IrBlock *block = &ir->functions[f].blocks[b];
            bool takes_error = false;
            bool drops_local = false;
            for (size_t i = 0U; i < block->instruction_count; ++i) {
                const IrInstruction *instruction =
                    &block->instructions[i];
                if (instruction->opcode ==
                        IR_OP_LOCAL_ENUM_PAYLOAD_MOVE &&
                    instruction->symbol != NULL &&
                    instruction->symbol_length == 3U &&
                    memcmp(instruction->symbol, "Err", 3U) == 0)
                    takes_error = true;
                if (instruction->opcode == IR_OP_LOCAL_DROP)
                    drops_local = true;
            }
            if (takes_error && drops_local &&
                block->terminator.kind == IR_TERM_RETURN)
                return true;
        }
    return false;
}

static bool contains_pair_type_metadata(const IrModule *ir) {
    for (size_t i = 0U; i < ir->type_count; ++i) {
        const IrType *type = &ir->types[i];
        if (type->shape == IR_TYPE_STRUCT &&
            strcmp(type->name, "Pair") == 0 &&
            type->field_count == 2U &&
            strcmp(type->field_names[0], "first") == 0 &&
            strcmp(type->field_names[1], "second") == 0 &&
            type->field_types != NULL &&
            type->member_layout_known &&
            type->field_offsets != NULL &&
            type->field_offsets[0] == 0U &&
            type->field_offsets[1] > type->field_offsets[0] &&
            type->field_types[0] < ir->type_count &&
            type->field_types[1] < ir->type_count &&
            ir->types[type->field_types[0]].shape ==
                IR_TYPE_SIGNED_INT &&
            ir->types[type->field_types[1]].shape ==
                IR_TYPE_SIGNED_INT)
            return true;
    }
    return false;
}

static bool contains_result_variant_metadata(const IrModule *ir) {
    for (size_t i = 0U; i < ir->type_count; ++i) {
        const IrType *type = &ir->types[i];
        if (type->shape == IR_TYPE_UNION &&
            type->error_type != IR_INVALID_ID &&
            type->variant_count == 2U &&
            strcmp(type->variant_names[0], "Ok") == 0 &&
            strcmp(type->variant_names[1], "Err") == 0 &&
            type->variant_payload_types != NULL &&
            type->member_layout_known &&
            type->variant_payload_offsets != NULL &&
            type->variant_payload_types[0] ==
                type->element_type &&
            type->variant_payload_types[1] ==
                type->error_type)
            return true;
    }
    return false;
}

static bool contains_plain_enum_metadata(const IrModule *ir) {
    for (size_t i = 0U; i < ir->type_count; ++i) {
        const IrType *type = &ir->types[i];
        if (type->shape == IR_TYPE_ENUM &&
            strcmp(type->name, "Phase") == 0 &&
            type->variant_count == 2U &&
            strcmp(type->variant_names[0], "Start") == 0 &&
            strcmp(type->variant_names[1], "Done") == 0 &&
            type->variant_payload_types != NULL &&
            type->variant_payload_types[0] == IR_INVALID_ID &&
            type->variant_payload_types[1] == IR_INVALID_ID)
            return true;
    }
    return false;
}

static bool contains_destructor_metadata(const IrModule *ir) {
    for (size_t i = 0U; i < ir->type_count; ++i) {
        const IrType *type = &ir->types[i];
        if (strcmp(type->name, "Marker") == 0 &&
            type->requires_cleanup &&
            type->target_layout_known &&
            type->target_alignment != 0U &&
            type->destructor_function < ir->function_count &&
            ir->functions[
                type->destructor_function].is_destructor)
            return true;
    }
    return false;
}

static bool contains_complete_function_metadata(const IrModule *ir) {
    for (size_t f = 0U; f < ir->function_count; ++f) {
        const IrFunction *function = &ir->functions[f];
        if (strcmp(function->name, "factorial") != 0)
            continue;
        return function->span.file != NULL &&
               function->abi.calling_convention ==
                   IR_CALLING_CONVENTION_ASTER &&
               function->abi.may_propagate_exception &&
               !function->abi.returns_async_task &&
               function->parameter_count == 1U &&
               function->parameters != NULL &&
               strcmp(function->parameters[0].name, "n") == 0 &&
               function->parameters[0].span.file != NULL &&
               function->parameters[0].mode == PARAMETER_MODE_VALUE &&
               function->parameters[0].type < ir->type_count;
    }
    return false;
}

static bool contains_complete_native_metadata(const IrModule *ir) {
    for (size_t f = 0U; f < ir->function_count; ++f)
        for (size_t b = 0U; b < ir->functions[f].block_count; ++b)
            for (size_t i = 0U;
                 i < ir->functions[f].blocks[b].instruction_count; ++i) {
                const IrInstruction *call =
                    &ir->functions[f].blocks[b].instructions[i];
                if (call->opcode != IR_OP_CALL_NATIVE ||
                    call->native_call == NULL)
                    continue;
                const IrNativeCallDescriptor *native = call->native_call;
                if (native->name != call->symbol ||
                    native->return_type != call->result_type ||
                    native->parameter_count != call->operand_count ||
                    native->calling_convention !=
                        IR_CALLING_CONVENTION_NATIVE)
                    return false;
                for (size_t p = 0U; p < native->parameter_count; ++p)
                    if (native->parameter_types[p] >= ir->type_count ||
                        native->parameter_modes[p] !=
                            call->argument_modes[p])
                        return false;
                if (strcmp(native->name, "NativeHandleOpenId") == 0 &&
                    !native->compiler_generated)
                    return true;
            }
    return false;
}

static bool contains_complete_type_policy(const IrModule *ir) {
    bool trivial = false;
    bool shared = false;
    bool noncopyable = false;
    bool custom_drop = false;
    for (size_t t = 0U; t < ir->type_count; ++t) {
        const IrType *type = &ir->types[t];
        trivial |= type->shape == IR_TYPE_SIGNED_INT &&
                   type->copy_policy == IR_COPY_TRIVIAL &&
                   type->drop_policy == IR_DROP_TRIVIAL;
        shared |= strcmp(type->name, "string") == 0 &&
                  type->copy_policy == IR_COPY_SHARED_RETAIN;
        noncopyable |= strcmp(type->name, "Arena") == 0 &&
                       type->copy_policy == IR_COPY_NONCOPYABLE;
        custom_drop |= strcmp(type->name, "Marker") == 0 &&
                       type->copy_policy == IR_COPY_DEEP &&
                       type->drop_policy == IR_DROP_CUSTOM &&
                       type->destructor_function < ir->function_count;
    }
    return trivial && shared && noncopyable && custom_drop;
}

static bool contains_static_css_metadata(const IrModule *ir) {
    for (size_t f = 0U; f < ir->function_count; ++f)
        for (size_t css = 0U;
             css < ir->functions[f].static_css_count; ++css) {
            const IrStaticCss *entry = &ir->functions[f].static_css[css];
            if (entry->scope_attribute != NULL && entry->text != NULL &&
                entry->text_length != 0U && entry->span.file != NULL)
                return true;
        }
    return false;
}

int main(void) {
    LangSource source;
    if (!lang_source_load("tests/ir_test.as", &source))
        return 1;
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool ok = lang_parse_module(&source, &diagnostics, &module);
    if (ok) ok = lang_check_module(&module, &diagnostics);
    LangTargetInfo target;
    lang_target_host(&target);
    IrModule ir;
    if (ok)
        ok = lang_ir_lower_module(
            &module, &target, &diagnostics, &ir);
    else
        ir = (IrModule){0};
    bool initially_valid =
        ok && lang_ir_verify_module(&ir, &diagnostics);
    bool closed_backend_input = initially_valid;
    for (size_t f = 0U; f < ir.function_count; ++f)
        closed_backend_input &= ir.functions[f].declaration == NULL;
    for (size_t t = 0U; t < ir.type_count; ++t)
        closed_backend_input &= ir.types[t].checked_type == NULL;
    BytecodeModule bytecode = {0};
    bool bytecode_from_closed_ir = closed_backend_input &&
        lang_ir_compile_bytecode(&ir, &diagnostics, &bytecode);
    lang_bytecode_free(&bytecode);
    FILE *generated = tmpfile();
    bool c_from_closed_ir = generated != NULL && closed_backend_input &&
        lang_c_emit_module(&ir, &diagnostics, generated);
    if (generated != NULL) fclose(generated);
    bool has_typed_math = contains_opcode(&ir, IR_OP_MUL_CHECKED);
    bool has_drop = contains_opcode(&ir, IR_OP_LOCAL_DROP);
    bool has_aggregate = contains_opcode(&ir, IR_OP_AGGREGATE_MAKE);
    bool has_field_get = contains_opcode(&ir, IR_OP_LOCAL_FIELD_GET);
    bool has_field_set = contains_opcode(&ir, IR_OP_LOCAL_FIELD_SET);
    bool has_index_get = contains_opcode(&ir, IR_OP_LOCAL_INDEX_GET);
    bool has_index_set = contains_opcode(&ir, IR_OP_LOCAL_INDEX_SET);
    bool has_enum_test = contains_opcode(&ir, IR_OP_LOCAL_ENUM_IS);
    bool has_payload_move =
        contains_opcode(&ir, IR_OP_LOCAL_ENUM_PAYLOAD_MOVE);
    bool has_iterator =
        contains_opcode(&ir, IR_OP_BORROWED_ITERATOR_BEGIN) &&
        contains_opcode(&ir, IR_OP_LOCAL_ITERATOR_HAS_NEXT) &&
        contains_opcode(&ir, IR_OP_LOCAL_ITERATOR_NEXT);
    bool has_raw_pointer =
        contains_opcode(&ir, IR_OP_RAW_ALLOC) &&
        contains_opcode(&ir, IR_OP_RAW_LOAD) &&
        contains_opcode(&ir, IR_OP_RAW_STORE);
    bool has_elements =
        contains_opcode(&ir, IR_OP_ELEMENT_BEGIN) &&
        contains_opcode(&ir, IR_OP_LOCAL_ELEMENT_APPEND) &&
        contains_opcode(&ir, IR_OP_LOCAL_ELEMENT_FINISH);
    bool has_component_call =
        contains_opcode(&ir, IR_OP_CALL_DIRECT);
    bool has_try_cleanup = contains_try_cleanup_edge(&ir);
    bool preserves_field_order = contains_reordered_fields(&ir);
    bool has_field_metadata = contains_pair_type_metadata(&ir);
    bool has_variant_metadata =
        contains_result_variant_metadata(&ir);
    bool has_plain_enum_metadata =
        contains_plain_enum_metadata(&ir);
    bool has_destructor_metadata =
        contains_destructor_metadata(&ir);
    bool has_function_metadata =
        contains_complete_function_metadata(&ir);
    bool has_native_metadata = contains_complete_native_metadata(&ir);
    bool has_type_policy = contains_complete_type_policy(&ir);
    bool has_static_css = contains_static_css_metadata(&ir);
    bool has_control_flow = false;
    bool rejected_malformed = false;
    bool rejected_bad_struct_metadata = false;
    bool rejected_bad_struct_field_type = false;
    bool rejected_bad_variant_metadata = false;
    bool rejected_bad_variant_payload_type = false;
    bool rejected_bad_enum_constructor = false;
    bool rejected_bad_destructor = false;
    bool rejected_bad_parameter_metadata = false;
    bool rejected_bad_native_metadata = false;
    bool rejected_bad_copy_policy = false;
    bool rejected_bad_async_metadata = false;
    bool rejected_frontend_link = false;
    bool rejected_bad_static_css = false;
    bool rejected_unknown_opcode = false;
    bool rejected_bad_operand_signature = false;
    bool rejected_bad_result_type = false;
    bool rejected_use_before_definition = false;
    bool rejected_non_dominating_value = false;
    bool rejected_unknown_terminator = false;
    if (initially_valid && ir.function_count != 0U) {
        IrInstruction *first_instruction = NULL;
        IrInstruction *multiply = NULL;
        IrInstruction *boolean_constant = NULL;
        IrTypeId integer_type = IR_INVALID_ID;
        for (size_t t = 0U; t < ir.type_count; ++t)
            if (ir.types[t].shape == IR_TYPE_SIGNED_INT) {
                integer_type = (IrTypeId)t;
                break;
            }
        for (size_t f = 0U; f < ir.function_count; ++f)
            if (ir.functions[f].parameter_count != 0U) {
                const char *saved = ir.functions[f].parameters[0].name;
                ir.functions[f].parameters[0].name = NULL;
                rejected_bad_parameter_metadata =
                    !lang_ir_verify_module(&ir, &diagnostics);
                ir.functions[f].parameters[0].name = saved;
                break;
            }
        for (size_t f = 0U;
             f < ir.function_count && !rejected_bad_native_metadata; ++f)
            for (size_t b = 0U;
                 b < ir.functions[f].block_count &&
                 !rejected_bad_native_metadata; ++b)
                for (size_t i = 0U;
                     i < ir.functions[f].blocks[b].instruction_count; ++i) {
                    IrInstruction *instruction =
                        &ir.functions[f].blocks[b].instructions[i];
                    if (instruction->opcode != IR_OP_CALL_NATIVE ||
                        instruction->native_call == NULL)
                        continue;
                    IrTypeId saved = instruction->native_call->return_type;
                    instruction->native_call->return_type = IR_INVALID_ID;
                    rejected_bad_native_metadata =
                        !lang_ir_verify_module(&ir, &diagnostics);
                    instruction->native_call->return_type = saved;
                    break;
                }
        for (size_t t = 0U; t < ir.type_count; ++t)
            if (ir.types[t].requires_cleanup || ir.types[t].managed) {
                IrCopyPolicy saved = ir.types[t].copy_policy;
                ir.types[t].copy_policy = IR_COPY_TRIVIAL;
                rejected_bad_copy_policy =
                    !lang_ir_verify_module(&ir, &diagnostics);
                ir.types[t].copy_policy = saved;
                break;
            }
        ir.functions[0].async_suspension_count++;
        rejected_bad_async_metadata =
            !lang_ir_verify_module(&ir, &diagnostics);
        ir.functions[0].async_suspension_count--;
        ir.functions[0].declaration = module.decls[0];
        rejected_frontend_link =
            !lang_ir_verify_module(&ir, &diagnostics);
        ir.functions[0].declaration = NULL;
        for (size_t f = 0U;
             f < ir.function_count && !rejected_bad_static_css; ++f)
            if (ir.functions[f].static_css_count != 0U) {
                const char *saved =
                    ir.functions[f].static_css[0].scope_attribute;
                ir.functions[f].static_css[0].scope_attribute = NULL;
                rejected_bad_static_css =
                    !lang_ir_verify_module(&ir, &diagnostics);
                ir.functions[f].static_css[0].scope_attribute = saved;
            }
        for (size_t f = 0U; f < ir.function_count; ++f)
            for (size_t b = 0U; b < ir.functions[f].block_count; ++b)
                for (size_t i = 0U;
                     i < ir.functions[f].blocks[b].instruction_count; ++i) {
                    IrInstruction *instruction =
                        &ir.functions[f].blocks[b].instructions[i];
                    if (first_instruction == NULL)
                        first_instruction = instruction;
                    if (multiply == NULL && instruction->opcode ==
                            IR_OP_MUL_CHECKED)
                        multiply = instruction;
                    if (boolean_constant == NULL && instruction->opcode ==
                            IR_OP_GREATER)
                        boolean_constant = instruction;
                }
        if (first_instruction != NULL) {
            IrOpcode saved = first_instruction->opcode;
            first_instruction->opcode = IR_OP_COUNT;
            rejected_unknown_opcode =
                !lang_ir_verify_module(&ir, &diagnostics);
            first_instruction->opcode = saved;
        }
        if (multiply != NULL) {
            IrOpcode saved_opcode = multiply->opcode;
            multiply->opcode = IR_OP_ADD_FLOAT;
            rejected_bad_operand_signature =
                !lang_ir_verify_module(&ir, &diagnostics);
            multiply->opcode = saved_opcode;

            IrValueId saved_operand = multiply->operands[0];
            multiply->operands[0] = multiply->result;
            rejected_use_before_definition =
                !lang_ir_verify_module(&ir, &diagnostics);
            multiply->operands[0] = saved_operand;
        }
        if (boolean_constant != NULL && integer_type != IR_INVALID_ID) {
            IrFunction *owner = NULL;
            for (size_t f = 0U; f < ir.function_count && owner == NULL; ++f)
                for (size_t b = 0U;
                     b < ir.functions[f].block_count && owner == NULL; ++b)
                    for (size_t i = 0U;
                         i < ir.functions[f].blocks[b].instruction_count;
                         ++i)
                        if (&ir.functions[f].blocks[b].instructions[i] ==
                            boolean_constant)
                            owner = &ir.functions[f];
            if (owner != NULL && boolean_constant->result <
                owner->value_count) {
                IrTypeId saved_type = boolean_constant->result_type;
                IrTypeId saved_value_type =
                    owner->value_types[boolean_constant->result];
                boolean_constant->result_type = integer_type;
                owner->value_types[boolean_constant->result] = integer_type;
                rejected_bad_result_type =
                    !lang_ir_verify_module(&ir, &diagnostics);
                boolean_constant->result_type = saved_type;
                owner->value_types[boolean_constant->result] =
                    saved_value_type;
            }
        }
        for (size_t f = 0U;
             f < ir.function_count && !rejected_non_dominating_value; ++f) {
            IrFunction *function = &ir.functions[f];
            IrTerminator *first_return = NULL;
            IrTerminator *second_return = NULL;
            for (size_t b = 0U; b < function->block_count; ++b) {
                IrTerminator *term = &function->blocks[b].terminator;
                if (term->kind != IR_TERM_RETURN ||
                    term->value >= function->value_count ||
                    function->value_types[term->value] !=
                        function->async_result_type)
                    continue;
                if (first_return == NULL)
                    first_return = term;
                else {
                    second_return = term;
                    break;
                }
            }
            if (first_return != NULL && second_return != NULL) {
                IrValueId saved = first_return->value;
                first_return->value = second_return->value;
                rejected_non_dominating_value =
                    !lang_ir_verify_module(&ir, &diagnostics);
                first_return->value = saved;
            }
        }
        for (size_t f = 0U;
             f < ir.function_count && !rejected_unknown_terminator; ++f)
            for (size_t b = 0U; b < ir.functions[f].block_count; ++b)
                if (ir.functions[f].blocks[b].terminator.kind ==
                    IR_TERM_TRAP) {
                    IrTerminator *term =
                        &ir.functions[f].blocks[b].terminator;
                    IrTerminatorKind saved = term->kind;
                    term->kind = (IrTerminatorKind)99;
                    rejected_unknown_terminator =
                        !lang_ir_verify_module(&ir, &diagnostics);
                    term->kind = saved;
                    break;
                }
        for (size_t t = 0U; t < ir.type_count; ++t)
            if (ir.types[t].shape == IR_TYPE_STRUCT &&
                ir.types[t].field_count != 0U) {
                const char *saved = ir.types[t].field_names[0];
                ir.types[t].field_names[0] = NULL;
                rejected_bad_struct_metadata =
                    !lang_ir_verify_module(&ir, &diagnostics);
                ir.types[t].field_names[0] = saved;
                IrTypeId saved_type = ir.types[t].field_types[0];
                ir.types[t].field_types[0] = IR_INVALID_ID;
                rejected_bad_struct_field_type =
                    !lang_ir_verify_module(&ir, &diagnostics);
                ir.types[t].field_types[0] = saved_type;
                break;
            }
        for (size_t f = 0U;
             f < ir.function_count &&
             !rejected_bad_enum_constructor; ++f)
            for (size_t b = 0U;
                 b < ir.functions[f].block_count &&
                 !rejected_bad_enum_constructor; ++b)
                for (size_t i = 0U;
                     i < ir.functions[f].blocks[b].instruction_count;
                     ++i) {
                    IrInstruction *instruction =
                        &ir.functions[f].blocks[b].instructions[i];
                    if (instruction->opcode !=
                            IR_OP_AGGREGATE_MAKE ||
                        instruction->result_type >= ir.type_count ||
                        ir.types[instruction->result_type].shape !=
                            IR_TYPE_UNION)
                        continue;
                    uint32_t saved = instruction->index;
                    instruction->index =
                        (uint32_t)ir.types[
                            instruction->result_type].variant_count;
                    rejected_bad_enum_constructor =
                        !lang_ir_verify_module(&ir, &diagnostics);
                    instruction->index = saved;
                    break;
                }
        for (size_t t = 0U; t < ir.type_count; ++t)
            if (ir.types[t].destructor_function !=
                IR_INVALID_ID) {
                IrFunctionId saved =
                    ir.types[t].destructor_function;
                ir.types[t].destructor_function =
                    (IrFunctionId)ir.function_count;
                rejected_bad_destructor =
                    !lang_ir_verify_module(&ir, &diagnostics);
                ir.types[t].destructor_function = saved;
                break;
            }
        for (size_t t = 0U; t < ir.type_count; ++t)
            if (ir.types[t].shape == IR_TYPE_UNION &&
                ir.types[t].variant_count != 0U) {
                const char *saved = ir.types[t].variant_names[0];
                ir.types[t].variant_names[0] = NULL;
                rejected_bad_variant_metadata =
                    !lang_ir_verify_module(&ir, &diagnostics);
                ir.types[t].variant_names[0] = saved;
                IrTypeId saved_payload =
                    ir.types[t].variant_payload_types[0];
                ir.types[t].variant_payload_types[0] =
                    (IrTypeId)ir.type_count;
                rejected_bad_variant_payload_type =
                    !lang_ir_verify_module(&ir, &diagnostics);
                ir.types[t].variant_payload_types[0] =
                    saved_payload;
                break;
            }
        IrTerminator *branch = NULL;
        for (size_t f = 0U; f < ir.function_count; ++f)
            for (size_t b = 0U;
                 b < ir.functions[f].block_count; ++b)
                if (ir.functions[f].blocks[b].terminator.kind ==
                    IR_TERM_BRANCH) {
                    branch = &ir.functions[f].blocks[b].terminator;
                }
        if (branch != NULL) {
            has_control_flow = true;
            IrBlockId saved = branch->alternate;
            branch->alternate = IR_INVALID_ID;
            rejected_malformed =
                !lang_ir_verify_module(&ir, &diagnostics);
            branch->alternate = saved;
        }
    }
    lang_ir_free_module(&ir);
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    bool passed =
           initially_valid && closed_backend_input &&
           bytecode_from_closed_ir && c_from_closed_ir &&
           has_typed_math && has_drop &&
           has_aggregate && has_field_get && has_field_set &&
           has_index_get && has_index_set && has_enum_test &&
           has_payload_move && has_iterator && has_raw_pointer &&
           has_elements && has_component_call &&
           has_try_cleanup &&
           preserves_field_order && has_field_metadata &&
           has_variant_metadata && has_plain_enum_metadata &&
           has_destructor_metadata &&
           has_function_metadata && has_native_metadata &&
           has_type_policy && has_static_css &&
           rejected_bad_struct_metadata &&
           rejected_bad_struct_field_type &&
           rejected_bad_variant_metadata &&
           rejected_bad_variant_payload_type &&
           rejected_bad_enum_constructor &&
           rejected_bad_destructor &&
           rejected_bad_parameter_metadata &&
           rejected_bad_native_metadata &&
           rejected_bad_copy_policy &&
           rejected_bad_async_metadata &&
           rejected_frontend_link && rejected_bad_static_css &&
           rejected_unknown_opcode &&
           rejected_bad_operand_signature &&
           rejected_bad_result_type &&
           rejected_use_before_definition &&
           rejected_non_dominating_value &&
           rejected_unknown_terminator &&
           has_control_flow && rejected_malformed;
    if (!passed)
        fprintf(
            stderr,
            "IR assertions: valid=%d metadata=%d/%d/%d/%d corrupt=%d/%d/%d/%d/%d/%d opcode=%d types=%d/%d dominance=%d/%d term=%d control=%d malformed=%d\n",
            initially_valid, has_field_metadata,
            has_variant_metadata, has_plain_enum_metadata,
            has_destructor_metadata,
            rejected_bad_struct_metadata,
            rejected_bad_struct_field_type,
            rejected_bad_variant_metadata,
            rejected_bad_variant_payload_type,
            rejected_bad_enum_constructor,
            rejected_bad_destructor,
            rejected_unknown_opcode,
            rejected_bad_operand_signature,
            rejected_bad_result_type,
            rejected_use_before_definition,
            rejected_non_dominating_value,
            rejected_unknown_terminator,
            has_control_flow, rejected_malformed);
    return passed ? 0 : 2;
}
