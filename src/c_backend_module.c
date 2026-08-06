#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool function_supported(CEmitter *emitter,
                               const IrFunction *function) {
    LangSpan span = function->span;
    if (!c_backend_async_function_supported(emitter, function))
        return false;
    if (!c_backend_type_is_supported(
            emitter->ir, function->return_type)) {
        c_backend_unsupported(emitter, span, "this function return type");
        return false;
    }
    for (size_t p = 0U; p < function->parameter_count; ++p)
        if (!c_backend_type_is_supported(
                emitter->ir, function->parameters[p].type)) {
            c_backend_unsupported(emitter, span, "this function parameter type");
            return false;
        }
    for (size_t l = 0U; l < function->local_count; ++l)
        if (!c_backend_type_is_supported(
                emitter->ir, function->locals[l].type)) {
            c_backend_unsupported(emitter, span, "this local type");
            return false;
        }
    for (size_t v = 0U; v < function->value_count; ++v)
        if (!c_backend_type_is_supported(
                emitter->ir, function->value_types[v])) {
            c_backend_unsupported(emitter, span, "this virtual value type");
            return false;
        }
    for (size_t b = 0U; b < function->block_count; ++b) {
        const IrBlock *block = &function->blocks[b];
        for (size_t i = 0U;
             i < block->instruction_count; ++i) {
            const IrInstruction *instruction =
                &block->instructions[i];
            if (instruction->opcode == IR_OP_VALUE_CLONE &&
                emitter->ir->types[
                    instruction->result_type].copy_policy !=
                    IR_COPY_TRIVIAL &&
                !c_backend_type_clone_supported(
                    emitter->ir,
                    instruction->result_type)) {
                c_backend_unsupported(
                    emitter, instruction->span,
                    "copying a cleanup-managed value without a lowered copy function");
                return false;
            }
        }
    }
    return true;
}

static bool function_uses_utc_clock(const IrFunction *function) {
    for (size_t b = 0U; b < function->block_count; ++b) {
        const IrBlock *block = &function->blocks[b];
        for (size_t i = 0U; i < block->instruction_count; ++i) {
            const IrInstruction *instruction = &block->instructions[i];
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "NativeUtcNowUnixMilliseconds") == 0)
                return true;
        }
    }
    return false;
}

/*
 * Cleanup-managed SSA values can remain live across a later throwing call
 * (for example, an earlier call argument).  Generated C therefore tracks
 * their ownership just like local slots instead of relying on C scope exit.
 */
static bool virtual_value_tracks_drop(
    const CEmitter *emitter, const IrFunction *function,
    IrValueId value
) {
    return value < function->value_count &&
           c_backend_type_needs_drop(
               emitter, function->value_types[value]);
}

static bool instruction_result_owns_value(
    const CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction
) {
    if (emitter->render_direct ||
        instruction->result == IR_INVALID_ID ||
        !virtual_value_tracks_drop(
            emitter, function, instruction->result))
        return false;
    switch (instruction->opcode) {
        case IR_OP_LOCAL_LOAD:
        case IR_OP_LOCAL_FIELD_BORROW:
        case IR_OP_ENUM_PAYLOAD_BORROW:
        case IR_OP_LIST_ELEMENT_BORROW:
        case IR_OP_QUEUE_FRONT_BORROW:
        case IR_OP_STACK_TOP_BORROW:
        case IR_OP_DICTIONARY_GET_BORROW:
        case IR_OP_DICTIONARY_FIND:
        case IR_OP_DICTIONARY_KEY_BORROW:
        case IR_OP_DICTIONARY_VALUE_BORROW:
        case IR_OP_LOCAL_ITERATOR_NEXT:
            return false;
        case IR_OP_FIELD_GET:
            return instruction->auxiliary != 1U &&
                   instruction->auxiliary != 2U;
        case IR_OP_INDEX_GET:
            return instruction->integer == 0U;
        case IR_OP_PARAMETER:
            return instruction->index >= function->parameter_count ||
                   !parameter_mode_is_reference(
                       function->parameters[instruction->index].mode);
        default:
            return true;
    }
}

static bool instruction_consumes_operand(
    const CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction, size_t operand
) {
    switch (instruction->opcode) {
        case IR_OP_LOCAL_STORE:
        case IR_OP_VALUE_DISCARD:
        case IR_OP_EXCEPTION_SET:
        case IR_OP_AGGREGATE_MAKE:
        case IR_OP_ITERATOR_BEGIN:
            return true;
        case IR_OP_VALUE_CLONE:
            return instruction->auxiliary != 0U &&
                   !c_backend_value_is_borrowed_projection(
                       function, instruction->operands[operand]);
        case IR_OP_LOCAL_FIELD_SET:
            return operand == 0U;
        case IR_OP_LOCAL_INDEX_SET:
            return operand == 1U;
        case IR_OP_RAW_STORE:
            return operand == 1U;
        case IR_OP_FIELD_GET:
            if (instruction->auxiliary == 1U ||
                instruction->auxiliary == 2U)
                return false;
            return operand == 0U &&
                   !c_backend_value_is_borrowed_projection(
                       function, instruction->operands[operand]);
        case IR_OP_INDEX_GET:
            if (instruction->integer != 0U) return false;
            return operand == 0U &&
                   !c_backend_value_is_borrowed_projection(
                       function, instruction->operands[operand]);
        case IR_OP_EQUAL:
        case IR_OP_NOT_EQUAL:
            return c_backend_type_needs_drop(
                emitter,
                function->value_types[
                    instruction->operands[operand]]);
        case IR_OP_CALL_DIRECT:
        case IR_OP_CALL_VIRTUAL:
            return operand >= instruction->argument_mode_count ||
                !parameter_mode_is_reference(
                    instruction->argument_modes[operand]);
        case IR_OP_CALL_INDIRECT:
            if (operand == 0U) return false;
            return operand - 1U >= instruction->argument_mode_count ||
                !parameter_mode_is_reference(
                    instruction->argument_modes[operand - 1U]);
        case IR_OP_CALL_NATIVE:
            return operand >= instruction->argument_mode_count ||
                !parameter_mode_is_reference(
                    instruction->argument_modes[operand]);
        case IR_OP_LOCAL_ELEMENT_PROPERTY:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND:
        case IR_OP_LOCAL_ELEMENT_CSS_VALUE:
        case IR_OP_LOCAL_ELEMENT_APPEND:
        case IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED:
            return true;
        default:
            return false;
    }
}

static void emit_virtual_result_begin(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction
) {
    if (!instruction_result_owns_value(
            emitter, function, instruction))
        return;
    fprintf(emitter->output,
            "    if (v%" PRIu32 "_live) ", instruction->result);
    c_backend_emit_drop_call(
        emitter, instruction->result_type, "v", instruction->result);
    fputs(";\n", emitter->output);
}

