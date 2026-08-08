#include "internal.h"
#include "ir_internal.h"

#include <stdlib.h>
#include <string.h>

static bool local_kill(IrOpcode opcode) {
    return opcode == IR_OP_LOCAL_STORE ||
           opcode == IR_OP_LOCAL_DROP ||
           opcode == IR_OP_LOCAL_DEFAULT ||
           opcode == IR_OP_LOCAL_INVALIDATE;
}

static bool local_use(const IrInstruction *instruction) {
    switch (instruction->opcode) {
        case IR_OP_LOCAL_LOAD:
        case IR_OP_LOCAL_MOVE:
        case IR_OP_LOCAL_TRANSFER:
        case IR_OP_LOCAL_FIELD_GET:
        case IR_OP_LOCAL_FIELD_MOVE:
        case IR_OP_LOCAL_FIELD_TRANSFER:
        case IR_OP_LOCAL_FIELD_BORROW:
        case IR_OP_LOCAL_FIELD_SET:
        case IR_OP_LOCAL_FIELD_DEFAULT:
        case IR_OP_LOCAL_INDEX_GET:
        case IR_OP_LOCAL_INDEX_SET:
        case IR_OP_LOCAL_ENUM_IS:
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE:
        case IR_OP_LOCAL_ITERATOR_HAS_NEXT:
        case IR_OP_LOCAL_ITERATOR_NEXT:
        case IR_OP_LOCAL_ELEMENT_PROPERTY:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND:
        case IR_OP_LOCAL_ELEMENT_CSS_VALUE:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_END:
        case IR_OP_LOCAL_ELEMENT_APPEND:
        case IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT:
        case IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED:
        case IR_OP_LOCAL_ELEMENT_FINISH:
            return true;
        case IR_OP_ELEMENT_BEGIN:
            return instruction->index != IR_INVALID_ID;
        case IR_OP_VALUE_DISCARD:
            return instruction->auxiliary != 0U &&
                   instruction->index != IR_INVALID_ID;
        default:
            return false;
    }
}

static bool mark_borrow_lifetime_uses(IrFunction *function) {
    const size_t width = function->value_count == 0U
        ? 1U : function->value_count;
    uint32_t *borrowed_roots = ir_resize(
        NULL, width, sizeof(*borrowed_roots));
    if (borrowed_roots == NULL) return false;
    for (size_t value = 0U; value < width; ++value)
        borrowed_roots[value] = IR_INVALID_ID;

    for (size_t b = 0U; b < function->block_count; ++b) {
        IrBlock *block = &function->blocks[b];
        for (size_t i = 0U; i < block->instruction_count; ++i) {
            IrInstruction *instruction = &block->instructions[i];
            if ((instruction->opcode == IR_OP_LOCAL_LOAD ||
                 instruction->opcode == IR_OP_LOCAL_FIELD_BORROW) &&
                instruction->result < function->value_count &&
                instruction->index < function->local_count)
                borrowed_roots[instruction->result] = instruction->index;
            if (instruction->opcode == IR_OP_VALUE_DISCARD &&
                instruction->auxiliary != 0U &&
                instruction->operand_count == 1U &&
                instruction->operands[0] < function->value_count) {
                uint32_t root =
                    borrowed_roots[instruction->operands[0]];
                if (root != IR_INVALID_ID)
                    instruction->index = root;
            }
        }
    }
    free(borrowed_roots);
    return true;
}

static void transfer_block_liveness(const IrFunction *function,
                                    size_t block_index,
                                    uint8_t *live) {
    const IrBlock *block = &function->blocks[block_index];
    for (size_t i = block->instruction_count; i > 0U; --i) {
        const IrInstruction *instruction = &block->instructions[i - 1U];
        if (instruction->index >= function->local_count)
            continue;
        if (local_kill(instruction->opcode))
            live[instruction->index] = 0U;
        else if (local_use(instruction))
            live[instruction->index] = 1U;
    }
}

