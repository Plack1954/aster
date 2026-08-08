#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct CAsyncLiveness {
    size_t await_count;
    size_t local_count;
    size_t value_count;
    uint8_t *locals;
    uint8_t *values;
    uint8_t *frame_locals;
    uint8_t *frame_values;
} CAsyncLiveness;

/*
 * Async C lowering splits one verified IR function into repeated step calls.
 * Backward liveness identifies state that crosses each suspension; forward
 * availability removes locals which have not yet acquired a value. Cleanup
 * operations count as uses so an otherwise-dead owner still reaches its drop.
 */

static bool async_local_reads(const CEmitter *emitter,
                              const IrFunction *function,
                              const IrInstruction *instruction) {
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
        case IR_OP_LOCAL_STORE:
        case IR_OP_LOCAL_DROP:
        case IR_OP_LOCAL_DEFAULT:
            return instruction->index < function->local_count &&
                   c_backend_local_tracks_drop(
                       emitter, function, instruction->index);
        default:
            return false;
    }
}

static bool async_local_defines(const IrInstruction *instruction) {
    switch (instruction->opcode) {
        case IR_OP_LOCAL_MOVE:
        case IR_OP_LOCAL_STORE:
        case IR_OP_LOCAL_DROP:
        case IR_OP_LOCAL_DEFAULT:
        case IR_OP_LOCAL_INVALIDATE:
            return true;
        default:
            return false;
    }
}

enum CAsyncLocalAvailability {
    C_ASYNC_LOCAL_UNAVAILABLE,
    C_ASYNC_LOCAL_AVAILABLE,
    C_ASYNC_LOCAL_MAYBE_AVAILABLE
};

static void async_update_local_availability(
    const IrFunction *function, const IrInstruction *instruction,
    uint8_t *state
) {
    if (instruction->opcode == IR_OP_PARAMETER &&
        instruction->index < function->local_count) {
        state[instruction->index] = C_ASYNC_LOCAL_AVAILABLE;
        return;
    }
    if (instruction->index >= function->local_count) return;
    switch (instruction->opcode) {
        case IR_OP_LOCAL_STORE:
        case IR_OP_LOCAL_DEFAULT:
            state[instruction->index] = C_ASYNC_LOCAL_AVAILABLE;
            break;
        case IR_OP_LOCAL_MOVE:
        case IR_OP_LOCAL_DROP:
        case IR_OP_LOCAL_INVALIDATE:
            state[instruction->index] = C_ASYNC_LOCAL_UNAVAILABLE;
            break;
        default:
            break;
    }
}

static uint8_t async_meet_availability(uint8_t left, uint8_t right) {
    return left == right ? left : C_ASYNC_LOCAL_MAYBE_AVAILABLE;
}

static void async_transfer_instruction(
    const CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction, uint8_t *locals, uint8_t *values
) {
    if (instruction->result != IR_INVALID_ID &&
        instruction->result < function->value_count)
        values[instruction->result] = 0U;
    for (size_t operand = 0U;
         operand < instruction->operand_count; ++operand)
        if (instruction->operands[operand] < function->value_count)
            values[instruction->operands[operand]] = 1U;
    if (instruction->index >= function->local_count) return;
    if (async_local_defines(instruction))
        locals[instruction->index] = 0U;
    if (async_local_reads(emitter, function, instruction))
        locals[instruction->index] = 1U;
}

static void async_add_terminator_uses(
    const IrFunction *function, const IrTerminator *terminator,
    uint8_t *values
) {
    if ((terminator->kind == IR_TERM_BRANCH ||
         terminator->kind == IR_TERM_RETURN) &&
        terminator->value < function->value_count)
        values[terminator->value] = 1U;
}

static void async_merge_successors(
    const IrFunction *function, size_t block,
    const uint8_t *live_in_locals, const uint8_t *live_in_values,
    uint8_t *locals, uint8_t *values
) {
    const IrTerminator *term = &function->blocks[block].terminator;
    IrBlockId successors[2];
    size_t count = 0U;
    if ((term->kind == IR_TERM_JUMP || term->kind == IR_TERM_BRANCH) &&
        term->target < function->block_count)
        successors[count++] = term->target;
    if (term->kind == IR_TERM_BRANCH &&
        term->alternate < function->block_count)
        successors[count++] = term->alternate;
    for (size_t successor = 0U; successor < count; ++successor) {
        size_t local_offset =
            (size_t)successors[successor] * function->local_count;
        size_t value_offset =
            (size_t)successors[successor] * function->value_count;
        for (size_t local = 0U; local < function->local_count; ++local)
            locals[local] |= live_in_locals[local_offset + local];
        for (size_t value = 0U; value < function->value_count; ++value)
            values[value] |= live_in_values[value_offset + value];
    }
    async_add_terminator_uses(function, term, values);
}

static void async_liveness_dispose(CAsyncLiveness *liveness) {
    if (liveness == NULL) return;
    free(liveness->frame_values);
    free(liveness->frame_locals);
    free(liveness->values);
    free(liveness->locals);
    free(liveness);
}

