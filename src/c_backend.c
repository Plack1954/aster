#include "internal.h"
#include "c_backend_internal.h"

#include <inttypes.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void c_backend_unsupported(CEmitter *emitter, LangSpan span,
                           const char *what) {
    if (!emitter->failed)
        lang_diag(emitter->diagnostics, span,
                  "C backend does not yet support %s", what);
    emitter->failed = true;
}
static void emit_value_name(FILE *output, IrValueId value) {
    fprintf(output, "v%" PRIu32, value);
}

static bool emit_borrowed_alias_address(
    CEmitter *emitter, const IrFunction *function, IrValueId value
) {
    const IrInstruction *producer =
        c_backend_find_value_producer(function, value);
    if (producer == NULL) return false;
    FILE *output = emitter->output;
    switch (producer->opcode) {
        case IR_OP_LOCAL_LOAD:
            fprintf(output, "&l%" PRIu32, producer->index);
            return true;
        case IR_OP_LOCAL_FIELD_BORROW: {
            const IrType *owner = &emitter->ir->types[
                function->locals[producer->index].type];
            fprintf(output, "&l%" PRIu32 "%sf%" PRIu32,
                    producer->index,
                    owner->shape == IR_TYPE_CLASS_REFERENCE ? "->" : ".",
                    producer->auxiliary);
            return true;
        }
        case IR_OP_FIELD_GET: {
            if (producer->auxiliary != 1U &&
                producer->auxiliary != 2U)
                return false;
            const IrType *owner = &emitter->ir->types[
                function->value_types[producer->operands[0]]];
            fprintf(output, "&v%" PRIu32 "%sf%" PRIu32,
                    producer->operands[0],
                    owner->shape == IR_TYPE_CLASS_REFERENCE ? "->" : ".",
                    producer->index);
            return true;
        }
        case IR_OP_INDEX_GET:
            if (producer->integer == 0U) return false;
            fprintf(output,
                    "&v%" PRIu32 ".items[(size_t)v%" PRIu32 "]",
                    producer->operands[0], producer->operands[1]);
            return true;
        case IR_OP_ENUM_PAYLOAD_BORROW: {
            const IrInstruction *owner = c_backend_find_value_producer(
                function, producer->operands[0]);
            if (owner != NULL && owner->opcode == IR_OP_LOCAL_LOAD)
                fprintf(output,
                        "&l%" PRIu32 ".payload.v%" PRIu32,
                        owner->index, producer->auxiliary);
            else
                fprintf(output,
                        "&v%" PRIu32 ".payload.v%" PRIu32,
                        producer->operands[0], producer->auxiliary);
            return true;
        }
        case IR_OP_LIST_ELEMENT_BORROW:
            fprintf(output,
                    "&v%" PRIu32 "->data[(size_t)v%" PRIu32 "]",
                    producer->operands[0], producer->operands[1]);
            return true;
        case IR_OP_QUEUE_FRONT_BORROW:
            fprintf(output, "&v%" PRIu32 "->data[v%" PRIu32 "->head]",
                    producer->operands[0], producer->operands[0]);
            return true;
        case IR_OP_STACK_TOP_BORROW:
            fprintf(output,
                    "&v%" PRIu32 "->data[v%" PRIu32 "->length - 1U]",
                    producer->operands[0], producer->operands[0]);
            return true;
        case IR_OP_DICTIONARY_GET_BORROW:
            fprintf(output,
                    "&v%" PRIu32
                    "->values[dictionary_borrow_match_%" PRIu32 "]",
                    producer->operands[0], producer->result);
            return true;
        case IR_OP_DICTIONARY_KEY_BORROW:
        case IR_OP_DICTIONARY_VALUE_BORROW:
            fprintf(output, "&v%" PRIu32 "->%s[v%" PRIu32 "]",
                    producer->operands[0],
                    producer->opcode == IR_OP_DICTIONARY_KEY_BORROW
                        ? "keys" : "values",
                    producer->operands[1]);
            return true;
        case IR_OP_LOCAL_ITERATOR_NEXT: {
            IrTypeId iterator_id =
                function->locals[producer->index].type;
            const IrType *iterator =
                &emitter->ir->types[iterator_id];
            const IrType *source = &emitter->ir->types[
                iterator->argument_types[0]];
            if (c_backend_type_is_vec(source))
                fprintf(output,
                        "&l%" PRIu32
                        ".vector->data[l%" PRIu32 ".index - 1U]",
                        producer->index, producer->index);
            else if (c_backend_type_is_queue(source))
                fprintf(output,
                        "&l%" PRIu32 ".queue->data[(l%" PRIu32
                        ".queue->head + l%" PRIu32
                        ".index - 1U) %% l%" PRIu32
                        ".queue->capacity]",
                        producer->index, producer->index,
                        producer->index, producer->index);
            else if (source->shape == IR_TYPE_ARRAY)
                fprintf(output,
                        "&(l%" PRIu32 ".borrowed ? l%" PRIu32
                        ".borrowed_array : &l%" PRIu32
                        ".owned_array)->items[l%" PRIu32
                        ".index - 1U]",
                        producer->index, producer->index,
                        producer->index, producer->index);
            else
                return false;
            return true;
        }
        default:
            return false;
    }
}

void c_backend_emit_byte_string(FILE *output,
                             const char *data, size_t length) {
    fputs("(const unsigned char *)\"", output);
    for (size_t i = 0U; i < length; ++i)
        fprintf(output, "\\x%02x",
                (unsigned)(unsigned char)data[i]);
    fputc('"', output);
}

void emit_operands(CEmitter *emitter,
                   const IrInstruction *instruction) {
    for (size_t i = 0U; i < instruction->operand_count; ++i) {
        if (i != 0U) fputs(", ", emitter->output);
        emit_value_name(emitter->output, instruction->operands[i]);
    }
}

void emit_borrowed_call_operand(
    CEmitter *emitter,
    const IrFunction *function,
    IrValueId value
) {
    const IrInstruction *producer =
        c_backend_find_value_producer(function, value);
    if (producer != NULL && producer->opcode == IR_OP_LOCAL_LOAD) {
        fprintf(emitter->output, "&l%" PRIu32, producer->index);
        return;
    }
    if (producer != NULL &&
        producer->opcode == IR_OP_LOCAL_FIELD_BORROW) {
        const IrType *owner = &emitter->ir->types[
            function->locals[producer->index].type];
        fprintf(
            emitter->output,
            "&l%" PRIu32 "%sf%" PRIu32,
            producer->index,
            owner->shape == IR_TYPE_CLASS_REFERENCE ? "->" : ".",
            producer->auxiliary
        );
        return;
    }
    fprintf(emitter->output, "&v%" PRIu32, value);
}

static void emit_call_operands_for_target(
    CEmitter *emitter,
    const IrFunction *function,
    const IrInstruction *instruction,
    IrFunctionId target,
    size_t offset
) {
    for (size_t i = offset; i < instruction->operand_count; ++i) {
        if (i != offset) fputs(", ", emitter->output);
        size_t argument = i - offset;
        bool borrowed = argument < instruction->argument_mode_count &&
            parameter_mode_is_reference(
                instruction->argument_modes[argument]);
        if (borrowed)
            emit_borrowed_call_operand(
                emitter, function, instruction->operands[i]);
        else {
            IrTypeId actual = function->value_types[
                instruction->operands[i]];
            IrTypeId expected = IR_INVALID_ID;
            if (target < emitter->ir->function_count &&
                argument < emitter->ir->functions[target].parameter_count)
                expected = emitter->ir->functions[target]
                    .parameters[argument].type;
            if (expected != IR_INVALID_ID && expected != actual &&
                emitter->ir->types[expected].shape ==
                    IR_TYPE_CLASS_REFERENCE &&
                emitter->ir->types[actual].shape ==
                    IR_TYPE_CLASS_REFERENCE) {
                fputc('(', emitter->output);
                c_backend_emit_type(emitter, expected);
                fputc(')', emitter->output);
            }
            emit_value_name(
                emitter->output, instruction->operands[i]);
        }
    }
}

void emit_call_operands(
    CEmitter *emitter,
    const IrFunction *function,
    const IrInstruction *instruction,
    size_t offset
) {
    emit_call_operands_for_target(
        emitter, function, instruction, instruction->index, offset);
}

static IrTypeId class_type_for_virtual_target(
    const IrModule *ir, const IrFunction *target
) {
    const char *separator = strstr(target->name, "::");
    if (separator == NULL) return IR_INVALID_ID;
    size_t owner_length = (size_t)(separator - target->name);
    for (size_t type = 0U; type < ir->type_count; ++type) {
        const IrType *candidate = &ir->types[type];
        if (candidate->shape != IR_TYPE_CLASS_REFERENCE ||
            candidate->name == NULL ||
            strlen(candidate->name) != owner_length ||
            strncmp(candidate->name, target->name, owner_length) != 0)
            continue;
        if (candidate->module_name != NULL && target->module_name != NULL &&
            strcmp(candidate->module_name, target->module_name) != 0)
            continue;
        return (IrTypeId)type;
    }
    return IR_INVALID_ID;
}

