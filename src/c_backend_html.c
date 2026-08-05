#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

const IrInstruction *c_backend_find_element_append_consumer(
    const IrFunction *function, IrValueId value) {
    const IrInstruction *consumer = NULL;
    size_t uses = 0U;
    for (size_t block = 0U; block < function->block_count; ++block)
        for (size_t index = 0U;
             index < function->blocks[block].instruction_count; ++index) {
            const IrInstruction *candidate =
                &function->blocks[block].instructions[index];
            for (size_t operand = 0U;
                 operand < candidate->operand_count; ++operand)
                if (candidate->operands[operand] == value) {
                    ++uses;
                    consumer = candidate;
                }
        }
    if (uses != 1U || consumer == NULL ||
        consumer->opcode != IR_OP_LOCAL_ELEMENT_APPEND ||
        consumer->operand_count != 1U)
        return NULL;
    return consumer;
}

bool c_backend_html_tag_is_void(const char *tag, size_t length) {
    static const char *const tags[] = {
        "area", "base", "br", "col", "embed", "hr", "img",
        "input", "link", "meta", "param", "source", "track", "wbr"
    };
    for (size_t i = 0U; i < sizeof(tags) / sizeof(tags[0]); ++i)
        if (strlen(tags[i]) == length &&
            memcmp(tags[i], tag, length) == 0)
            return true;
    return false;
}

bool c_backend_html_tag_is_fragment(const char *tag, size_t length) {
    return length == 9U && memcmp(tag, "#fragment", 9U) == 0;
}

static bool html_tag_is_raw_text(const char *tag, size_t length) {
    return (length == 6U && memcmp(tag, "script", 6U) == 0) ||
           (length == 5U && memcmp(tag, "style", 5U) == 0);
}

static size_t add_render_size(size_t left, size_t right) {
    return right > SIZE_MAX - left ? SIZE_MAX : left + right;
}

static const IrInstruction *find_local_element_begin(
    const IrFunction *function, uint32_t local) {
    for (size_t b = 0U; b < function->block_count; ++b)
        for (size_t i = 0U;
             i < function->blocks[b].instruction_count; ++i) {
            const IrInstruction *store =
                &function->blocks[b].instructions[i];
            if (store->opcode != IR_OP_LOCAL_STORE ||
                store->index != local || store->operand_count == 0U)
                continue;
            const IrInstruction *begin = c_backend_find_value_producer(
                function, store->operands[0]);
            if (begin != NULL && begin->opcode == IR_OP_ELEMENT_BEGIN)
                return begin;
        }
    return NULL;
}

bool c_backend_local_element_is_raw_text(
    const IrFunction *function, uint32_t local) {
    const IrInstruction *begin =
        find_local_element_begin(function, local);
    return begin != NULL &&
           html_tag_is_raw_text(
               begin->symbol, begin->symbol_length);
}