static CAsyncLiveness *async_liveness_build(
    CEmitter *emitter, const IrFunction *function
) {
    CAsyncLiveness *result = calloc(1U, sizeof(*result));
    if (result == NULL) goto fail;
    result->local_count = function->local_count;
    result->value_count = function->value_count;
    for (size_t block = 0U; block < function->block_count; ++block)
        for (size_t i = 0U;
             i < function->blocks[block].instruction_count; ++i)
            if (function->blocks[block].instructions[i].opcode ==
                IR_OP_AWAIT)
                ++result->await_count;

    size_t local_width = function->local_count == 0U
        ? 1U : function->local_count;
    size_t value_width = function->value_count == 0U
        ? 1U : function->value_count;
    size_t blocks = function->block_count == 0U
        ? 1U : function->block_count;
    size_t awaits = result->await_count == 0U
        ? 1U : result->await_count;
    if (blocks > SIZE_MAX / local_width ||
        blocks > SIZE_MAX / value_width ||
        awaits > SIZE_MAX / local_width ||
        awaits > SIZE_MAX / value_width)
        goto fail;
    uint8_t *live_in_locals = calloc(blocks * local_width, 1U);
    uint8_t *live_in_values = calloc(blocks * value_width, 1U);
    uint8_t *locals = calloc(local_width, 1U);
    uint8_t *values = calloc(value_width, 1U);
    result->locals = calloc(awaits * local_width, 1U);
    result->values = calloc(awaits * value_width, 1U);
    result->frame_locals = calloc(local_width, 1U);
    result->frame_values = calloc(value_width, 1U);
    if (live_in_locals == NULL || live_in_values == NULL ||
        locals == NULL || values == NULL || result->locals == NULL ||
        result->values == NULL || result->frame_locals == NULL ||
        result->frame_values == NULL) {
        free(values);
        free(locals);
        free(live_in_values);
        free(live_in_locals);
        goto fail;
    }

    bool changed;
    do {
        changed = false;
        for (size_t reverse = function->block_count;
             reverse > 0U; --reverse) {
            size_t block = reverse - 1U;
            memset(locals, 0, local_width);
            memset(values, 0, value_width);
            async_merge_successors(
                function, block, live_in_locals, live_in_values,
                locals, values);
            const IrBlock *ir_block = &function->blocks[block];
            for (size_t i = ir_block->instruction_count; i > 0U; --i)
                async_transfer_instruction(
                    emitter, function, &ir_block->instructions[i - 1U],
                    locals, values);
            uint8_t *local_entry =
                live_in_locals + block * local_width;
            uint8_t *value_entry =
                live_in_values + block * value_width;
            if (memcmp(local_entry, locals, local_width) != 0) {
                memcpy(local_entry, locals, local_width);
                changed = true;
            }
            if (memcmp(value_entry, values, value_width) != 0) {
                memcpy(value_entry, values, value_width);
                changed = true;
            }
        }
    } while (changed);

    uint8_t *available_in = calloc(blocks * local_width, 1U);
    uint8_t *available_out = calloc(blocks * local_width, 1U);
    bool *known_in = calloc(blocks, sizeof(*known_in));
    bool *known_out = calloc(blocks, sizeof(*known_out));
    if (available_in == NULL || available_out == NULL ||
        known_in == NULL || known_out == NULL) {
        free(known_out);
        free(known_in);
        free(available_out);
        free(available_in);
        free(values);
        free(locals);
        free(live_in_values);
        free(live_in_locals);
        goto fail;
    }
    known_in[function->entry_block] = true;
    do {
        changed = false;
        for (size_t block = 0U; block < function->block_count; ++block) {
            if (!known_in[block]) continue;
            memcpy(locals, available_in + block * local_width,
                   local_width);
            const IrBlock *ir_block = &function->blocks[block];
            for (size_t i = 0U;
                 i < ir_block->instruction_count; ++i)
                async_update_local_availability(
                    function, &ir_block->instructions[i], locals);
            bool exit_changed = !known_out[block] ||
                memcmp(available_out + block * local_width,
                       locals, local_width) != 0;
            if (!exit_changed) continue;
            memcpy(available_out + block * local_width,
                   locals, local_width);
            known_out[block] = true;
            const IrTerminator *term = &ir_block->terminator;
            IrBlockId successors[2];
            size_t successor_count = 0U;
            if ((term->kind == IR_TERM_JUMP ||
                 term->kind == IR_TERM_BRANCH) &&
                term->target < function->block_count)
                successors[successor_count++] = term->target;
            if (term->kind == IR_TERM_BRANCH &&
                term->alternate < function->block_count)
                successors[successor_count++] = term->alternate;
            for (size_t s = 0U; s < successor_count; ++s) {
                uint8_t *entry = available_in +
                    (size_t)successors[s] * local_width;
                bool entry_changed = !known_in[successors[s]];
                if (!known_in[successors[s]]) {
                    memcpy(entry, locals, local_width);
                    known_in[successors[s]] = true;
                } else {
                    for (size_t local = 0U;
                         local < function->local_count; ++local) {
                        uint8_t merged = async_meet_availability(
                            entry[local], locals[local]);
                        if (merged != entry[local]) {
                            entry[local] = merged;
                            entry_changed = true;
                        }
                    }
                }
                changed |= entry_changed;
            }
        }
    } while (changed);

    size_t await_index = result->await_count;
    for (size_t reverse = function->block_count;
         reverse > 0U; --reverse) {
        size_t block = reverse - 1U;
        memset(locals, 0, local_width);
        memset(values, 0, value_width);
        async_merge_successors(
            function, block, live_in_locals, live_in_values,
            locals, values);
        const IrBlock *ir_block = &function->blocks[block];
        for (size_t i = ir_block->instruction_count; i > 0U; --i) {
            const IrInstruction *instruction =
                &ir_block->instructions[i - 1U];
            async_transfer_instruction(
                emitter, function, instruction, locals, values);
            if (instruction->opcode != IR_OP_AWAIT) continue;
            --await_index;
            uint8_t *await_locals =
                result->locals + await_index * local_width;
            uint8_t *await_values =
                result->values + await_index * value_width;
            memcpy(await_locals, locals, local_width);
            memcpy(await_values, values, value_width);
            for (size_t local = 0U;
                 local < function->local_count; ++local)
                result->frame_locals[local] |= await_locals[local];
            for (size_t value = 0U;
                 value < function->value_count; ++value)
                result->frame_values[value] |= await_values[value];
        }
    }

    await_index = 0U;
    for (size_t block = 0U; block < function->block_count; ++block) {
        memcpy(locals, available_in + block * local_width,
               local_width);
        const IrBlock *ir_block = &function->blocks[block];
        for (size_t i = 0U;
             i < ir_block->instruction_count; ++i) {
            const IrInstruction *instruction = &ir_block->instructions[i];
            if (instruction->opcode == IR_OP_AWAIT) {
                uint8_t *await_locals =
                    result->locals + await_index * local_width;
                for (size_t local = 0U;
                     local < function->local_count; ++local)
                    if (locals[local] == C_ASYNC_LOCAL_UNAVAILABLE)
                        await_locals[local] = 0U;
                ++await_index;
            }
            async_update_local_availability(
                function, instruction, locals);
        }
    }
    memset(result->frame_locals, 0, local_width);
    for (size_t index = 0U; index < result->await_count; ++index)
        for (size_t local = 0U;
             local < function->local_count; ++local)
            result->frame_locals[local] |=
                result->locals[index * local_width + local];
    free(known_out);
    free(known_in);
    free(available_out);
    free(available_in);
    free(values);
    free(locals);
    free(live_in_values);
    free(live_in_locals);
    return result;

fail:
    async_liveness_dispose(result);
    lang_diag(emitter->diagnostics, function->span,
              "out of memory computing async suspension liveness");
    emitter->failed = true;
    return NULL;
}