static IrFunctionId virtual_target_for_runtime_type(
    const IrModule *ir, IrFunctionId root, IrTypeId runtime_type
) {
    for (size_t entry = 0U;
         entry < ir->interface_dispatch_count; ++entry)
        if (ir->interface_dispatches[entry].interface_function == root &&
            ir->interface_dispatches[entry].runtime_type == runtime_type)
            return ir->interface_dispatches[entry].target_function;
    for (IrTypeId owner = runtime_type;
         owner != IR_INVALID_ID && owner < ir->type_count;
         owner = ir->types[owner].base_type) {
        for (size_t function = 0U;
             function < ir->function_count; ++function) {
            const IrFunction *candidate = &ir->functions[function];
            if (candidate->is_abstract ||
                candidate->virtual_root != root)
                continue;
            if (class_type_for_virtual_target(ir, candidate) == owner)
                return (IrFunctionId)function;
        }
    }
    return IR_INVALID_ID;
}

static bool runtime_type_assignable_to(
    const IrModule *ir, IrTypeId actual, IrTypeId expected, size_t depth
) {
    if (actual == expected) return true;
    if (actual == IR_INVALID_ID || actual >= ir->type_count ||
        expected == IR_INVALID_ID || expected >= ir->type_count ||
        depth >= ir->type_count)
        return false;
    if (runtime_type_assignable_to(
            ir, ir->types[actual].base_type, expected, depth + 1U))
        return true;
    for (size_t interface = 0U;
         interface < ir->types[actual].interface_count; ++interface)
        if (runtime_type_assignable_to(
                ir, ir->types[actual].interface_types[interface],
                expected, depth + 1U))
            return true;
    return false;
}

bool c_backend_function_has_render_root(
    const IrFunction *function) {
    return function != NULL && function->has_render_root;
}

static bool function_supports_direct_render_inner(
    const IrModule *ir, size_t function_index, size_t depth) {
    if (function_index >= ir->function_count ||
        depth > ir->function_count)
        return false;
    const IrFunction *function = &ir->functions[function_index];
    if (!c_backend_function_has_render_root(function)) return false;
    for (size_t b = 0U; b < function->block_count; ++b)
        for (size_t i = 0U;
             i < function->blocks[b].instruction_count; ++i) {
            const IrInstruction *instruction =
                &function->blocks[b].instructions[i];
            if (instruction->opcode == IR_OP_LOCAL_ELEMENT_APPEND &&
                instruction->operand_count != 0U) {
                IrValueId child = instruction->operands[0];
                if (child < function->value_count &&
                    ir->types[function->value_types[child]]
                        .element_child_collection)
                    return false;
            }
            if (instruction->opcode == IR_OP_CALL_DIRECT &&
                instruction->index < ir->function_count &&
                c_backend_function_has_render_root(
                    &ir->functions[instruction->index]) &&
                !function_supports_direct_render_inner(
                    ir, instruction->index, depth + 1U))
                return false;
        }
    return true;
}

bool c_backend_function_supports_direct_render(
    const IrModule *ir, size_t function_index) {
    return function_supports_direct_render_inner(
        ir, function_index, 0U);
}

static void emit_index_guard(
    CEmitter *emitter, const IrFunction *function,
    IrValueId index, size_t length) {
    IrTypeId index_type = function->value_types[index];
    IrTypeShape shape = emitter->ir->types[index_type].shape;
    fputs("    if (", emitter->output);
    if (shape == IR_TYPE_SIGNED_INT)
        fprintf(emitter->output,
                "v%" PRIu32 " < 0 || ", index);
    fprintf(emitter->output,
            "(uint64_t)v%" PRIu32 " >= UINT64_C(%zu)) "
            "aster_trap(\"index out of bounds\");\n",
            index, length);
}

void emit_list_element_equality(
    CEmitter *emitter, const IrType *element,
    IrValueId list, const char *index, IrValueId item) {
    FILE *output = emitter->output;
    if (element->shape == IR_TYPE_STRING_VIEW) {
        fprintf(output,
                "aster_str_equal(v%" PRIu32 "->data[%s], v%" PRIu32 ")",
                list, index, item);
    } else if (element->shape == IR_TYPE_BUILTIN_OBJECT &&
               strcmp(element->name, "string") == 0) {
        fprintf(output,
                "aster_str_equal(aster_string_as_str(v%" PRIu32
                "->data[%s]), aster_string_as_str(v%" PRIu32 "))",
                list, index, item);
    } else {
        fprintf(output, "v%" PRIu32 "->data[%s] == v%" PRIu32,
                list, index, item);
    }
}

void emit_dictionary_key_equality(
    CEmitter *emitter, const IrType *key,
    IrValueId dictionary, const char *index, IrValueId candidate) {
    FILE *output = emitter->output;
    if (key->shape == IR_TYPE_STRING_VIEW) {
        fprintf(output,
                "aster_str_equal(v%" PRIu32 "->keys[%s], v%" PRIu32 ")",
                dictionary, index, candidate);
    } else if (key->shape == IR_TYPE_BUILTIN_OBJECT &&
               strcmp(key->name, "string") == 0) {
        fprintf(output,
                "aster_str_equal(aster_string_as_str(v%" PRIu32
                "->keys[%s]), aster_string_as_str(v%" PRIu32 "))",
                dictionary, index, candidate);
    } else {
        fprintf(output, "v%" PRIu32 "->keys[%s] == v%" PRIu32,
                dictionary, index, candidate);
    }
}

void emit_dictionary_value_equality(
    CEmitter *emitter, const IrType *value,
    IrValueId dictionary, const char *index, IrValueId candidate) {
    FILE *output = emitter->output;
    if (value->shape == IR_TYPE_STRING_VIEW) {
        fprintf(output,
                "aster_str_equal(v%" PRIu32 "->values[%s], v%" PRIu32 ")",
                dictionary, index, candidate);
    } else if (value->shape == IR_TYPE_BUILTIN_OBJECT &&
               strcmp(value->name, "string") == 0) {
        fprintf(output,
                "aster_str_equal(aster_string_as_str(v%" PRIu32
                "->values[%s]), aster_string_as_str(v%" PRIu32 "))",
                dictionary, index, candidate);
    } else {
        fprintf(output, "v%" PRIu32 "->values[%s] == v%" PRIu32,
                dictionary, index, candidate);
    }
}

const IrInstruction *c_backend_find_value_producer(
    const IrFunction *function, IrValueId value) {
    for (size_t block = 0U; block < function->block_count; ++block)
        for (size_t index = 0U;
             index < function->blocks[block].instruction_count;
             ++index) {
            const IrInstruction *producer =
                &function->blocks[block].instructions[index];
            if (producer->result != value) continue;
            return producer;
        }
    return NULL;
}

const IrInstruction *c_backend_find_direct_render_consumer(
    const IrFunction *function, IrValueId value) {
    const IrInstruction *consumer = NULL;
    size_t uses = 0U;
    for (size_t block = 0U; block < function->block_count; ++block)
        for (size_t index = 0U;
             index < function->blocks[block].instruction_count;
             ++index) {
            const IrInstruction *candidate =
                &function->blocks[block].instructions[index];
            for (size_t operand = 0U;
                 operand < candidate->operand_count; ++operand)
                if (candidate->operands[operand] == value) {
                    ++uses;
                    consumer = candidate;
                }
        }
    if (uses != 1U || consumer == NULL) return NULL;
    if (consumer->opcode == IR_OP_CALL_NATIVE &&
        consumer->symbol != NULL &&
        strcmp(consumer->symbol, "Html::ToHtmlString") == 0 &&
        consumer->operand_count == 1U)
        return consumer;
    if (consumer->opcode != IR_OP_LOCAL_STORE) return NULL;

    uint32_t local = consumer->index;
    size_t stores = 0U;
    const IrInstruction *forward = NULL;
    for (size_t block = 0U; block < function->block_count; ++block)
        for (size_t index = 0U;
             index < function->blocks[block].instruction_count; ++index) {
            const IrInstruction *candidate =
                &function->blocks[block].instructions[index];
            if (candidate->opcode == IR_OP_LOCAL_STORE &&
                candidate->index == local)
                ++stores;
            if ((candidate->opcode == IR_OP_LOCAL_LOAD ||
                 candidate->opcode == IR_OP_LOCAL_MOVE) &&
                candidate->index == local) {
                if (forward != NULL) return NULL;
                forward = candidate;
            }
        }
    if (stores != 1U || forward == NULL) return NULL;
    return c_backend_find_direct_render_consumer(
        function, forward->result);
}

const IrInstruction *c_backend_find_direct_render_producer(
    const IrFunction *function, IrValueId value) {
    const IrInstruction *producer =
        c_backend_find_value_producer(function, value);
    if (producer == NULL) return NULL;
    if (producer->opcode == IR_OP_CALL_DIRECT) return producer;
    if (producer->opcode != IR_OP_LOCAL_LOAD &&
        producer->opcode != IR_OP_LOCAL_MOVE)
        return NULL;

    const IrInstruction *store = NULL;
    for (size_t block = 0U; block < function->block_count; ++block)
        for (size_t index = 0U;
             index < function->blocks[block].instruction_count; ++index) {
            const IrInstruction *candidate =
                &function->blocks[block].instructions[index];
            if (candidate->opcode != IR_OP_LOCAL_STORE ||
                candidate->index != producer->index ||
                candidate->operand_count != 1U)
                continue;
            if (store != NULL) return NULL;
            store = candidate;
        }
    if (store == NULL) return NULL;
    return c_backend_find_direct_render_producer(
        function, store->operands[0]);
}