static size_t direct_render_static_bytes_inner(
    const IrModule *ir, size_t function_index, size_t depth) {
    if (function_index >= ir->function_count ||
        depth > ir->function_count)
        return 0U;
    const IrFunction *function = &ir->functions[function_index];
    size_t total = 0U;
    for (size_t b = 0U; b < function->block_count; ++b)
        for (size_t i = 0U;
             i < function->blocks[b].instruction_count; ++i) {
            const IrInstruction *instruction =
                &function->blocks[b].instructions[i];
            if (instruction->opcode == IR_OP_ELEMENT_BEGIN) {
                if (!c_backend_html_tag_is_fragment(
                        instruction->symbol,
                        instruction->symbol_length))
                    total = add_render_size(
                        total, instruction->symbol_length + 2U);
                continue;
            }
            if (instruction->opcode == IR_OP_LOCAL_ELEMENT_PROPERTY) {
                IrValueId value = instruction->operands[0];
                const IrType *type = &ir->types[
                    function->value_types[value]];
                if (type->shape == IR_TYPE_BOOL ||
                    type->shape == IR_TYPE_UNION)
                    continue;
                total = add_render_size(
                    total, instruction->symbol_length + 4U);
                const IrInstruction *producer = c_backend_find_value_producer(
                    function, value);
                if (type->shape == IR_TYPE_STRING_VIEW &&
                    producer != NULL &&
                    producer->opcode == IR_OP_CONST_STRING)
                    total = add_render_size(
                        total, c_backend_html_escaped_length(
                            producer->symbol,
                            producer->symbol_length, true));
                continue;
            }
            if (instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN) {
                total = add_render_size(
                    total, instruction->symbol_length + 3U);
                continue;
            }
            if (instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_PROPERTY_END) {
                total = add_render_size(total, 1U);
                continue;
            }
            if (instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT) {
                total = add_render_size(
                    total, instruction->symbol_length);
                continue;
            }
            if (instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND ||
                instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_CSS_VALUE ||
                instruction->opcode ==
                    IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED ||
                instruction->opcode == IR_OP_LOCAL_ELEMENT_APPEND) {
                IrValueId value = instruction->operands[0];
                const IrType *type = &ir->types[
                    function->value_types[value]];
                const IrInstruction *producer = c_backend_find_value_producer(
                    function, value);
                if (type->shape == IR_TYPE_STRING_VIEW &&
                    producer != NULL &&
                    producer->opcode == IR_OP_CONST_STRING)
                    total = add_render_size(
                        total, c_backend_html_escaped_length(
                            producer->symbol,
                            producer->symbol_length,
                            instruction->opcode ==
                                IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND));
                continue;
            }
            if (instruction->opcode == IR_OP_LOCAL_ELEMENT_FINISH) {
                const IrInstruction *begin = find_local_element_begin(
                    function, instruction->index);
                if (begin != NULL &&
                    !c_backend_html_tag_is_fragment(
                        begin->symbol, begin->symbol_length) &&
                    !c_backend_html_tag_is_void(
                        begin->symbol, begin->symbol_length))
                    total = add_render_size(
                        total, begin->symbol_length + 3U);
                continue;
            }
            if (instruction->opcode == IR_OP_CALL_DIRECT &&
                instruction->index < ir->function_count &&
                c_backend_function_supports_direct_render(
                    ir, instruction->index))
                total = add_render_size(
                    total, direct_render_static_bytes_inner(
                        ir, instruction->index, depth + 1U));
        }
    return total;
}

size_t c_backend_direct_render_initial_capacity(
    const IrModule *ir, size_t function_index) {
    size_t bytes = direct_render_static_bytes_inner(
        ir, function_index, 0U);
    size_t capacity = 64U;
    while (capacity < bytes && capacity <= SIZE_MAX / 2U)
        capacity *= 2U;
    return capacity < bytes ? bytes : capacity;
}

bool c_backend_value_is_borrowed_projection(
    const IrFunction *function, IrValueId value) {
    const IrInstruction *producer =
        c_backend_find_value_producer(function, value);
    if (producer != NULL)
        return producer->opcode == IR_OP_LOCAL_FIELD_BORROW;
    return false;
}

static const char *html_escape_replacement(
    unsigned char byte, bool attribute) {
    switch (byte) {
        case '&': return "&amp;";
        case '<': return "&lt;";
        case '>': return "&gt;";
        case '"': return attribute ? "&quot;" : NULL;
        case '\'': return attribute ? "&#39;" : NULL;
        default: return NULL;
    }
}

size_t c_backend_html_escaped_length(
    const char *data, size_t length, bool attribute) {
    size_t result = 0U;
    for (size_t i = 0U; i < length; ++i) {
        const char *replacement = html_escape_replacement(
            (unsigned char)data[i], attribute);
        size_t additional = replacement == NULL
            ? 1U : strlen(replacement);
        if (additional > SIZE_MAX - result) return SIZE_MAX;
        result += additional;
    }
    return result;
}

void c_backend_emit_html_escaped_byte_string(
    FILE *output, const char *data, size_t length, bool attribute) {
    fputs("(const unsigned char *)\"", output);
    for (size_t i = 0U; i < length; ++i) {
        const char *replacement = html_escape_replacement(
            (unsigned char)data[i], attribute);
        if (replacement == NULL) {
            fprintf(output, "\\x%02x",
                    (unsigned)(unsigned char)data[i]);
            continue;
        }
        for (size_t byte = 0U; replacement[byte] != '\0'; ++byte)
            fprintf(output, "\\x%02x",
                    (unsigned)(unsigned char)replacement[byte]);
    }
    fputc('"', output);
}