bool c_backend_async_function_supported(
    CEmitter *emitter, const IrFunction *function) {
    if (!function->is_async) return true;
    IrTypeId result = function->async_result_type;
    if (!c_backend_type_is_supported(emitter->ir, result) ||
        (c_backend_type_needs_drop(emitter, result) &&
         !c_backend_type_clone_supported(emitter->ir, result))) {
        c_backend_unsupported(
            emitter, function->span,
            "an async completion type without generated copy and cleanup support");
        return false;
    }
    return true;
}

void c_backend_emit_async_runtime(FILE *output) {
    fputs(
        "#if !defined(__wasm32__)\n"
        "#include <threads.h>\n"
        "#include <time.h>\n"
        "#endif\n\n"
        "typedef enum aster_task_state {\n"
        "    ASTER_TASK_PENDING,\n"
        "    ASTER_TASK_SUCCEEDED,\n"
        "    ASTER_TASK_FAULTED,\n"
        "    ASTER_TASK_CANCELED\n"
        "} aster_task_state;\n"
        "typedef void (*aster_task_callback)(void *context);\n"
        "typedef void (*aster_task_result_drop)(void *result);\n"
        "typedef struct aster_task_continuation {\n"
        "    aster_task_callback callback;\n"
        "    void *context;\n"
        "    struct aster_task_continuation *next;\n"
        "} aster_task_continuation;\n"
        "struct aster_task {\n"
        "    size_t references;\n"
        "    aster_task_state state;\n"
        "    void *result;\n"
        "    size_t result_size;\n"
        "    aster_task_result_drop result_drop;\n"
        "    aster_string *exception_message;\n"
        "    const char *exception_type;\n"
        "    aster_task_continuation *continuations;\n"
        "};\n"
        "typedef struct aster_task_timer {\n"
        "    int64_t deadline_ms;\n"
        "    aster_task *task;\n"
        "    aster_cancellation_state *cancellation;\n"
        "    struct aster_task_timer *next;\n"
        "} aster_task_timer;\n"
        "static aster_task_timer *aster_task_timers = NULL;\n\n"
        "static aster_task *aster_task_new(void) {\n"
        "    aster_task *task = aster_allocate(sizeof(*task));\n"
        "    *task = (aster_task){0};\n"
        "    task->references = 1U;\n"
        "    task->state = ASTER_TASK_PENDING;\n"
        "    return task;\n"
        "}\n"
        "static aster_task *aster_task_retain(aster_task *task) {\n"
        "    if (task == NULL) return NULL;\n"
        "    if (task->references == SIZE_MAX)\n"
        "        aster_trap(\"task reference count overflow\");\n"
        "    ++task->references;\n"
        "    return task;\n"
        "}\n"
        "static void aster_task_drop(aster_task *task) {\n"
        "    if (task == NULL) return;\n"
        "    if (task->references > 1U) {\n"
        "        --task->references;\n"
        "        return;\n"
        "    }\n"
        "    if (task->state == ASTER_TASK_PENDING)\n"
        "        aster_trap(\"last Task reference dropped while pending\");\n"
        "    if (task->result_drop != NULL && task->result != NULL)\n"
        "        task->result_drop(task->result);\n"
        "    free(task->result);\n"
        "    aster_string_drop(task->exception_message);\n"
        "    while (task->continuations != NULL) {\n"
        "        aster_task_continuation *next =\n"
        "            task->continuations->next;\n"
        "        free(task->continuations);\n"
        "        task->continuations = next;\n"
        "    }\n"
        "    free(task);\n"
        "}\n",
        output);
    fputs(
        "static void aster_task_run_continuations(aster_task *task) {\n"
        "    aster_task_continuation *continuation =\n"
        "        task->continuations;\n"
        "    task->continuations = NULL;\n"
        "    while (continuation != NULL) {\n"
        "        aster_task_continuation *next = continuation->next;\n"
        "        aster_task_callback callback = continuation->callback;\n"
        "        void *context = continuation->context;\n"
        "        free(continuation);\n"
        "        callback(context);\n"
        "        continuation = next;\n"
        "    }\n"
        "}\n"
        "static void aster_task_on_completed(\n"
        "        aster_task *task, aster_task_callback callback,\n"
        "        void *context) {\n"
        "    if (task->state != ASTER_TASK_PENDING) {\n"
        "        callback(context);\n"
        "        return;\n"
        "    }\n"
        "    aster_task_continuation *continuation =\n"
        "        aster_allocate(sizeof(*continuation));\n"
        "    continuation->callback = callback;\n"
        "    continuation->context = context;\n"
        "    continuation->next = task->continuations;\n"
        "    task->continuations = continuation;\n"
        "}\n"
        "static void aster_task_succeed(\n"
        "        aster_task *task, const void *result, size_t size,\n"
        "        aster_task_result_drop result_drop) {\n"
        "    if (task->state != ASTER_TASK_PENDING)\n"
        "        aster_trap(\"Task completed more than once\");\n"
        "    task->result_size = size;\n"
        "    task->result_drop = result_drop;\n"
        "    if (size != 0U) {\n"
        "        task->result = aster_allocate(size);\n"
        "        memcpy(task->result, result, size);\n"
        "    }\n"
        "    task->state = ASTER_TASK_SUCCEEDED;\n"
        "    aster_task_run_continuations(task);\n"
        "}\n"
        "static void aster_task_fault_from_current(aster_task *task) {\n"
        "    if (task->state != ASTER_TASK_PENDING ||\n"
        "        !aster_exception_pending)\n"
        "        aster_trap(\"Task faulted without a current exception\");\n"
        "    task->exception_message = aster_exception_message;\n"
        "    task->exception_type = aster_exception_type;\n"
        "    aster_exception_message = NULL;\n"
        "    aster_exception_type = NULL;\n"
        "    aster_exception_pending = false;\n"
        "    task->state = ASTER_TASK_FAULTED;\n"
        "    aster_task_run_continuations(task);\n"
        "}\n"
        "static void aster_task_cancel_from_current(aster_task *task) {\n"
        "    if (task->state != ASTER_TASK_PENDING ||\n"
        "        !aster_exception_pending)\n"
        "        aster_trap(\"Task canceled without a current exception\");\n"
        "    task->exception_message = aster_exception_message;\n"
        "    task->exception_type = aster_exception_type;\n"
        "    aster_exception_message = NULL;\n"
        "    aster_exception_type = NULL;\n"
        "    aster_exception_pending = false;\n"
        "    task->state = ASTER_TASK_CANCELED;\n"
        "    aster_task_run_continuations(task);\n"
        "}\n"
        "static void aster_task_restore_fault(aster_task *task) {\n"
        "    if (task->state != ASTER_TASK_FAULTED &&\n"
        "        task->state != ASTER_TASK_CANCELED)\n"
        "        aster_trap(\"await exception restoration requires a terminal Task\");\n"
        "    if (aster_exception_pending)\n"
        "        aster_string_drop(aster_exception_message);\n"
        "    aster_exception_message =\n"
        "        aster_string_clone(task->exception_message);\n"
        "    aster_exception_type = task->exception_type;\n"
        "    aster_exception_pending = true;\n"
        "}\n",
        output);
    fputs(
        "static void aster_task_fault_from_task(\n"
        "        aster_task *target, aster_task *source) {\n"
        "    if (target->state != ASTER_TASK_PENDING ||\n"
        "        source->state != ASTER_TASK_FAULTED)\n"
        "        aster_trap(\"invalid Task fault propagation\");\n"
        "    target->exception_message =\n"
        "        aster_string_clone(source->exception_message);\n"
        "    target->exception_type = source->exception_type;\n"
        "    target->state = ASTER_TASK_FAULTED;\n"
        "    aster_task_run_continuations(target);\n"
        "}\n"
        "static void aster_task_result_drop_task(void *storage) {\n"
        "    aster_task_drop(*(aster_task **)storage);\n"
        "}\n"
        "typedef struct aster_when_all_state {\n"
        "    aster_task *output;\n"
        "    aster_task **tasks;\n"
        "    size_t count;\n"
        "    size_t remaining;\n"
        "    void (*finish)(struct aster_when_all_state *state);\n"
        "} aster_when_all_state;\n"
        "static inline void aster_when_all_release(aster_when_all_state *state) {\n"
        "    for (size_t i = 0U; i < state->count; ++i)\n"
        "        aster_task_drop(state->tasks[i]);\n"
        "    free(state->tasks);\n"
        "    aster_task_drop(state->output);\n"
        "    free(state);\n"
        "}\n"
        "static inline bool aster_when_all_propagate_fault(\n"
        "        aster_when_all_state *state) {\n"
        "    for (size_t i = 0U; i < state->count; ++i) {\n"
        "        if (state->tasks[i]->state == ASTER_TASK_FAULTED) {\n"
        "            aster_task_fault_from_task(\n"
        "                state->output, state->tasks[i]);\n"
        "            return true;\n"
        "        }\n"
        "    }\n"
        "    return false;\n"
        "}\n"
        "static inline bool aster_when_all_propagate_cancellation(\n"
        "        aster_when_all_state *state) {\n"
        "    for (size_t i = 0U; i < state->count; ++i) {\n"
        "        if (state->tasks[i]->state == ASTER_TASK_CANCELED) {\n"
        "            aster_exception_message = aster_string_clone(\n"
        "                state->tasks[i]->exception_message);\n"
        "            aster_exception_type = state->tasks[i]->exception_type;\n"
        "            aster_exception_pending = true;\n"
        "            aster_task_cancel_from_current(state->output);\n"
        "            return true;\n"
        "        }\n"
        "    }\n"
        "    return false;\n"
        "}\n"
        "static void aster_when_all_completed(void *context) {\n"
        "    aster_when_all_state *state = context;\n"
        "    if (--state->remaining == 0U) state->finish(state);\n"
        "}\n"
        "static inline aster_task *aster_when_all_start(\n"
        "        aster_task **tasks, size_t count,\n"
        "        void (*finish)(aster_when_all_state *state)) {\n"
        "    aster_task *output = aster_task_new();\n"
        "    aster_when_all_state *state = aster_allocate(sizeof(*state));\n"
        "    state->output = aster_task_retain(output);\n"
        "    state->tasks = tasks;\n"
        "    state->count = count;\n"
        "    state->remaining = count;\n"
        "    state->finish = finish;\n"
        "    if (count == 0U) {\n"
        "        finish(state);\n"
        "        return output;\n"
        "    }\n"
        "    for (size_t i = 0U; i < count; ++i)\n"
        "        aster_task_on_completed(\n"
        "            tasks[i], aster_when_all_completed, state);\n"
        "    return output;\n"
        "}\n",
        output);
    fputs(
        "typedef struct aster_when_any_state aster_when_any_state;\n"
        "typedef struct aster_when_any_context {\n"
        "    aster_when_any_state *state;\n"
        "    aster_task *task;\n"
        "} aster_when_any_context;\n"
        "struct aster_when_any_state {\n"
        "    aster_task *output;\n"
        "    aster_task **tasks;\n"
        "    aster_when_any_context *contexts;\n"
        "    size_t count;\n"
        "    size_t remaining;\n"
        "    bool winner_selected;\n"
        "};\n"
        "static void aster_when_any_completed(void *raw_context) {\n"
        "    aster_when_any_context *context = raw_context;\n"
        "    aster_when_any_state *state = context->state;\n"
        "    if (!state->winner_selected) {\n"
        "        state->winner_selected = true;\n"
        "        aster_task *winner = aster_task_retain(context->task);\n"
        "        aster_task_succeed(\n"
        "            state->output, &winner, sizeof(winner),\n"
        "            aster_task_result_drop_task);\n"
        "        aster_task_drop(state->output);\n"
        "        state->output = NULL;\n"
        "    }\n"
        "    if (--state->remaining != 0U) return;\n"
        "    for (size_t i = 0U; i < state->count; ++i)\n"
        "        aster_task_drop(state->tasks[i]);\n"
        "    free(state->tasks);\n"
        "    free(state->contexts);\n"
        "    free(state);\n"
        "}\n"
        "static inline aster_task *aster_when_any_start(\n"
        "        aster_task **tasks, size_t count) {\n"
        "    if (count == 0U) {\n"
        "        free(tasks);\n"
        "        aster_task *output = aster_task_new();\n"
        "        if (aster_exception_pending)\n"
        "            aster_string_drop(aster_exception_message);\n"
        "        aster_exception_message = aster_string_from(\n"
        "            (aster_str){(const unsigned char *)\n"
        "                         \"Task.WhenAny requires at least one Task\",\n"
        "                         sizeof(\"Task.WhenAny requires at least one Task\") - 1U});\n"
        "        aster_exception_type = \"ArgumentException\";\n"
        "        aster_exception_pending = true;\n"
        "        aster_task_fault_from_current(output);\n"
        "        return output;\n"
        "    }\n"
        "    aster_task *output = aster_task_new();\n"
        "    aster_when_any_state *state = aster_allocate(sizeof(*state));\n"
        "    *state = (aster_when_any_state){0};\n"
        "    state->output = aster_task_retain(output);\n"
        "    state->tasks = tasks;\n"
        "    state->count = count;\n"
        "    state->remaining = count;\n"
        "    state->contexts = aster_allocate(\n"
        "        count * sizeof(*state->contexts));\n"
        "    for (size_t i = 0U; i < count; ++i) {\n"
        "        state->contexts[i].state = state;\n"
        "        state->contexts[i].task = tasks[i];\n"
        "    }\n"
        "    for (size_t i = 0U; i < count; ++i)\n"
        "        aster_task_on_completed(\n"
        "            tasks[i], aster_when_any_completed,\n"
        "            &state->contexts[i]);\n"
        "    return output;\n"
        "}\n",
        output);
    fputs(
        "#if defined(__wasm32__)\n"
        "__attribute__((import_module(\"aster\"), import_name(\"now_ms\")))\n"
        "int64_t aster_browser_now_ms(void);\n"
        "static int64_t aster_task_now_ms(void) {\n"
        "    return aster_browser_now_ms();\n"
        "}\n"
        "#else\n"
        "static int64_t aster_task_now_ms(void) {\n"
        "    struct timespec now;\n"
        "    if (timespec_get(&now, TIME_UTC) != TIME_UTC)\n"
        "        aster_trap(\"could not read clock for Task.Delay\");\n"
        "    return (int64_t)now.tv_sec * INT64_C(1000) +\n"
        "           (int64_t)now.tv_nsec / INT64_C(1000000);\n"
        "}\n"
        "#endif\n"
        "static aster_task *aster_task_delay(\n"
        "        int64_t milliseconds,\n"
        "        aster_cancellation_state *cancellation) {\n"
        "    if (milliseconds < 0)\n"
        "        aster_trap(\"Task.Delay requires nonnegative milliseconds\");\n"
        "    aster_task *task = aster_task_new();\n"
        "    if (cancellation != NULL && cancellation->requested) {\n"
        "        aster_exception_message = aster_string_from((aster_str){\n"
        "            (const unsigned char *)\"A task was canceled.\",\n"
        "            sizeof(\"A task was canceled.\") - 1U});\n"
        "        aster_exception_type = \"TaskCanceledException\";\n"
        "        aster_exception_pending = true;\n"
        "        aster_task_cancel_from_current(task);\n"
        "        aster_cancellation_drop(cancellation);\n"
        "        return task;\n"
        "    }\n"
        "    aster_task_timer *timer = aster_allocate(sizeof(*timer));\n"
        "    timer->deadline_ms = aster_task_now_ms() + milliseconds;\n"
        "    timer->task = aster_task_retain(task);\n"
        "    timer->cancellation = cancellation;\n"
        "    timer->next = aster_task_timers;\n"
        "    aster_task_timers = timer;\n"
        "    return task;\n"
        "}\n"
        "static bool aster_task_process_timers(void) {\n"
        "    int64_t now = aster_task_now_ms();\n"
        "    bool completed = false;\n"
        "    aster_task_timer **link = &aster_task_timers;\n"
        "    while (*link != NULL) {\n"
        "        aster_task_timer *timer = *link;\n"
        "        bool canceled = timer->cancellation != NULL &&\n"
        "            timer->cancellation->requested;\n"
        "        if (!canceled && timer->deadline_ms > now) {\n"
        "            link = &timer->next;\n"
        "            continue;\n"
        "        }\n"
        "        *link = timer->next;\n"
        "        if (canceled) {\n"
        "            aster_exception_message = aster_string_from((aster_str){\n"
        "                (const unsigned char *)\"A task was canceled.\",\n"
        "                sizeof(\"A task was canceled.\") - 1U});\n"
        "            aster_exception_type = \"TaskCanceledException\";\n"
        "            aster_exception_pending = true;\n"
        "            aster_task_cancel_from_current(timer->task);\n"
        "        } else {\n"
        "            uint8_t unit = UINT8_C(0);\n"
        "            aster_task_succeed(\n"
        "                timer->task, &unit, sizeof(unit), NULL);\n"
        "        }\n"
        "        aster_task_drop(timer->task);\n"
        "        aster_cancellation_drop(timer->cancellation);\n"
        "        free(timer);\n"
        "        completed = true;\n"
        "    }\n"
        "    return completed;\n"
        "}\n"
        "static void aster_task_run_until(aster_task *task) {\n"
        "    (void)aster_task_delay;\n"
        "    (void)aster_when_all_start;\n"
        "    (void)aster_when_any_start;\n"
        "#if defined(__wasm32__)\n"
        "    if (task->state == ASTER_TASK_PENDING)\n"
        "        aster_trap(\"browser Task must be awaited by the host\");\n"
        "#else\n"
        "    while (task->state == ASTER_TASK_PENDING) {\n"
        "        if (aster_task_process_timers()) continue;\n"
        "        if (aster_task_timers == NULL)\n"
        "            aster_trap(\"async executor has no work for pending Task\");\n"
        "        int64_t wait_ms = aster_task_timers->deadline_ms -\n"
        "                          aster_task_now_ms();\n"
        "        for (aster_task_timer *timer = aster_task_timers;\n"
        "             timer != NULL; timer = timer->next) {\n"
        "            int64_t candidate = timer->deadline_ms -\n"
        "                                aster_task_now_ms();\n"
        "            if (candidate < wait_ms) wait_ms = candidate;\n"
        "        }\n"
        "        if (wait_ms < 0) wait_ms = 0;\n"
        "        struct timespec duration = {\n"
        "            .tv_sec = (time_t)(wait_ms / 1000),\n"
        "            .tv_nsec = (long)((wait_ms % 1000) * 1000000)\n"
        "        };\n"
        "        (void)thrd_sleep(&duration, NULL);\n"
        "    }\n"
        "#endif\n"
        "}\n\n",
        output);
}

