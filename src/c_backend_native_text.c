#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

bool c_backend_emit_native_text(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction
) {
    FILE *output = emitter->output;
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "String::from") == 0) {
        const IrType *source = &emitter->ir->types[
            function->value_types[instruction->operands[0]]];
        const IrInstruction *producer =
            c_backend_find_value_producer(
                function, instruction->operands[0]);
        if (source->shape == IR_TYPE_BUILTIN_OBJECT &&
            strcmp(source->name, "string") == 0)
            fprintf(output,
                    "    v%" PRIu32 " = v%" PRIu32 ";\n",
                    instruction->result,
                    instruction->operands[0]);
        else if (producer != NULL &&
                 producer->opcode == IR_OP_CONST_STRING) {
            fprintf(output,
                    "    static aster_string literal_%" PRIu32
                    " = {0U, ", instruction->result);
            if (producer->symbol_length == 0U) {
                fputs("NULL, 0U};\n", output);
            } else {
                fputs("(unsigned char *)\"", output);
                for (size_t i = 0U;
                     i < producer->symbol_length; ++i)
                    fprintf(output, "\\x%02x",
                            (unsigned)(unsigned char)
                                producer->symbol[i]);
                fprintf(output, "\", %zuU};\n",
                        producer->symbol_length);
            }
            fprintf(output,
                    "    v%" PRIu32 " = &literal_%" PRIu32 ";\n",
                    instruction->result, instruction->result);
        }
        else
            fprintf(output,
                    "    v%" PRIu32
                    " = aster_string_from(v%" PRIu32 ");\n",
                    instruction->result,
                    instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "StringBuilder::New") == 0) {
        fprintf(output,
                "    v%" PRIu32
                " = aster_builder_new();\n",
                instruction->result);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol,
               "StringBuilder::Append") == 0) {
        const IrType *text_type = &emitter->ir->types[
            function->value_types[instruction->operands[1]]];
        if (text_type->shape == IR_TYPE_BUILTIN_OBJECT &&
            strcmp(text_type->name, "string") == 0) {
            fprintf(output,
                    "    aster_builder_append(v%" PRIu32
                    ", aster_string_as_str(v%" PRIu32 "));\n"
                    "    aster_string_drop(v%" PRIu32 ");\n",
                    instruction->operands[0],
                    instruction->operands[1],
                    instruction->operands[1]);
        } else if (text_type->shape == IR_TYPE_SIGNED_INT)
            fprintf(output,
                    "    aster_builder_append_i64(v%" PRIu32
                    ", v%" PRIu32 ");\n",
                    instruction->operands[0],
                    instruction->operands[1]);
        else if (text_type->shape == IR_TYPE_CHAR)
            fprintf(output,
                    "    { uint64_t cp = v%" PRIu32 "; "
                    "unsigned char b[4]; size_t n; "
                    "if (cp <= 0x7fU) { b[0]=(unsigned char)cp; n=1U; } "
                    "else if (cp <= 0x7ffU) { b[0]=(unsigned char)(0xc0U|(cp>>6U)); "
                    "b[1]=(unsigned char)(0x80U|(cp&0x3fU)); n=2U; } "
                    "else if (cp <= 0xffffU && "
                    "!(cp>=0xd800U&&cp<=0xdfffU)) { "
                    "b[0]=(unsigned char)(0xe0U|(cp>>12U)); "
                    "b[1]=(unsigned char)(0x80U|((cp>>6U)&0x3fU)); "
                    "b[2]=(unsigned char)(0x80U|(cp&0x3fU)); n=3U; } "
                    "else if (cp <= 0x10ffffU) { "
                    "b[0]=(unsigned char)(0xf0U|(cp>>18U)); "
                    "b[1]=(unsigned char)(0x80U|((cp>>12U)&0x3fU)); "
                    "b[2]=(unsigned char)(0x80U|((cp>>6U)&0x3fU)); "
                    "b[3]=(unsigned char)(0x80U|(cp&0x3fU)); n=4U; } "
                    "else aster_trap(\"invalid Unicode scalar\"); "
                    "aster_builder_append(v%" PRIu32
                    ", (aster_str){b,n}); }\n",
                    instruction->operands[1],
                    instruction->operands[0]);
        else if (text_type->shape == IR_TYPE_UNSIGNED_INT)
            fprintf(output,
                    "    aster_builder_append_u64(v%" PRIu32
                    ", v%" PRIu32 ");\n",
                    instruction->operands[0],
                    instruction->operands[1]);
        else if (text_type->shape == IR_TYPE_FLOAT)
            fprintf(output,
                    "    aster_builder_append_f64(v%" PRIu32
                    ", v%" PRIu32 ");\n",
                    instruction->operands[0],
                    instruction->operands[1]);
        else if (text_type->shape == IR_TYPE_BOOL)
            fprintf(output,
                    "    aster_builder_append_bool(v%" PRIu32
                    ", v%" PRIu32 ");\n",
                    instruction->operands[0],
                    instruction->operands[1]);
        else
            fprintf(output,
                    "    aster_builder_append(v%" PRIu32
                    ", v%" PRIu32 ");\n",
                    instruction->operands[0],
                    instruction->operands[1]);
        fprintf(output,
                "    v%" PRIu32 " = UINT8_C(0);\n",
                instruction->result);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol,
               "StringBuilder::AppendByte") == 0) {
        fprintf(output,
                "    unsigned char builder_byte_%" PRIu32
                " = (unsigned char)v%" PRIu32 ";\n"
                "    aster_builder_append(v%" PRIu32
                ", (aster_str){&builder_byte_%" PRIu32
                ", 1U});\n"
                "    v%" PRIu32 " = UINT8_C(0);\n",
                instruction->result,
                instruction->operands[1],
                instruction->operands[0],
                instruction->result,
                instruction->result);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol,
               "StringBuilder::Finish") == 0) {
        fprintf(output,
                "    v%" PRIu32
                " = aster_builder_finish(v%" PRIu32 ");\n",
                instruction->result,
                instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol,
               "StringBuilder::ToString") == 0) {
        fprintf(output,
                "    v%" PRIu32
                " = aster_string_from((aster_str){v%" PRIu32
                "->data, v%" PRIu32 "->length});\n",
                instruction->result,
                instruction->operands[0],
                instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol,
               "StringBuilder::Length") == 0) {
        fprintf(output,
                "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                "->length;\n",
                instruction->result,
                instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol,
               "StringBuilder::Clear") == 0) {
        fprintf(output,
                "    v%" PRIu32 "->length = 0U;\n"
                "    v%" PRIu32 " = UINT8_C(0);\n",
                instruction->operands[0],
                instruction->result);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "StringView") == 0) {
        fprintf(output,
                "    v%" PRIu32
                " = aster_string_as_str(v%" PRIu32 ");\n",
                instruction->result,
                instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        (strcmp(instruction->symbol, "Url::relative") == 0 ||
         strcmp(instruction->symbol, "Url::fragment") == 0)) {
        const IrType *source_type =
            &emitter->ir->types[
                function->value_types[
                    instruction->operands[0]]];
        if (strcmp(
                instruction->symbol,
                "Url::relative") == 0 &&
            source_type->shape ==
                IR_TYPE_BUILTIN_OBJECT &&
            source_type->name != NULL &&
            strcmp(source_type->name, "string") == 0) {
            fprintf(
                output,
                "    v%" PRIu32
                " = (aster_url *)v%" PRIu32 ";\n",
                instruction->result,
                instruction->operands[0]);
            return true;
        }
        fprintf(output,
                "    v%" PRIu32 " = %s(%sv%" PRIu32 "%s);\n",
                instruction->result,
                strcmp(instruction->symbol, "Url::fragment") == 0
                    ? "aster_url_fragment"
                    : "aster_url_relative",
                source_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                strcmp(source_type->name, "string") == 0
                    ? "aster_string_as_str(" : "",
                instruction->operands[0],
                source_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                strcmp(source_type->name, "string") == 0
                    ? ")" : "");
        if (source_type->shape == IR_TYPE_BUILTIN_OBJECT &&
            strcmp(source_type->name, "string") == 0)
            fprintf(output,
                    "    aster_string_drop(v%" PRIu32 ");\n",
                    instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "Html::ToHtmlString") == 0) {
        const IrInstruction *producer = c_backend_find_value_producer(
            function, instruction->operands[0]);
        if (!emitter->render_direct && producer != NULL &&
            producer->opcode == IR_OP_CALL_DIRECT &&
            producer->index < emitter->ir->function_count &&
            c_backend_function_supports_direct_render(
                emitter->ir, producer->index) &&
            c_backend_find_direct_render_consumer(
                function, producer->result) == instruction) {
            fprintf(output, "    v%" PRIu32
                    " = aster_fn_%" PRIu32 "_render(",
                    instruction->result, producer->index);
            emit_call_operands(
                emitter, function, producer, 0U);
            fputs(");\n", output);
            return true;
        }
        fprintf(output,
                "    v%" PRIu32
                " = aster_html_to_string(v%" PRIu32 ");\n",
                instruction->result,
                instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "Html::UnsafeRaw") == 0) {
        fprintf(output,
                "    v%" PRIu32
                " = aster_html_unsafe_raw(aster_string_as_str("
                "v%" PRIu32 "));\n"
                "    aster_string_drop(v%" PRIu32 ");\n",
                instruction->result,
                instruction->operands[0],
                instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "HttpPathMatches") == 0) {
        fprintf(output,
                "    v%" PRIu32
                " = aster_http_path_matches(aster_string_as_str(v%" PRIu32
                "), aster_string_as_str(v%" PRIu32 "));\n"
                "    aster_string_drop(v%" PRIu32 ");\n"
                "    aster_string_drop(v%" PRIu32 ");\n",
                instruction->result,
                instruction->operands[0],
                instruction->operands[1],
                instruction->operands[0],
                instruction->operands[1]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "HttpPathParam") == 0) {
        fprintf(output,
                "    v%" PRIu32
                " = aster_string_from(aster_http_path_param("
                "aster_string_as_str(v%" PRIu32 "), "
                "aster_string_as_str(v%" PRIu32 "), "
                "aster_string_as_str(v%" PRIu32 ")));\n"
                "    aster_string_drop(v%" PRIu32 ");\n"
                "    aster_string_drop(v%" PRIu32 ");\n"
                "    aster_string_drop(v%" PRIu32 ");\n",
                instruction->result,
                instruction->operands[0],
                instruction->operands[1],
                instruction->operands[2],
                instruction->operands[0],
                instruction->operands[1],
                instruction->operands[2]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(
            instruction->symbol,
            "__interpolation_builder_append_formatted") ==
            0) {
        const IrType *value_type =
            &emitter->ir->types[
                function->value_types[
                    instruction->operands[1]]];
        const char *suffix =
            value_type->shape == IR_TYPE_BOOL
                ? "bool"
            : value_type->shape ==
                  IR_TYPE_SIGNED_INT
                ? "i64"
            : value_type->shape ==
                  IR_TYPE_UNSIGNED_INT ||
              value_type->shape == IR_TYPE_CHAR
                ? "u64"
            : value_type->shape == IR_TYPE_FLOAT
                ? "f64" : NULL;
        if (suffix == NULL) {
            c_backend_unsupported(
                emitter, instruction->span,
                "owned interpolation value");
            return true;
        }
        fprintf(
            output,
            "    aster_builder_append_%s(v%" PRIu32
            ", v%" PRIu32 ");\n"
            "    v%" PRIu32 " = UINT8_C(0);\n",
            suffix, instruction->operands[0],
            instruction->operands[1],
            instruction->result);
        return true;
    }
    if (instruction->symbol != NULL &&
        (strcmp(instruction->symbol, "I64ToString") == 0 ||
         strcmp(instruction->symbol, "U64ToString") == 0 ||
         strcmp(instruction->symbol, "F32ToString") == 0 ||
         strcmp(instruction->symbol, "F64ToString") == 0)) {
        const char *conversion =
            strcmp(instruction->symbol, "I64ToString") == 0
                ? "aster_i64_to_string"
            : strcmp(instruction->symbol, "U64ToString") == 0
                ? "aster_u64_to_string"
            : strcmp(instruction->symbol, "F32ToString") == 0
                ? "aster_f32_to_string"
                : "aster_f64_to_string";
        fprintf(output,
                "    v%" PRIu32 " = %s(v%" PRIu32 ");\n",
                instruction->result, conversion,
                instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        (strcmp(instruction->symbol, "TextLen") == 0 ||
         strcmp(instruction->symbol, "StringLen") == 0)) {
        IrTypeId argument_type =
            function->value_types[instruction->operands[0]];
        const IrType *argument =
            &emitter->ir->types[argument_type];
        bool borrowed = instruction->argument_mode_count != 0U &&
            parameter_mode_is_reference(
                instruction->argument_modes[0]);
        if (argument->shape == IR_TYPE_STRING_VIEW)
            fprintf(output,
                    "    v%" PRIu32
                    " = (uint64_t)v%" PRIu32 ".length;\n",
                    instruction->result,
                    instruction->operands[0]);
        else if (borrowed)
            fprintf(output,
                    "    v%" PRIu32
                    " = (uint64_t)v%" PRIu32 "->length;\n",
                    instruction->result,
                    instruction->operands[0]);
        else
            fprintf(output,
                    "    v%" PRIu32
                    " = (uint64_t)v%" PRIu32 "->length;\n"
                    "    aster_string_drop(v%" PRIu32 ");\n",
                    instruction->result,
                    instruction->operands[0],
                    instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "StringIndexOfOrdinal") == 0) {
        const IrType *value_type = &emitter->ir->types[
            function->value_types[instruction->operands[0]]];
        const IrType *needle_type = &emitter->ir->types[
            function->value_types[instruction->operands[1]]];
        bool value_view =
            value_type->shape == IR_TYPE_STRING_VIEW;
        bool needle_view =
            needle_type->shape == IR_TYPE_STRING_VIEW;
        fprintf(output,
                "    v%" PRIu32 " = aster_str_index_of(%s"
                "v%" PRIu32 "%s, %sv%" PRIu32 "%s, "
                "(size_t)v%" PRIu32 ");\n",
                instruction->result,
                value_view ? "" : "aster_string_as_str(",
                instruction->operands[0], value_view ? "" : ")",
                needle_view ? "" : "aster_string_as_str(",
                instruction->operands[1], needle_view ? "" : ")",
                instruction->operands[2]);
        for (size_t i = 0U; i < 2U; ++i) {
            const IrType *argument = &emitter->ir->types[
                function->value_types[instruction->operands[i]]];
            bool borrowed =
                i < instruction->argument_mode_count &&
                parameter_mode_is_reference(
                    instruction->argument_modes[i]);
            if (argument->shape == IR_TYPE_BUILTIN_OBJECT &&
                !borrowed)
                fprintf(output,
                        "    aster_string_drop(v%" PRIu32 ");\n",
                        instruction->operands[i]);
        }
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "StringByteAt") == 0) {
        const IrType *argument = &emitter->ir->types[
            function->value_types[instruction->operands[0]]];
        bool owned = argument->shape == IR_TYPE_BUILTIN_OBJECT;
        bool borrowed = instruction->argument_mode_count != 0U &&
            parameter_mode_is_reference(
                instruction->argument_modes[0]);
        fprintf(output,
                "    if (v%" PRIu32 " >= v%" PRIu32 "%slength)\n"
                "        aster_trap(\"string_byte_at index is "
                "outside the string view\");\n"
                "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                "%sdata[(size_t)v%" PRIu32 "];\n",
                instruction->operands[1],
                instruction->operands[0],
                owned ? "->" : ".",
                instruction->result,
                instruction->operands[0],
                owned ? "->" : ".",
                instruction->operands[1]);
        if (owned && !borrowed)
            fprintf(output,
                    "    aster_string_drop(v%" PRIu32 ");\n",
                    instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "StringSlice") == 0) {
        const IrType *argument = &emitter->ir->types[
            function->value_types[instruction->operands[0]]];
        bool owned = argument->shape == IR_TYPE_BUILTIN_OBJECT;
        bool borrowed = instruction->argument_mode_count != 0U &&
            parameter_mode_is_reference(
                instruction->argument_modes[0]);
        fprintf(output,
                "    if (v%" PRIu32 " > v%" PRIu32 " || "
                "v%" PRIu32 " > v%" PRIu32 "%slength)\n"
                "        aster_trap(\"string_slice requires valid "
                "start and end byte offsets\");\n"
                "    v%" PRIu32 " = aster_string_from((aster_str){"
                "v%" PRIu32 " == v%" PRIu32 " ? NULL : "
                "v%" PRIu32 "%sdata + (size_t)v%" PRIu32 ", "
                "(size_t)(v%" PRIu32 " - v%" PRIu32 ")});\n",
                instruction->operands[1],
                instruction->operands[2],
                instruction->operands[2],
                instruction->operands[0],
                owned ? "->" : ".",
                instruction->result,
                instruction->operands[1],
                instruction->operands[2],
                instruction->operands[0],
                owned ? "->" : ".",
                instruction->operands[1],
                instruction->operands[2],
                instruction->operands[1]);
        if (owned && !borrowed)
            fprintf(output,
                    "    aster_string_drop(v%" PRIu32 ");\n",
                    instruction->operands[0]);
        return true;
    }
    return false;
}