void c_backend_emit_instruction(CEmitter *emitter,
                                const IrFunction *function,
                                const IrInstruction *instruction) {
    FILE *output = emitter->output;
    switch (instruction->opcode) {
        case IR_OP_PARAMETER:
            if (instruction->index < function->parameter_count &&
                parameter_mode_is_reference(
                    function->parameters[instruction->index].mode))
                fprintf(output,
                        "    v%" PRIu32 " = *p%" PRIu32 ";\n",
                        instruction->result, instruction->index);
            else
                fprintf(output,
                        "    v%" PRIu32 " = p%" PRIu32 ";\n",
                        instruction->result, instruction->index);
            return;
        case IR_OP_UNIT:
            fprintf(output, "    v%" PRIu32 " = UINT8_C(0);\n",
                    instruction->result);
            return;
        case IR_OP_CONST_BOOL:
            fprintf(output, "    v%" PRIu32 " = %s;\n",
                    instruction->result,
                    instruction->integer != 0U ? "true" : "false");
            return;
        case IR_OP_CONST_INT: {
            const IrType *type =
                &emitter->ir->types[instruction->result_type];
            if (type->shape == IR_TYPE_SIGNED_INT) {
                if (instruction->integer == (UINT64_C(1) << 63U))
                    fprintf(output,
                            "    v%" PRIu32 " = INT64_MIN;\n",
                            instruction->result);
                else
                    fprintf(output,
                            "    v%" PRIu32 " = INT64_C(%" PRIu64 ");\n",
                            instruction->result,
                            instruction->integer);
            } else {
                fprintf(output,
                        "    v%" PRIu32 " = UINT64_C(%" PRIu64 ");\n",
                        instruction->result, instruction->integer);
            }
            return;
        }
        case IR_OP_CONST_FLOAT:
            fprintf(output, "    v%" PRIu32 " = %.17g;\n",
                    instruction->result, instruction->floating);
            return;
        case IR_OP_CONST_STRING:
            fprintf(output,
                    "    v%" PRIu32 " = (aster_str){",
                    instruction->result);
            if (instruction->symbol_length == 0U) {
                fputs("NULL, 0U};\n", output);
            } else {
                fputs("(const unsigned char *)\"", output);
                for (size_t i = 0U;
                     i < instruction->symbol_length; ++i) {
                    unsigned char byte =
                        (unsigned char)instruction->symbol[i];
                    fprintf(output, "\\x%02x", (unsigned)byte);
                }
                fprintf(output, "\", %zuU};\n",
                        instruction->symbol_length);
            }
            return;
        case IR_OP_CONST_NULL:
            fprintf(output, "    v%" PRIu32 " = NULL;\n",
                    instruction->result);
            return;
        case IR_OP_LOCAL_LOAD:
        case IR_OP_LOCAL_MOVE:
            fprintf(output, "    v%" PRIu32 " = l%" PRIu32 ";\n",
                    instruction->result, instruction->index);
            if (instruction->opcode == IR_OP_LOCAL_MOVE &&
                !(emitter->render_direct &&
                  emitter->ir->types[function->locals[
                      instruction->index].type].shape ==
                      IR_TYPE_ELEMENT_BUILDER) &&
                c_backend_local_tracks_drop(
                    emitter, function, instruction->index))
                fprintf(output,
                        "    l%" PRIu32 "_live = false;\n",
                        instruction->index);
            return;
        case IR_OP_LOCAL_STORE: {
            IrTypeId local_type =
                function->locals[instruction->index].type;
            if (c_backend_local_is_borrowed_alias(
                    emitter, function, instruction->index)) {
                fprintf(output, "    l%" PRIu32 "_ref = ",
                        instruction->index);
                if (!emit_borrowed_alias_address(
                        emitter, function,
                        instruction->operands[0])) {
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "this borrowed aggregate alias");
                    fputs("NULL", output);
                }
                fputs(";\n", output);
                return;
            }
            bool reference_local =
                instruction->index < function->parameter_count &&
                parameter_mode_is_reference(
                    function->parameters[instruction->index].mode);
            if (emitter->render_direct &&
                emitter->ir->types[local_type].shape ==
                    IR_TYPE_ELEMENT_BUILDER) {
                bool fragment = emitter->direct_local_tags != NULL &&
                    emitter->direct_local_tags[instruction->index] != NULL &&
                    c_backend_html_tag_is_fragment(
                        emitter->direct_local_tags[instruction->index],
                        emitter->direct_local_tag_lengths[
                            instruction->index]);
                fprintf(output,
                        "    l%" PRIu32 " = v%" PRIu32 ";\n",
                        instruction->index, instruction->operands[0]);
                if (emitter->direct_straight_line &&
                    emitter->direct_local_open != NULL)
                    emitter->direct_local_open[instruction->index] =
                        !fragment;
                else
                    fprintf(output,
                            "    l%" PRIu32 "_direct_open = %s;\n",
                            instruction->index,
                            fragment ? "false" : "true");
                return;
            }
            if (reference_local &&
                c_backend_type_needs_drop(emitter, local_type)) {
                fputs("    ", output);
                c_backend_emit_drop_call(
                    emitter, local_type, "l", instruction->index);
                fputs(";\n", output);
            } else if (c_backend_local_tracks_drop(
                           emitter, function, instruction->index)) {
                fprintf(output,
                        "    if (l%" PRIu32 "_live) ",
                        instruction->index);
                c_backend_emit_drop_call(
                    emitter, local_type, "l",
                    instruction->index);
                fputs(";\n", output);
            }
            fprintf(output, "    l%" PRIu32 " = ", instruction->index);
            IrTypeId value_type = function->value_types[
                instruction->operands[0]];
            if (local_type != value_type &&
                emitter->ir->types[local_type].shape ==
                    IR_TYPE_CLASS_REFERENCE &&
                emitter->ir->types[value_type].shape ==
                    IR_TYPE_CLASS_REFERENCE) {
                fputc('(', output);
                c_backend_emit_type(emitter, local_type);
                fputc(')', output);
            }
            fprintf(output, "v%" PRIu32 ";\n",
                    instruction->operands[0]);
            if (c_backend_local_tracks_drop(
                    emitter, function, instruction->index))
                fprintf(output,
                        "    l%" PRIu32 "_live = true;\n",
                        instruction->index);
            return;
        }
        case IR_OP_LOCAL_DROP: {
            IrTypeId local_type =
                function->locals[instruction->index].type;
            if (emitter->render_direct &&
                emitter->ir->types[local_type].shape ==
                    IR_TYPE_ELEMENT_BUILDER) {
                if (emitter->direct_straight_line &&
                    emitter->direct_local_open != NULL)
                    emitter->direct_local_open[instruction->index] = false;
                else
                    fprintf(output,
                            "    l%" PRIu32 "_direct_open = false;\n",
                            instruction->index);
                return;
            }
            if (c_backend_local_tracks_drop(
                    emitter, function, instruction->index)) {
                fprintf(output,
                        "    if (l%" PRIu32 "_live) {\n"
                        "        ",
                        instruction->index);
                c_backend_emit_drop_call(
                    emitter, local_type, "l",
                    instruction->index);
                fprintf(output,
                        ";\n"
                        "        l%" PRIu32 "_live = false;\n"
                        "    }\n",
                        instruction->index);
            }
            return;
        }
        case IR_OP_LOCAL_DEFAULT: {
            IrTypeId local_type =
                function->locals[instruction->index].type;
            if (c_backend_local_tracks_drop(
                    emitter, function, instruction->index)) {
                fprintf(output, "    if (l%" PRIu32 "_live) ",
                        instruction->index);
                c_backend_emit_drop_call(
                    emitter, local_type, "l", instruction->index);
                fputs(";\n", output);
            }
            fprintf(output,
                    "    memset(&l%" PRIu32
                    ", 0, sizeof(l%" PRIu32 "));\n",
                    instruction->index, instruction->index);
            if (c_backend_local_tracks_drop(
                    emitter, function, instruction->index))
                fprintf(output, "    l%" PRIu32 "_live = true;\n",
                        instruction->index);
            return;
        }
        case IR_OP_LOCAL_INVALIDATE:
            fprintf(output,
                    "    memset(&l%" PRIu32
                    ", 0, sizeof(l%" PRIu32 "));\n",
                    instruction->index, instruction->index);
            if (c_backend_local_tracks_drop(
                    emitter, function, instruction->index))
                fprintf(output, "    l%" PRIu32 "_live = false;\n",
                        instruction->index);
            return;
        case IR_OP_VALUE_DISCARD: {
            if (instruction->auxiliary == 1U) return;
            IrTypeId value_type =
                function->value_types[instruction->operands[0]];
            if (emitter->ir->types[value_type].requires_cleanup ||
                emitter->ir->types[value_type].managed) {
                fputs("    ", output);
                c_backend_emit_drop_call(
                    emitter, value_type, "v",
                    instruction->operands[0]);
                fputs(";\n", output);
            }
            return;
        }
        case IR_OP_VALUE_CLONE:
            {
            const IrType *result_type =
                &emitter->ir->types[instruction->result_type];
            if (result_type->copy_policy != IR_COPY_TRIVIAL)
                fprintf(output,
                        "    v%" PRIu32 " = aster_clone_%" PRIu32
                        "(v%" PRIu32 ");\n",
                        instruction->result,
                        instruction->result_type,
                        instruction->operands[0]);
            else
                fprintf(output,
                        "    v%" PRIu32 " = v%" PRIu32 ";\n",
                        instruction->result, instruction->operands[0]);
            if (instruction->auxiliary != 0U &&
                !c_backend_value_is_borrowed_projection(
                    function, instruction->operands[0]) &&
                result_type->drop_policy != IR_DROP_TRIVIAL) {
                fputs("    ", output);
                c_backend_emit_drop_call(
                    emitter, instruction->result_type, "v",
                    instruction->operands[0]);
                fputs(";\n", output);
            }
            return;
            }
        case IR_OP_ELEMENT_BEGIN:
        case IR_OP_LOCAL_ELEMENT_PROPERTY:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND:
        case IR_OP_LOCAL_ELEMENT_CSS_VALUE:
        case IR_OP_LOCAL_ELEMENT_PROPERTY_END:
        case IR_OP_LOCAL_ELEMENT_APPEND:
        case IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED:
        case IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT:
        case IR_OP_LOCAL_ELEMENT_FINISH:
            c_backend_emit_element_instruction(
                emitter, function, instruction);
            return;
        case IR_OP_AGGREGATE_MAKE: {
            const IrType *aggregate =
                &emitter->ir->types[instruction->result_type];
            if (aggregate->shape == IR_TYPE_ENUM) {
                fprintf(output,
                        "    v%" PRIu32 " = UINT32_C(%" PRIu32 ");\n",
                        instruction->result, instruction->index);
                return;
            }
            if (aggregate->shape == IR_TYPE_CLASS_REFERENCE) {
                fprintf(output,
                        "    v%" PRIu32 " = malloc(sizeof(*v%" PRIu32 "));\n"
                        "    if (v%" PRIu32 " == NULL) aster_trap(\"class allocation failed\");\n",
                        instruction->result, instruction->result,
                        instruction->result);
                fprintf(output,
                        "    v%" PRIu32 "->_type_id = UINT32_C(%" PRIu32 ");\n",
                        instruction->result, instruction->result_type);
                for (size_t i = 0U; i < instruction->operand_count; ++i)
                    fprintf(output,
                            "    v%" PRIu32 "->f%" PRIu32 " = v%" PRIu32 ";\n",
                            instruction->result, instruction->labels[i],
                            instruction->operands[i]);
                return;
            }
            fprintf(output, "    v%" PRIu32 " = (",
                    instruction->result);
            c_backend_emit_type(emitter, instruction->result_type);
            fputs("){", output);
            if (aggregate->shape == IR_TYPE_ARRAY) {
                fputs(".items = {", output);
                emit_operands(emitter, instruction);
                fputs("}", output);
            } else if (aggregate->shape == IR_TYPE_STRUCT) {
                for (size_t i = 0U;
                     i < instruction->operand_count; ++i) {
                    fprintf(output, "%s.f%" PRIu32 " = v%" PRIu32,
                            i == 0U ? "" : ", ",
                            instruction->labels[i],
                            instruction->operands[i]);
                }
            } else {
                fprintf(output, ".tag = UINT32_C(%" PRIu32 ")",
                        instruction->index);
                if (instruction->operand_count == 1U)
                    fprintf(
                        output,
                        ", .payload.v%" PRIu32 " = v%" PRIu32,
                        instruction->index,
                        instruction->operands[0]);
            }
            fputs("};\n", output);
            return;
        }
        case IR_OP_LOCAL_ENUM_IS:
            if (emitter->ir->types[function->locals[
                    instruction->index].type].shape == IR_TYPE_ENUM)
                fprintf(
                    output,
                    "    v%" PRIu32 " = l%" PRIu32
                    " == UINT32_C(%" PRIu32 ");\n",
                    instruction->result, instruction->index,
                    instruction->auxiliary);
            else
                fprintf(
                    output,
                    "    v%" PRIu32 " = l%" PRIu32
                    ".tag == UINT32_C(%" PRIu32 ");\n",
                    instruction->result, instruction->index,
                    instruction->auxiliary);
            return;
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE:
            fprintf(
                output,
                "    v%" PRIu32 " = l%" PRIu32
                ".payload.v%" PRIu32 ";\n",
                instruction->result, instruction->index,
                instruction->auxiliary);
            if (c_backend_type_needs_drop(
                    emitter, instruction->result_type) &&
                c_backend_local_tracks_drop(
                    emitter, function, instruction->index))
                fprintf(output,
                        "    l%" PRIu32 "_live = false;\n",
                        instruction->index);
            return;
        case IR_OP_ENUM_IS:
            fprintf(
                output,
                "    v%" PRIu32 " = v%" PRIu32
                ".tag == UINT32_C(%" PRIu32 ");\n",
                instruction->result, instruction->operands[0],
                instruction->auxiliary);
            return;
        case IR_OP_ENUM_PAYLOAD_BORROW:
            fprintf(
                output,
                "    v%" PRIu32 " = v%" PRIu32
                ".payload.v%" PRIu32 ";\n",
                instruction->result, instruction->operands[0],
                instruction->auxiliary);
            return;
        case IR_OP_COLLECTION_COUNT:
            fprintf(output,
                    "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                    "->length;\n",
                    instruction->result, instruction->operands[0]);
            return;
        case IR_OP_LIST_ELEMENT_BORROW:
            fprintf(output,
                    "    if (v%" PRIu32 " >= v%" PRIu32
                    "->length) aster_trap(\"List index out of bounds\");\n"
                    "    v%" PRIu32 " = v%" PRIu32
                    "->data[(size_t)v%" PRIu32 "];\n",
                    instruction->operands[1], instruction->operands[0],
                    instruction->result, instruction->operands[0],
                    instruction->operands[1]);
            return;
        case IR_OP_QUEUE_FRONT_BORROW:
            fprintf(output,
                    "    if (v%" PRIu32
                    "->length == 0U) aster_trap(\"Queue is empty\");\n"
                    "    v%" PRIu32 " = v%" PRIu32
                    "->data[v%" PRIu32 "->head];\n",
                    instruction->operands[0], instruction->result,
                    instruction->operands[0], instruction->operands[0]);
            return;
        case IR_OP_STACK_TOP_BORROW:
            fprintf(output,
                    "    if (v%" PRIu32
                    "->length == 0U) aster_trap(\"Stack is empty\");\n"
                    "    v%" PRIu32 " = v%" PRIu32
                    "->data[v%" PRIu32 "->length - 1U];\n",
                    instruction->operands[0], instruction->result,
                    instruction->operands[0], instruction->operands[0]);
            return;
        case IR_OP_DICTIONARY_GET_BORROW: {
            const IrType *dictionary = &emitter->ir->types[
                function->value_types[instruction->operands[0]]];
            fprintf(output,
                    "    size_t dictionary_borrow_match_%" PRIu32
                    " = SIZE_MAX;\n"
                    "    for (size_t i = 0U; i < v%" PRIu32
                    "->length; ++i) {\n"
                    "        if (",
                    instruction->result, instruction->operands[0]);
            emit_dictionary_key_equality(
                emitter,
                &emitter->ir->types[dictionary->element_type],
                instruction->operands[0], "i",
                instruction->operands[1]);
            fprintf(output,
                    ") { dictionary_borrow_match_%" PRIu32
                    " = i; break; }\n"
                    "    }\n"
                    "    if (dictionary_borrow_match_%" PRIu32
                    " == SIZE_MAX) aster_trap(\"Dictionary key not found\");\n"
                    "    v%" PRIu32 " = v%" PRIu32
                    "->values[dictionary_borrow_match_%" PRIu32 "];\n",
                    instruction->result, instruction->result,
                    instruction->result, instruction->operands[0],
                    instruction->result);
            return;
        }
        case IR_OP_DICTIONARY_FIND: {
            const IrType *dictionary = &emitter->ir->types[
                function->value_types[instruction->operands[0]]];
            fprintf(output,
                    "    v%" PRIu32 " = UINT64_MAX;\n"
                    "    for (size_t i = 0U; i < v%" PRIu32
                    "->length; ++i) {\n"
                    "        if (",
                    instruction->result, instruction->operands[0]);
            emit_dictionary_key_equality(
                emitter,
                &emitter->ir->types[dictionary->element_type],
                instruction->operands[0], "i",
                instruction->operands[1]);
            fprintf(output,
                    ") { v%" PRIu32 " = (uint64_t)i; break; }\n"
                    "    }\n",
                    instruction->result);
            if (c_backend_type_needs_drop(
                    emitter, dictionary->element_type)) {
                fputs("    ", output);
                c_backend_emit_drop_call(
                    emitter, dictionary->element_type, "v",
                    instruction->operands[1]);
                fputs(";\n", output);
            }
            return;
        }
        case IR_OP_DICTIONARY_KEY_BORROW:
        case IR_OP_DICTIONARY_VALUE_BORROW:
            fprintf(output,
                    "    if (v%" PRIu32 " >= v%" PRIu32
                    "->length) aster_trap(\"dictionary copy index out of bounds\");\n"
                    "    v%" PRIu32 " = v%" PRIu32 "->%s[v%" PRIu32 "];\n",
                    instruction->operands[1], instruction->operands[0],
                    instruction->result, instruction->operands[0],
                    instruction->opcode == IR_OP_DICTIONARY_KEY_BORROW
                        ? "keys" : "values",
                    instruction->operands[1]);
            return;
        case IR_OP_LOCAL_FIELD_MOVE:
            if (emitter->ir->types[function->locals[
                    instruction->index].type].shape ==
                IR_TYPE_CLASS_REFERENCE) {
                fprintf(output,
                        "    if (l%" PRIu32 " == NULL) aster_trap(\"null class reference\");\n"
                        "    v%" PRIu32 " = l%" PRIu32 "->f%" PRIu32 ";\n"
                        "    memset(&l%" PRIu32 "->f%" PRIu32
                        ", 0, sizeof(l%" PRIu32 "->f%" PRIu32 "));\n",
                        instruction->index, instruction->result,
                        instruction->index, instruction->auxiliary,
                        instruction->index, instruction->auxiliary,
                        instruction->index, instruction->auxiliary);
                return;
            }
            fprintf(output,
                    "    v%" PRIu32 " = l%" PRIu32 ".f%" PRIu32 ";\n"
                    "    memset(&l%" PRIu32 ".f%" PRIu32
                    ", 0, sizeof(l%" PRIu32 ".f%" PRIu32 "));\n",
                    instruction->result, instruction->index,
                    instruction->auxiliary, instruction->index,
                    instruction->auxiliary, instruction->index,
                    instruction->auxiliary);
            return;
        case IR_OP_LOCAL_FIELD_GET:
            if (emitter->ir->types[function->locals[
                    instruction->index].type].shape ==
                IR_TYPE_CLASS_REFERENCE) {
                fprintf(output,
                        "    if (l%" PRIu32 " == NULL) aster_trap(\"null class reference\");\n",
                        instruction->index);
                if (c_backend_type_needs_drop(emitter, instruction->result_type))
                    fprintf(output,
                            "    v%" PRIu32 " = aster_clone_%" PRIu32
                            "(l%" PRIu32 "->f%" PRIu32 ");\n",
                            instruction->result, instruction->result_type,
                            instruction->index, instruction->auxiliary);
                else
                    fprintf(output,
                            "    v%" PRIu32 " = l%" PRIu32 "->f%" PRIu32 ";\n",
                            instruction->result, instruction->index,
                            instruction->auxiliary);
                return;
            }
            if (c_backend_type_needs_drop(emitter, instruction->result_type))
                fprintf(output,
                        "    v%" PRIu32 " = aster_clone_%" PRIu32
                        "(l%" PRIu32 ".f%" PRIu32 ");\n",
                        instruction->result, instruction->result_type,
                        instruction->index, instruction->auxiliary);
            else
                fprintf(output,
                        "    v%" PRIu32 " = l%" PRIu32 ".f%" PRIu32 ";\n",
                        instruction->result, instruction->index,
                        instruction->auxiliary);
            return;
        case IR_OP_STATIC_FIELD_LOAD:
            fprintf(output,
                    "    v%" PRIu32 " = aster_static_%" PRIu32 ";\n",
                    instruction->result, instruction->index);
            return;
        case IR_OP_STATIC_FIELD_STORE:
            fprintf(output,
                    "    aster_static_%" PRIu32 " = v%" PRIu32 ";\n",
                    instruction->index, instruction->operands[0]);
            return;
        case IR_OP_LOCAL_FIELD_BORROW:
            if (emitter->ir->types[function->locals[
                    instruction->index].type].shape ==
                IR_TYPE_CLASS_REFERENCE) {
                fprintf(output,
                        "    if (l%" PRIu32 " == NULL) aster_trap(\"null class reference\");\n"
                        "    v%" PRIu32 " = l%" PRIu32 "->f%" PRIu32 ";\n",
                        instruction->index, instruction->result,
                        instruction->index, instruction->auxiliary);
                return;
            }
            fprintf(output,
                    "    v%" PRIu32 " = l%" PRIu32 ".f%" PRIu32 ";\n",
                    instruction->result, instruction->index,
                    instruction->auxiliary);
            return;
        case IR_OP_FIELD_GET:
            if (emitter->ir->types[function->value_types[
                    instruction->operands[0]]].shape ==
                IR_TYPE_CLASS_REFERENCE) {
                fprintf(output,
                        "    if (v%" PRIu32 " == NULL) aster_trap(\"null class reference\");\n",
                        instruction->operands[0]);
                if (instruction->auxiliary == 1U ||
                    instruction->auxiliary == 2U)
                    fprintf(output,
                            "    v%" PRIu32 " = v%" PRIu32 "->f%" PRIu32 ";\n",
                            instruction->result, instruction->operands[0],
                            instruction->index);
                else if (c_backend_type_needs_drop(emitter, instruction->result_type))
                    fprintf(output,
                            "    v%" PRIu32 " = aster_clone_%" PRIu32
                            "(v%" PRIu32 "->f%" PRIu32 ");\n",
                            instruction->result, instruction->result_type,
                            instruction->operands[0], instruction->index);
                else
                    fprintf(output,
                            "    v%" PRIu32 " = v%" PRIu32 "->f%" PRIu32 ";\n",
                            instruction->result, instruction->operands[0],
                            instruction->index);
                return;
            }
            if (instruction->auxiliary == 1U ||
                instruction->auxiliary == 2U)
                fprintf(output,
                        "    v%" PRIu32 " = v%" PRIu32 ".f%" PRIu32 ";\n",
                        instruction->result, instruction->operands[0],
                        instruction->index);
            else if (c_backend_type_needs_drop(emitter, instruction->result_type))
                fprintf(output,
                        "    v%" PRIu32 " = aster_clone_%" PRIu32
                        "(v%" PRIu32 ".f%" PRIu32 ");\n",
                        instruction->result, instruction->result_type,
                        instruction->operands[0], instruction->index);
            else
                fprintf(output,
                        "    v%" PRIu32 " = v%" PRIu32 ".f%" PRIu32 ";\n",
                        instruction->result, instruction->operands[0],
                        instruction->index);
            if (instruction->auxiliary != 1U &&
                instruction->auxiliary != 2U &&
                c_backend_type_needs_drop(
                    emitter,
                    function->value_types[instruction->operands[0]]) &&
                !c_backend_value_is_borrowed_projection(
                    function, instruction->operands[0])) {
                fputs("    ", output);
                c_backend_emit_drop_call(
                    emitter,
                    function->value_types[instruction->operands[0]],
                    "v", instruction->operands[0]);
                fputs(";\n", output);
            }
            return;
        case IR_OP_LOCAL_FIELD_SET:
            {
            IrTypeId structure_type =
                function->locals[instruction->index].type;
            const IrType *structure =
                &emitter->ir->types[structure_type];
            IrTypeId field_type =
                structure->field_types[instruction->auxiliary];
            if (structure->shape == IR_TYPE_CLASS_REFERENCE)
                fprintf(output,
                        "    if (l%" PRIu32 " == NULL) aster_trap(\"null class reference\");\n",
                        instruction->index);
            if (c_backend_type_needs_drop(emitter, field_type))
                fprintf(
                    output,
                    "    aster_drop_%" PRIu32
                    "(&l%" PRIu32 "%sf%" PRIu32 ");\n",
                    field_type, instruction->index,
                    structure->shape == IR_TYPE_CLASS_REFERENCE ? "->" : ".",
                    instruction->auxiliary);
            fprintf(output,
                    "    l%" PRIu32 "%sf%" PRIu32 " = v%" PRIu32 ";\n",
                    instruction->index,
                    structure->shape == IR_TYPE_CLASS_REFERENCE ? "->" : ".",
                    instruction->auxiliary,
                    instruction->operands[0]);
            return;
            }
        case IR_OP_LOCAL_FIELD_DEFAULT: {
            IrTypeId structure_type =
                function->locals[instruction->index].type;
            const IrType *structure =
                &emitter->ir->types[structure_type];
            IrTypeId field_type =
                structure->field_types[instruction->auxiliary];
            const char *access = structure->shape ==
                    IR_TYPE_CLASS_REFERENCE ? "->" : ".";
            if (structure->shape == IR_TYPE_CLASS_REFERENCE)
                fprintf(output,
                        "    if (l%" PRIu32
                        " == NULL) aster_trap(\"null class reference\");\n",
                        instruction->index);
            if (c_backend_type_needs_drop(emitter, field_type))
                fprintf(output,
                        "    aster_drop_%" PRIu32
                        "(&l%" PRIu32 "%sf%" PRIu32 ");\n",
                        field_type, instruction->index,
                        access, instruction->auxiliary);
            fprintf(output,
                    "    memset(&l%" PRIu32 "%sf%" PRIu32
                    ", 0, sizeof(l%" PRIu32 "%sf%" PRIu32 "));\n",
                    instruction->index, access,
                    instruction->auxiliary, instruction->index,
                    access, instruction->auxiliary);
            return;
        }
        case IR_OP_LOCAL_INDEX_GET: {
            const IrType *array =
                &emitter->ir->types[
                    function->locals[instruction->index].type];
            emit_index_guard(
                emitter, function, instruction->operands[0],
                array->array_length);
            fprintf(output,
                    "    v%" PRIu32 " = ", instruction->result);
            if (c_backend_type_needs_drop(emitter, instruction->result_type))
                fprintf(output, "aster_clone_%" PRIu32 "(",
                        instruction->result_type);
            fprintf(output, "l%" PRIu32
                    ".items[(size_t)v%" PRIu32 "]",
                    instruction->index, instruction->operands[0]);
            fputs(c_backend_type_needs_drop(emitter, instruction->result_type)
                    ? ");\n" : ";\n", output);
            return;
        }
        case IR_OP_LOCAL_INDEX_MOVE: {
            const IrType *array =
                &emitter->ir->types[
                    function->locals[instruction->index].type];
            emit_index_guard(
                emitter, function, instruction->operands[0],
                array->array_length);
            fprintf(output,
                    "    v%" PRIu32 " = l%" PRIu32
                    ".items[(size_t)v%" PRIu32 "];\n"
                    "    memset(&l%" PRIu32
                    ".items[(size_t)v%" PRIu32
                    "], 0, sizeof(l%" PRIu32
                    ".items[(size_t)v%" PRIu32 "]));\n",
                    instruction->result, instruction->index,
                    instruction->operands[0], instruction->index,
                    instruction->operands[0], instruction->index,
                    instruction->operands[0]);
            return;
        }
        case IR_OP_INDEX_GET: {
            IrTypeId array_type =
                function->value_types[instruction->operands[0]];
            const IrType *array =
                &emitter->ir->types[array_type];
            emit_index_guard(
                emitter, function, instruction->operands[1],
                array->array_length);
            fprintf(output,
                    "    v%" PRIu32 " = ", instruction->result);
            if (instruction->integer == 0U &&
                c_backend_type_needs_drop(emitter, instruction->result_type))
                fprintf(output, "aster_clone_%" PRIu32 "(",
                        instruction->result_type);
            fprintf(output, "v%" PRIu32
                    ".items[(size_t)v%" PRIu32 "]",
                    instruction->operands[0], instruction->operands[1]);
            fputs(instruction->integer == 0U &&
                  c_backend_type_needs_drop(emitter, instruction->result_type)
                    ? ");\n" : ";\n", output);
            if (instruction->integer == 0U &&
                c_backend_type_needs_drop(emitter, array_type) &&
                !c_backend_value_is_borrowed_projection(
                    function, instruction->operands[0])) {
                fputs("    ", output);
                c_backend_emit_drop_call(
                    emitter, array_type, "v",
                    instruction->operands[0]);
                fputs(";\n", output);
            }
            return;
        }
        case IR_OP_LOCAL_INDEX_SET: {
            const IrType *array =
                &emitter->ir->types[
                    function->locals[instruction->index].type];
            emit_index_guard(
                emitter, function, instruction->operands[0],
                array->array_length);
            if (c_backend_type_needs_drop(
                    emitter, array->element_type))
                fprintf(
                    output,
                    "    aster_drop_%" PRIu32
                    "(&l%" PRIu32 ".items[(size_t)v%" PRIu32 "]);\n",
                    array->element_type, instruction->index,
                    instruction->operands[0]);
            fprintf(output,
                    "    l%" PRIu32 ".items[(size_t)v%" PRIu32
                    "] = v%" PRIu32 ";\n",
                    instruction->index, instruction->operands[0],
                    instruction->operands[1]);
            return;
        }
        case IR_OP_ADD_CHECKED:
        case IR_OP_SUB_CHECKED:
        case IR_OP_MUL_CHECKED:
        case IR_OP_DIV_CHECKED:
        case IR_OP_REM_CHECKED:
        case IR_OP_SHIFT_LEFT_CHECKED:
        case IR_OP_SHIFT_RIGHT_CHECKED:
        case IR_OP_BIT_AND:
        case IR_OP_BIT_OR:
        case IR_OP_BIT_XOR:
        case IR_OP_ADD_FLOAT:
        case IR_OP_SUB_FLOAT:
        case IR_OP_MUL_FLOAT:
        case IR_OP_DIV_FLOAT:
        case IR_OP_EQUAL:
        case IR_OP_NOT_EQUAL:
        case IR_OP_LESS:
        case IR_OP_LESS_EQUAL:
        case IR_OP_GREATER:
        case IR_OP_GREATER_EQUAL:
        case IR_OP_NEGATE:
        case IR_OP_NOT:
        case IR_OP_CAST:
        case IR_OP_BIT_NOT:
        case IR_OP_EXCEPTION_SET:
        case IR_OP_EXCEPTION_PENDING:
        case IR_OP_EXCEPTION_MATCH:
        case IR_OP_EXCEPTION_TAKE:
            c_backend_emit_operation_instruction(
                emitter, function, instruction);
            return;
        case IR_OP_CALL_DIRECT:
            if (emitter->render_direct &&
                instruction->index < emitter->ir->function_count &&
                c_backend_function_supports_direct_render(
                    emitter->ir, instruction->index)) {
                c_backend_flush_direct_builder_literals(emitter);
                if (instruction->render_destination != IR_INVALID_ID) {
                    c_backend_emit_direct_close_open(
                        emitter, instruction->render_destination);
                } else {
                    const IrInstruction *consumer =
                        c_backend_find_element_append_consumer(
                            function, instruction->result);
                    if (consumer != NULL)
                        c_backend_emit_direct_close_open(
                            emitter, consumer->index);
                }
                fprintf(output, "    aster_fn_%" PRIu32
                        "_append(render_builder",
                        instruction->index);
                if (instruction->operand_count != 0U) fputs(", ", output);
                emit_call_operands(
                    emitter, function, instruction, 0U);
                fputs(");\n", output);
                fprintf(output,
                        "    v%" PRIu32 " = (aster_html *)render_builder;\n",
                        instruction->result);
                return;
            }
            if (!emitter->render_direct &&
                instruction->index < emitter->ir->function_count &&
                c_backend_function_supports_direct_render(
                    emitter->ir, instruction->index) &&
                c_backend_find_direct_render_consumer(
                    function, instruction->result) != NULL) {
                fprintf(output,
                        "    aster_string *direct_render_%" PRIu32
                        " = aster_fn_%" PRIu32 "_render(",
                        instruction->result, instruction->index);
                emit_call_operands(
                    emitter, function, instruction, 0U);
                fprintf(output,
                        ");\n    v%" PRIu32 " = NULL;\n",
                        instruction->result);
                return;
            }
            fprintf(
                output, "    v%" PRIu32 " = aster_fn_%" PRIu32,
                instruction->result, instruction->index);
            if (instruction->render_destination != IR_INVALID_ID &&
                instruction->index < emitter->ir->function_count &&
                c_backend_function_has_render_root(
                    &emitter->ir->functions[
                        instruction->index])) {
                fprintf(
                    output, "_into(l%" PRIu32,
                    instruction->render_destination);
                if (instruction->operand_count != 0U)
                    fputs(", ", output);
            } else {
                fputc('(', output);
            }
            emit_call_operands(
                emitter, function, instruction, 0U);
            fputs(");\n", output);
            return;
        case IR_OP_CALL_VIRTUAL: {
            IrFunctionId root = instruction->index;
            if (root < emitter->ir->function_count &&
                emitter->ir->functions[root].virtual_root != IR_INVALID_ID)
                root = emitter->ir->functions[root].virtual_root;
            fprintf(output,
                    "    if (v%" PRIu32 " == NULL) aster_trap(\"virtual call requires a non-null receiver\");\n"
                    "    switch (*((uint32_t *)(void *)v%" PRIu32 ")) {\n",
                    instruction->operands[0], instruction->operands[0]);
            bool emitted = false;
            IrTypeId root_type = root < emitter->ir->function_count
                ? class_type_for_virtual_target(
                      emitter->ir, &emitter->ir->functions[root])
                : IR_INVALID_ID;
            for (size_t runtime_type = 0U;
                 runtime_type < emitter->ir->type_count; ++runtime_type) {
                if (emitter->ir->types[runtime_type].shape !=
                    IR_TYPE_CLASS_REFERENCE)
                    continue;
                bool belongs = runtime_type_assignable_to(
                    emitter->ir, (IrTypeId)runtime_type, root_type, 0U);
                if (!belongs) continue;
                IrFunctionId target_id = virtual_target_for_runtime_type(
                    emitter->ir, root, (IrTypeId)runtime_type);
                if (target_id == IR_INVALID_ID) continue;
                emitted = true;
                fprintf(output,
                        "    case UINT32_C(%zu): v%" PRIu32
                        " = aster_fn_%" PRIu32 "(",
                        runtime_type, instruction->result, target_id);
                emit_call_operands_for_target(
                    emitter, function, instruction,
                    target_id, 0U);
                fputs("); break;\n", output);
            }
            if (!emitted)
                fputs("    default: aster_trap(\"virtual call has no concrete implementation\");\n",
                      output);
            else
                fputs("    default: aster_trap(\"invalid runtime class for virtual call\");\n",
                      output);
            fputs("    }\n", output);
            return;
        }
        case IR_OP_FUNCTION_REF:
            fprintf(output,
                    "    v%" PRIu32 " = (aster_type_%" PRIu32
                    "){aster_delegate_unbound_%" PRIu32 ", NULL};\n",
                    instruction->result, instruction->result_type,
                    instruction->index);
            return;
        case IR_OP_BOUND_METHOD_REF:
            fprintf(output,
                    "    if (v%" PRIu32 " == NULL) "
                    "aster_trap(\"cannot bind an instance method to a null class reference\");\n",
                    instruction->operands[0]);
            if (instruction->index < emitter->ir->function_count &&
                emitter->ir->functions[instruction->index].is_virtual) {
                IrFunctionId root = emitter->ir->functions[
                    instruction->index].virtual_root;
                if (root == IR_INVALID_ID) root = instruction->index;
                fprintf(output,
                        "    switch (*((uint32_t *)(void *)v%" PRIu32 ")) {\n",
                        instruction->operands[0]);
                for (size_t runtime = 0U;
                     runtime < emitter->ir->type_count; ++runtime) {
                    if (emitter->ir->types[runtime].shape !=
                        IR_TYPE_CLASS_REFERENCE)
                        continue;
                    IrFunctionId target = virtual_target_for_runtime_type(
                        emitter->ir, root, (IrTypeId)runtime);
                    if (target == IR_INVALID_ID) continue;
                    fprintf(output,
                            "    case UINT32_C(%zu): v%" PRIu32
                            " = (aster_type_%" PRIu32
                            "){aster_delegate_bound_%" PRIu32
                            ", (void *)v%" PRIu32 "}; break;\n",
                            runtime, instruction->result,
                            instruction->result_type, target,
                            instruction->operands[0]);
                }
                fputs("    default: aster_trap(\"invalid runtime class while binding virtual method\");\n"
                      "    }\n", output);
            } else {
                fprintf(output,
                        "    v%" PRIu32 " = (aster_type_%" PRIu32
                        "){aster_delegate_bound_%" PRIu32 ", "
                        "(void *)v%" PRIu32 "};\n",
                        instruction->result, instruction->result_type,
                        instruction->index, instruction->operands[0]);
            }
            return;
        case IR_OP_CALL_INDIRECT:
            fprintf(output,
                    "    if (v%" PRIu32 ".invoke == NULL) "
                    "aster_trap(\"indirect call requires a valid function value\");\n"
                    "    v%" PRIu32 " = v%" PRIu32 ".invoke("
                    "v%" PRIu32 ".receiver",
                    instruction->operands[0],
                    instruction->result, instruction->operands[0],
                    instruction->operands[0]);
            if (instruction->operand_count > 1U) fputs(", ", output);
            emit_call_operands(
                emitter, function, instruction, 1U);
            fputs(");\n", output);
            return;
        case IR_OP_ITERATOR_BEGIN: {
            const IrType *iterator =
                &emitter->ir->types[instruction->result_type];
            const IrType *source =
                &emitter->ir->types[iterator->argument_types[0]];
            if (c_backend_type_is_vec(source))
                fprintf(output,
                        "    v%" PRIu32
                        " = (aster_iterator_%" PRIu32
                        "){v%" PRIu32 ", 0U, false};\n",
                        instruction->result, instruction->result_type,
                        instruction->operands[0]);
            else if (source->shape == IR_TYPE_ARRAY)
                fprintf(output,
                        "    v%" PRIu32
                        " = (aster_iterator_%" PRIu32
                        "){v%" PRIu32 ", NULL, 0U, false};\n",
                        instruction->result, instruction->result_type,
                        instruction->operands[0]);
            else if (source->shape == IR_TYPE_BUILTIN_OBJECT &&
                     strcmp(source->name, "string") == 0)
                fprintf(output,
                        "    v%" PRIu32
                        " = (aster_iterator_%" PRIu32
                        "){v%" PRIu32 ", 0U, false};\n",
                        instruction->result, instruction->result_type,
                        instruction->operands[0]);
            else
                fprintf(output,
                        "    v%" PRIu32
                        " = (aster_iterator_%" PRIu32
                        "){v%" PRIu32 ", 0U, true};\n",
                        instruction->result, instruction->result_type,
                        instruction->operands[0]);
            return;
        }
        case IR_OP_BORROWED_ITERATOR_BEGIN: {
            const IrType *iterator =
                &emitter->ir->types[instruction->result_type];
            const IrType *source =
                &emitter->ir->types[iterator->argument_types[0]];
            if (c_backend_type_is_vec(source)) {
                if (instruction->operand_count == 1U)
                    fprintf(output,
                            "    v%" PRIu32
                            " = (aster_iterator_%" PRIu32
                            "){v%" PRIu32 ", 0U, true};\n",
                            instruction->result,
                            instruction->result_type,
                            instruction->operands[0]);
                else
                    fprintf(output,
                            "    v%" PRIu32
                            " = (aster_iterator_%" PRIu32
                            "){l%" PRIu32 ", 0U, true};\n",
                            instruction->result,
                            instruction->result_type,
                            instruction->index);
            }
            else if (c_backend_type_is_queue(source)) {
                if (instruction->operand_count == 1U)
                    fprintf(output,
                            "    v%" PRIu32
                            " = (aster_iterator_%" PRIu32
                            "){v%" PRIu32 ", 0U, true};\n",
                            instruction->result,
                            instruction->result_type,
                            instruction->operands[0]);
                else
                    fprintf(output,
                            "    v%" PRIu32
                            " = (aster_iterator_%" PRIu32
                            "){l%" PRIu32 ", 0U, true};\n",
                            instruction->result,
                            instruction->result_type,
                            instruction->index);
            }
            else if (source->shape == IR_TYPE_ARRAY) {
                fprintf(output,
                        "    v%" PRIu32
                        " = (aster_iterator_%" PRIu32 "){0};\n"
                        "    v%" PRIu32 ".borrowed_array = &l%" PRIu32
                        ";\n"
                        "    v%" PRIu32 ".borrowed = true;\n",
                        instruction->result, instruction->result_type,
                        instruction->result, instruction->index,
                        instruction->result);
            } else
                fprintf(output,
                        "    v%" PRIu32
                        " = (aster_iterator_%" PRIu32
                        "){l%" PRIu32 ", 0U, true};\n",
                        instruction->result, instruction->result_type,
                        instruction->index);
            return;
        }
        case IR_OP_LOCAL_ITERATOR_HAS_NEXT: {
            IrTypeId iterator_id =
                function->locals[instruction->index].type;
            const IrType *iterator =
                &emitter->ir->types[iterator_id];
            const IrType *source =
                &emitter->ir->types[iterator->argument_types[0]];
            if (c_backend_type_is_vec(source))
                fprintf(output,
                        "    v%" PRIu32 " = l%" PRIu32
                        ".vector != NULL && l%" PRIu32
                        ".index < l%" PRIu32 ".vector->length;\n",
                        instruction->result, instruction->index,
                        instruction->index, instruction->index);
            else if (c_backend_type_is_queue(source))
                fprintf(output,
                        "    v%" PRIu32 " = l%" PRIu32
                        ".queue != NULL && l%" PRIu32
                        ".index < l%" PRIu32 ".queue->length;\n",
                        instruction->result, instruction->index,
                        instruction->index, instruction->index);
            else if (source->shape == IR_TYPE_ARRAY)
                fprintf(output,
                        "    v%" PRIu32 " = l%" PRIu32
                        ".index < %zuU;\n",
                        instruction->result, instruction->index,
                        source->array_length);
            else if (source->shape == IR_TYPE_BUILTIN_OBJECT &&
                     strcmp(source->name, "string") == 0)
                fprintf(output,
                        "    v%" PRIu32 " = l%" PRIu32
                        ".slice != NULL && l%" PRIu32
                        ".index < l%" PRIu32 ".slice->length;\n",
                        instruction->result, instruction->index,
                        instruction->index, instruction->index);
            else
                fprintf(output,
                        "    v%" PRIu32 " = l%" PRIu32
                        ".index < l%" PRIu32 ".slice.length;\n",
                        instruction->result, instruction->index,
                        instruction->index);
            return;
        }
        case IR_OP_LOCAL_ITERATOR_NEXT: {
            IrTypeId iterator_id =
                function->locals[instruction->index].type;
            const IrType *iterator =
                &emitter->ir->types[iterator_id];
            const IrType *source =
                &emitter->ir->types[iterator->argument_types[0]];
            if (c_backend_type_is_vec(source))
                fprintf(output,
                        "    if (l%" PRIu32 ".vector == NULL || "
                        "l%" PRIu32 ".index >= "
                        "l%" PRIu32 ".vector->length)\n"
                        "        aster_trap(\"iterator advanced past its end\");\n"
                        "    v%" PRIu32 " = l%" PRIu32
                        ".vector->data[l%" PRIu32 ".index++];\n",
                        instruction->index, instruction->index,
                        instruction->index, instruction->result,
                        instruction->index, instruction->index);
            else if (c_backend_type_is_queue(source))
                fprintf(output,
                        "    if (l%" PRIu32 ".queue == NULL || "
                        "l%" PRIu32 ".index >= l%" PRIu32
                        ".queue->length)\n"
                        "        aster_trap(\"iterator advanced past its end\");\n"
                        "    v%" PRIu32 " = l%" PRIu32
                        ".queue->data[(l%" PRIu32 ".queue->head + "
                        "l%" PRIu32 ".index++) %% l%" PRIu32
                        ".queue->capacity];\n",
                        instruction->index, instruction->index,
                        instruction->index, instruction->result,
                        instruction->index, instruction->index,
                        instruction->index, instruction->index);
            else if (source->shape == IR_TYPE_ARRAY)
                fprintf(output,
                        "    if (l%" PRIu32 ".index >= %zuU)\n"
                        "        aster_trap(\"iterator advanced past its end\");\n"
                        "    v%" PRIu32 " = (l%" PRIu32 ".borrowed\n"
                        "        ? l%" PRIu32 ".borrowed_array\n"
                        "        : &l%" PRIu32 ".owned_array)->items["
                        "l%" PRIu32 ".index++];\n",
                        instruction->index, source->array_length,
                        instruction->result, instruction->index,
                        instruction->index, instruction->index,
                        instruction->index);
            else if (source->shape == IR_TYPE_BUILTIN_OBJECT &&
                     strcmp(source->name, "string") == 0)
                fprintf(output,
                        "    if (l%" PRIu32 ".slice == NULL || l%" PRIu32
                        ".index >= l%" PRIu32 ".slice->length)\n"
                        "        aster_trap(\"iterator advanced past its end\");\n"
                        "    {\n"
                        "        const unsigned char *s = l%" PRIu32
                        ".slice->data;\n"
                        "        size_t i = l%" PRIu32 ".index;\n"
                        "        uint32_t cp; size_t n;\n"
                        "        unsigned char b = s[i];\n"
                        "        if (b < 0x80U) { cp = b; n = 1U; }\n"
                        "        else if (b >= 0xc2U && b <= 0xdfU) "
                        "{ cp = b & 0x1fU; n = 2U; }\n"
                        "        else if (b >= 0xe0U && b <= 0xefU) "
                        "{ cp = b & 0x0fU; n = 3U; }\n"
                        "        else if (b >= 0xf0U && b <= 0xf4U) "
                        "{ cp = b & 0x07U; n = 4U; }\n"
                        "        else aster_trap(\"invalid UTF-8 string iteration\");\n"
                        "        if (i + n > l%" PRIu32 ".slice->length) "
                        "aster_trap(\"incomplete UTF-8 string iteration\");\n"
                        "        for (size_t o = 1U; o < n; ++o) { "
                        "unsigned char c = s[i + o]; "
                        "if ((c & 0xc0U) != 0x80U) "
                        "aster_trap(\"invalid UTF-8 string iteration\"); "
                        "cp = (cp << 6U) | (c & 0x3fU); }\n"
                        "        if ((n == 3U && cp < 0x800U) || "
                        "(n == 4U && cp < 0x10000U) || cp > 0x10ffffU || "
                        "(cp >= 0xd800U && cp <= 0xdfffU)) "
                        "aster_trap(\"invalid UTF-8 string iteration\");\n"
                        "        l%" PRIu32 ".index += n; v%" PRIu32 " = cp;\n"
                        "    }\n",
                        instruction->index, instruction->index,
                        instruction->index, instruction->index,
                        instruction->index, instruction->index,
                        instruction->index, instruction->result);
            else
                fprintf(output,
                        "    if (l%" PRIu32 ".index >= "
                        "l%" PRIu32 ".slice.length)\n"
                        "        aster_trap(\"iterator advanced past its end\");\n"
                        "    v%" PRIu32 " = (uint64_t)l%" PRIu32
                        ".slice.data[l%" PRIu32 ".index++];\n",
                        instruction->index, instruction->index,
                        instruction->result, instruction->index,
                        instruction->index);
            return;
        }
        case IR_OP_CLASS_DELETE: {
            IrValueId value = instruction->operands[0];
            IrTypeId type_id = function->value_types[value];
            fprintf(output, "    if (v%" PRIu32 " != NULL) {\n", value);
            fprintf(output,
                    "        switch (*((uint32_t *)(void *)v%" PRIu32 ")) {\n",
                    value);
            for (size_t runtime_id = 0U;
                 runtime_id < emitter->ir->type_count; ++runtime_id) {
                const IrType *runtime = &emitter->ir->types[runtime_id];
                if (runtime->shape != IR_TYPE_CLASS_REFERENCE) continue;
                bool subtype = runtime_type_assignable_to(
                    emitter->ir, (IrTypeId)runtime_id, type_id, 0U);
                if (!subtype) continue;
                fprintf(output, "        case UINT32_C(%zu):\n", runtime_id);
                if (runtime->destructor_function != IR_INVALID_ID) {
                    const IrFunction *destructor = &emitter->ir->functions[
                        runtime->destructor_function];
                    fprintf(output,
                            "            (void)aster_fn_%" PRIu32 "((",
                            runtime->destructor_function);
                    c_backend_emit_type(
                        emitter, destructor->parameters[0].type);
                    fprintf(output, ")v%" PRIu32 ");\n", value);
                }
                for (size_t field = runtime->field_count;
                     field > 0U; --field) {
                    IrTypeId field_type = runtime->field_types[field - 1U];
                    if (c_backend_type_needs_drop(emitter, field_type))
                        fprintf(output,
                                "            aster_drop_%" PRIu32
                                "(&((aster_type_%zu *)v%" PRIu32
                                ")->f%zu);\n",
                                field_type, runtime_id, value, field - 1U);
                }
                fputs("            break;\n", output);
            }
            fprintf(output,
                    "        default: aster_trap(\"invalid runtime class during delete\");\n"
                    "        }\n"
                    "        free(v%" PRIu32 ");\n"
                    "    }\n",
                    value);
            return;
        }
        case IR_OP_RAW_ALLOC: {
            IrTypeId size_type =
                function->value_types[instruction->operands[1]];
            if (emitter->ir->types[size_type].shape ==
                IR_TYPE_SIGNED_INT)
                fprintf(output,
                        "    if (v%" PRIu32 " < 0)\n"
                        "        aster_trap(\"arena allocation size must be non-negative\");\n",
                        instruction->operands[1]);
            fprintf(output, "    v%" PRIu32 " = (",
                    instruction->result);
            c_backend_emit_type(emitter, instruction->result_type);
            fprintf(output, ")aster_arena_alloc(v%" PRIu32
                    ", (size_t)v%" PRIu32 ");\n",
                    instruction->operands[0],
                    instruction->operands[1]);
            return;
        }
        case IR_OP_RAW_LOAD:
            fprintf(output,
                    "    if (v%" PRIu32 " == NULL)\n"
                    "        aster_trap(\"null pointer dereference\");\n"
                    "    v%" PRIu32 " = *v%" PRIu32 ";\n",
                    instruction->operands[0],
                    instruction->result,
                    instruction->operands[0]);
            return;
        case IR_OP_RAW_STORE:
            fprintf(output,
                    "    if (v%" PRIu32 " == NULL)\n"
                    "        aster_trap(\"null pointer dereference\");\n"
                    "    *v%" PRIu32 " = v%" PRIu32 ";\n",
                    instruction->operands[0],
                    instruction->operands[0],
                    instruction->operands[1]);
            if (instruction->result != IR_INVALID_ID)
                fprintf(output,
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->result);
            return;
        case IR_OP_CALL_NATIVE:
            c_backend_emit_native_instruction(
                emitter, function, instruction);
            return;
        case IR_OP_AWAIT:
            if (!function->is_async) {
                c_backend_unsupported(
                    emitter, instruction->span,
                    "await outside an async function");
                return;
            }
            c_backend_emit_async_await(
                emitter, function, instruction);
            return;
        default:
            (void)function;
            c_backend_unsupported(emitter, instruction->span,
                        "this typed IR instruction");
            return;
    }
}