void c_backend_emit_async_result_helpers(CEmitter *emitter) {
    for (size_t type = 0U; type < emitter->ir->type_count; ++type) {
        bool completion_type = false;
        for (size_t function = 0U;
             function < emitter->ir->function_count; ++function) {
            const IrFunction *candidate =
                &emitter->ir->functions[function];
            if (emitter->reachable_functions[function] &&
                candidate->is_async &&
                candidate->async_result_type == (IrTypeId)type) {
                completion_type = true;
                break;
            }
        }
        for (size_t function = 0U;
             !completion_type && function < emitter->ir->function_count;
             ++function) {
            if (!emitter->reachable_functions[function]) continue;
            const IrFunction *candidate = &emitter->ir->functions[function];
            for (size_t block = 0U;
                 !completion_type && block < candidate->block_count; ++block)
                for (size_t instruction = 0U;
                     instruction < candidate->blocks[block].instruction_count;
                     ++instruction) {
                    const IrInstruction *call =
                        &candidate->blocks[block].instructions[instruction];
                    if (call->opcode == IR_OP_CALL_NATIVE &&
                        call->symbol != NULL &&
                        strcmp(call->symbol, "Task::WhenAll") == 0 &&
                        call->result_type < emitter->ir->type_count &&
                        emitter->ir->types[call->result_type].element_type ==
                            (IrTypeId)type) {
                        completion_type = true;
                        break;
                    }
                }
        }
        if (!completion_type ||
            !c_backend_type_needs_drop(emitter, (IrTypeId)type))
            continue;
        fprintf(emitter->output,
                "static void aster_task_result_drop_%zu(void *storage) {\n"
                "    aster_drop_%zu((",
                type, type);
        c_backend_emit_type(emitter, (IrTypeId)type);
        fputs(" *)storage);\n}\n\n", emitter->output);
    }
}