static void emit_virtual_ownership_update(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction
) {
    if (emitter->render_direct) return;
    if (instruction_result_owns_value(
            emitter, function, instruction))
        fprintf(emitter->output,
                "    v%" PRIu32 "_live = true;\n",
                instruction->result);
    for (size_t i = 0U; i < instruction->operand_count; ++i) {
        IrValueId operand = instruction->operands[i];
        if (virtual_value_tracks_drop(emitter, function, operand) &&
            instruction_consumes_operand(
                emitter, function, instruction, i))
            fprintf(emitter->output,
                    "    v%" PRIu32 "_live = false;\n", operand);
    }
}

static void emit_function_signature(CEmitter *emitter, size_t index,
                                    bool prototype) {
    const IrFunction *function = &emitter->ir->functions[index];
    c_backend_emit_type(emitter, function->return_type);
    fprintf(emitter->output, " aster_fn_%zu(", index);
    if (function->parameter_count == 0U) {
        fputs("void", emitter->output);
    } else {
        for (size_t p = 0U; p < function->parameter_count; ++p) {
            if (p != 0U) fputs(", ", emitter->output);
            c_backend_emit_type(emitter, function->parameters[p].type);
            if (parameter_mode_is_reference(function->parameters[p].mode))
                fputs(" *", emitter->output);
            fprintf(emitter->output, " p%zu", p);
        }
    }
    fputs(prototype ? ");\n" : ") {\n", emitter->output);
}

static void emit_delegate_adapter(
    CEmitter *emitter, size_t index, bool bound
) {
    const IrFunction *function = &emitter->ir->functions[index];
    size_t first_parameter = bound ? 1U : 0U;
    c_backend_emit_type(emitter, function->return_type);
    fprintf(
        emitter->output, " aster_delegate_%s_%zu(void *receiver",
        bound ? "bound" : "unbound", index);
    for (size_t p = first_parameter;
         p < function->parameter_count; ++p) {
        fputs(", ", emitter->output);
        c_backend_emit_type(emitter, function->parameters[p].type);
        if (parameter_mode_is_reference(function->parameters[p].mode))
            fputs(" *", emitter->output);
        fprintf(emitter->output, " p%zu", p);
    }
    fputs(") {\n", emitter->output);
    if (bound)
        fputs("    if (receiver == NULL) "
              "aster_trap(\"bound delegate receiver is null\");\n",
              emitter->output);
    else
        fputs("    (void)receiver;\n", emitter->output);
    fprintf(emitter->output, "    return aster_fn_%zu(", index);
    if (bound) {
        fputc('(', emitter->output);
        c_backend_emit_type(
            emitter, function->parameters[0].type);
        fputs(")receiver", emitter->output);
    }
    for (size_t p = first_parameter;
         p < function->parameter_count; ++p) {
        if (p != first_parameter || bound) fputs(", ", emitter->output);
        fprintf(emitter->output, "p%zu", p);
    }
    fputs(");\n}\n\n", emitter->output);
}