static bool compute_liveness(IrFunction *function,
                             uint8_t **out_live_out) {
    const size_t blocks = function->block_count;
    const size_t locals = function->local_count;
    const size_t width = locals == 0U ? 1U : locals;
    if (blocks != 0U && width > SIZE_MAX / blocks)
        return false;
    uint8_t *live_in = calloc(blocks * width, 1U);
    uint8_t *live_out = calloc(blocks * width, 1U);
    uint8_t *scratch = calloc(width, 1U);
    if (live_in == NULL || live_out == NULL || scratch == NULL) {
        free(live_in);
        free(live_out);
        free(scratch);
        return false;
    }

    bool changed;
    do {
        changed = false;
        for (size_t reverse = blocks; reverse > 0U; --reverse) {
            size_t b = reverse - 1U;
            const IrTerminator *term = &function->blocks[b].terminator;
            memset(scratch, 0, width);
            if ((term->kind == IR_TERM_JUMP ||
                 term->kind == IR_TERM_BRANCH) &&
                term->target < blocks)
                for (size_t l = 0U; l < locals; ++l)
                    scratch[l] |= live_in[(size_t)term->target * width + l];
            if (term->kind == IR_TERM_BRANCH && term->alternate < blocks)
                for (size_t l = 0U; l < locals; ++l)
                    scratch[l] |=
                        live_in[(size_t)term->alternate * width + l];
            if (memcmp(live_out + b * width, scratch, width) != 0) {
                memcpy(live_out + b * width, scratch, width);
                changed = true;
            }
            transfer_block_liveness(function, b, scratch);
            if (memcmp(live_in + b * width, scratch, width) != 0) {
                memcpy(live_in + b * width, scratch, width);
                changed = true;
            }
        }
    } while (changed);

    free(live_in);
    free(scratch);
    *out_live_out = live_out;
    return true;
}

static bool mark_transfer_decisions(IrFunction *function,
                                    const uint8_t *live_out) {
    const size_t locals = function->local_count;
    const size_t width = locals == 0U ? 1U : locals;
    uint8_t *live = calloc(width, 1U);
    if (live == NULL) return false;
    for (size_t b = 0U; b < function->block_count; ++b) {
        memcpy(live, live_out + b * width, width);
        IrBlock *block = &function->blocks[b];
        for (size_t i = block->instruction_count; i > 0U; --i) {
            IrInstruction *instruction = &block->instructions[i - 1U];
            if (instruction->index >= locals) continue;
            if (instruction->opcode == IR_OP_LOCAL_TRANSFER ||
                instruction->opcode == IR_OP_LOCAL_FIELD_TRANSFER)
                instruction->integer =
                    function->locals[instruction->index].borrowed ||
                    live[instruction->index] != 0U;
            if (local_kill(instruction->opcode))
                live[instruction->index] = 0U;
            else if (local_use(instruction))
                live[instruction->index] = 1U;
        }
    }
    free(live);
    return true;
}

static IrValueId remap_value(IrBuilder *builder, const IrValueId *values,
                             size_t value_count, IrValueId old,
                             LangSpan span) {
    if (old == IR_INVALID_ID) return IR_INVALID_ID;
    if (old < value_count && values[old] != IR_INVALID_ID)
        return values[old];
    lang_diag(builder->diagnostics, span,
              "internal IR error: ownership pass found an unmapped value");
    builder->failed = true;
    return IR_INVALID_ID;
}

static void move_instruction_metadata(IrInstruction *destination,
                                      IrInstruction *source) {
    destination->labels = source->labels;
    destination->label_count = source->label_count;
    destination->argument_modes = source->argument_modes;
    destination->argument_mode_count = source->argument_mode_count;
    destination->native_call = source->native_call;
    destination->integer = source->integer;
    destination->floating = source->floating;
    destination->index = source->index;
    destination->auxiliary = source->auxiliary;
    destination->render_destination = source->render_destination;
    destination->symbol = source->symbol;
    destination->symbol_length = source->symbol_length;
    destination->error_cleanup = source->error_cleanup;
    destination->exception_handler = source->exception_handler;
    destination->has_exception_handler = source->has_exception_handler;
    source->labels = NULL;
    source->argument_modes = NULL;
    source->native_call = NULL;
}