static bool instruction_is_when_all(const IrInstruction *instruction) {
    return instruction->opcode == IR_OP_CALL_NATIVE &&
           instruction->symbol != NULL &&
           strcmp(instruction->symbol, "Task::WhenAll") == 0 &&
           instruction->operand_count == 1U;
}

void c_backend_emit_async_combinator_helpers(CEmitter *emitter) {
    bool *emitted = calloc(emitter->ir->type_count, sizeof(*emitted));
    if (emitted == NULL) {
        c_backend_unsupported(emitter, (LangSpan){0},
                              "Task.WhenAll helper bookkeeping");
        return;
    }
    for (size_t f = 0U; f < emitter->ir->function_count; ++f) {
        if (!emitter->reachable_functions[f]) continue;
        const IrFunction *function = &emitter->ir->functions[f];
        for (size_t b = 0U; b < function->block_count; ++b) {
            const IrBlock *block = &function->blocks[b];
            for (size_t i = 0U; i < block->instruction_count; ++i) {
                const IrInstruction *instruction = &block->instructions[i];
                if (!instruction_is_when_all(instruction)) continue;
                IrTypeId input_type = function->value_types[
                    instruction->operands[0]];
                if (input_type >= emitter->ir->type_count ||
                    emitted[input_type])
                    continue;
                emitted[input_type] = true;
                const IrType *output_task =
                    &emitter->ir->types[instruction->result_type];
                IrTypeId result_type = output_task->element_type;
                const IrType *result = &emitter->ir->types[result_type];
                fprintf(emitter->output,
                        "static void aster_when_all_finish_%" PRIu32
                        "(aster_when_all_state *state) {\n",
                        input_type);
                fputs("    if (aster_when_all_propagate_fault(state)) {\n"
                      "        aster_when_all_release(state);\n"
                      "        return;\n"
                      "    }\n", emitter->output);
                fputs("    if (aster_when_all_propagate_cancellation(state)) {\n"
                      "        aster_when_all_release(state);\n"
                      "        return;\n"
                      "    }\n", emitter->output);
                if (result->shape == IR_TYPE_UNIT) {
                    fputs("    uint8_t unit = UINT8_C(0);\n"
                          "    aster_task_succeed(state->output, &unit, "
                          "sizeof(unit), NULL);\n",
                          emitter->output);
                } else {
                    IrTypeId element_type = result->element_type;
                    fprintf(emitter->output,
                            "    aster_vec_%" PRIu32 " *results = "
                            "aster_allocate(sizeof(*results));\n"
                            "    *results = (aster_vec_%" PRIu32 "){0};\n"
                            "    results->length = state->count;\n"
                            "    results->capacity = state->count;\n"
                            "    results->data = aster_allocate(\n"
                            "        state->count * sizeof(*results->data));\n"
                            "    for (size_t i = 0U; i < state->count; ++i)\n"
                            "        results->data[i] = ",
                            result_type, result_type);
                    bool clone_element =
                        c_backend_type_needs_drop(emitter, element_type);
                    if (clone_element)
                        fprintf(emitter->output,
                                "aster_clone_%" PRIu32 "(", element_type);
                    fputs("*((", emitter->output);
                    c_backend_emit_type(emitter, element_type);
                    fputs(" *)state->tasks[i]->result)", emitter->output);
                    if (clone_element)
                        fputc(')', emitter->output);
                    fputs(";\n", emitter->output);
                    fprintf(emitter->output,
                            "    aster_task_succeed(state->output, &results, "
                            "sizeof(results), aster_task_result_drop_%" PRIu32
                            ");\n",
                            result_type);
                }
                fputs("    aster_when_all_release(state);\n"
                      "}\n", emitter->output);
                fprintf(emitter->output,
                        "static aster_task *aster_when_all_%" PRIu32
                        "(aster_vec_%" PRIu32 " *list) {\n"
                        "    size_t count = list != NULL ? list->length : 0U;\n"
                        "    aster_task **tasks = list != NULL "
                        "? list->data : NULL;\n"
                        "    free(list);\n"
                        "    return aster_when_all_start(\n"
                        "        tasks, count, aster_when_all_finish_%" PRIu32
                        ");\n"
                        "}\n\n",
                        input_type, input_type, input_type);
            }
        }
    }
    free(emitted);
}

