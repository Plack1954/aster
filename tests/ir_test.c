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

int main(void) {
    LangSource source;
    if (!lang_source_load("tests/ir_test.lang", &source))
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
        contains_opcode(&ir, IR_OP_ITERATOR_BEGIN) &&
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
    bool has_control_flow = false;
    bool rejected_malformed = false;
    bool rejected_bad_struct_metadata = false;
    bool rejected_bad_struct_field_type = false;
    bool rejected_bad_variant_metadata = false;
    bool rejected_bad_variant_payload_type = false;
    bool rejected_bad_enum_constructor = false;
    bool rejected_bad_destructor = false;
    if (initially_valid && ir.function_count != 0U) {
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
            branch->alternate = IR_INVALID_ID;
            rejected_malformed =
                !lang_ir_verify_module(&ir, &diagnostics);
        }
    }
    lang_ir_free_module(&ir);
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    bool passed =
           initially_valid && has_typed_math && has_drop &&
           has_aggregate && has_field_get && has_field_set &&
           has_index_get && has_index_set && has_enum_test &&
           has_payload_move && has_iterator && has_raw_pointer &&
           has_elements && has_component_call &&
           has_try_cleanup &&
           preserves_field_order && has_field_metadata &&
           has_variant_metadata && has_plain_enum_metadata &&
           has_destructor_metadata &&
           rejected_bad_struct_metadata &&
           rejected_bad_struct_field_type &&
           rejected_bad_variant_metadata &&
           rejected_bad_variant_payload_type &&
           rejected_bad_enum_constructor &&
           rejected_bad_destructor &&
           has_control_flow && rejected_malformed;
    if (!passed)
        fprintf(
            stderr,
            "IR assertions: valid=%d metadata=%d/%d/%d/%d corrupt=%d/%d/%d/%d/%d/%d control=%d malformed=%d\n",
            initially_valid, has_field_metadata,
            has_variant_metadata, has_plain_enum_metadata,
            has_destructor_metadata,
            rejected_bad_struct_metadata,
            rejected_bad_struct_field_type,
            rejected_bad_variant_metadata,
            rejected_bad_variant_payload_type,
            rejected_bad_enum_constructor,
            rejected_bad_destructor,
            has_control_flow, rejected_malformed);
    return passed ? 0 : 2;
}
