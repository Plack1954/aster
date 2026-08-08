#include "variation_source.h"

#include <stddef.h>
#include <stdint.h>

int ASTER_VARIATION_ENTRY(const uint8_t *data, size_t size);

static uint64_t variation_hash(const uint8_t *data, size_t size) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0U; i < size; ++i) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void vary_ir(IrModule *ir, uint64_t selector) {
    switch ((unsigned)(selector % 5U)) {
        case 0U:
            if (ir->type_count != 0U) {
                IrType *type = &ir->types[
                    (size_t)(selector % ir->type_count)];
                type->shape = (IrTypeShape)((selector >> 8U) & 31U);
                type->copy_policy =
                    (IrCopyPolicy)((selector >> 16U) & 7U);
                type->drop_policy =
                    (IrDropPolicy)((selector >> 24U) & 7U);
            }
            break;
        case 1U:
            if (ir->function_count != 0U) {
                IrFunction *function = &ir->functions[
                    (size_t)(selector % ir->function_count)];
                function->return_type =
                    (IrTypeId)(selector % (ir->type_count + 2U));
                function->entry_block =
                    (IrBlockId)((selector >> 16U) %
                        (function->block_count + 2U));
            }
            break;
        case 2U:
            if (ir->function_count != 0U) {
                IrFunction *function = &ir->functions[
                    (size_t)(selector % ir->function_count)];
                if (function->local_count != 0U)
                    function->locals[
                        (size_t)((selector >> 8U) %
                            function->local_count)].type =
                        (IrTypeId)((selector >> 16U) %
                            (ir->type_count + 2U));
            }
            break;
        case 3U:
            if (ir->function_count != 0U) {
                IrFunction *function = &ir->functions[
                    (size_t)(selector % ir->function_count)];
                if (function->block_count != 0U) {
                    IrTerminator *terminator =
                        &function->blocks[
                            (size_t)((selector >> 8U) %
                                function->block_count)].terminator;
                    terminator->kind =
                        (IrTerminatorKind)((selector >> 16U) & 7U);
                    terminator->target =
                        (IrBlockId)((selector >> 24U) %
                            (function->block_count + 2U));
                }
            }
            break;
        default:
            if (ir->function_count != 0U) {
                IrFunction *function = &ir->functions[
                    (size_t)(selector % ir->function_count)];
                if (function->block_count == 0U) break;
                IrBlock *block = &function->blocks[
                    (size_t)((selector >> 8U) % function->block_count)];
                if (block->instruction_count == 0U) break;
                IrInstruction *instruction = &block->instructions[
                    (size_t)((selector >> 16U) %
                        block->instruction_count)];
                instruction->opcode =
                    (IrOpcode)((selector >> 24U) % (IR_OP_COUNT + 4U));
                instruction->result_type =
                    (IrTypeId)((selector >> 32U) %
                        (ir->type_count + 2U));
                if (instruction->operand_count != 0U)
                    instruction->operands[
                        (size_t)((selector >> 40U) %
                            instruction->operand_count)] =
                        (IrValueId)(selector >> 48U);
            }
            break;
    }
}

int ASTER_VARIATION_ENTRY(const uint8_t *data, size_t size) {
    LangSource source;
    if (!aster_variation_source_init(data, size, &source)) return 0;
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool ok = lang_parse_module(&source, &diagnostics, &module);
    if (ok) ok = lang_check_module(&module, &diagnostics);
    IrModule ir = {0};
    if (ok) {
        LangTargetInfo target;
        lang_target_host(&target);
        ok = lang_ir_lower_module(&module, &target, &diagnostics, &ir);
    }
    if (ok) {
        vary_ir(&ir, variation_hash(data, size));
        (void)lang_ir_verify_module(&ir, &diagnostics);
    }
    lang_ir_free_module(&ir);
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    return 0;
}