bool c_backend_emit_html_property_value(
    CEmitter *emitter, const IrInstruction *instruction,
    const IrType *type, const char *expression,
    const char *indent) {
    const char *function = NULL;
    if (type->shape == IR_TYPE_STRING_VIEW ||
        (type->shape == IR_TYPE_BUILTIN_OBJECT &&
         (strcmp(type->name, "string") == 0 ||
          strcmp(type->name, "Url") == 0)))
        function = "aster_html_property";
    else if (type->shape == IR_TYPE_BOOL)
        function = "aster_html_property_bool";
    else if (type->shape == IR_TYPE_SIGNED_INT)
        function = "aster_html_property_i64";
    else if (type->shape == IR_TYPE_UNSIGNED_INT ||
             type->shape == IR_TYPE_CHAR)
        function = "aster_html_property_u64";
    else if (type->shape == IR_TYPE_FLOAT)
        function = "aster_html_property_f64";
    else
        return false;

    fprintf(
        emitter->output, "%s%s(l%" PRIu32 ", (aster_str){",
        indent, function, instruction->index);
    c_backend_emit_byte_string(
        emitter->output, instruction->symbol,
        instruction->symbol_length);
    fprintf(
        emitter->output, ", %zuU}, ",
        instruction->symbol_length);
    if (type->shape == IR_TYPE_BUILTIN_OBJECT)
        fprintf(
            emitter->output,
            "aster_string_as_str((const aster_string *)%s)",
            expression);
    else if (type->shape == IR_TYPE_SIGNED_INT)
        fprintf(emitter->output, "(int64_t)(%s)", expression);
    else if (type->shape == IR_TYPE_UNSIGNED_INT ||
             type->shape == IR_TYPE_CHAR)
        fprintf(emitter->output, "(uint64_t)(%s)", expression);
    else if (type->shape == IR_TYPE_FLOAT)
        fprintf(emitter->output, "(double)(%s)", expression);
    else
        fputs(expression, emitter->output);
    fputs(");\n", emitter->output);

    if (type->shape == IR_TYPE_BUILTIN_OBJECT)
        fprintf(
            emitter->output,
            "%saster_string_drop((aster_string *)%s);\n",
            indent, expression);
    return true;
}

bool c_backend_emit_html_interpolation_value(
    CEmitter *emitter, const IrFunction *ir_function, const IrType *type,
    uint32_t local, IrValueId value,
    bool attribute, bool css_value) {
    const IrInstruction *producer =
        c_backend_find_value_producer(ir_function, value);
    if (type->shape == IR_TYPE_STRING_VIEW && producer != NULL &&
        producer->opcode == IR_OP_CONST_STRING) {
        bool raw_text = !attribute &&
            c_backend_local_element_is_raw_text(ir_function, local);
        size_t escaped_length = raw_text
            ? producer->symbol_length
            : c_backend_html_escaped_length(
                  producer->symbol, producer->symbol_length, attribute);
        if (escaped_length == SIZE_MAX) return false;
        fprintf(
            emitter->output,
            "    aster_html_interpolation_literal(l%" PRIu32
            ", (aster_str){",
            local);
        if (raw_text)
            c_backend_emit_byte_string(
                emitter->output, producer->symbol,
                producer->symbol_length);
        else
            c_backend_emit_html_escaped_byte_string(
                emitter->output, producer->symbol,
                producer->symbol_length, attribute);
        fprintf(
            emitter->output, ", %zuU}, %s);\n",
            escaped_length, attribute ? "true" : "false");
        return true;
    }
    const char *function = NULL;
    if (type->shape == IR_TYPE_STRING_VIEW ||
        (type->shape == IR_TYPE_BUILTIN_OBJECT &&
         strcmp(type->name, "string") == 0))
        function = css_value
            ? "aster_html_css_value_str"
            : "aster_html_interpolation_str";
    else if (type->shape == IR_TYPE_BOOL)
        function = "aster_html_interpolation_bool";
    else if (type->shape == IR_TYPE_SIGNED_INT)
        function = "aster_html_interpolation_i64";
    else if (type->shape == IR_TYPE_UNSIGNED_INT ||
             type->shape == IR_TYPE_CHAR)
        function = "aster_html_interpolation_u64";
    else if (type->shape == IR_TYPE_FLOAT)
        function = "aster_html_interpolation_f64";
    else
        return false;

    fprintf(
        emitter->output,
        "    %s(l%" PRIu32 ", ",
        function, local);
    if (type->shape == IR_TYPE_BUILTIN_OBJECT)
        fprintf(
            emitter->output,
            "aster_string_as_str((const aster_string *)v%" PRIu32 ")",
            value);
    else
        fprintf(emitter->output, "v%" PRIu32, value);
    fprintf(
        emitter->output, ", %s);\n",
        attribute ? "true" : "false");
    if (type->shape == IR_TYPE_BUILTIN_OBJECT)
        fprintf(
            emitter->output,
            "    aster_string_drop((aster_string *)v%" PRIu32 ");\n",
            value);
    return true;
}