void c_backend_emit_virtual_cleanup(
    CEmitter *emitter, const IrFunction *function,
    IrValueId preserved, const char *indent, bool clear
) {
    if (emitter->render_direct) return;
    FILE *output = emitter->output;
    for (size_t v = function->value_count; v > 0U; --v) {
        IrValueId value = (IrValueId)(v - 1U);
        if (value == preserved ||
            !c_backend_type_needs_drop(
                emitter, function->value_types[value]))
            continue;
        fprintf(output, "%sif (v%" PRIu32 "_live) ", indent, value);
        if (clear) fputs("{ ", output);
        c_backend_emit_drop_call(
            emitter, function->value_types[value], "v", value);
        if (clear)
            fprintf(output,
                    "; v%" PRIu32 "_live = false; }\n", value);
        else
            fputs(";\n", output);
    }
}

void c_backend_emit_terminator(CEmitter *emitter,
                               const IrFunction *function,
                               const IrTerminator *terminator) {
    FILE *output = emitter->output;
    if (function->is_async) {
        c_backend_emit_async_terminator(
            emitter, function, terminator);
        return;
    }
    if (emitter->render_direct)
        c_backend_flush_direct_builder_literals(emitter);
    switch (terminator->kind) {
        case IR_TERM_JUMP:
            fprintf(output, "    goto b%" PRIu32 ";\n",
                    terminator->target);
            break;
        case IR_TERM_BRANCH:
            {
            const IrInstruction *condition = c_backend_find_value_producer(
                function, terminator->value);
            if (!emitter->render_direct && condition != NULL &&
                condition->opcode == IR_OP_EXCEPTION_PENDING) {
                fprintf(output,
                        "    if (v%" PRIu32 ") {\n",
                        terminator->value);
                if (condition->index < function->value_count &&
                    c_backend_type_needs_drop(
                        emitter,
                        function->value_types[condition->index]))
                    fprintf(output,
                            "        v%" PRIu32 "_live = false;\n",
                            condition->index);
                c_backend_emit_virtual_cleanup(
                    emitter, function,
                    condition->index < function->value_count
                        ? condition->index : IR_INVALID_ID,
                    "        ", true);
                fprintf(output,
                        "        goto b%" PRIu32 ";\n"
                        "    } else goto b%" PRIu32 ";\n",
                        terminator->target, terminator->alternate);
            } else {
                fprintf(output,
                        "    if (v%" PRIu32 ") goto b%" PRIu32
                        "; else goto b%" PRIu32 ";\n",
                        terminator->value, terminator->target,
                        terminator->alternate);
            }
            }
            break;
        case IR_TERM_RETURN:
            if (emitter->render_direct)
                fputs("    return;\n", output);
            else
                fprintf(output, "    return v%" PRIu32 ";\n",
                        terminator->value);
            break;
        case IR_TERM_PROPAGATE_EXCEPTION:
            if (emitter->render_direct) {
                fputs("    return;\n", output);
            } else {
                fputs("    return (", output);
                c_backend_emit_type(emitter, function->return_type);
                fputs("){0};\n", output);
            }
            break;
        case IR_TERM_TRAP:
            fputs("    aster_trap(\"Aster runtime trap\");\n"
                  "    return (", output);
            c_backend_emit_type(emitter, function->return_type);
            fputs("){0};\n", output);
            break;
        case IR_TERM_NONE:
            c_backend_unsupported(emitter, terminator->span,
                        "an unterminated IR block");
            break;
    }
}