static void emit_delegate_adapters(CEmitter *emitter) {
    size_t count = emitter->ir->function_count;
    bool *unbound = calloc(count, sizeof(*unbound));
    bool *bound = calloc(count, sizeof(*bound));
    if ((count != 0U && unbound == NULL) ||
        (count != 0U && bound == NULL)) {
        free(unbound);
        free(bound);
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    for (size_t f = 0U; f < count; ++f) {
        if (!emitter->reachable_functions[f]) continue;
        const IrFunction *function = &emitter->ir->functions[f];
        for (size_t b = 0U; b < function->block_count; ++b)
            for (size_t i = 0U;
                 i < function->blocks[b].instruction_count; ++i) {
                const IrInstruction *instruction =
                    &function->blocks[b].instructions[i];
                if (instruction->index >= count) continue;
                if (instruction->opcode == IR_OP_FUNCTION_REF)
                    unbound[instruction->index] = true;
                else if (instruction->opcode == IR_OP_BOUND_METHOD_REF) {
                    if (emitter->ir->functions[
                            instruction->index].is_virtual) {
                        IrFunctionId root = emitter->ir->functions[
                            instruction->index].virtual_root;
                        if (root == IR_INVALID_ID)
                            root = instruction->index;
                        for (size_t target = 0U; target < count; ++target)
                            if (emitter->ir->functions[target].virtual_root == root &&
                                !emitter->ir->functions[target].is_abstract)
                                bound[target] = true;
                        for (size_t entry = 0U;
                             entry < emitter->ir->interface_dispatch_count;
                             ++entry)
                            if (emitter->ir->interface_dispatches[entry]
                                    .interface_function == root)
                                bound[emitter->ir->interface_dispatches[entry]
                                    .target_function] = true;
                    } else {
                        bound[instruction->index] = true;
                    }
                }
            }
    }
    for (size_t f = 0U; f < count; ++f) {
        if (unbound[f]) emit_delegate_adapter(emitter, f, false);
        if (bound[f]) emit_delegate_adapter(emitter, f, true);
    }
    free(unbound);
    free(bound);
}

static void emit_render_function_signature(
    CEmitter *emitter, size_t index, bool prototype) {
    const IrFunction *function = &emitter->ir->functions[index];
    c_backend_emit_type(emitter, function->return_type);
    fprintf(
        emitter->output,
        " aster_fn_%zu_into("
        "aster_element_builder *render_destination",
        index);
    for (size_t p = 0U; p < function->parameter_count; ++p) {
        fputs(", ", emitter->output);
        c_backend_emit_type(emitter, function->parameters[p].type);
        if (parameter_mode_is_reference(function->parameters[p].mode))
            fputs(" *", emitter->output);
        fprintf(emitter->output, " p%zu", p);
    }
    fputs(prototype ? ");\n" : ") {\n", emitter->output);
}

static void emit_direct_append_signature(
    CEmitter *emitter, size_t index, bool prototype) {
    const IrFunction *function = &emitter->ir->functions[index];
    fprintf(
        emitter->output,
        "static void aster_fn_%zu_append("
        "aster_string_builder *render_builder",
        index);
    for (size_t p = 0U; p < function->parameter_count; ++p) {
        fputs(", ", emitter->output);
        c_backend_emit_type(emitter, function->parameters[p].type);
        if (parameter_mode_is_reference(function->parameters[p].mode))
            fputs(" *", emitter->output);
        fprintf(emitter->output, " p%zu", p);
    }
    fputs(prototype ? ");\n" : ") {\n", emitter->output);
}

static void emit_direct_render_signature(
    CEmitter *emitter, size_t index, bool prototype) {
    const IrFunction *function = &emitter->ir->functions[index];
    fprintf(emitter->output,
            "aster_string *aster_fn_%zu_render(", index);
    if (function->parameter_count == 0U) {
        fputs("void", emitter->output);
    } else {
        for (size_t p = 0U; p < function->parameter_count; ++p) {
            if (p != 0U) fputs(", ", emitter->output);
            c_backend_emit_type(emitter, function->parameters[p].type);
            if (parameter_mode_is_reference(function->parameters[p].mode))
                fputs(" *", emitter->output);
            fprintf(emitter->output, " p%zu", p);
        }
    }
    fputs(prototype ? ");\n" : ") {\n", emitter->output);
}

static void emit_function_variant(
    CEmitter *emitter, size_t index, bool render_into,
    bool render_direct) {
    const IrFunction *function = &emitter->ir->functions[index];
    bool previous_render_into = emitter->render_into;
    bool previous_render_direct = emitter->render_direct;
    const char **previous_direct_local_tags =
        emitter->direct_local_tags;
    size_t *previous_direct_local_tag_lengths =
        emitter->direct_local_tag_lengths;
    const char **direct_local_tags = NULL;
    size_t *direct_local_tag_lengths = NULL;
    LangSpan previous_render_root_span =
        emitter->render_root_span;
    if (render_direct && function->local_count != 0U) {
        direct_local_tags = calloc(
            function->local_count, sizeof(*direct_local_tags));
        direct_local_tag_lengths = calloc(
            function->local_count, sizeof(*direct_local_tag_lengths));
        if (direct_local_tags == NULL ||
            direct_local_tag_lengths == NULL) {
            free(direct_local_tags);
            free(direct_local_tag_lengths);
            c_backend_unsupported(
                emitter, function->render_root_span,
                "direct HTML render allocation");
            return;
        }
        for (size_t b = 0U; b < function->block_count; ++b)
            for (size_t i = 0U;
                 i < function->blocks[b].instruction_count; ++i) {
                const IrInstruction *store =
                    &function->blocks[b].instructions[i];
                if (store->opcode != IR_OP_LOCAL_STORE ||
                    store->index >= function->local_count ||
                    store->operand_count == 0U ||
                    emitter->ir->types[
                        function->locals[store->index].type].shape !=
                        IR_TYPE_ELEMENT_BUILDER)
                    continue;
                const IrInstruction *begin = c_backend_find_value_producer(
                    function, store->operands[0]);
                if (begin == NULL || begin->opcode != IR_OP_ELEMENT_BEGIN)
                    continue;
                direct_local_tags[store->index] = begin->symbol;
                direct_local_tag_lengths[store->index] =
                    begin->symbol_length;
            }
    }
    emitter->render_into = render_into;
    emitter->render_direct = render_direct;
    emitter->direct_local_tags = direct_local_tags;
    emitter->direct_local_tag_lengths = direct_local_tag_lengths;
    emitter->render_root_span =
        render_into ? function->render_root_span : (LangSpan){0};
    if (render_direct)
        emit_direct_append_signature(emitter, index, false);
    else if (render_into)
        emit_render_function_signature(emitter, index, false);
    else
        emit_function_signature(emitter, index, false);
    for (size_t l = 0U; l < function->local_count; ++l) {
        bool referenced_parameter =
            l < function->parameter_count &&
            parameter_mode_is_reference(function->parameters[l].mode);
        if (referenced_parameter) {
            fprintf(emitter->output,
                    "#define l%zu (*p%zu)\n"
                    "    (void)l%zu;\n", l, l, l);
            if (c_backend_local_tracks_drop(
                    emitter, function, (uint32_t)l))
                fprintf(emitter->output,
                        "    bool l%zu_live = false;\n", l);
            continue;
        }
        fputs("    ", emitter->output);
        c_backend_emit_type(emitter, function->locals[l].type);
        fprintf(emitter->output, " l%zu = {0};\n", l);
        fprintf(emitter->output, "    (void)l%zu;\n", l);
        if (c_backend_local_tracks_drop(
                emitter, function, (uint32_t)l) &&
            !(render_direct && emitter->ir->types[
                function->locals[l].type].shape ==
                IR_TYPE_ELEMENT_BUILDER))
            fprintf(emitter->output,
                    "    bool l%zu_live = false;\n", l);
        if (render_direct && emitter->ir->types[
                function->locals[l].type].shape ==
                IR_TYPE_ELEMENT_BUILDER)
            fprintf(emitter->output,
                    "    bool l%zu_direct_open = false;\n", l);
    }
    for (size_t v = 0U; v < function->value_count; ++v) {
        fputs("    ", emitter->output);
        c_backend_emit_type(emitter, function->value_types[v]);
        fprintf(emitter->output, " v%zu = {0};\n", v);
        fprintf(emitter->output, "    (void)v%zu;\n", v);
        if (!render_direct && virtual_value_tracks_drop(
                emitter, function, (IrValueId)v))
            fprintf(emitter->output,
                    "    bool v%zu_live = false;\n", v);
    }
    /*
     * Typed IR retains structurally valid unreachable merge blocks. Mention
     * every label from a dead branch so warning-clean C can still emit those
     * blocks without backend-specific CFG deletion.
     */
    for (size_t b = 0U; b < function->block_count; ++b)
        fprintf(emitter->output,
                "    if (false) goto b%zu;\n", b);
    fprintf(emitter->output, "    goto b%" PRIu32 ";\n",
            function->entry_block);
    for (size_t b = 0U; b < function->block_count; ++b) {
        /* A C label must precede a statement, not a declaration. The empty
         * statement keeps blocks valid when their first IR instruction emits
         * a small temporary local. */
        fprintf(emitter->output, "b%zu: ;\n", b);
        const IrBlock *block = &function->blocks[b];
        for (size_t i = 0U; i < block->instruction_count; ++i) {
            emit_virtual_result_begin(
                emitter, function, &block->instructions[i]);
            c_backend_emit_instruction(emitter, function,
                             &block->instructions[i]);
            emit_virtual_ownership_update(
                emitter, function, &block->instructions[i]);
            if (emitter->failed) {
                free(direct_local_tags);
                free(direct_local_tag_lengths);
                emitter->render_into = previous_render_into;
                emitter->render_direct = previous_render_direct;
                emitter->direct_local_tags = previous_direct_local_tags;
                emitter->direct_local_tag_lengths =
                    previous_direct_local_tag_lengths;
                emitter->render_root_span =
                    previous_render_root_span;
                return;
            }
        }
        if (!render_direct &&
            (block->terminator.kind == IR_TERM_RETURN ||
             block->terminator.kind == IR_TERM_PROPAGATE_EXCEPTION))
            c_backend_emit_virtual_cleanup(
                emitter, function,
                block->terminator.kind == IR_TERM_RETURN
                    ? block->terminator.value : IR_INVALID_ID,
                "    ", false);
        c_backend_emit_terminator(emitter, function, &block->terminator);
        if (emitter->failed) {
            free(direct_local_tags);
            free(direct_local_tag_lengths);
            emitter->render_into = previous_render_into;
            emitter->render_direct = previous_render_direct;
            emitter->direct_local_tags = previous_direct_local_tags;
            emitter->direct_local_tag_lengths =
                previous_direct_local_tag_lengths;
            emitter->render_root_span =
                previous_render_root_span;
            return;
        }
    }
    for (size_t l = 0U; l < function->parameter_count; ++l)
        if (parameter_mode_is_reference(function->parameters[l].mode))
            fprintf(emitter->output, "#undef l%zu\n", l);
    fputs("}\n\n", emitter->output);
    free(direct_local_tags);
    free(direct_local_tag_lengths);
    emitter->render_into = previous_render_into;
    emitter->render_direct = previous_render_direct;
    emitter->direct_local_tags = previous_direct_local_tags;
    emitter->direct_local_tag_lengths =
        previous_direct_local_tag_lengths;
    emitter->render_root_span =
        previous_render_root_span;
}

static void emit_function(CEmitter *emitter, size_t index) {
    if (emitter->ir->functions[index].is_async)
        c_backend_emit_async_function(emitter, index);
    else
        emit_function_variant(emitter, index, false, false);
}

static void emit_direct_render_wrapper(
    CEmitter *emitter, size_t index) {
    const IrFunction *function = &emitter->ir->functions[index];
    size_t initial_capacity = c_backend_direct_render_initial_capacity(
        emitter->ir, index);
    emit_direct_render_signature(emitter, index, false);
    fprintf(emitter->output,
            "    aster_string_builder *render_builder = "
            "aster_builder_with_capacity(%zuU);\n",
            initial_capacity);
    fprintf(emitter->output, "    aster_fn_%zu_append(render_builder", index);
    for (size_t p = 0U; p < function->parameter_count; ++p)
        fprintf(emitter->output, ", p%zu", p);
    fputs(");\n    return aster_builder_finish(render_builder);\n"
          "}\n\n", emitter->output);
}

static void emit_drop_helper_prototypes(CEmitter *emitter) {
    for (size_t type = 0U;
         type < emitter->ir->type_count; ++type) {
        if (!emitter->used_types[type] ||
            !c_backend_type_needs_drop(
                emitter, (IrTypeId)type))
            continue;
        fputs("void aster_drop_", emitter->output);
        fprintf(emitter->output, "%zu(", type);
        c_backend_emit_type(emitter, (IrTypeId)type);
        fputs(" *value);\n", emitter->output);
    }
}

static void emit_clone_helper_prototypes(CEmitter *emitter) {
    for (size_t type = 0U;
         type < emitter->ir->type_count; ++type) {
        if (!emitter->used_types[type] ||
            (!emitter->ir->types[type].requires_cleanup &&
             !emitter->ir->types[type].managed) ||
            !c_backend_type_clone_supported(
                emitter->ir, (IrTypeId)type))
            continue;
        c_backend_emit_type(emitter, (IrTypeId)type);
        fprintf(emitter->output,
                " aster_clone_%zu(", type);
        c_backend_emit_type(emitter, (IrTypeId)type);
        fputs(" value);\n", emitter->output);
    }
}

static void emit_clone_helper(
    CEmitter *emitter, IrTypeId type_id
) {
    const IrType *type = &emitter->ir->types[type_id];
    c_backend_emit_type(emitter, type_id);
    fprintf(emitter->output,
            " aster_clone_%" PRIu32 "(", type_id);
    c_backend_emit_type(emitter, type_id);
    fputs(" value) {\n", emitter->output);
    if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
        strcmp(type->name, "string") == 0) {
        fputs("    return aster_string_clone(value);\n",
              emitter->output);
    } else if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
               strcmp(type->name, "StringBuilder") == 0) {
        fputs("    return aster_builder_clone(value);\n",
              emitter->output);
    } else if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
               strcmp(type->name, "Html") == 0) {
        fputs("    return aster_html_clone(value);\n",
              emitter->output);
    } else if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
               strcmp(type->name, "Url") == 0) {
        fputs("    return (aster_url *)aster_string_clone("
              "(const aster_string *)value);\n",
              emitter->output);
    } else if (c_backend_type_is_native_handle(type)) {
        fputs(
            "    if (value == NULL) return NULL;\n"
            "    if (value->references == SIZE_MAX)\n"
            "        aster_trap(\"native handle reference count overflow\");\n"
            "    ++value->references;\n"
            "    return value;\n",
            emitter->output);
    } else if (c_backend_type_is_buffer(type)) {
        fputs(
            "    if (value == NULL) return NULL;\n"
            "    aster_buffer *result = aster_allocate(sizeof(*result));\n"
            "    result->length = value->length;\n"
            "    result->data = value->length == 0U\n"
            "        ? NULL : aster_allocate(value->length);\n"
            "    if (value->length != 0U)\n"
            "        memcpy(result->data, value->data, value->length);\n"
            "    return result;\n",
            emitter->output);
    } else if (c_backend_type_is_task(type)) {
        fputs(
            "    return aster_task_retain(value);\n",
            emitter->output);
    } else if (c_backend_type_is_cancellation(type)) {
        fputs(
            "    return aster_cancellation_retain(value);\n",
            emitter->output);
    } else if (c_backend_type_is_vec(type)) {
        fputs("    if (value == NULL) return NULL;\n    ",
              emitter->output);
        c_backend_emit_type(emitter, type_id);
        fprintf(emitter->output,
                " result = aster_allocate(sizeof(*result));\n"
                "    *result = (aster_vec_%" PRIu32 "){0};\n",
                type_id);
        fprintf(emitter->output,
                "    result->length = value->length;\n"
                "    result->capacity = value->length;\n"
                "    if (value->length != 0U) {\n"
                "        result->data = aster_allocate("
                "value->length * sizeof(*result->data));\n"
                "        for (size_t i = 0U; i < value->length; ++i)\n");
        if (c_backend_type_needs_drop(emitter, type->element_type))
            fprintf(emitter->output,
                    "            result->data[i] = aster_clone_%" PRIu32
                    "(value->data[i]);\n",
                    type->element_type);
        else
            fputs("            result->data[i] = value->data[i];\n",
                  emitter->output);
        fputs("    }\n    return result;\n", emitter->output);
    } else if (c_backend_type_is_queue(type)) {
        fputs("    if (value == NULL) return NULL;\n    ",
              emitter->output);
        c_backend_emit_type(emitter, type_id);
        fprintf(emitter->output,
                " result = aster_allocate(sizeof(*result));\n"
                "    *result = (aster_queue_%" PRIu32 "){0};\n"
                "    result->length = value->length;\n"
                "    result->capacity = value->length;\n"
                "    if (value->length != 0U) {\n"
                "        result->data = aster_allocate("
                "value->length * sizeof(*result->data));\n"
                "        for (size_t i = 0U; i < value->length; ++i)\n",
                type_id);
        if (c_backend_type_needs_drop(emitter, type->element_type))
            fprintf(emitter->output,
                    "            result->data[i] = aster_clone_%" PRIu32
                    "(value->data[(value->head + i) %% value->capacity]);\n",
                    type->element_type);
        else
            fputs("            result->data[i] = "
                  "value->data[(value->head + i) % value->capacity];\n",
                  emitter->output);
        fputs("    }\n    return result;\n", emitter->output);
    } else if (c_backend_type_is_dictionary(type)) {
        fputs("    if (value == NULL) return NULL;\n    ",
              emitter->output);
        c_backend_emit_type(emitter, type_id);
        fprintf(emitter->output,
                " result = aster_allocate(sizeof(*result));\n"
                "    *result = (aster_dictionary_%" PRIu32 "){0};\n"
                "    result->length = value->length;\n"
                "    result->capacity = value->length;\n"
                "    if (value->length != 0U) {\n"
                "        result->keys = aster_allocate("
                "value->length * sizeof(*result->keys));\n"
                "        result->values = aster_allocate("
                "value->length * sizeof(*result->values));\n"
                "        result->hashes = aster_allocate("
                "value->length * sizeof(*result->hashes));\n"
                "        memcpy(result->hashes, value->hashes, "
                "value->length * sizeof(*result->hashes));\n"
                "        for (size_t i = 0U; i < value->length; ++i) {\n",
                type_id);
        if (c_backend_type_needs_drop(emitter, type->element_type))
            fprintf(emitter->output,
                    "            result->keys[i] = aster_clone_%" PRIu32
                    "(value->keys[i]);\n",
                    type->element_type);
        else
            fputs("            result->keys[i] = value->keys[i];\n",
                  emitter->output);
        if (c_backend_type_needs_drop(emitter, type->error_type))
            fprintf(emitter->output,
                    "            result->values[i] = aster_clone_%" PRIu32
                    "(value->values[i]);\n",
                    type->error_type);
        else
            fputs("            result->values[i] = value->values[i];\n",
                  emitter->output);
        fprintf(emitter->output,
                "        }\n    }\n"
                "    aster_dictionary_rebuild_%" PRIu32 "(result);\n"
                "    return result;\n",
                type_id);
    } else if (type->shape == IR_TYPE_STRUCT) {
        fputs("    ", emitter->output);
        c_backend_emit_type(emitter, type_id);
        fputs(" result = value;\n", emitter->output);
        for (size_t field = 0U; field < type->field_count; ++field) {
            IrTypeId field_type = type->field_types[field];
            if (!c_backend_type_needs_drop(emitter, field_type))
                continue;
            fprintf(emitter->output,
                    "    result.f%zu = aster_clone_%" PRIu32
                    "(value.f%zu);\n",
                    field, field_type, field);
        }
        fputs("    return result;\n", emitter->output);
    } else if (type->shape == IR_TYPE_ARRAY) {
        fputs("    ", emitter->output);
        c_backend_emit_type(emitter, type_id);
        fputs(" result = value;\n", emitter->output);
        if (c_backend_type_needs_drop(emitter, type->element_type))
            fprintf(emitter->output,
                    "    for (size_t i = 0U; i < %zuU; ++i)\n"
                    "        result.items[i] = aster_clone_%" PRIu32
                    "(value.items[i]);\n",
                    type->array_length, type->element_type);
        fputs("    return result;\n", emitter->output);
    } else if (type->shape == IR_TYPE_UNION) {
        fputs("    ", emitter->output);
        c_backend_emit_type(emitter, type_id);
        fputs(" result = value;\n    switch (value.tag) {\n",
              emitter->output);
        for (size_t variant = 0U;
             variant < type->variant_count; ++variant) {
            IrTypeId payload =
                type->variant_payload_types[variant];
            if (payload == IR_INVALID_ID ||
                !c_backend_type_needs_drop(emitter, payload))
                continue;
            fprintf(emitter->output,
                    "        case UINT32_C(%zu):\n"
                    "            result.payload.v%zu = "
                    "aster_clone_%" PRIu32
                    "(value.payload.v%zu);\n"
                    "            break;\n",
                    variant, variant, payload, variant);
        }
        fputs("        default: break;\n"
              "    }\n    return result;\n",
              emitter->output);
    } else {
        fputs("    return value;\n", emitter->output);
    }
    fputs("}\n\n", emitter->output);
}