void c_backend_emit_direct_builder_literal(
    FILE *output, const char *data, size_t length) {
    fputs("    aster_builder_append(render_builder, (aster_str){",
          output);
    c_backend_emit_byte_string(output, data, length);
    fprintf(output, ", %zuU});\n", length);
}

void c_backend_emit_direct_close_open(CEmitter *emitter, uint32_t local) {
    fprintf(
        emitter->output,
        "    if (l%" PRIu32 "_direct_open) {\n"
        "        aster_builder_append(render_builder, (aster_str){"
        "(const unsigned char *)\">\", 1U});\n"
        "        l%" PRIu32 "_direct_open = false;\n"
        "    }\n",
        local, local);
}

static bool emit_direct_html_expression(
    CEmitter *emitter, const IrType *type, const char *expression,
    bool attribute, bool raw_text, bool consume) {
    if (type->shape == IR_TYPE_STRING_VIEW) {
        if (raw_text)
            fprintf(emitter->output,
                    "    aster_builder_append(render_builder, %s);\n",
                    expression);
        else
            fprintf(emitter->output,
                    "    aster_builder_append_html_escaped(render_builder, "
                    "%s, %s);\n",
                    expression, attribute ? "true" : "false");
        return true;
    }
    if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
        (strcmp(type->name, "string") == 0 ||
         strcmp(type->name, "Url") == 0)) {
        if (raw_text)
            fprintf(emitter->output,
                    "    aster_builder_append(render_builder, "
                    "aster_string_as_str((const aster_string *)%s));\n",
                    expression);
        else
            fprintf(emitter->output,
                    "    aster_builder_append_html_escaped(render_builder, "
                    "aster_string_as_str((const aster_string *)%s), %s);\n",
                    expression, attribute ? "true" : "false");
        if (consume)
            fprintf(emitter->output,
                    "    aster_string_drop((aster_string *)%s);\n",
                    expression);
        return true;
    }
    const char *append = NULL;
    if (type->shape == IR_TYPE_BOOL)
        append = "aster_builder_append_bool";
    else if (type->shape == IR_TYPE_SIGNED_INT)
        append = "aster_builder_append_i64";
    else if (type->shape == IR_TYPE_UNSIGNED_INT ||
             type->shape == IR_TYPE_CHAR)
        append = "aster_builder_append_u64";
    else if (type->shape == IR_TYPE_FLOAT)
        append = "aster_builder_append_f64";
    if (append == NULL) return false;
    fprintf(emitter->output, "    %s(render_builder, %s);\n",
            append, expression);
    return true;
}

bool c_backend_emit_direct_html_value(
    CEmitter *emitter, const IrFunction *function,
    const IrType *type, IrValueId value, bool attribute,
    bool raw_text) {
    const IrInstruction *producer =
        c_backend_find_value_producer(function, value);
    if (type->shape == IR_TYPE_STRING_VIEW && producer != NULL &&
        producer->opcode == IR_OP_CONST_STRING) {
        size_t escaped_length = raw_text
            ? producer->symbol_length
            : c_backend_html_escaped_length(
                  producer->symbol, producer->symbol_length, attribute);
        if (escaped_length == SIZE_MAX) return false;
        fputs("    aster_builder_append(render_builder, (aster_str){",
              emitter->output);
        if (raw_text)
            c_backend_emit_byte_string(
                emitter->output, producer->symbol,
                producer->symbol_length);
        else
            c_backend_emit_html_escaped_byte_string(
                emitter->output, producer->symbol,
                producer->symbol_length, attribute);
        fprintf(emitter->output, ", %zuU});\n", escaped_length);
        return true;
    }
    char expression[48];
    (void)snprintf(expression, sizeof(expression),
                   "v%" PRIu32, value);
    return emit_direct_html_expression(
        emitter, type, expression, attribute, raw_text, true);
}