static IrValueId expand_transfer(
    IrBuilder *builder, const IrInstruction *old,
    const IrBlockId *blocks, size_t old_block_count
) {
    bool copy = old->integer != 0U;
    const Type *checked = old->result_type < builder->module->type_count
        ? builder->module->types[old->result_type].checked_type : NULL;
    if (checked == NULL) {
        lang_diag(builder->diagnostics, old->span,
                  "internal IR error: ownership transfer lost its checked type");
        builder->failed = true;
        return IR_INVALID_ID;
    }
    if (old->opcode == IR_OP_LOCAL_TRANSFER) {
        IrInstruction *source = ir_append_instruction(
            builder, copy ? IR_OP_LOCAL_LOAD : IR_OP_LOCAL_MOVE,
            old->result_type, NULL, 0U, old->span);
        if (source == NULL) return IR_INVALID_ID;
        source->index = old->index;
        if (!copy) return source->result;
        IrBlockId previous_handler = builder->copy_exception_handler;
        bool previous_has_handler = builder->copy_has_exception_handler;
        builder->copy_has_exception_handler =
            old->has_exception_handler &&
            old->exception_handler < old_block_count;
        if (builder->copy_has_exception_handler)
            builder->copy_exception_handler = blocks[old->exception_handler];
        IrValueId result = ir_emit_recursive_copy(
            builder, checked, source->result, old->span, true,
            &old->error_cleanup);
        builder->copy_exception_handler = previous_handler;
        builder->copy_has_exception_handler = previous_has_handler;
        return result;
    }

    IrInstruction *source = ir_append_instruction(
        builder, copy ? IR_OP_LOCAL_FIELD_BORROW
                      : IR_OP_LOCAL_FIELD_MOVE,
        old->result_type, NULL, 0U, old->span);
    if (source == NULL) return IR_INVALID_ID;
    source->index = old->index;
    source->auxiliary = old->auxiliary;
    source->symbol = old->symbol;
    source->symbol_length = old->symbol_length;
    if (copy) {
        IrBlockId previous_handler = builder->copy_exception_handler;
        bool previous_has_handler = builder->copy_has_exception_handler;
        builder->copy_has_exception_handler =
            old->has_exception_handler &&
            old->exception_handler < old_block_count;
        if (builder->copy_has_exception_handler)
            builder->copy_exception_handler = blocks[old->exception_handler];
        IrValueId copied = ir_emit_recursive_copy(
            builder, checked, source->result, old->span, true,
            &old->error_cleanup);
        builder->copy_exception_handler = previous_handler;
        builder->copy_has_exception_handler = previous_has_handler;
        return copied;
    }
    IrValueId result = source->result;
    IrInstruction *drop = ir_append_instruction(
        builder, IR_OP_LOCAL_DROP, IR_INVALID_ID, NULL, 0U, old->span);
    if (drop != NULL) drop->index = old->index;
    return result;
}

static void free_old_blocks(IrBlock *blocks, size_t block_count) {
    for (size_t b = 0U; b < block_count; ++b) {
        for (size_t i = 0U; i < blocks[b].instruction_count; ++i) {
            IrInstruction *instruction = &blocks[b].instructions[i];
            free(instruction->operands);
            free(instruction->labels);
            free(instruction->argument_modes);
            if (instruction->native_call != NULL) {
                free(instruction->native_call->parameter_types);
                free(instruction->native_call->parameter_modes);
                free(instruction->native_call);
            }
        }
        free(blocks[b].instructions);
    }
    free(blocks);
}

