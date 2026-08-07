#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool same_source_span(LangSpan left, LangSpan right) {
    return left.file == right.file &&
           left.start == right.start &&
           left.end == right.end;
}

void c_backend_emit_element_instruction(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction
) {
    FILE *output = emitter->output;
    switch (instruction->opcode) {
        case IR_OP_ELEMENT_BEGIN:
            if (emitter->render_direct) {
                if (instruction->index != IR_INVALID_ID)
                    c_backend_emit_direct_close_open(emitter, instruction->index);
                fprintf(
                    output,
                    "    v%" PRIu32 " = (aster_element_builder *)"
                    "render_builder;\n",
                    instruction->result);
                if (!c_backend_html_tag_is_fragment(
                        instruction->symbol,
                        instruction->symbol_length)) {
                    c_backend_emit_direct_builder_literal(output, "<", 1U);
                    c_backend_emit_direct_builder_literal(
                        output, instruction->symbol,
                        instruction->symbol_length);
                }
                return;
            }
            if (emitter->render_into &&
                instruction->index == IR_INVALID_ID &&
                same_source_span(
                    instruction->span,
                    emitter->render_root_span))
                fprintf(
                    output,
                    "    v%" PRIu32
                    " = aster_html_begin_child("
                    "render_destination, (aster_str){",
                    instruction->result);
            else if (instruction->index == IR_INVALID_ID)
                fprintf(
                    output,
                    "    v%" PRIu32
                    " = aster_html_begin((aster_str){",
                    instruction->result);
            else
                fprintf(
                    output,
                    "    v%" PRIu32
                    " = aster_html_begin_child(l%" PRIu32
                    ", (aster_str){",
                    instruction->result, instruction->index);
            c_backend_emit_byte_string(output, instruction->symbol,
                             instruction->symbol_length);
            fprintf(output, ", %zuU});\n", instruction->symbol_length);
            return;
        case IR_OP_LOCAL_ELEMENT_PROPERTY: {
            IrValueId value = instruction->operands[0];
            const IrType *value_type =
                &emitter->ir->types[function->value_types[value]];
            const IrInstruction *producer =
                c_backend_find_value_producer(function, value);
            if (emitter->render_direct) {
                if (value_type->shape == IR_TYPE_BOOL) {
                    fprintf(output,
                            "    if (v%" PRIu32 ") {\n", value);
                    c_backend_emit_direct_builder_literal(output, " ", 1U);
                    c_backend_emit_direct_builder_literal(
                        output, instruction->symbol,
                        instruction->symbol_length);
                    fputs("    }\n", output);
                    return;
                }
                if (value_type->shape == IR_TYPE_UNION) {
                    size_t some = value_type->variant_count;
                    for (size_t variant = 0U;
                         variant < value_type->variant_count; ++variant)
                        if (value_type->variant_names[variant] != NULL &&
                            strcmp(value_type->variant_names[variant],
                                   "Some") == 0) {
                            some = variant;
                            break;
                        }
                    if (some < value_type->variant_count &&
                        value_type->variant_payload_types[some] !=
                            IR_INVALID_ID) {
                        const IrType *payload = &emitter->ir->types[
                            value_type->variant_payload_types[some]];
                        if (payload->shape == IR_TYPE_BOOL) {
                            fprintf(output,
                                    "    if (v%" PRIu32
                                    ".tag == UINT32_C(%zu) && "
                                    "v%" PRIu32 ".payload.v%zu) {\n",
                                    value, some, value, some);
                            c_backend_emit_direct_builder_literal(output, " ", 1U);
                            c_backend_emit_direct_builder_literal(
                                output, instruction->symbol,
                                instruction->symbol_length);
                            fputs("    }\n", output);
                            return;
                        }
                        fprintf(output,
                                "    if (v%" PRIu32
                                ".tag == UINT32_C(%zu)) {\n",
                                value, some);
                        c_backend_emit_direct_builder_literal(output, " ", 1U);
                        c_backend_emit_direct_builder_literal(
                            output, instruction->symbol,
                            instruction->symbol_length);
                        c_backend_emit_direct_builder_literal(output, "=\"", 2U);
                        char payload_expression[96];
                        (void)snprintf(
                            payload_expression, sizeof(payload_expression),
                            "v%" PRIu32 ".payload.v%zu", value, some);
                        const char *append = NULL;
                        if (payload->shape == IR_TYPE_STRING_VIEW) {
                            fprintf(output,
                                    "    aster_builder_append_html_escaped("
                                    "render_builder, %s, true);\n",
                                    payload_expression);
                        } else if (payload->shape == IR_TYPE_BUILTIN_OBJECT &&
                                   (strcmp(payload->name, "string") == 0 ||
                                    strcmp(payload->name, "Url") == 0)) {
                            fprintf(output,
                                    "    aster_builder_append_html_escaped("
                                    "render_builder, aster_string_as_str("
                                    "(const aster_string *)%s), true);\n"
                                    "    aster_string_drop((aster_string *)%s);\n",
                                    payload_expression, payload_expression);
                        } else if (payload->shape == IR_TYPE_SIGNED_INT)
                            append = "aster_builder_append_i64";
                        else if (payload->shape == IR_TYPE_UNSIGNED_INT ||
                                 payload->shape == IR_TYPE_CHAR)
                            append = "aster_builder_append_u64";
                        else if (payload->shape == IR_TYPE_FLOAT)
                            append = "aster_builder_append_f64";
                        if (append != NULL)
                            fprintf(output, "    %s(render_builder, %s);\n",
                                    append, payload_expression);
                        else if (payload->shape != IR_TYPE_STRING_VIEW &&
                                 payload->shape != IR_TYPE_BUILTIN_OBJECT) {
                            c_backend_unsupported(
                                emitter, instruction->span,
                                "this optional direct element property type");
                            return;
                        }
                        c_backend_emit_direct_builder_literal(output, "\"", 1U);
                        fputs("    }\n", output);
                        return;
                    }
                }
                c_backend_emit_direct_builder_literal(output, " ", 1U);
                c_backend_emit_direct_builder_literal(
                    output, instruction->symbol,
                    instruction->symbol_length);
                c_backend_emit_direct_builder_literal(output, "=\"", 2U);
                if (!c_backend_emit_direct_html_value(
                        emitter, function, value_type, value, true,
                        false)) {
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "this direct element property value type");
                    return;
                }
                c_backend_emit_direct_builder_literal(output, "\"", 1U);
                return;
            }
            if (value_type->shape == IR_TYPE_STRING_VIEW &&
                producer != NULL &&
                producer->opcode == IR_OP_CONST_STRING) {
                size_t escaped_length = c_backend_html_escaped_length(
                    producer->symbol, producer->symbol_length, true);
                if (escaped_length == SIZE_MAX) {
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "an element property value this large");
                    return;
                }
                fprintf(
                    output,
                    "    aster_html_property_literal(l%" PRIu32
                    ", (aster_str){",
                    instruction->index);
                c_backend_emit_byte_string(
                    output, instruction->symbol,
                    instruction->symbol_length);
                fputs(", ", output);
                fprintf(output, "%zuU}, (aster_str){",
                        instruction->symbol_length);
                c_backend_emit_html_escaped_byte_string(
                    output, producer->symbol,
                    producer->symbol_length, true);
                fprintf(output, ", %zuU});\n", escaped_length);
                return;
            }
            char expression[96];
            (void)snprintf(
                expression, sizeof(expression),
                "v%" PRIu32, value);
            if (c_backend_emit_html_property_value(
                    emitter, instruction, value_type,
                    expression, "    "))
                return;
            if (value_type->shape == IR_TYPE_UNION) {
                size_t some = value_type->variant_count;
                for (size_t variant = 0U;
                     variant < value_type->variant_count; ++variant)
                    if (value_type->variant_names[variant] != NULL &&
                        strcmp(
                            value_type->variant_names[variant],
                            "Some") == 0) {
                        some = variant;
                        break;
                    }
                if (some < value_type->variant_count &&
                    value_type->variant_payload_types[some] !=
                        IR_INVALID_ID) {
                    IrTypeId payload_id =
                        value_type->variant_payload_types[some];
                    fprintf(
                        output,
                        "    if (v%" PRIu32
                        ".tag == UINT32_C(%zu)) {\n",
                        value, some);
                    (void)snprintf(
                        expression, sizeof(expression),
                        "v%" PRIu32 ".payload.v%zu",
                        value, some);
                    if (c_backend_emit_html_property_value(
                            emitter, instruction,
                            &emitter->ir->types[payload_id],
                            expression, "        ")) {
                        fputs("    }\n", output);
                        return;
                    }
                    fputs("    }\n", output);
                }
            }
            {
                c_backend_unsupported(emitter, instruction->span,
                            "this element property value type");
            }
            return;
        }
        case IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN:
            if (emitter->render_direct) {
                c_backend_emit_direct_builder_literal(output, " ", 1U);
                c_backend_emit_direct_builder_literal(
                    output, instruction->symbol,
                    instruction->symbol_length);
                c_backend_emit_direct_builder_literal(output, "=\"", 2U);
                return;
            }
            fprintf(
                output,
                "    aster_html_interpolation_begin(l%" PRIu32
                ", (aster_str){",
                instruction->index);
            c_backend_emit_byte_string(
                output, instruction->symbol,
                instruction->symbol_length);
            fprintf(
                output, ", %zuU});\n",
                instruction->symbol_length);
            return;
        case IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND: {
            IrValueId value = instruction->operands[0];
            const IrType *value_type =
                &emitter->ir->types[
                    function->value_types[value]];
            bool emitted = emitter->render_direct
                ? c_backend_emit_direct_html_value(
                    emitter, function, value_type, value, true,
                    false)
                : c_backend_emit_html_interpolation_value(
                    emitter, function, value_type,
                    instruction->index, value, true, false);
            if (!emitted)
                c_backend_unsupported(
                    emitter, instruction->span,
                    "this interpolated attribute value type");
            return;
        }
        case IR_OP_LOCAL_ELEMENT_CSS_VALUE: {
            IrValueId value = instruction->operands[0];
            const IrType *value_type =
                &emitter->ir->types[
                    function->value_types[value]];
            if (emitter->render_direct &&
                (value_type->shape == IR_TYPE_STRING_VIEW ||
                 (value_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                  strcmp(value_type->name, "string") == 0))) {
                fprintf(output, "    if (!aster_css_atom(");
                if (value_type->shape == IR_TYPE_BUILTIN_OBJECT)
                    fprintf(
                        output,
                        "aster_string_as_str((const aster_string *)v%" PRIu32 ")",
                        value);
                else
                    fprintf(output, "v%" PRIu32, value);
                fputs(
                    ")) aster_trap(\"unsafe CSS custom property value\");\n",
                    output);
            }
            bool emitted = emitter->render_direct
                ? c_backend_emit_direct_html_value(
                    emitter, function, value_type, value, true, false)
                : c_backend_emit_html_interpolation_value(
                    emitter, function, value_type,
                    instruction->index, value, true, true);
            if (!emitted)
                c_backend_unsupported(
                    emitter, instruction->span,
                    "this CSS custom property value type");
            return;
        }
        case IR_OP_LOCAL_ELEMENT_PROPERTY_END:
            if (emitter->render_direct) {
                c_backend_emit_direct_builder_literal(output, "\"", 1U);
                return;
            }
            fprintf(
                output,
                "    aster_html_interpolation_end(l%" PRIu32 ");\n",
                instruction->index);
            return;
        case IR_OP_LOCAL_ELEMENT_APPEND: {
            IrValueId child = instruction->operands[0];
            const IrType *child_type =
                &emitter->ir->types[function->value_types[child]];
            if (emitter->render_direct) {
                c_backend_emit_direct_close_open(emitter, instruction->index);
                if (child_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                    strcmp(child_type->name, "Html") == 0) {
                    fprintf(output,
                            "    if (v%" PRIu32 " != (aster_html *)"
                            "render_builder) {\n"
                            "        aster_string *direct_html_%" PRIu32
                            " = aster_html_render(v%" PRIu32 ");\n"
                            "        aster_builder_append(render_builder, "
                            "aster_string_as_str(direct_html_%" PRIu32
                            "));\n"
                            "        aster_string_drop(direct_html_%" PRIu32
                            ");\n"
                            "    }\n",
                            child, child, child, child, child);
                    return;
                }
                if (child_type->element_child_collection) {
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "a collection in direct HTML rendering");
                    return;
                }
                if (!c_backend_emit_direct_html_value(
                        emitter, function, child_type, child, false,
                        c_backend_local_element_is_raw_text(
                            function, instruction->index)))
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "this direct element child value type");
                return;
            }
            if (child_type->shape == IR_TYPE_STRING_VIEW)
                fprintf(output,
                        "    aster_html_append_text(l%" PRIu32
                        ", v%" PRIu32 ");\n",
                        instruction->index, child);
            else if (child_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                     strcmp(child_type->name, "string") == 0)
                fprintf(output,
                        "    aster_html_append_owned_text(l%" PRIu32
                        ", v%" PRIu32 ");\n",
                        instruction->index, child);
            else if (child_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                     strcmp(child_type->name, "Html") == 0)
                fprintf(output,
                        "    aster_html_append_html(l%" PRIu32
                        ", v%" PRIu32 ");\n",
                        instruction->index, child);
            else if (child_type->element_child_collection &&
                     child_type->element_type < emitter->ir->type_count) {
                const IrType *element =
                    &emitter->ir->types[child_type->element_type];
                const char *append = NULL;
                if (element->shape == IR_TYPE_STRING_VIEW)
                    append = "aster_html_append_text";
                else if (element->shape == IR_TYPE_BUILTIN_OBJECT &&
                         strcmp(element->name, "string") == 0)
                    append = "aster_html_append_owned_text";
                else if (element->shape == IR_TYPE_BUILTIN_OBJECT &&
                         strcmp(element->name, "Html") == 0)
                    append = "aster_html_append_html";
                if (append == NULL) {
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "this element collection child type");
                    return;
                }
                if (child_type->shape == IR_TYPE_ARRAY) {
                    fprintf(
                        output,
                        "    for (size_t child_index = 0U; "
                        "child_index < %zuU; ++child_index)\n"
                        "        %s(l%" PRIu32 ", v%" PRIu32
                        ".items[child_index]);\n",
                        child_type->array_length, append,
                        instruction->index, child);
                } else if (c_backend_type_is_vec(child_type)) {
                    fprintf(
                        output,
                        "    for (size_t child_index = 0U; "
                        "child_index < v%" PRIu32
                        "->length; ++child_index)\n"
                        "        %s(l%" PRIu32 ", v%" PRIu32
                        "->data[child_index]);\n"
                        "    free(v%" PRIu32 "->data);\n"
                        "    free(v%" PRIu32 ");\n",
                        child, append, instruction->index, child,
                        child, child);
                } else if (child_type->shape == IR_TYPE_UNION) {
                    size_t some = child_type->variant_count;
                    for (size_t variant = 0U;
                         variant < child_type->variant_count; ++variant)
                        if (child_type->variant_names[variant] != NULL &&
                            strcmp(
                                child_type->variant_names[variant],
                                "Some") == 0) {
                            some = variant;
                            break;
                        }
                    if (some == child_type->variant_count) {
                        c_backend_unsupported(
                            emitter, instruction->span,
                            "this optional element collection");
                        return;
                    }
                    fprintf(
                        output,
                        "    if (v%" PRIu32 ".tag == UINT32_C(%zu))\n"
                        "        %s(l%" PRIu32 ", v%" PRIu32
                        ".payload.v%zu);\n",
                        child, some, append, instruction->index,
                        child, some);
                } else {
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "this element child collection");
                }
            }
            else
                c_backend_unsupported(emitter, instruction->span,
                            "this element child value type");
            return;
        }
        case IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED: {
            IrValueId value = instruction->operands[0];
            const IrType *value_type =
                &emitter->ir->types[
                    function->value_types[value]];
            if (emitter->render_direct)
                c_backend_emit_direct_close_open(emitter, instruction->index);
            bool emitted = emitter->render_direct
                ? c_backend_emit_direct_html_value(
                    emitter, function, value_type, value, false,
                    c_backend_local_element_is_raw_text(
                        function, instruction->index))
                : c_backend_emit_html_interpolation_value(
                    emitter, function, value_type,
                    instruction->index, value, false, false);
            if (!emitted)
                c_backend_unsupported(
                    emitter, instruction->span,
                    "this interpolated text value type");
            return;
        }
        case IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT:
            if (instruction->auxiliary != 0U) {
                if (emitter->render_direct) {
                    c_backend_emit_direct_close_open(
                        emitter, instruction->index);
                    c_backend_emit_direct_builder_literal(
                        output, instruction->symbol,
                        instruction->symbol_length);
                } else {
                    fprintf(output,
                            "    aster_html_append_html(l%" PRIu32
                            ", aster_html_unsafe_raw((aster_str){",
                            instruction->index);
                    c_backend_emit_byte_string(
                        output, instruction->symbol,
                        instruction->symbol_length);
                    fprintf(output, ", %zuU}));\n",
                            instruction->symbol_length);
                }
                return;
            }
            if (emitter->render_direct) {
                c_backend_emit_direct_close_open(emitter, instruction->index);
                if (c_backend_local_element_is_raw_text(
                        function, instruction->index)) {
                    c_backend_emit_direct_builder_literal(
                        output, instruction->symbol,
                        instruction->symbol_length);
                } else {
                    size_t escaped_length = c_backend_html_escaped_length(
                        instruction->symbol,
                        instruction->symbol_length, false);
                    fputs(
                        "    aster_builder_append(render_builder, "
                        "(aster_str){",
                        output);
                    c_backend_emit_html_escaped_byte_string(
                        output, instruction->symbol,
                        instruction->symbol_length, false);
                    fprintf(output, ", %zuU});\n", escaped_length);
                }
                return;
            }
            fprintf(
                output,
                "    aster_html_append_text(l%" PRIu32
                ", (aster_str){",
                instruction->index);
            c_backend_emit_byte_string(
                output, instruction->symbol,
                instruction->symbol_length);
            fprintf(output, ", %zuU});\n",
                    instruction->symbol_length);
            return;
        case IR_OP_LOCAL_ELEMENT_FINISH:
            if (emitter->render_direct) {
                c_backend_emit_direct_close_open(emitter, instruction->index);
                if (instruction->index < function->local_count &&
                    emitter->direct_local_tags != NULL &&
                    emitter->direct_local_tags[instruction->index] != NULL &&
                    emitter->direct_local_tag_lengths[instruction->index] != 0U &&
                    !c_backend_html_tag_is_fragment(
                        emitter->direct_local_tags[instruction->index],
                        emitter->direct_local_tag_lengths[
                            instruction->index]) &&
                    !c_backend_html_tag_is_void(
                        emitter->direct_local_tags[instruction->index],
                        emitter->direct_local_tag_lengths[
                            instruction->index])) {
                    c_backend_emit_direct_builder_literal(output, "</", 2U);
                    c_backend_emit_direct_builder_literal(
                        output,
                        emitter->direct_local_tags[instruction->index],
                        emitter->direct_local_tag_lengths[
                            instruction->index]);
                    c_backend_emit_direct_builder_literal(output, ">", 1U);
                }
                fprintf(
                    output,
                    "    v%" PRIu32 " = (aster_html *)render_builder;\n"
                    "    l%" PRIu32 "_direct_open = false;\n",
                    instruction->result, instruction->index);
                return;
            }
            fprintf(output,
                    "    v%" PRIu32 " = aster_html_finish(l%" PRIu32
                    ");\n",
                    instruction->result, instruction->index);
            if (c_backend_local_tracks_drop(
                    emitter, function, instruction->index))
                fprintf(output,
                        "    l%" PRIu32 "_live = false;\n",
                        instruction->index);
            return;
        default:
            c_backend_unsupported(
                emitter, instruction->span,
                "this element IR instruction");
            return;
    }
}
