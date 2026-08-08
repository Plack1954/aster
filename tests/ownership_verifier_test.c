#include "internal.h"

#include <stdbool.h>
#include <stddef.h>

int main(void) {
    LangSource source;
    if (!lang_source_load("tests/projection_moves.as", &source)) return 1;
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool ok = lang_parse_module(&source, &diagnostics, &module);
    if (ok) ok = lang_check_module(&module, &diagnostics);
    LangTargetInfo target;
    lang_target_host(&target);
    IrModule ir = {0};
    if (ok)
        ok = lang_ir_lower_module(
            &module, &target, &diagnostics, &ir);
    if (ok) ok = lang_ir_verify_module(&ir, &diagnostics);

    IrInstruction *first_field = NULL;
    IrInstruction *second_field = NULL;
    IrInstruction *first_index = NULL;
    IrInstruction *second_index = NULL;
    for (size_t f = 0U; f < ir.function_count; ++f)
        for (size_t b = 0U; b < ir.functions[f].block_count; ++b)
            for (size_t i = 0U;
                 i < ir.functions[f].blocks[b].instruction_count; ++i) {
                IrInstruction *instruction =
                    &ir.functions[f].blocks[b].instructions[i];
                if (instruction->opcode == IR_OP_LOCAL_FIELD_MOVE) {
                    if (first_field == NULL)
                        first_field = instruction;
                    else if (second_field == NULL &&
                             instruction->index == first_field->index &&
                             instruction->auxiliary !=
                                 first_field->auxiliary)
                        second_field = instruction;
                }
                if (instruction->opcode == IR_OP_LOCAL_INDEX_MOVE &&
                    instruction->has_constant_index) {
                    if (first_index == NULL)
                        first_index = instruction;
                    else if (second_index == NULL &&
                             instruction->index == first_index->index &&
                             instruction->constant_index !=
                                 first_index->constant_index)
                        second_index = instruction;
                }
            }

    bool rejected_repeated_field = false;
    if (ok && first_field != NULL && second_field != NULL) {
        uint32_t saved_field = second_field->auxiliary;
        const char *saved_symbol = second_field->symbol;
        size_t saved_length = second_field->symbol_length;
        second_field->auxiliary = first_field->auxiliary;
        second_field->symbol = first_field->symbol;
        second_field->symbol_length = first_field->symbol_length;
        rejected_repeated_field =
            !lang_ir_verify_module(&ir, &diagnostics);
        second_field->auxiliary = saved_field;
        second_field->symbol = saved_symbol;
        second_field->symbol_length = saved_length;
    }

    bool rejected_repeated_index = false;
    if (ok && first_index != NULL && second_index != NULL) {
        uint64_t saved_constant = second_index->constant_index;
        IrValueId saved_operand = second_index->operands[0];
        second_index->constant_index = first_index->constant_index;
        second_index->operands[0] = first_index->operands[0];
        rejected_repeated_index =
            !lang_ir_verify_module(&ir, &diagnostics);
        second_index->constant_index = saved_constant;
        second_index->operands[0] = saved_operand;
    }

    bool rejected_unproven_index = false;
    if (ok && first_index != NULL) {
        uint64_t saved_constant = first_index->constant_index;
        first_index->constant_index = saved_constant == 0U ? 1U : 0U;
        rejected_unproven_index =
            !lang_ir_verify_module(&ir, &diagnostics);
        first_index->constant_index = saved_constant;
    }

    bool restored = ok && lang_ir_verify_module(&ir, &diagnostics);
    lang_ir_free_module(&ir);
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    return restored && rejected_repeated_field && rejected_repeated_index &&
        rejected_unproven_index ? 0 : 2;
}