void c_backend_emit_async_frame_declaration(
    CEmitter *emitter, size_t function_index) {
    const IrFunction *function =
        &emitter->ir->functions[function_index];
    if (!function->is_async) return;
    CAsyncLiveness *liveness =
        async_liveness_build(emitter, function);
    if (liveness == NULL) return;
    FILE *output = emitter->output;
    fprintf(output, "typedef struct aster_async_frame_%zu {\n", function_index);
    fputs("    size_t state;\n    aster_task *task;\n", output);
    for (size_t p = 0U; p < function->parameter_count; ++p) {
        fputs("    ", output);
        c_backend_emit_type(emitter, function->parameters[p].type);
        fprintf(output, " p%zu;\n", p);
    }
    for (size_t l = 0U; l < function->local_count; ++l) {
        if (!liveness->frame_locals[l]) continue;
        fputs("    ", output);
        c_backend_emit_type(emitter, function->locals[l].type);
        fprintf(output,
                c_backend_local_is_borrowed_alias(
                    emitter, function, (uint32_t)l)
                    ? " *local%zu;\n" : " local%zu;\n",
                l);
        if (c_backend_local_tracks_drop(
                emitter, function, (uint32_t)l))
            fprintf(output, "    bool local%zu_live;\n", l);
    }
    for (size_t v = 0U; v < function->value_count; ++v) {
        if (!liveness->frame_values[v]) continue;
        fputs("    ", output);
        c_backend_emit_type(emitter, function->value_types[v]);
        fprintf(output, " v%zu;\n", v);
    }
    fprintf(output, "} aster_async_frame_%zu;\n\n", function_index);
    async_liveness_dispose(liveness);
}