bool ir_resolve_ownership_transfers(IrBuilder *builder) {
    IrFunction *function = builder->function;
    uint8_t *live_out = NULL;
    if (!mark_borrow_lifetime_uses(function)) {
        lang_diag(builder->diagnostics, function->span,
                  "out of memory during borrow lifetime analysis");
        builder->failed = true;
        return false;
    }
    if (!compute_liveness(function, &live_out)) {
        lang_diag(builder->diagnostics, function->span,
                  "out of memory during ownership analysis");
        builder->failed = true;
        return false;
    }
    if (!mark_transfer_decisions(function, live_out)) {
        free(live_out);
        lang_diag(builder->diagnostics, function->span,
                  "out of memory during ownership analysis");
        builder->failed = true;
        return false;
    }
    free(live_out);

    IrBlock *old_blocks = function->blocks;
    size_t old_block_count = function->block_count;
    IrBlockId old_entry = function->entry_block;
    IrTypeId *old_value_types = function->value_types;
    size_t old_value_count = function->value_count;
    IrValueId *values = ir_resize(
        NULL, old_value_count == 0U ? 1U : old_value_count,
        sizeof(*values));
    IrBlockId *blocks = ir_resize(
        NULL, old_block_count == 0U ? 1U : old_block_count,
        sizeof(*blocks));
    for (size_t v = 0U; v < old_value_count; ++v)
        values[v] = IR_INVALID_ID;

    function->blocks = NULL;
    function->block_count = 0U;
    function->block_capacity = 0U;
    function->value_types = NULL;
    function->value_count = 0U;
    function->value_capacity = 0U;
    for (size_t b = 0U; b < old_block_count; ++b)
        blocks[b] = ir_add_block(function);
    function->entry_block = old_entry < old_block_count
        ? blocks[old_entry] : IR_INVALID_ID;

    for (size_t b = 0U; b < old_block_count; ++b) {
        builder->current = blocks[b];
        IrBlock *old_block = &old_blocks[b];
        for (size_t i = 0U; i < old_block->instruction_count; ++i) {
            IrInstruction *old = &old_block->instructions[i];
            IrValueId result;
            if (old->opcode == IR_OP_LOCAL_TRANSFER ||
                old->opcode == IR_OP_LOCAL_FIELD_TRANSFER) {
                result = expand_transfer(
                    builder, old, blocks, old_block_count);
            } else {
                IrValueId *operands = ir_resize(
                    NULL, old->operand_count,
                    sizeof(*operands));
                for (size_t o = 0U; o < old->operand_count; ++o)
                    operands[o] = remap_value(
                        builder, values, old_value_count,
                        old->operands[o], old->span);
                IrInstruction *copy = ir_append_instruction(
                    builder, old->opcode, old->result_type,
                    operands, old->operand_count, old->span);
                free(operands);
                if (copy == NULL) {
                    result = IR_INVALID_ID;
                } else {
                    move_instruction_metadata(copy, old);
                    if (old->opcode == IR_OP_EXCEPTION_PENDING &&
                        old->index < old_value_count)
                        copy->index = remap_value(
                            builder, values, old_value_count,
                            old->index, old->span);
                    result = copy->result;
                }
            }
            if (old->result != IR_INVALID_ID && old->result < old_value_count)
                values[old->result] = result;
        }
        const IrTerminator *term = &old_block->terminator;
        IrValueId value = remap_value(
            builder, values, old_value_count, term->value, term->span);
        IrBlockId target = term->target < old_block_count
            ? blocks[term->target] : IR_INVALID_ID;
        IrBlockId alternate = term->alternate < old_block_count
            ? blocks[term->alternate] : IR_INVALID_ID;
        ir_set_terminator(
            builder, term->kind, value, target, alternate, term->span);
    }

    free(values);
    free(blocks);
    free(old_value_types);
    free_old_blocks(old_blocks, old_block_count);
    return !builder->failed;
}