static void emit_clone_helpers(CEmitter *emitter) {
    for (size_t type = 0U;
         type < emitter->ir->type_count; ++type)
        if (emitter->used_types[type] &&
            (emitter->ir->types[type].requires_cleanup ||
             emitter->ir->types[type].managed) &&
            c_backend_type_clone_supported(
                emitter->ir, (IrTypeId)type))
            emit_clone_helper(emitter, (IrTypeId)type);
}

static void emit_drop_helper(
    CEmitter *emitter, IrTypeId type_id) {
    const IrType *type = &emitter->ir->types[type_id];
    fprintf(emitter->output,
            "void aster_drop_%" PRIu32 "(",
            type_id);
    c_backend_emit_type(emitter, type_id);
    fputs(" *value) {\n", emitter->output);
    if (c_backend_type_is_vec(type)) {
        fputs("    if (*value != NULL) {\n", emitter->output);
        if (c_backend_type_needs_drop(emitter, type->element_type))
            fprintf(
                emitter->output,
                "        for (size_t i = (*value)->length; i > 0U; --i)\n"
                "            aster_drop_%" PRIu32
                "(&(*value)->data[i - 1U]);\n",
                type->element_type);
        fputs(
            "        free((*value)->data);\n"
            "        free(*value);\n"
            "        *value = NULL;\n"
            "    }\n",
            emitter->output);
    } else if (c_backend_type_is_dictionary(type)) {
        fputs("    if (*value != NULL) {\n", emitter->output);
        if (c_backend_type_needs_drop(emitter, type->element_type))
            fprintf(emitter->output,
                    "        for (size_t i = (*value)->length; i > 0U; --i)\n"
                    "            aster_drop_%" PRIu32
                    "(&(*value)->keys[i - 1U]);\n",
                    type->element_type);
        if (c_backend_type_needs_drop(emitter, type->error_type))
            fprintf(emitter->output,
                    "        for (size_t i = (*value)->length; i > 0U; --i)\n"
                    "            aster_drop_%" PRIu32
                    "(&(*value)->values[i - 1U]);\n",
                    type->error_type);
        fputs("        free((*value)->keys);\n"
              "        free((*value)->values);\n"
              "        free((*value)->hashes);\n"
              "        free((*value)->buckets);\n"
              "        free(*value);\n"
              "        *value = NULL;\n"
              "    }\n",
              emitter->output);
    } else if (c_backend_type_is_queue(type)) {
        fputs("    if (*value != NULL) {\n", emitter->output);
        if (c_backend_type_needs_drop(emitter, type->element_type))
            fprintf(emitter->output,
                    "        for (size_t i = (*value)->length; i > 0U; --i)\n"
                    "            aster_drop_%" PRIu32
                    "(&(*value)->data[((*value)->head + i - 1U) %% "
                    "(*value)->capacity]);\n",
                    type->element_type);
        fputs("        free((*value)->data);\n"
              "        free(*value);\n"
              "        *value = NULL;\n"
              "    }\n",
              emitter->output);
    } else if (type->shape == IR_TYPE_ITERATOR) {
        const IrType *source =
            &emitter->ir->types[type->argument_types[0]];
        if (c_backend_type_is_vec(source)) {
            if (c_backend_type_needs_drop(emitter, type->element_type))
                fprintf(
                    emitter->output,
                    "    if (value->vector != NULL && !value->borrowed)\n"
                    "        for (size_t i = value->vector->length;\n"
                    "             i > value->index; --i)\n"
                    "            aster_drop_%" PRIu32
                    "(&value->vector->data[i - 1U]);\n",
                    type->element_type);
            fputs(
                "    if (value->vector != NULL && !value->borrowed) {\n"
                "        free(value->vector->data);\n"
                "        free(value->vector);\n"
                "    }\n"
                "    value->vector = NULL;\n",
                emitter->output);
        } else if (c_backend_type_is_queue(source)) {
            if (c_backend_type_needs_drop(emitter, type->element_type))
                fprintf(
                    emitter->output,
                    "    if (value->queue != NULL && !value->borrowed)\n"
                    "        for (size_t i = value->queue->length;\n"
                    "             i > value->index; --i)\n"
                    "            aster_drop_%" PRIu32
                    "(&value->queue->data[(value->queue->head + i - 1U) %% "
                    "value->queue->capacity]);\n",
                    type->element_type);
            fputs(
                "    if (value->queue != NULL && !value->borrowed) {\n"
                "        free(value->queue->data);\n"
                "        free(value->queue);\n"
                "    }\n"
                "    value->queue = NULL;\n",
                emitter->output);
        } else if (source->shape == IR_TYPE_ARRAY) {
            if (c_backend_type_needs_drop(emitter, type->element_type))
                fprintf(
                    emitter->output,
                    "    if (!value->borrowed)\n"
                    "        for (size_t i = %zuU; i > value->index; --i)\n"
                    "            aster_drop_%" PRIu32
                    "(&value->owned_array.items[i - 1U]);\n",
                    source->array_length, type->element_type);
            fputs("    value->borrowed_array = NULL;\n",
                  emitter->output);
        } else if (source->shape == IR_TYPE_BUILTIN_OBJECT &&
                   strcmp(source->name, "string") == 0) {
            fputs("    if (value->slice != NULL && !value->borrowed)\n"
                  "        aster_string_drop(value->slice);\n"
                  "    value->slice = NULL;\n",
                  emitter->output);
        } else {
            fputs("    value->slice.data = NULL;\n"
                  "    value->slice.length = 0U;\n",
                  emitter->output);
        }
    } else if (c_backend_type_is_native_handle(type)) {
        fputs(
            "    aster_native_handle_drop(*value);\n"
            "    *value = NULL;\n",
            emitter->output);
    } else if (c_backend_type_is_buffer(type)) {
        fputs(
            "    if (*value != NULL) {\n"
            "        free((*value)->data);\n"
            "        free(*value);\n"
            "        *value = NULL;\n"
            "    }\n",
            emitter->output);
    } else if (c_backend_type_is_arena(type)) {
        fputs(
            "    aster_arena_drop(*value);\n"
            "    *value = NULL;\n",
            emitter->output);
    } else if (c_backend_type_is_task(type)) {
        fputs(
            "    aster_task_drop(*value);\n"
            "    *value = NULL;\n",
            emitter->output);
    } else if (c_backend_type_is_cancellation(type)) {
        fputs(
            "    aster_cancellation_drop(*value);\n"
            "    *value = NULL;\n",
            emitter->output);
    } else if (type->shape == IR_TYPE_ELEMENT_BUILDER) {
        fputs(
            "    aster_html_drop(*value);\n"
            "    *value = NULL;\n",
            emitter->output);
    } else if (type->shape == IR_TYPE_BUILTIN_OBJECT) {
        const char *drop =
            strcmp(type->name, "StringBuilder") == 0
                ? "aster_builder_drop"
            : strcmp(type->name, "Html") == 0
                ? "aster_html_drop"
                : "aster_string_drop";
        fprintf(
            emitter->output,
            "    %s(*value);\n"
            "    *value = NULL;\n",
            drop);
    }
    if (type->destructor_function != IR_INVALID_ID)
        fprintf(
            emitter->output,
            "    (void)aster_fn_%" PRIu32 "(*value);\n",
            type->destructor_function);
    if (type->shape == IR_TYPE_STRUCT) {
        for (size_t field = type->field_count;
             field > 0U; --field) {
            IrTypeId field_type =
                type->field_types[field - 1U];
            if (!c_backend_type_needs_drop(
                    emitter, field_type))
                continue;
            fprintf(
                emitter->output,
                "    aster_drop_%" PRIu32
                "(&value->f%zu);\n",
                field_type, field - 1U);
        }
    } else if (type->shape == IR_TYPE_ARRAY) {
        if (c_backend_type_needs_drop(
                emitter, type->element_type))
            fprintf(
                emitter->output,
                "    for (size_t i = %zu; i > 0; --i)\n"
                "        aster_drop_%" PRIu32
                "(&value->items[i - 1]);\n",
                type->array_length, type->element_type);
    } else if (type->shape == IR_TYPE_UNION) {
        fputs("    switch (value->tag) {\n",
              emitter->output);
        for (size_t variant = 0U;
             variant < type->variant_count; ++variant) {
            IrTypeId payload =
                type->variant_payload_types[variant];
            if (!c_backend_type_needs_drop(emitter, payload))
                continue;
            fprintf(
                emitter->output,
                "        case UINT32_C(%zu):\n"
                "            aster_drop_%" PRIu32
                "(&value->payload.v%zu);\n"
                "            break;\n",
                variant, payload, variant);
        }
        fputs("        default: break;\n"
              "    }\n", emitter->output);
    }
    fputs("}\n\n", emitter->output);
}