void c_backend_emit_async_step_prototype(
    CEmitter *emitter, size_t function_index) {
    if (!emitter->ir->functions[function_index].is_async) return;
    fprintf(emitter->output,
            "static void aster_async_step_%zu(void *context);\n",
            function_index);
}

static void emit_frame_save(CEmitter *emitter,
                            const IrFunction *function) {
    FILE *output = emitter->output;
    CAsyncLiveness *liveness = emitter->async_liveness;
    size_t await_index = emitter->async_await_index - 1U;
    const uint8_t *locals = liveness->locals +
        await_index * (liveness->local_count == 0U
            ? 1U : liveness->local_count);
    const uint8_t *values = liveness->values +
        await_index * (liveness->value_count == 0U
            ? 1U : liveness->value_count);
    for (size_t l = 0U; l < function->local_count; ++l) {
        if (!locals[l]) continue;
        if (c_backend_local_is_borrowed_alias(
                emitter, function, (uint32_t)l))
            fprintf(output,
                    "        frame->local%zu = l%zu_ref;\n", l, l);
        else
            fprintf(output, "        frame->local%zu = l%zu;\n", l, l);
        if (c_backend_local_tracks_drop(
                emitter, function, (uint32_t)l))
            fprintf(output,
                    "        frame->local%zu_live = l%zu_live;\n", l, l);
    }
    for (size_t v = 0U; v < function->value_count; ++v) {
        if (!values[v]) continue;
        fprintf(output, "        frame->v%zu = v%zu;\n", v, v);
    }
}

static void emit_frame_restore(CEmitter *emitter,
                               const IrFunction *function,
                               size_t await_index) {
    FILE *output = emitter->output;
    CAsyncLiveness *liveness = emitter->async_liveness;
    const uint8_t *locals = liveness->locals +
        await_index * (liveness->local_count == 0U
            ? 1U : liveness->local_count);
    const uint8_t *values = liveness->values +
        await_index * (liveness->value_count == 0U
            ? 1U : liveness->value_count);
    /* Restore this state only. Loading the frame union would resurrect stale
     * bits for owners moved between two different suspension points. */
    for (size_t local = 0U; local < function->local_count; ++local) {
        if (!locals[local]) continue;
        if (c_backend_local_is_borrowed_alias(
                emitter, function, (uint32_t)local))
            fprintf(output,
                    "            l%zu_ref = frame->local%zu;\n",
                    local, local);
        else
            fprintf(output,
                    "            l%zu = frame->local%zu;\n",
                    local, local);
        if (c_backend_local_tracks_drop(
                emitter, function, (uint32_t)local))
            fprintf(output,
                    "            l%zu_live = frame->local%zu_live;\n",
                    local, local);
    }
    for (size_t value = 0U; value < function->value_count; ++value)
        if (values[value])
            fprintf(output,
                    "            v%zu = frame->v%zu;\n",
                    value, value);
}

void c_backend_emit_async_await(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction) {
    FILE *output = emitter->output;
    size_t state = ++emitter->async_await_index;
    IrValueId task_value = instruction->operands[0];
    fprintf(output,
            "    if (v%" PRIu32 " == NULL)\n"
            "        aster_trap(\"cannot await a null Task\");\n"
            "    if (v%" PRIu32 "->state == ASTER_TASK_PENDING) {\n"
            "        frame->state = %zuU;\n",
            task_value, task_value, state);
    emit_frame_save(emitter, function);
    fprintf(output,
            "        aster_task_on_completed(v%" PRIu32
            ", aster_async_step_%zu, frame);\n"
            "        return;\n"
            "    }\n"
            "aster_async_%zu_resume_%zu: ;\n"
            "    if (v%" PRIu32 "->state == ASTER_TASK_FAULTED ||\n"
            "        v%" PRIu32 "->state == ASTER_TASK_CANCELED)\n"
            "        aster_task_restore_fault(v%" PRIu32 ");\n"
            "    else if (v%" PRIu32 "->state != ASTER_TASK_SUCCEEDED)\n"
            "        aster_trap(\"await resumed from invalid Task state\");\n",
            task_value, emitter->async_function_index,
            emitter->async_function_index, state,
            task_value, task_value, task_value, task_value);
    const IrType *result =
        &emitter->ir->types[instruction->result_type];
    if (result->shape == IR_TYPE_UNIT) {
        fprintf(output,
                "    v%" PRIu32 " = UINT8_C(0);\n",
                instruction->result);
    } else {
        fprintf(output,
                "    if (v%" PRIu32 "->state == ASTER_TASK_SUCCEEDED) {\n"
                "        if (v%" PRIu32 "->result_size != sizeof(v%" PRIu32 "))\n"
                "            aster_trap(\"awaited Task result size mismatch\");\n"
                "        v%" PRIu32 " = ",
                task_value, task_value, instruction->result,
                instruction->result);
        if (c_backend_type_needs_drop(
                emitter, instruction->result_type))
            fprintf(output, "aster_clone_%" PRIu32 "(*(",
                    instruction->result_type);
        else
            fputs("*(", output);
        c_backend_emit_type(emitter, instruction->result_type);
        fprintf(output, " *)v%" PRIu32 "->result%s;\n    }\n",
                task_value,
                c_backend_type_needs_drop(
                    emitter, instruction->result_type) ? ")" : "");
    }
    fprintf(output,
            "    aster_task_drop(v%" PRIu32 ");\n"
            "    v%" PRIu32 " = NULL;\n",
            task_value, task_value);
}

