#include "ir_verify_internal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

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

enum IrLocalAvailability {
    IR_LOCAL_UNAVAILABLE,
    IR_LOCAL_AVAILABLE,
    IR_LOCAL_MAYBE_AVAILABLE
};

static bool instruction_reads_local(const IrInstruction *instruction) {
    switch (instruction->opcode) {
        case IR_OP_LOCAL_LOAD:
        case IR_OP_LOCAL_MOVE:
        case IR_OP_LOCAL_FIELD_GET:
        case IR_OP_LOCAL_FIELD_MOVE:
        case IR_OP_LOCAL_FIELD_BORROW:
        case IR_OP_LOCAL_FIELD_SET:
        case IR_OP_LOCAL_FIELD_DEFAULT:
        case IR_OP_LOCAL_INDEX_GET:
        case IR_OP_LOCAL_INDEX_MOVE:
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

static void update_local_availability(
    const IrFunction *function, const IrInstruction *instruction,
    uint8_t *state
) {
    if (instruction->opcode == IR_OP_PARAMETER &&
        instruction->index < function->local_count) {
        state[instruction->index] = IR_LOCAL_AVAILABLE;
        return;
    }
    if (instruction->index >= function->local_count) return;
    switch (instruction->opcode) {
        case IR_OP_LOCAL_STORE:
        case IR_OP_LOCAL_DEFAULT:
            state[instruction->index] = IR_LOCAL_AVAILABLE;
            break;
        case IR_OP_LOCAL_MOVE:
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE:
        case IR_OP_LOCAL_DROP:
        case IR_OP_LOCAL_INVALIDATE:
            state[instruction->index] = IR_LOCAL_UNAVAILABLE;
            break;
        default:
            break;
    }
}

static uint8_t meet_local_availability(uint8_t left, uint8_t right) {
    return left == right ? left : IR_LOCAL_MAYBE_AVAILABLE;
}

static bool verifier_local_requires_cleanup(
    const IrModule *ir, const IrFunction *function, size_t local
) {
    if (function->locals[local].borrowed ||
        (function->is_destructor && local == 0U))
        return false;
    IrTypeId id = function->locals[local].type;
    if (id >= ir->type_count) return false;
    const IrType *type = &ir->types[id];
    return type->requires_cleanup || type->managed;
}

static bool verify_local_availability(
    const IrModule *ir, const IrFunction *function,
    LangDiagnostics *diagnostics
) {
    const size_t blocks = function->block_count;
    const size_t locals = function->local_count;
    const size_t width = locals == 0U ? 1U : locals;
    if (blocks > SIZE_MAX / width) {
        lang_diag(diagnostics, function->span,
                  "IR function `%s` has too much ownership state",
                  function->name);
        return false;
    }
    uint8_t *entry = calloc(blocks * width, 1U);
    uint8_t *exit = calloc(blocks * width, 1U);
    uint8_t *scratch = calloc(width, 1U);
    bool *known_entry = calloc(blocks, sizeof(*known_entry));
    bool *known_exit = calloc(blocks, sizeof(*known_exit));
    IrBlockId *work = ir_resize(NULL, blocks, sizeof(*work));
    bool *queued = calloc(blocks, sizeof(*queued));
    if (entry == NULL || exit == NULL || scratch == NULL ||
        known_entry == NULL || known_exit == NULL || work == NULL ||
        queued == NULL) {
        free(queued);
        free(work);
        free(known_exit);
        free(known_entry);
        free(scratch);
        free(exit);
        free(entry);
        lang_diag(diagnostics, function->span,
                  "out of memory verifying IR ownership in `%s`",
                  function->name);
        return false;
    }

    size_t work_count = 0U;
    known_entry[function->entry_block] = true;
    work[work_count++] = function->entry_block;
    queued[function->entry_block] = true;
    while (work_count != 0U) {
        IrBlockId block_id = work[--work_count];
        queued[block_id] = false;
        memcpy(scratch, entry + (size_t)block_id * width, width);
        const IrBlock *block = &function->blocks[block_id];
        for (size_t i = 0U; i < block->instruction_count; ++i)
            update_local_availability(
                function, &block->instructions[i], scratch);
        bool changed = !known_exit[block_id] ||
            memcmp(exit + (size_t)block_id * width,
                   scratch, width) != 0;
        if (!changed) continue;
        memcpy(exit + (size_t)block_id * width, scratch, width);
        known_exit[block_id] = true;
        size_t successors = terminator_successor_count(
            &block->terminator, blocks);
        for (size_t edge = 0U; edge < successors; ++edge) {
            IrBlockId next = terminator_successor(
                &block->terminator, edge);
            uint8_t *next_entry = entry + (size_t)next * width;
            bool next_changed = !known_entry[next];
            if (!known_entry[next]) {
                memcpy(next_entry, scratch, width);
                known_entry[next] = true;
            } else {
                for (size_t local = 0U; local < locals; ++local) {
                    uint8_t merged = meet_local_availability(
                        next_entry[local], scratch[local]);
                    if (merged != next_entry[local]) {
                        next_entry[local] = merged;
                        next_changed = true;
                    }
                }
            }
            if (next_changed && !queued[next]) {
                work[work_count++] = next;
                queued[next] = true;
            }
        }
    }

    bool ok = true;
    for (size_t b = 0U; b < blocks; ++b) {
        if (!known_entry[b]) continue;
        memcpy(scratch, entry + b * width, width);
        const IrBlock *block = &function->blocks[b];
        for (size_t i = 0U; i < block->instruction_count; ++i) {
            const IrInstruction *instruction = &block->instructions[i];
            if (instruction_reads_local(instruction) &&
                instruction->index < locals &&
                scratch[instruction->index] != IR_LOCAL_AVAILABLE) {
                lang_diag(
                    diagnostics, instruction->span,
                    "IR `%s` reads unavailable local %u in `%s`",
                    ir_opcode_name(instruction->opcode),
                    instruction->index, function->name);
                ok = false;
            }
            update_local_availability(function, instruction, scratch);
        }
        if (block->terminator.kind == IR_TERM_RETURN ||
            block->terminator.kind == IR_TERM_PROPAGATE_EXCEPTION)
            for (size_t local = 0U; local < locals; ++local)
                if (verifier_local_requires_cleanup(
                        ir, function, local) &&
                    scratch[local] != IR_LOCAL_UNAVAILABLE) {
                    lang_diag(
                        diagnostics, block->terminator.span,
                        "IR exit leaves owning local %zu live in `%s`",
                        local, function->name);
                    ok = false;
                }
    }

    free(queued);
    free(work);
    free(known_exit);
    free(known_entry);
    free(scratch);
    free(exit);
    free(entry);
    return ok;
}

static size_t local_projection_count(
    const IrModule *ir, const IrFunction *function, size_t local
) {
    if (local >= function->local_count ||
        function->locals[local].type >= ir->type_count)
        return 0U;
    const IrType *type = &ir->types[function->locals[local].type];
    if (type->shape == IR_TYPE_STRUCT) return type->field_count;
    if (type->shape == IR_TYPE_ARRAY) return type->array_length;
    return 0U;
}

static void set_projection_range(
    uint8_t *state, const size_t *offsets, size_t local, uint8_t value
) {
    for (size_t slot = offsets[local]; slot < offsets[local + 1U]; ++slot)
        state[slot] = value;
}

static bool projection_instruction_whole_read(
    const IrInstruction *instruction
) {
    if (!instruction_reads_local(instruction)) return false;
    switch (instruction->opcode) {
        case IR_OP_LOCAL_FIELD_GET:
        case IR_OP_LOCAL_FIELD_MOVE:
        case IR_OP_LOCAL_FIELD_BORROW:
        case IR_OP_LOCAL_FIELD_SET:
        case IR_OP_LOCAL_FIELD_DEFAULT:
        case IR_OP_LOCAL_INDEX_GET:
        case IR_OP_LOCAL_INDEX_MOVE:
        case IR_OP_LOCAL_INDEX_SET:
            return false;
        default:
            return true;
    }
}

static bool projection_read_available(
    const IrInstruction *instruction, const uint8_t *state,
    const size_t *offsets, size_t local
) {
    size_t begin = offsets[local];
    size_t count = offsets[local + 1U] - begin;
    if (count == 0U) return true;
    if (instruction->opcode == IR_OP_LOCAL_FIELD_GET ||
        instruction->opcode == IR_OP_LOCAL_FIELD_MOVE ||
        instruction->opcode == IR_OP_LOCAL_FIELD_BORROW) {
        return instruction->auxiliary < count &&
               state[begin + instruction->auxiliary] == IR_LOCAL_AVAILABLE;
    }
    if (instruction->opcode == IR_OP_LOCAL_INDEX_GET ||
        instruction->opcode == IR_OP_LOCAL_INDEX_MOVE) {
        if (instruction->has_constant_index)
            return instruction->constant_index < count &&
                   state[begin + (size_t)instruction->constant_index] ==
                       IR_LOCAL_AVAILABLE;
        for (size_t slot = begin; slot < begin + count; ++slot)
            if (state[slot] != IR_LOCAL_AVAILABLE) return false;
        return true;
    }
    if (!projection_instruction_whole_read(instruction)) return true;
    for (size_t slot = begin; slot < begin + count; ++slot)
        if (state[slot] != IR_LOCAL_AVAILABLE) return false;
    return true;
}

static void update_projection_availability(
    const IrInstruction *instruction, uint8_t *state,
    const size_t *offsets, size_t locals
) {
    if (instruction->index >= locals) return;
    size_t local = instruction->index;
    size_t begin = offsets[local];
    size_t count = offsets[local + 1U] - begin;
    if (count == 0U) return;
    switch (instruction->opcode) {
        case IR_OP_PARAMETER:
        case IR_OP_LOCAL_STORE:
        case IR_OP_LOCAL_DEFAULT:
            set_projection_range(
                state, offsets, local, IR_LOCAL_AVAILABLE);
            return;
        case IR_OP_LOCAL_MOVE:
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE:
        case IR_OP_LOCAL_DROP:
        case IR_OP_LOCAL_INVALIDATE:
            set_projection_range(
                state, offsets, local, IR_LOCAL_UNAVAILABLE);
            return;
        case IR_OP_LOCAL_FIELD_MOVE:
            if (instruction->auxiliary < count)
                state[begin + instruction->auxiliary] =
                    IR_LOCAL_UNAVAILABLE;
            return;
        case IR_OP_LOCAL_FIELD_SET:
        case IR_OP_LOCAL_FIELD_DEFAULT:
            if (instruction->auxiliary < count)
                state[begin + instruction->auxiliary] =
                    IR_LOCAL_AVAILABLE;
            return;
        case IR_OP_LOCAL_INDEX_MOVE:
            if (instruction->has_constant_index &&
                instruction->constant_index < count)
                state[begin + (size_t)instruction->constant_index] =
                    IR_LOCAL_UNAVAILABLE;
            else
                set_projection_range(
                    state, offsets, local, IR_LOCAL_MAYBE_AVAILABLE);
            return;
        case IR_OP_LOCAL_INDEX_SET:
            if (instruction->has_constant_index &&
                instruction->constant_index < count) {
                state[begin + (size_t)instruction->constant_index] =
                    IR_LOCAL_AVAILABLE;
            } else {
                bool all_available = true;
                for (size_t slot = begin; slot < begin + count; ++slot)
                    if (state[slot] != IR_LOCAL_AVAILABLE) {
                        all_available = false;
                        break;
                    }
                if (!all_available)
                    set_projection_range(
                        state, offsets, local, IR_LOCAL_MAYBE_AVAILABLE);
            }
            return;
        default:
            return;
    }
}

static bool verify_projection_availability(
    const IrModule *ir, const IrFunction *function,
    LangDiagnostics *diagnostics
) {
    size_t locals = function->local_count;
    size_t *offsets = ir_resize(
        NULL, locals + 1U, sizeof(*offsets));
    offsets[0] = 0U;
    for (size_t local = 0U; local < locals; ++local) {
        size_t count = local_projection_count(ir, function, local);
        if (offsets[local] > SIZE_MAX - count) {
            free(offsets);
            lang_diag(diagnostics, function->span,
                      "IR function `%s` has too much projection state",
                      function->name);
            return false;
        }
        offsets[local + 1U] = offsets[local] + count;
    }
    size_t projections = offsets[locals];
    if (projections == 0U) {
        free(offsets);
        return true;
    }
    size_t blocks = function->block_count;
    if (blocks > SIZE_MAX / projections) {
        free(offsets);
        lang_diag(diagnostics, function->span,
                  "IR function `%s` has too much projection state",
                  function->name);
        return false;
    }
    uint8_t *entry = calloc(blocks * projections, 1U);
    uint8_t *exit = calloc(blocks * projections, 1U);
    uint8_t *scratch = calloc(projections, 1U);
    bool *known_entry = calloc(blocks, sizeof(*known_entry));
    bool *known_exit = calloc(blocks, sizeof(*known_exit));
    bool *queued = calloc(blocks, sizeof(*queued));
    IrBlockId *work = ir_resize(NULL, blocks, sizeof(*work));
    if (entry == NULL || exit == NULL || scratch == NULL ||
        known_entry == NULL || known_exit == NULL || queued == NULL ||
        work == NULL) {
        free(work); free(queued); free(known_exit); free(known_entry);
        free(scratch); free(exit); free(entry); free(offsets);
        lang_diag(diagnostics, function->span,
                  "out of memory verifying projections in `%s`",
                  function->name);
        return false;
    }
    size_t work_count = 0U;
    known_entry[function->entry_block] = true;
    queued[function->entry_block] = true;
    work[work_count++] = function->entry_block;
    while (work_count != 0U) {
        IrBlockId block_id = work[--work_count];
        queued[block_id] = false;
        memcpy(scratch, entry + (size_t)block_id * projections,
               projections);
        const IrBlock *block = &function->blocks[block_id];
        for (size_t i = 0U; i < block->instruction_count; ++i)
            update_projection_availability(
                &block->instructions[i], scratch, offsets, locals);
        bool changed = !known_exit[block_id] ||
            memcmp(exit + (size_t)block_id * projections,
                   scratch, projections) != 0;
        if (!changed) continue;
        memcpy(exit + (size_t)block_id * projections,
               scratch, projections);
        known_exit[block_id] = true;
        size_t successors = terminator_successor_count(
            &block->terminator, blocks);
        for (size_t edge = 0U; edge < successors; ++edge) {
            IrBlockId next = terminator_successor(
                &block->terminator, edge);
            uint8_t *next_entry =
                entry + (size_t)next * projections;
            bool next_changed = !known_entry[next];
            if (!known_entry[next]) {
                memcpy(next_entry, scratch, projections);
                known_entry[next] = true;
            } else {
                for (size_t slot = 0U; slot < projections; ++slot) {
                    uint8_t merged = meet_local_availability(
                        next_entry[slot], scratch[slot]);
                    if (merged != next_entry[slot]) {
                        next_entry[slot] = merged;
                        next_changed = true;
                    }
                }
            }
            if (next_changed && !queued[next]) {
                work[work_count++] = next;
                queued[next] = true;
            }
        }
    }
    bool ok = true;
    for (size_t b = 0U; b < blocks; ++b) {
        if (!known_entry[b]) continue;
        memcpy(scratch, entry + b * projections, projections);
        const IrBlock *block = &function->blocks[b];
        for (size_t i = 0U; i < block->instruction_count; ++i) {
            const IrInstruction *instruction = &block->instructions[i];
            if (instruction->index < locals &&
                !projection_read_available(
                    instruction, scratch, offsets,
                    instruction->index)) {
                lang_diag(
                    diagnostics, instruction->span,
                    "IR `%s` reads an unavailable projection of local %u in `%s`",
                    ir_opcode_name(instruction->opcode),
                    instruction->index, function->name);
                ok = false;
            }
            update_projection_availability(
                instruction, scratch, offsets, locals);
        }
    }
    free(work); free(queued); free(known_exit); free(known_entry);
    free(scratch); free(exit); free(entry); free(offsets);
    return ok;
}

static void compute_dominators(const IrFunction *function,
                               bool *reachable, uint64_t *dominators,
                               size_t words) {
    size_t count = function->block_count;
    IrBlockId *work = ir_resize(NULL, count, sizeof(*work));
    size_t *predecessor_counts = ir_resize(
        NULL, count, sizeof(*predecessor_counts));
    size_t *predecessor_offsets = ir_resize(
        NULL, count + 1U, sizeof(*predecessor_offsets));
    size_t *predecessor_cursors = ir_resize(
        NULL, count, sizeof(*predecessor_cursors));
    IrBlockId *predecessors = NULL;
    uint64_t *next_row = ir_resize(NULL, words, sizeof(*next_row));
    uint64_t *reachable_bits = ir_resize(
        NULL, words, sizeof(*reachable_bits));
    memset(reachable_bits, 0, words * sizeof(*reachable_bits));
    memset(predecessor_counts, 0, count * sizeof(*predecessor_counts));
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
    for (size_t block = 0U; block < count; ++block)
        if (reachable[block])
            reachable_bits[block / 64U] |=
                UINT64_C(1) << (block % 64U);

    size_t edge_count = 0U;
    for (size_t predecessor = 0U; predecessor < count; ++predecessor) {
        const IrTerminator *term =
            &function->blocks[predecessor].terminator;
        size_t successors = terminator_successor_count(term, count);
        for (size_t edge = 0U; edge < successors; ++edge) {
            IrBlockId successor = terminator_successor(term, edge);
            ++predecessor_counts[successor];
            ++edge_count;
        }
    }
    predecessor_offsets[0] = 0U;
    for (size_t block = 0U; block < count; ++block) {
        predecessor_offsets[block + 1U] =
            predecessor_offsets[block] + predecessor_counts[block];
        predecessor_cursors[block] = predecessor_offsets[block];
    }
    predecessors = ir_resize(
        NULL, edge_count, sizeof(*predecessors));
    for (size_t predecessor = 0U; predecessor < count; ++predecessor) {
        const IrTerminator *term =
            &function->blocks[predecessor].terminator;
        size_t successors = terminator_successor_count(term, count);
        for (size_t edge = 0U; edge < successors; ++edge) {
            IrBlockId successor = terminator_successor(term, edge);
            predecessors[predecessor_cursors[successor]++] =
                (IrBlockId)predecessor;
        }
    }

    for (size_t block = 0U; block < count; ++block) {
        if (!reachable[block]) {
            dominators[block * words + block / 64U] |=
                UINT64_C(1) << (block % 64U);
            continue;
        }
        if (block == function->entry_block) {
            dominators[block * words + block / 64U] |=
                UINT64_C(1) << (block % 64U);
            continue;
        }
        memcpy(dominators + block * words, reachable_bits,
               words * sizeof(*reachable_bits));
    }

    bool changed;
    do {
        changed = false;
        for (size_t block = 0U; block < count; ++block) {
            if (!reachable[block] || block == function->entry_block)
                continue;
            bool has_predecessor = false;
            memcpy(next_row, reachable_bits,
                   words * sizeof(*reachable_bits));
            for (size_t index = predecessor_offsets[block];
                 index < predecessor_offsets[block + 1U]; ++index) {
                size_t predecessor = predecessors[index];
                if (!reachable[predecessor]) continue;
                has_predecessor = true;
                const uint64_t *predecessor_row =
                    dominators + predecessor * words;
                for (size_t word = 0U; word < words; ++word)
                    next_row[word] &= predecessor_row[word];
            }
            if (!has_predecessor)
                memset(next_row, 0, words * sizeof(*next_row));
            next_row[block / 64U] |= UINT64_C(1) << (block % 64U);
            uint64_t *row = dominators + block * words;
            if (memcmp(row, next_row, words * sizeof(*row)) != 0) {
                memcpy(row, next_row, words * sizeof(*row));
                changed = true;
            }
        }
    } while (changed);
    free(reachable_bits);
    free(next_row);
    free(predecessors);
    free(predecessor_cursors);
    free(predecessor_offsets);
    free(predecessor_counts);
}

static bool value_available(const IrFunction *function,
                            const bool *defined,
                            const size_t *definition_blocks,
                            const size_t *definition_instructions,
                            const bool *reachable,
                            const uint64_t *dominators,
                            size_t dominator_words,
                            IrValueId value, size_t use_block,
                            size_t use_instruction) {
    if (!ir_verify_value(function, value) || !defined[value]) return false;
    size_t definition_block = definition_blocks[value];
    if (definition_block == use_block)
        return definition_instructions[value] < use_instruction;
    /* Dominance is only meaningful in the entry-reachable CFG. */
    if (!reachable[use_block]) return true;
    return reachable[definition_block] &&
           (dominators[use_block * dominator_words +
                       definition_block / 64U] &
            (UINT64_C(1) << (definition_block % 64U))) != 0U;
}

static bool copyable_type_contains_noncopyable(
    const IrModule *ir, const IrType *type
) {
    if (type->copy_policy == IR_COPY_NONCOPYABLE ||
        type->copy_policy == IR_COPY_CUSTOM)
        return false;
    if (type->element_type != IR_INVALID_ID &&
        ir_verify_type(ir, type->element_type) &&
        ir->types[type->element_type].copy_policy == IR_COPY_NONCOPYABLE)
        return true;
    if (type->error_type != IR_INVALID_ID &&
        ir_verify_type(ir, type->error_type) &&
        ir->types[type->error_type].copy_policy == IR_COPY_NONCOPYABLE)
        return true;
    for (size_t field = 0U;
         type->field_types != NULL && field < type->field_count; ++field)
        if (ir_verify_type(ir, type->field_types[field]) &&
            ir->types[type->field_types[field]].copy_policy ==
                IR_COPY_NONCOPYABLE)
            return true;
    for (size_t variant = 0U;
         type->variant_payload_types != NULL &&
         variant < type->variant_count; ++variant)
        if (type->variant_payload_types[variant] != IR_INVALID_ID &&
            ir_verify_type(ir, type->variant_payload_types[variant]) &&
            ir->types[type->variant_payload_types[variant]].copy_policy ==
                IR_COPY_NONCOPYABLE)
            return true;
    return false;
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
            value->owner_name == NULL || !ir_verify_type(ir, value->type)) {
            lang_diag(diagnostics, value->span,
                      "IR static field %zu is malformed", field);
            ok = false;
        }
    }
    for (size_t t = 0U; t < ir->type_count; ++t) {
        const IrType *type = &ir->types[t];
        if (type->checked_type != NULL || type->name == NULL ||
            !ir_verify_type_shape(type->shape) ||
            !ir_verify_copy_policy(type->copy_policy) ||
            !ir_verify_drop_policy(type->drop_policy) ||
            (type->copy_policy == IR_COPY_TRIVIAL &&
             (type->requires_cleanup || type->managed)) ||
            (type->drop_policy == IR_DROP_TRIVIAL &&
             (type->requires_cleanup || type->managed)) ||
            (type->copy_function != IR_INVALID_ID &&
             type->copy_policy != IR_COPY_CUSTOM) ||
            copyable_type_contains_noncopyable(ir, type) ||
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
            !ir_verify_type(ir, type->element_type)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu has an invalid element type", t);
            ok = false;
        }
        if (type->base_type != IR_INVALID_ID &&
            (!ir_verify_type(ir, type->base_type) ||
             type->shape != IR_TYPE_CLASS_REFERENCE ||
             ir->types[type->base_type].shape !=
                 IR_TYPE_CLASS_REFERENCE)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu (`%s`) has invalid base t%" PRIu32
                      " (`%s`, shape %d)",
                      t, type->name, type->base_type,
                      ir_verify_type(ir, type->base_type)
                          ? ir->types[type->base_type].name : "<invalid>",
                      ir_verify_type(ir, type->base_type)
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
            if (!ir_verify_type(ir, type->interface_types[interface]) ||
                type->shape != IR_TYPE_CLASS_REFERENCE ||
                ir->types[type->interface_types[interface]].shape !=
                    IR_TYPE_CLASS_REFERENCE) {
                lang_diag(
                    diagnostics, (LangSpan){NULL, 0U, 0U},
                    "IR type t%zu has invalid interface metadata", t);
                ok = false;
            }
        if (type->base_type != IR_INVALID_ID &&
            ir_verify_type(ir, type->base_type)) {
            IrTypeId base = type->base_type;
            size_t depth = 0U;
            while (base != IR_INVALID_ID && ir_verify_type(ir, base) &&
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
            !ir_verify_type(ir, type->error_type)) {
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
                    !ir_verify_type_assignable(
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
            if (!ir_verify_type(ir, type->argument_types[a])) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR type t%zu has an invalid argument type", t);
                ok = false;
            }
        if (type->shape == IR_TYPE_FUNCTION)
            for (size_t a = 0U; a < type->argument_count; ++a)
                if (type->parameter_modes == NULL ||
                    !ir_verify_parameter_mode(type->parameter_modes[a])) {
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
            !ir_verify_type(ir, type->element_type)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR aggregate type t%zu is missing its element type",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_ITERATOR &&
            (type->argument_count != 1U ||
             type->argument_types == NULL ||
             !ir_verify_type(ir, type->argument_types[0]))) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR iterator type t%zu has invalid source metadata",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_FUNCTION &&
            (!ir_verify_type(ir, type->element_type) ||
             (type->argument_count != 0U &&
              type->parameter_modes == NULL))) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR function type t%zu has invalid result metadata",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_ELEMENT_BUILDER &&
            (!ir_verify_type(ir, type->element_type) ||
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
                    if (!ir_verify_type(ir, type->field_types[field])) {
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
                        !ir_verify_type(
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
                        ir_verify_type(
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
        if (!ir_verify_type(ir, dispatch->interface_type) ||
            !ir_verify_type(ir, dispatch->runtime_type) ||
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
            ir_verify_type_assignable(
                ir, dispatch->interface_type, dispatch->runtime_type) &&
            ir_verify_type_assignable(
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
            !ir_verify_type(ir, function->return_type) ||
            !ir_verify_type(ir, function->async_result_type) ||
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
                !ir_verify_type(ir, function->parameters[p].type) ||
                !ir_verify_parameter_mode(function->parameters[p].mode)) {
                lang_diag(diagnostics, function_span,
                          "IR function `%s` has an invalid parameter type",
                          function->name);
                ok = false;
            }
        for (size_t l = 0U; l < function->local_count; ++l)
            if (!ir_verify_type(ir, function->locals[l].type)) {
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
                bool known_opcode = ir_verify_opcode_valid(instruction->opcode);
                bool valid_operands = known_opcode &&
                    ir_verify_operand_count(instruction) &&
                    (instruction->operand_count == 0U ||
                     instruction->operands != NULL);
                bool valid_labels = instruction->label_count == 0U ||
                                    instruction->labels != NULL;
                bool valid_result = known_opcode &&
                    ir_verify_result_type(ir, function, instruction);
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
                    !ir_verify_instruction_signature(
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
                } else if (!ir_verify_value(function, instruction->result) ||
                           !ir_verify_type(ir, instruction->result_type) ||
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
                     instruction->opcode == IR_OP_LOCAL_INVALIDATE ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_GET ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_MOVE ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_BORROW ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_SET ||
                     instruction->opcode == IR_OP_LOCAL_INDEX_GET ||
                     instruction->opcode == IR_OP_LOCAL_INDEX_MOVE ||
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
        size_t dominator_words =
            (function->block_count + 63U) / 64U;
        if (dominator_words != 0U &&
            function->block_count > SIZE_MAX / dominator_words) {
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
            function->block_count * dominator_words;
        bool *reachable = ir_resize(
            NULL, function->block_count, sizeof(*reachable));
        uint64_t *dominators = ir_resize(
            NULL, dominator_slots, sizeof(*dominators));
        memset(reachable, 0,
               function->block_count * sizeof(*reachable));
        memset(dominators, 0,
               dominator_slots * sizeof(*dominators));
        compute_dominators(
            function, reachable, dominators, dominator_words);
        for (size_t b = 0U; b < function->block_count; ++b) {
            const IrBlock *block = &function->blocks[b];
            if (block->instruction_count != 0U &&
                block->instructions == NULL)
                continue;
            for (size_t i = 0U; i < block->instruction_count; ++i) {
                const IrInstruction *instruction =
                    &block->instructions[i];
                if ((instruction->opcode == IR_OP_LOCAL_INDEX_GET ||
                     instruction->opcode == IR_OP_LOCAL_INDEX_MOVE ||
                     instruction->opcode == IR_OP_LOCAL_INDEX_SET) &&
                    instruction->has_constant_index) {
                    IrValueId index_value = instruction->operand_count != 0U
                        ? instruction->operands[0] : IR_INVALID_ID;
                    bool proven = index_value < function->value_count &&
                        definition_blocks[index_value] <
                            function->block_count &&
                        definition_instructions[index_value] <
                            function->blocks[
                                definition_blocks[index_value]]
                                .instruction_count;
                    if (proven) {
                        const IrInstruction *definition =
                            &function->blocks[
                                definition_blocks[index_value]].instructions[
                                definition_instructions[index_value]];
                        proven = definition->opcode == IR_OP_CONST_INT &&
                            definition->integer ==
                                instruction->constant_index;
                    }
                    if (!proven) {
                        lang_diag(
                            diagnostics, instruction->span,
                            "IR `%s` has unproven constant-index ownership metadata",
                            ir_opcode_name(instruction->opcode));
                        ok = false;
                    }
                }
                if (instruction->operand_count != 0U &&
                    instruction->operands == NULL)
                    continue;
                for (size_t o = 0U; o < instruction->operand_count; ++o)
                    if (!value_available(
                            function, defined, definition_blocks,
                            definition_instructions, reachable,
                            dominators, dominator_words,
                            instruction->operands[o], b, i)) {
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
                            dominators, dominator_words, term->value, b,
                            block->instruction_count) ||
                        !ir_verify_value_type(ir, function, term->value,
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
                            dominators, dominator_words, term->value, b,
                            block->instruction_count) ||
                        !ir_verify_value_is_type(function, term->value,
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
        if (!verify_local_availability(ir, function, diagnostics))
            ok = false;
        if (!verify_projection_availability(ir, function, diagnostics))
            ok = false;
        free(definition_instructions);
        free(definition_blocks);
        free(defined);
    }
    return ok;
}