static void emit_drop_helpers(CEmitter *emitter) {
    for (size_t type = 0U;
         type < emitter->ir->type_count; ++type)
        if (emitter->used_types[type] &&
            c_backend_type_needs_drop(
                emitter, (IrTypeId)type))
            emit_drop_helper(
                emitter, (IrTypeId)type);
}

static bool c_emit_module(const IrModule *ir,
                          LangDiagnostics *diagnostics,
                          FILE *output,
                          const char *css_directory) {
    if (ir == NULL || diagnostics == NULL || output == NULL)
        return false;
    CEmitter emitter = {
        .ir=ir,
        .diagnostics=diagnostics,
        .output=output
    };
    emitter.reachable_functions =
        calloc(ir->function_count, sizeof(*emitter.reachable_functions));
    emitter.used_types =
        calloc(ir->type_count, sizeof(*emitter.used_types));
    if ((ir->function_count != 0U &&
         emitter.reachable_functions == NULL) ||
        (ir->type_count != 0U && emitter.used_types == NULL)) {
        free(emitter.reachable_functions);
        free(emitter.used_types);
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    size_t entry = ir->function_count;
    for (size_t f = 0U; f < ir->function_count; ++f)
        if (ir->functions[f].is_entry) entry = f;
    if (entry == ir->function_count) {
        lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                  "C backend requires an entry function");
        free(emitter.reachable_functions);
        free(emitter.used_types);
        return false;
    }
    if (ir->functions[entry].parameter_count != 0U) {
        lang_diag(diagnostics, ir->functions[entry].span,
                  "C backend entry function cannot have parameters");
        free(emitter.reachable_functions);
        free(emitter.used_types);
        return false;
    }
    c_backend_mark_function(&emitter, (IrFunctionId)entry);
    for (size_t field = 0U; field < ir->static_field_count; ++field)
        if (ir->static_fields[field].type < ir->type_count)
            emitter.used_types[ir->static_fields[field].type] = true;
    for (size_t function = 0U;
         function < ir->function_count; ++function)
        if (c_backend_function_is_entry_module_export(
                ir, function, entry))
            c_backend_mark_function(
                &emitter, (IrFunctionId)function);
    bool needs_async = false;
    for (size_t f = 0U; f < ir->function_count; ++f)
        if (emitter.reachable_functions[f] &&
            ir->functions[f].is_async) {
            needs_async = true;
            break;
        }
    if (!needs_async)
        for (size_t t = 0U; t < ir->type_count; ++t)
            if (emitter.used_types[t] &&
                c_backend_type_is_task(&ir->types[t])) {
                needs_async = true;
                break;
            }
    for (size_t f = 0U; f < ir->function_count; ++f)
        if (emitter.reachable_functions[f] &&
            !function_supported(&emitter, &ir->functions[f])) {
            free(emitter.reachable_functions);
            free(emitter.used_types);
            return false;
        }
    if (css_directory != NULL) {
        emitter.css_asset_href = c_backend_emit_static_css_asset(
            &emitter, css_directory);
        if (emitter.failed) {
            free(emitter.css_asset_href);
            free(emitter.reachable_functions);
            free(emitter.used_types);
            return false;
        }
    }
    c_backend_emit_prelude(output, emitter.needs_native_runtime);
    bool needs_utc_clock = false;
    for (size_t f = 0U; f < ir->function_count; ++f)
        if (emitter.reachable_functions[f] &&
            function_uses_utc_clock(&ir->functions[f])) {
            needs_utc_clock = true;
            break;
        }
    if (needs_utc_clock)
        fputs(
            "#include <time.h>\n\n"
            "static int64_t aster_utc_now_unix_milliseconds(void) {\n"
            "    struct timespec value;\n"
            "    if (timespec_get(&value, TIME_UTC) != TIME_UTC) {\n"
            "        fputs(\"could not read the UTC system clock\\n\", stderr);\n"
            "        exit(1);\n"
            "    }\n"
            "    return (int64_t)value.tv_sec * INT64_C(1000) +\n"
            "           (int64_t)value.tv_nsec / INT64_C(1000000);\n"
            "}\n\n",
            output);
    c_backend_emit_html_prelude(output, emitter.css_asset_href);
    if (!c_backend_emit_aggregate_types(&emitter)) {
        lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                  "C backend cannot order recursive inline aggregate types");
        free(emitter.reachable_functions);
        free(emitter.used_types);
        free(emitter.css_asset_href);
        return false;
    }
    for (size_t field = 0U; field < ir->static_field_count; ++field) {
        fputs("static ", output);
        c_backend_emit_type(&emitter, ir->static_fields[field].type);
        const IrType *type =
            &ir->types[ir->static_fields[field].type];
        fprintf(output, " aster_static_%zu = ", field);
        if (type->shape == IR_TYPE_FLOAT)
            fprintf(output, "%.17g", ir->static_fields[field].initial_floating);
        else if (type->shape == IR_TYPE_CLASS_REFERENCE ||
                 type->shape == IR_TYPE_RAW_POINTER)
            fputs("NULL", output);
        else if (type->shape == IR_TYPE_UNSIGNED_INT ||
                 type->shape == IR_TYPE_CHAR)
            fprintf(output, "UINT64_C(%" PRIu64 ")",
                    ir->static_fields[field].initial_integer);
        else
            fprintf(output, "INT64_C(%" PRId64 ")",
                    (int64_t)ir->static_fields[field].initial_integer);
        fputs(";\n", output);
    }
    if (ir->static_field_count != 0U) fputc('\n', output);
    fputs("static bool aster_exception_pending = false;\n"
          "static aster_string *aster_exception_message = NULL;\n"
          "const char *aster_exception_type = NULL;\n\n",
          output);
    if (needs_async)
        c_backend_emit_async_runtime(output);
    for (size_t f = 0U; f < ir->function_count; ++f)
        if (emitter.reachable_functions[f] &&
            ir->functions[f].is_async)
            c_backend_emit_async_frame_declaration(&emitter, f);
    for (size_t f = 0U; f < ir->function_count; ++f)
        if (emitter.reachable_functions[f]) {
            if (c_backend_function_needs_normal_variant(&emitter, f))
                emit_function_signature(&emitter, f, true);
            if (ir->functions[f].is_async)
                c_backend_emit_async_step_prototype(&emitter, f);
            if (c_backend_function_needs_render_into_variant(&emitter, f)) {
                emit_render_function_signature(
                    &emitter, f, true);
            }
            if (c_backend_function_supports_direct_render(ir, f)) {
                emit_direct_append_signature(
                    &emitter, f, true);
                emit_direct_render_signature(
                    &emitter, f, true);
            }
        }
    emit_delegate_adapters(&emitter);
    emit_clone_helper_prototypes(&emitter);
    emit_drop_helper_prototypes(&emitter);
    fputc('\n', output);
    if (needs_async)
        c_backend_emit_async_result_helpers(&emitter);
    if (needs_async)
        c_backend_emit_async_combinator_helpers(&emitter);
    for (size_t f = 0U; f < ir->function_count; ++f) {
        if (!emitter.reachable_functions[f]) continue;
        if (c_backend_function_needs_normal_variant(&emitter, f))
            emit_function(&emitter, f);
        if (!emitter.failed &&
            c_backend_function_needs_render_into_variant(&emitter, f))
            emit_function_variant(
                &emitter, f, true, false);
        if (!emitter.failed &&
            c_backend_function_supports_direct_render(ir, f)) {
            emit_function_variant(
                &emitter, f, false, true);
            if (!emitter.failed)
                emit_direct_render_wrapper(&emitter, f);
        }
        if (emitter.failed) {
            free(emitter.reachable_functions);
            free(emitter.used_types);
            free(emitter.css_asset_href);
            return false;
        }
    }
    emit_clone_helpers(&emitter);
    emit_drop_helpers(&emitter);
    for (size_t function = 0U;
         function < ir->function_count; ++function)
        if (emitter.reachable_functions[function] &&
            c_backend_function_is_entry_module_export(
                ir, function, entry))
        {
            c_backend_emit_public_export_wrapper(&emitter, function);
            c_backend_emit_public_aggregate_accessors(&emitter, function);
        }
    if (c_backend_web_exports_use_strings(ir, entry))
        c_backend_emit_web_string_abi(output);
    if (c_backend_web_exports_use_html_result(ir, entry))
        c_backend_emit_web_html_abi(output);
    if (ir->functions[entry].is_async) {
        if (emitter.needs_native_runtime)
            fputs("int main(void) {\n"
                  "    aster_vm = lang_vm_new();\n"
                  "    if (aster_vm == NULL) return 2;\n",
                  output);
        else
            fputs("int main(void) {\n", output);
        if (emitter.needs_http_client_runtime)
            fputs("    lang_configure_http_client_registrar("
                  "lang_register_http_client_natives);\n", output);
        if (emitter.needs_crypto_runtime)
            fputs("    lang_configure_crypto_registrar("
                  "lang_register_crypto_natives);\n", output);
        if (emitter.needs_native_runtime)
            fputs("    lang_vm_register_builtins(aster_vm);\n", output);
        fprintf(output,
                "    aster_task *entry_task = aster_fn_%zu();\n"
                "    aster_task_run_until(entry_task);\n"
                "    int status = 1;\n"
                "    if (entry_task->state == ASTER_TASK_SUCCEEDED) {\n"
                "        if (entry_task->result_size != sizeof(int64_t))\n"
                "            aster_trap(\"async main must complete with int\");\n"
                "        status = *(int64_t *)entry_task->result == 0 ? 0 : 1;\n"
                "    } else if (entry_task->state == ASTER_TASK_FAULTED ||\n"
                "               entry_task->state == ASTER_TASK_CANCELED) {\n"
                "        aster_task_restore_fault(entry_task);\n"
                "    }\n"
                "    aster_task_drop(entry_task);\n"
                "    if (aster_exception_pending) {\n"
                "        fputs(\"unhandled Aster Exception: \", stderr);\n"
                "        if (aster_exception_message != NULL &&\n"
                "            aster_exception_message->length != 0U)\n"
                "            (void)fwrite(aster_exception_message->data, 1U, "
                "aster_exception_message->length, stderr);\n"
                "        fputc('\\n', stderr);\n"
                "        aster_string_drop(aster_exception_message);\n"
                "        aster_exception_message = NULL;\n"
                "        status = 1;\n"
                "    }\n",
                entry);
        if (emitter.needs_native_runtime)
            fputs("    lang_vm_free(aster_vm);\n"
                  "    aster_vm = NULL;\n", output);
        fputs("    return status;\n}\n", output);
    } else if (emitter.needs_native_runtime) {
        fputs(
                "int main(void) {\n"
                "    aster_vm = lang_vm_new();\n"
                "    if (aster_vm == NULL) return 2;\n",
                output);
        if (emitter.needs_http_client_runtime)
            fputs("    lang_configure_http_client_registrar("
                  "lang_register_http_client_natives);\n", output);
        if (emitter.needs_crypto_runtime)
            fputs("    lang_configure_crypto_registrar("
                  "lang_register_crypto_natives);\n", output);
        fprintf(output,
                "    lang_vm_register_builtins(aster_vm);\n"
                "    int status = aster_fn_%zu() == 0 ? 0 : 1;\n"
                "    if (aster_exception_pending) {\n"
                "        fputs(\"unhandled Aster Exception: \", stderr);\n"
                "        if (aster_exception_message != NULL &&\n"
                "            aster_exception_message->length != 0U)\n"
                "            (void)fwrite(aster_exception_message->data, 1U, "
                "aster_exception_message->length, stderr);\n"
                "        fputc('\\n', stderr);\n"
                "        aster_string_drop(aster_exception_message);\n"
                "        aster_exception_message = NULL;\n"
                "        status = 1;\n"
                "    }\n"
                "    lang_vm_free(aster_vm);\n"
                "    aster_vm = NULL;\n"
                "    return status;\n"
                "}\n",
                entry);
    } else
        fprintf(output,
                "int main(void) {\n"
                "    int status = aster_fn_%zu() == 0 ? 0 : 1;\n"
                "    if (aster_exception_pending) {\n"
                "        fputs(\"unhandled Aster Exception: \", stderr);\n"
                "        if (aster_exception_message != NULL &&\n"
                "            aster_exception_message->length != 0U)\n"
                "            (void)fwrite(aster_exception_message->data, 1U, "
                "aster_exception_message->length, stderr);\n"
                "        fputc('\\n', stderr);\n"
                "        aster_string_drop(aster_exception_message);\n"
                "        status = 1;\n"
                "    }\n"
                "    return status;\n"
                "}\n",
                entry);
    bool ok = !emitter.failed && ferror(output) == 0;
    free(emitter.reachable_functions);
    free(emitter.used_types);
    free(emitter.css_asset_href);
    return ok;
}

bool lang_c_emit_module(const IrModule *ir,
                        LangDiagnostics *diagnostics,
                        FILE *output) {
    return c_emit_module(ir, diagnostics, output, NULL);
}

bool lang_c_emit_site(const IrModule *ir,
                      LangDiagnostics *diagnostics,
                      FILE *output,
                      const char *css_directory) {
    if (css_directory == NULL) return false;
    return c_emit_module(ir, diagnostics, output, css_directory);
}