void c_backend_emit_async_terminator(
    CEmitter *emitter, const IrFunction *function,
    const IrTerminator *terminator) {
    FILE *output = emitter->output;
    switch (terminator->kind) {
        case IR_TERM_JUMP:
            fprintf(output, "    goto b%" PRIu32 ";\n", terminator->target);
            return;
        case IR_TERM_BRANCH:
            fprintf(output,
                    "    if (v%" PRIu32 ") goto b%" PRIu32
                    "; else goto b%" PRIu32 ";\n",
                    terminator->value, terminator->target,
                    terminator->alternate);
            return;
        case IR_TERM_RETURN:
            fputs("    {\n"
                  "    aster_task *completed_task = frame->task;\n", output);
            fprintf(output,
                    "    aster_task_succeed(completed_task, &v%" PRIu32
                    ", sizeof(v%" PRIu32 "), ",
                    terminator->value, terminator->value);
            if (c_backend_type_needs_drop(
                    emitter, function->async_result_type))
                fprintf(output, "aster_task_result_drop_%" PRIu32,
                        function->async_result_type);
            else
                fputs("NULL", output);
            fputs(");\n", output);
            fputs("    free(frame);\n"
                  "    aster_task_drop(completed_task);\n"
                  "    return;\n"
                  "    }\n", output);
            return;
        case IR_TERM_PROPAGATE_EXCEPTION:
            fputs("    {\n"
                  "    aster_task *faulted_task = frame->task;\n"
                  "    aster_task_fault_from_current(faulted_task);\n"
                  "    free(frame);\n"
                  "    aster_task_drop(faulted_task);\n"
                  "    return;\n"
                  "    }\n", output);
            return;
        case IR_TERM_TRAP:
            fputs("    aster_trap(\"Aster runtime trap\");\n", output);
            return;
        case IR_TERM_NONE:
            c_backend_unsupported(
                emitter, terminator->span,
                "an unterminated async IR block");
            return;
    }
}

void c_backend_emit_async_function(
    CEmitter *emitter, size_t function_index) {
    const IrFunction *function =
        &emitter->ir->functions[function_index];
    CAsyncLiveness *liveness =
        async_liveness_build(emitter, function);
    if (liveness == NULL) return;
    emitter->async_liveness = liveness;
    FILE *output = emitter->output;
    c_backend_emit_type(emitter, function->return_type);
    fprintf(output, " aster_fn_%zu(", function_index);
    if (function->parameter_count == 0U) {
        fputs("void", output);
    } else {
        for (size_t p = 0U; p < function->parameter_count; ++p) {
            if (p != 0U) fputs(", ", output);
            c_backend_emit_type(emitter, function->parameters[p].type);
            fprintf(output, " p%zu", p);
        }
    }
    fprintf(output,
            ") {\n"
            "    aster_async_frame_%zu *frame =\n"
            "        aster_allocate(sizeof(*frame));\n"
            "    *frame = (aster_async_frame_%zu){0};\n"
            "    frame->task = aster_task_new();\n"
            "    (void)aster_task_retain(frame->task);\n",
            function_index, function_index);
    for (size_t p = 0U; p < function->parameter_count; ++p)
        fprintf(output, "    frame->p%zu = p%zu;\n", p, p);
    fprintf(output,
            "    aster_task *task = frame->task;\n"
            "    aster_async_step_%zu(frame);\n"
            "    return task;\n"
            "}\n\n"
            "static void aster_async_step_%zu(void *context) {\n"
            "    aster_async_frame_%zu *frame = context;\n",
            function_index, function_index, function_index);
    for (size_t p = 0U; p < function->parameter_count; ++p) {
        fputs("    ", output);
        c_backend_emit_type(emitter, function->parameters[p].type);
        fprintf(output, " p%zu = frame->p%zu;\n", p, p);
    }
    for (size_t l = 0U; l < function->local_count; ++l) {
        fputs("    ", output);
        c_backend_emit_type(emitter, function->locals[l].type);
        if (c_backend_local_is_borrowed_alias(
                emitter, function, (uint32_t)l)) {
            fprintf(output, " *l%zu_ref = NULL;\n", l);
            fprintf(output, "#define l%zu (*l%zu_ref)\n", l, l);
        } else {
            fprintf(output, " l%zu = {0};\n", l);
            fprintf(output, "    (void)l%zu;\n", l);
        }
        if (c_backend_local_tracks_drop(
                emitter, function, (uint32_t)l))
            fprintf(output, "    bool l%zu_live = false;\n", l);
    }
    for (size_t v = 0U; v < function->value_count; ++v) {
        fputs("    ", output);
        c_backend_emit_type(emitter, function->value_types[v]);
        fprintf(output, " v%zu = {0};\n", v);
        fprintf(output, "    (void)v%zu;\n", v);
    }
    fputs("    switch (frame->state) {\n"
          "        case 0U: break;\n", output);
    size_t await_count = 0U;
    for (size_t b = 0U; b < function->block_count; ++b)
        for (size_t i = 0U;
             i < function->blocks[b].instruction_count; ++i)
            if (function->blocks[b].instructions[i].opcode == IR_OP_AWAIT) {
                ++await_count;
                fprintf(output,
                        "        case %zuU:\n",
                        await_count);
                emit_frame_restore(
                    emitter, function, await_count - 1U);
                fprintf(output,
                        "            goto aster_async_%zu_resume_%zu;\n",
                        function_index, await_count);
            }
    fputs("        default: aster_trap(\"invalid async frame state\");\n"
          "    }\n", output);
    for (size_t b = 0U; b < function->block_count; ++b)
        fprintf(output, "    if (false) goto b%zu;\n", b);
    fprintf(output, "    goto b%" PRIu32 ";\n", function->entry_block);
    emitter->async_function_index = function_index;
    emitter->async_await_index = 0U;
    for (size_t b = 0U; b < function->block_count; ++b) {
        fprintf(output, "b%zu: ;\n", b);
        const IrBlock *block = &function->blocks[b];
        for (size_t i = 0U; i < block->instruction_count; ++i) {
            c_backend_emit_instruction(
                emitter, function, &block->instructions[i]);
            if (emitter->failed) goto done;
        }
        c_backend_emit_terminator(emitter, function, &block->terminator);
        if (emitter->failed) goto done;
    }
    for (size_t l = 0U; l < function->local_count; ++l)
        if (c_backend_local_is_borrowed_alias(
                emitter, function, (uint32_t)l))
            fprintf(output, "#undef l%zu\n", l);
    fputs("}\n\n", output);
done:
    emitter->async_liveness = NULL;
    async_liveness_dispose(liveness);
}
