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
static unsigned type_width(const CEmitter *emitter,
                           const IrType *type) {
    if (type->shape == IR_TYPE_ENUM) return 32U;
    return type->bit_width != 0U
         ? type->bit_width
         : (unsigned)emitter->ir->target.pointer_size * 8U;
}

static void emit_signed_bounds(CEmitter *emitter,
                               const IrType *type) {
    unsigned width = type_width(emitter, type);
    if (width >= 64U) {
        fputs("INT64_MIN, INT64_MAX", emitter->output);
    } else {
        uint64_t magnitude = UINT64_C(1) << (width - 1U);
        fprintf(emitter->output,
                "-INT64_C(%" PRIu64 "), INT64_C(%" PRIu64 ")",
                magnitude, magnitude - 1U);
    }
}

static void emit_value_name(FILE *output, IrValueId value) {
    fprintf(output, "v%" PRIu32, value);
}

void c_backend_emit_byte_string(FILE *output,
                             const char *data, size_t length) {
    fputs("(const unsigned char *)\"", output);
    for (size_t i = 0U; i < length; ++i)
        fprintf(output, "\\x%02x",
                (unsigned)(unsigned char)data[i]);
    fputc('"', output);
}

static void emit_operands(CEmitter *emitter,
                          const IrInstruction *instruction) {
    for (size_t i = 0U; i < instruction->operand_count; ++i) {
        if (i != 0U) fputs(", ", emitter->output);
        emit_value_name(emitter->output, instruction->operands[i]);
    }
}

static void emit_borrowed_call_operand(
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
        fprintf(
            emitter->output,
            "&l%" PRIu32 ".f%" PRIu32,
            producer->index,
            producer->auxiliary
        );
        return;
    }
    fprintf(emitter->output, "&v%" PRIu32, value);
}

static void emit_call_operands(
    CEmitter *emitter,
    const IrFunction *function,
    const IrInstruction *instruction,
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
        else
            emit_value_name(
                emitter->output, instruction->operands[i]);
    }
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

static bool same_source_span(LangSpan left, LangSpan right) {
    return left.file == right.file &&
           left.start == right.start &&
           left.end == right.end;
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

static void emit_list_element_equality(
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

static void emit_dictionary_key_equality(
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

static void emit_dictionary_value_equality(
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

static void emit_list_callback_call(
    CEmitter *emitter, IrTypeId element,
    IrValueId callback, IrValueId list, const char *index) {
    FILE *output = emitter->output;
    fprintf(output, "v%" PRIu32 "(", callback);
    if (c_backend_type_needs_drop(emitter, element))
        fprintf(output, "aster_clone_%" PRIu32 "(", element);
    fprintf(output, "v%" PRIu32 "->data[%s]", list, index);
    if (c_backend_type_needs_drop(emitter, element))
        fputc(')', output);
    fputc(')', output);
}

static void emit_checked_integer(CEmitter *emitter,
                                 const IrInstruction *instruction,
                                 const char *signed_name,
                                 const char *unsigned_name) {
    const IrType *type =
        &emitter->ir->types[instruction->result_type];
    fprintf(emitter->output, "    v%" PRIu32 " = %s(",
            instruction->result,
            type->shape == IR_TYPE_SIGNED_INT
                ? signed_name : unsigned_name);
    emit_operands(emitter, instruction);
    if (type->shape == IR_TYPE_SIGNED_INT) {
        fputs(", ", emitter->output);
        emit_signed_bounds(emitter, type);
    } else {
        unsigned width = type_width(emitter, type);
        if (width >= 64U)
            fputs(", UINT64_MAX", emitter->output);
        else
            fprintf(emitter->output, ", UINT64_C(%" PRIu64 ")",
                    (UINT64_C(1) << width) - 1U);
    }
    fputs(");\n", emitter->output);
}

bool c_backend_registry_native_symbol(const char *symbol) {
    return symbol != NULL &&
           (strncmp(symbol, "NativeFile", 10U) == 0 ||
            strncmp(symbol, "NativeDirectory", 15U) == 0 ||
            strncmp(symbol, "NativePath", 10U) == 0 ||
            strcmp(symbol, "NativeCreateDirectory") == 0 ||
            strcmp(symbol, "NativeRenamePath") == 0 ||
            strcmp(symbol, "NativeRemoveFile") == 0 ||
            strcmp(symbol, "NativeRemoveDirectory") == 0 ||
            strncmp(symbol, "NativeSqlite", 12U) == 0 ||
            strncmp(symbol, "H2O", 3U) == 0 ||
            strcmp(symbol, "NativeProcessEnvironment") == 0 ||
            strcmp(symbol, "NativeProcessArg") == 0 ||
            strcmp(symbol, "NativeProcessArgCount") == 0 ||
            strcmp(symbol, "NativeEnvironmentNewLine") == 0 ||
            strcmp(symbol, "NativeHandleOpenId") == 0 ||
            strcmp(symbol, "NativeFailHandle") == 0 ||
            strcmp(symbol, "HttpFormValue") == 0 ||
            strcmp(symbol, "HttpTryServerOpen") == 0 ||
            strcmp(symbol, "HttpServerPort") == 0 ||
            strcmp(symbol, "HttpTryAccept") == 0 ||
            strcmp(symbol, "HttpRequestMethod") == 0 ||
            strcmp(symbol, "HttpRequestPath") == 0 ||
            strcmp(symbol, "HttpRequestHeader") == 0 ||
            strcmp(symbol, "HttpRequestHeaders") == 0 ||
            strcmp(symbol, "HttpRequestBody") == 0 ||
            strcmp(symbol, "HttpRequestRemoteIpAddress") == 0 ||
            strcmp(symbol, "HttpTryRequestNext") == 0 ||
            strcmp(symbol, "HttpTryRespondHtml") == 0 ||
            strcmp(symbol, "HttpTryRespondHtmlReuse") == 0 ||
            strcmp(symbol, "HttpTryRespondRedirectReuse") == 0 ||
            strcmp(symbol, "HttpTryRespondReuse") == 0 ||
            strcmp(symbol, "HttpTryRespondHeadersReuse") == 0 ||
            strcmp(symbol, "HttpTryRespondEmptyHeadersReuse") == 0 ||
            strcmp(symbol, "HttpTryRespondHtmlHeadersReuse") == 0 ||
            strcmp(symbol, "HttpStreamBegin") == 0 ||
            strcmp(symbol, "HttpStreamBeginHeaders") == 0 ||
            strcmp(symbol, "HttpStreamChunk") == 0 ||
            strcmp(symbol, "HttpStreamFinish") == 0 ||
            strcmp(symbol, "ByteSliceToString") == 0 ||
            strcmp(symbol, "ByteSliceSet") == 0 ||
            strcmp(symbol, "UnicodeToUpper") == 0 ||
            strcmp(symbol, "UnicodeToLower") == 0 ||
            strcmp(symbol, "UnicodeSpecialUpper") == 0 ||
            strcmp(symbol, "UnicodeSpecialLower") == 0 ||
            strcmp(symbol, "UnicodeIsLetter") == 0 ||
            strcmp(symbol, "UnicodeIsDigit") == 0 ||
            strcmp(symbol, "UnicodeIsUpper") == 0 ||
            strcmp(symbol, "UnicodeIsLower") == 0 ||
            strcmp(symbol, "UnicodeIsWhiteSpace") == 0 ||
            strcmp(symbol, "UnicodeDecodeScalar") == 0);
}

static void emit_native_argument(CEmitter *emitter,
                                 const IrFunction *function,
                                 IrValueId value) {
    const IrType *type =
        &emitter->ir->types[function->value_types[value]];
    if (type->shape == IR_TYPE_STRING_VIEW)
        fprintf(emitter->output,
                "(LangValue){.tag=LANG_VALUE_STRING_VIEW,"
                ".as.string={(const char *)v%" PRIu32 ".data,"
                "v%" PRIu32 ".length}}", value, value);
    else if (c_backend_type_is_native_handle(type))
        fprintf(emitter->output, "v%" PRIu32 "->value", value);
    else if (type->shape == IR_TYPE_SLICE)
        fprintf(emitter->output,
                "(LangValue){.tag=LANG_VALUE_BYTE_SLICE,"
                ".as.bytes={v%" PRIu32 ".data,v%" PRIu32 ".length}}",
                value, value);
    else if (type->shape == IR_TYPE_BOOL)
        fprintf(emitter->output,
                "(LangValue){.tag=LANG_VALUE_BOOL,"
                ".as.boolean=v%" PRIu32 "}", value);
    else if (type->shape == IR_TYPE_SIGNED_INT)
        fprintf(emitter->output,
                "(LangValue){.tag=LANG_VALUE_I64,"
                ".as.i64=v%" PRIu32 "}", value);
    else if (type->shape == IR_TYPE_UNSIGNED_INT ||
             type->shape == IR_TYPE_CHAR)
        fprintf(emitter->output,
                "(LangValue){.tag=LANG_VALUE_U64,"
                ".as.u64=v%" PRIu32 "}", value);
    else if (type->shape == IR_TYPE_FLOAT)
        fprintf(emitter->output,
                "(LangValue){.tag=LANG_VALUE_F64,"
                ".as.f64=(double)v%" PRIu32 "}", value);
    else if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
             strcmp(type->name, "string") == 0)
        fprintf(emitter->output,
                "(LangValue){.tag=LANG_VALUE_STRING_VIEW,"
                ".as.string={(const char *)v%" PRIu32 "->data,"
                "v%" PRIu32 "->length}}", value, value);
    else if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
             strcmp(type->name, "Html") == 0)
        fprintf(emitter->output,
                "(LangValue){.tag=LANG_VALUE_STRING_VIEW,"
                ".as.string={(const char *)aster_html_as_str("
                "v%" PRIu32 ").data,aster_html_as_str("
                "v%" PRIu32 ").length}}", value, value);
    else
        fputs("(LangValue){.tag=LANG_VALUE_UNIT}", emitter->output);
}

static bool emit_native_payload(
    CEmitter *emitter, const IrType *type,
    IrValueId result, size_t variant,
    const char *payload) {
    FILE *output = emitter->output;
    if (type->shape == IR_TYPE_UNIT) {
        fprintf(output,
                "        v%" PRIu32 ".payload.v%zu = UINT8_C(0);\n",
                result, variant);
    } else if (type->shape == IR_TYPE_BOOL) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_BOOL) "
                "aster_trap(\"invalid native bool result\");\n"
                "        v%" PRIu32 ".payload.v%zu = %s.as.boolean;\n",
                payload, result, variant, payload);
    } else if (type->shape == IR_TYPE_SIGNED_INT) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_I64) "
                "aster_trap(\"invalid native integer result\");\n"
                "        v%" PRIu32 ".payload.v%zu = %s.as.i64;\n",
                payload, result, variant, payload);
    } else if (type->shape == IR_TYPE_UNSIGNED_INT ||
               type->shape == IR_TYPE_CHAR) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_U64) "
                "aster_trap(\"invalid native integer result\");\n"
                "        v%" PRIu32 ".payload.v%zu = %s.as.u64;\n",
                payload, result, variant, payload);
    } else if (type->shape == IR_TYPE_FLOAT) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_F64) "
                "aster_trap(\"invalid native floating-point result\");\n"
                "        v%" PRIu32 ".payload.v%zu = %s.as.f64;\n",
                payload, result, variant, payload);
    } else if (type->shape == IR_TYPE_STRING_VIEW) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_STRING_VIEW) "
                "aster_trap(\"invalid native string-view result\");\n"
                "        v%" PRIu32 ".payload.v%zu = (aster_str){"
                "(const unsigned char *)%s.as.string.data,"
                "%s.as.string.length};\n",
                payload, result, variant, payload, payload);
    } else if (c_backend_type_is_native_handle(type)) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_OBJECT) "
                "aster_trap(\"invalid native handle result\");\n"
                "        v%" PRIu32 ".payload.v%zu = "
                "aster_allocate(sizeof(*v%" PRIu32 ".payload.v%zu));\n"
                "        v%" PRIu32 ".payload.v%zu->references = 1U;\n"
                "        v%" PRIu32 ".payload.v%zu->value = %s;\n",
                payload, result, variant, result, variant,
                result, variant,
                result, variant, payload);
    } else if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
               strcmp(type->name, "string") == 0) {
        fprintf(output,
                "        LangStringView native_view_%" PRIu32 "_%zu;\n"
                "        if (!lang_value_string_view(&%s, "
                "&native_view_%" PRIu32 "_%zu))\n"
                "            aster_trap(\"invalid native String result\");\n"
                "        v%" PRIu32 ".payload.v%zu = aster_string_from("
                "(aster_str){(const unsigned char *)"
                "native_view_%" PRIu32 "_%zu.data,"
                "native_view_%" PRIu32 "_%zu.length});\n"
                "        lang_value_drop(aster_vm, &%s);\n",
                result, variant, payload, result, variant,
                result, variant, result, variant, result, variant,
                payload);
    } else {
        return false;
    }
    return true;
}

static bool emit_native_direct_result(
    CEmitter *emitter, const IrType *type,
    IrValueId result, const char *value) {
    FILE *output = emitter->output;
    if (type->shape == IR_TYPE_UNIT) {
        fprintf(output,
                "        v%" PRIu32 " = UINT8_C(0);\n", result);
    } else if (type->shape == IR_TYPE_BOOL) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_BOOL) "
                "aster_trap(\"invalid native bool result\");\n"
                "        v%" PRIu32 " = %s.as.boolean;\n",
                value, result, value);
    } else if (type->shape == IR_TYPE_SIGNED_INT) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_I64) "
                "aster_trap(\"invalid native integer result\");\n"
                "        v%" PRIu32 " = %s.as.i64;\n",
                value, result, value);
    } else if (type->shape == IR_TYPE_UNSIGNED_INT ||
               type->shape == IR_TYPE_CHAR) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_U64) "
                "aster_trap(\"invalid native integer result\");\n"
                "        v%" PRIu32 " = %s.as.u64;\n",
                value, result, value);
    } else if (type->shape == IR_TYPE_FLOAT) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_F64) "
                "aster_trap(\"invalid native floating-point result\");\n"
                "        v%" PRIu32 " = %s.as.f64;\n",
                value, result, value);
    } else if (type->shape == IR_TYPE_STRING_VIEW) {
        fprintf(output,
                "        LangStringView native_view_%" PRIu32 ";\n"
                "        if (!lang_value_string_view(&%s, "
                "&native_view_%" PRIu32 "))\n"
                "            aster_trap(\"invalid native string-view result\");\n"
                "        v%" PRIu32 " = (aster_str){"
                "(const unsigned char *)native_view_%" PRIu32 ".data,"
                "native_view_%" PRIu32 ".length};\n",
                result, value, result, result, result, result);
    } else if (c_backend_type_is_native_handle(type)) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_OBJECT) "
                "aster_trap(\"invalid native handle result\");\n"
                "        v%" PRIu32
                " = aster_allocate(sizeof(*v%" PRIu32 "));\n"
                "        v%" PRIu32 "->references = 1U;\n"
                "        v%" PRIu32 "->value = %s;\n",
                value, result, result, result, result, value);
    } else if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
               strcmp(type->name, "string") == 0) {
        fprintf(output,
                "        LangStringView native_view_%" PRIu32 ";\n"
                "        if (!lang_value_string_view(&%s, "
                "&native_view_%" PRIu32 "))\n"
                "            aster_trap(\"invalid native string result\");\n"
                "        v%" PRIu32 " = aster_string_from((aster_str){"
                "(const unsigned char *)native_view_%" PRIu32 ".data,"
                "native_view_%" PRIu32 ".length});\n"
                "        lang_value_drop(aster_vm, &%s);\n",
                result, value, result, result, result, result, value);
    } else {
        return false;
    }
    return true;
}

static void emit_native_argument_cleanup(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction) {
    for (size_t i = 0U; i < instruction->operand_count; ++i) {
        if (i < instruction->argument_mode_count &&
            parameter_mode_is_reference(
                instruction->argument_modes[i]))
            continue;
        IrTypeId type = function->value_types[
            instruction->operands[i]];
        if (!c_backend_type_needs_drop(emitter, type)) continue;
        fputs("        ", emitter->output);
        c_backend_emit_drop_call(
            emitter, type, "v", instruction->operands[i]);
        fputs(";\n", emitter->output);
    }
}

static bool emit_registry_native_call(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction) {
    if (!c_backend_registry_native_symbol(instruction->symbol))
        return false;
    const IrType *result_type =
        &emitter->ir->types[instruction->result_type];
    size_t ok_variant = result_type->variant_count;
    size_t err_variant = result_type->variant_count;
    if (result_type->shape == IR_TYPE_UNION) {
        for (size_t v = 0U; v < result_type->variant_count; ++v) {
            if (strcmp(result_type->variant_names[v], "Ok") == 0)
                ok_variant = v;
            else if (strcmp(result_type->variant_names[v], "Err") == 0)
                err_variant = v;
        }
        if (ok_variant == result_type->variant_count ||
            err_variant == result_type->variant_count)
            return false;
    }
    FILE *output = emitter->output;
    fprintf(output,
            "    {\n"
            "        LangValue native_args_%" PRIu32 "[%zu] = {",
            instruction->result,
            instruction->operand_count == 0U
                ? 1U : instruction->operand_count);
    if (instruction->operand_count == 0U)
        fputs("(LangValue){.tag=LANG_VALUE_UNIT}", output);
    for (size_t i = 0U; i < instruction->operand_count; ++i) {
        if (i != 0U) fputs(", ", output);
        emit_native_argument(
            emitter, function, instruction->operands[i]);
    }
    fprintf(output,
            "};\n"
            "        LangNativeResult native_result_%" PRIu32 ";\n"
            "        if (!lang_vm_call_native(aster_vm, \"",
            instruction->result);
    fwrite(instruction->symbol, 1U,
           instruction->symbol_length, output);
    fprintf(output,
            "\", native_args_%" PRIu32 ", %zuU, "
            "&native_result_%" PRIu32 "))\n"
            "            aster_trap(\"unregistered native function\");\n"
            "        if (!native_result_%" PRIu32 ".ok) {\n"
            "            const char *native_error_%" PRIu32 " = "
            "lang_native_result_error_message(&native_result_%" PRIu32 ");\n"
            "            if (native_error_%" PRIu32 " == NULL) "
            "native_error_%" PRIu32 " = \"native function failed\";\n"
            "            if (aster_exception_pending) "
            "aster_string_drop(aster_exception_message);\n"
            "            aster_exception_message = aster_string_from("
            "(aster_str){(const unsigned char *)native_error_%" PRIu32 ", "
            "strlen(native_error_%" PRIu32 ")});\n"
            "            lang_native_result_drop(&native_result_%" PRIu32 ");\n"
            "            aster_exception_pending = true;\n"
            "            v%" PRIu32 " = (",
            instruction->result, instruction->operand_count,
            instruction->result, instruction->result,
            instruction->result, instruction->result,
            instruction->result, instruction->result,
            instruction->result, instruction->result,
            instruction->result,
            instruction->result);
    c_backend_emit_type(emitter, instruction->result_type);
    fputs("){0};\n        } else {\n", output);
    if (result_type->shape != IR_TYPE_UNION) {
        char direct_value[64];
        (void)snprintf(
            direct_value, sizeof(direct_value),
            "native_result_%" PRIu32 ".value",
            instruction->result);
        if (!emit_native_direct_result(
                emitter, result_type, instruction->result,
                direct_value))
            return false;
        fputs("        }\n", output);
        emit_native_argument_cleanup(
            emitter, function, instruction);
        fputs("    }\n", output);
        return true;
    }
    fprintf(output,
            "        bool native_ok_%" PRIu32 ";\n"
            "        LangValue native_payload_%" PRIu32 ";\n"
            "        if (!lang_result_take(aster_vm, "
            "&native_result_%" PRIu32 ".value, &native_ok_%" PRIu32 ", "
            "&native_payload_%" PRIu32 "))\n"
            "            aster_trap(\"native call returned an invalid Result\");\n"
            "        if (native_ok_%" PRIu32 ") {\n"
            "            v%" PRIu32 ".tag = UINT32_C(%zu);\n",
            instruction->result, instruction->result,
            instruction->result, instruction->result,
            instruction->result, instruction->result,
            instruction->result, ok_variant);
    char payload[64];
    (void)snprintf(payload, sizeof(payload),
                   "native_payload_%" PRIu32, instruction->result);
    IrTypeId ok_type =
        result_type->variant_payload_types[ok_variant];
    if (ok_type == IR_INVALID_ID ||
        !emit_native_payload(
            emitter, &emitter->ir->types[ok_type],
            instruction->result, ok_variant, payload))
        return false;
    fprintf(output,
            "        } else {\n"
            "            v%" PRIu32 ".tag = UINT32_C(%zu);\n",
            instruction->result, err_variant);
    IrTypeId err_type =
        result_type->variant_payload_types[err_variant];
    if (err_type == IR_INVALID_ID ||
        !emit_native_payload(
            emitter, &emitter->ir->types[err_type],
            instruction->result, err_variant, payload))
        return false;
    fputs("        }\n        }\n", output);
    emit_native_argument_cleanup(
        emitter, function, instruction);
    fputs("    }\n", output);
    return true;
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
    if (uses != 1U || consumer == NULL ||
        consumer->opcode != IR_OP_CALL_NATIVE ||
        consumer->symbol == NULL ||
        strcmp(consumer->symbol, "Html::ToHtmlString") != 0 ||
        consumer->operand_count != 1U)
        return NULL;
    return consumer;
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
            bool reference_local =
                instruction->index < function->parameter_count &&
                parameter_mode_is_reference(
                    function->parameters[instruction->index].mode);
            if (emitter->render_direct &&
                emitter->ir->types[local_type].shape ==
                    IR_TYPE_ELEMENT_BUILDER) {
                fprintf(output,
                        "    l%" PRIu32 " = v%" PRIu32 ";\n"
                        "    l%" PRIu32 "_direct_open = %s;\n",
                        instruction->index, instruction->operands[0],
                        instruction->index,
                        emitter->direct_local_tags != NULL &&
                        emitter->direct_local_tags[instruction->index] != NULL &&
                        c_backend_html_tag_is_fragment(
                            emitter->direct_local_tags[instruction->index],
                            emitter->direct_local_tag_lengths[
                                instruction->index])
                            ? "false" : "true");
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
            fprintf(output, "    l%" PRIu32 " = v%" PRIu32 ";\n",
                    instruction->index, instruction->operands[0]);
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
        case IR_OP_VALUE_DISCARD: {
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
                if (c_backend_type_needs_drop(emitter, instruction->result_type))
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
            if (c_backend_type_needs_drop(emitter, instruction->result_type))
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
            if (c_backend_type_needs_drop(
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
            if (c_backend_type_needs_drop(emitter, instruction->result_type))
                fprintf(output, "aster_clone_%" PRIu32 "(",
                        instruction->result_type);
            fprintf(output, "v%" PRIu32
                    ".items[(size_t)v%" PRIu32 "]",
                    instruction->operands[0], instruction->operands[1]);
            fputs(c_backend_type_needs_drop(emitter, instruction->result_type)
                    ? ");\n" : ";\n", output);
            if (c_backend_type_needs_drop(emitter, array_type) &&
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
            emit_checked_integer(
                emitter, instruction,
                "aster_add_s", "aster_add_u");
            return;
        case IR_OP_SUB_CHECKED:
            emit_checked_integer(
                emitter, instruction,
                "aster_sub_s", "aster_sub_u");
            return;
        case IR_OP_MUL_CHECKED:
            emit_checked_integer(
                emitter, instruction,
                "aster_mul_s", "aster_mul_u");
            return;
        case IR_OP_DIV_CHECKED:
        case IR_OP_REM_CHECKED: {
            const IrType *type =
                &emitter->ir->types[instruction->result_type];
            const char *name =
                instruction->opcode == IR_OP_DIV_CHECKED
                    ? "aster_div" : "aster_rem";
            fprintf(output, "    v%" PRIu32 " = %s_%c(",
                    instruction->result, name,
                    type->shape == IR_TYPE_SIGNED_INT ? 's' : 'u');
            emit_operands(emitter, instruction);
            if (type->shape == IR_TYPE_SIGNED_INT) {
                fputs(", ", output);
                emit_signed_bounds(emitter, type);
            }
            fputs(");\n", output);
            return;
        }
        case IR_OP_SHIFT_LEFT_CHECKED:
        case IR_OP_SHIFT_RIGHT_CHECKED: {
            const IrType *type =
                &emitter->ir->types[instruction->result_type];
            fprintf(output, "    v%" PRIu32 " = aster_shift_%c(",
                    instruction->result,
                    type->shape == IR_TYPE_SIGNED_INT ? 's' : 'u');
            fprintf(output, "v%" PRIu32 ", (uint64_t)v%" PRIu32,
                    instruction->operands[0],
                    instruction->operands[1]);
            fprintf(output, ", %uU, %s",
                    type_width(emitter, type),
                    instruction->opcode == IR_OP_SHIFT_RIGHT_CHECKED
                        ? "true" : "false");
            if (type->shape == IR_TYPE_SIGNED_INT) {
                fputs(", ", output);
                emit_signed_bounds(emitter, type);
            } else {
                unsigned width = type_width(emitter, type);
                if (width >= 64U)
                    fputs(", UINT64_MAX", output);
                else
                    fprintf(output, ", UINT64_C(%" PRIu64 ")",
                            (UINT64_C(1) << width) - 1U);
            }
            fputs(");\n", output);
            return;
        }
        case IR_OP_BIT_AND:
        case IR_OP_BIT_OR:
        case IR_OP_BIT_XOR: {
            static const char *operators[] = {"&", "|", "^"};
            size_t index =
                (size_t)instruction->opcode -
                (size_t)IR_OP_BIT_AND;
            const IrType *type =
                &emitter->ir->types[instruction->result_type];
            fprintf(output, "    v%" PRIu32 " = (",
                    instruction->result);
            c_backend_emit_type(emitter, instruction->result_type);
            fprintf(output,
                    ")aster_bits_%c("
                    "(uint64_t)v%" PRIu32 " %s "
                    "(uint64_t)v%" PRIu32 ", %uU);\n",
                    type->shape == IR_TYPE_SIGNED_INT ? 's' : 'u',
                    instruction->operands[0], operators[index],
                    instruction->operands[1],
                    type_width(emitter, type));
            return;
        }
        case IR_OP_ADD_FLOAT:
        case IR_OP_SUB_FLOAT:
        case IR_OP_MUL_FLOAT:
        case IR_OP_DIV_FLOAT: {
            static const char *operators[] = {"+", "-", "*", "/"};
            size_t index =
                (size_t)instruction->opcode -
                (size_t)IR_OP_ADD_FLOAT;
            fprintf(output,
                    "    v%" PRIu32 " = v%" PRIu32 " %s v%" PRIu32 ";\n",
                    instruction->result, instruction->operands[0],
                    operators[index], instruction->operands[1]);
            return;
        }
        case IR_OP_EQUAL:
        case IR_OP_NOT_EQUAL:
        case IR_OP_LESS:
        case IR_OP_LESS_EQUAL:
        case IR_OP_GREATER:
        case IR_OP_GREATER_EQUAL: {
            static const char *operators[] = {
                "==", "!=", "<", "<=", ">", ">="
            };
            size_t index =
                (size_t)instruction->opcode -
                (size_t)IR_OP_EQUAL;
            const IrType *operand_type =
                &emitter->ir->types[
                    function->value_types[instruction->operands[0]]];
            if (operand_type->shape == IR_TYPE_STRING_VIEW &&
                (instruction->opcode == IR_OP_EQUAL ||
                 instruction->opcode == IR_OP_NOT_EQUAL)) {
                fprintf(output,
                        "    v%" PRIu32 " = %saster_str_equal("
                        "v%" PRIu32 ", v%" PRIu32 ");\n",
                        instruction->result,
                        instruction->opcode == IR_OP_NOT_EQUAL ? "!" : "",
                        instruction->operands[0],
                        instruction->operands[1]);
            } else if (operand_type->shape == IR_TYPE_UNION &&
                       (instruction->opcode == IR_OP_EQUAL ||
                        instruction->opcode == IR_OP_NOT_EQUAL)) {
                const IrInstruction *left =
                    c_backend_find_value_producer(
                        function, instruction->operands[0]);
                const IrInstruction *right =
                    c_backend_find_value_producer(
                        function, instruction->operands[1]);
                bool left_payloadless =
                    left != NULL &&
                    left->opcode == IR_OP_AGGREGATE_MAKE &&
                    left->index < operand_type->variant_count &&
                    operand_type->variant_payload_types[left->index] ==
                        IR_INVALID_ID;
                bool right_payloadless =
                    right != NULL &&
                    right->opcode == IR_OP_AGGREGATE_MAKE &&
                    right->index < operand_type->variant_count &&
                    operand_type->variant_payload_types[right->index] ==
                        IR_INVALID_ID;
                if (!left_payloadless && !right_payloadless) {
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "union equality without a payloadless variant");
                    return;
                }
                fprintf(output,
                        "    v%" PRIu32 " = v%" PRIu32
                        ".tag %s v%" PRIu32 ".tag;\n",
                        instruction->result,
                        instruction->operands[0],
                        instruction->opcode == IR_OP_NOT_EQUAL
                            ? "!=" : "==",
                        instruction->operands[1]);
                if (c_backend_type_needs_drop(
                        emitter,
                        function->value_types[
                            instruction->operands[0]])) {
                    c_backend_emit_drop_call(
                        emitter,
                        function->value_types[
                            instruction->operands[0]],
                        "v", instruction->operands[0]);
                    fputs(";\n", output);
                    c_backend_emit_drop_call(
                        emitter,
                        function->value_types[
                            instruction->operands[1]],
                        "v", instruction->operands[1]);
                    fputs(";\n", output);
                }
            } else if (operand_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                       strcmp(operand_type->name, "string") == 0 &&
                       (instruction->opcode == IR_OP_EQUAL ||
                        instruction->opcode == IR_OP_NOT_EQUAL)) {
                fprintf(output,
                        "    v%" PRIu32 " = %saster_str_equal("
                        "aster_string_as_str(v%" PRIu32 "), "
                        "aster_string_as_str(v%" PRIu32 "));\n"
                        "    aster_string_drop(v%" PRIu32 ");\n"
                        "    aster_string_drop(v%" PRIu32 ");\n",
                        instruction->result,
                        instruction->opcode == IR_OP_NOT_EQUAL ? "!" : "",
                        instruction->operands[0],
                        instruction->operands[1],
                        instruction->operands[0],
                        instruction->operands[1]);
            } else {
                fprintf(output,
                        "    v%" PRIu32 " = v%" PRIu32
                        " %s v%" PRIu32 ";\n",
                        instruction->result, instruction->operands[0],
                        operators[index], instruction->operands[1]);
            }
            return;
        }
        case IR_OP_NEGATE:
            if (emitter->ir->types[
                    instruction->result_type].shape == IR_TYPE_FLOAT) {
                fprintf(output,
                        "    v%" PRIu32 " = -v%" PRIu32 ";\n",
                        instruction->result,
                        instruction->operands[0]);
            } else {
                fprintf(output,
                        "    v%" PRIu32 " = aster_neg_s(v%" PRIu32 ", ",
                        instruction->result,
                        instruction->operands[0]);
                emit_signed_bounds(
                    emitter,
                    &emitter->ir->types[instruction->result_type]);
                fputs(");\n", output);
            }
            return;
        case IR_OP_NOT:
            fprintf(output, "    v%" PRIu32 " = !v%" PRIu32 ";\n",
                    instruction->result, instruction->operands[0]);
            return;
        case IR_OP_CAST: {
            const IrType *source =
                &emitter->ir->types[
                    function->value_types[instruction->operands[0]]];
            const IrType *target =
                &emitter->ir->types[instruction->result_type];
            bool source_integer =
                source->shape == IR_TYPE_SIGNED_INT ||
                source->shape == IR_TYPE_UNSIGNED_INT ||
                source->shape == IR_TYPE_CHAR;
            bool target_integer =
                target->shape == IR_TYPE_SIGNED_INT ||
                target->shape == IR_TYPE_UNSIGNED_INT ||
                target->shape == IR_TYPE_CHAR;
            if (target->shape == IR_TYPE_FLOAT &&
                (source_integer || source->shape == IR_TYPE_FLOAT)) {
                const char *maximum = type_width(emitter, target) == 32U
                    ? "FLT_MAX" : "DBL_MAX";
                fprintf(output,
                        "    ;\n"
                        "    double cast_value_%" PRIu32
                        " = (double)v%" PRIu32 ";\n"
                        "    if (cast_value_%" PRIu32
                        " != cast_value_%" PRIu32
                        " || cast_value_%" PRIu32 " > %s"
                        " || cast_value_%" PRIu32 " < -%s)\n"
                        "        aster_trap(\"numeric cast is out of range\");\n"
                        "    v%" PRIu32 " = (",
                        instruction->result,
                        instruction->operands[0],
                        instruction->result,
                        instruction->result,
                        instruction->result, maximum,
                        instruction->result, maximum,
                        instruction->result);
                c_backend_emit_type(emitter, instruction->result_type);
                fprintf(output,
                        ")cast_value_%" PRIu32 ";\n",
                        instruction->result);
                if (target->shape == IR_TYPE_CHAR)
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " >= UINT64_C(0xd800) && "
                            "(uint64_t)v%" PRIu32
                            " <= UINT64_C(0xdfff))\n"
                            "        aster_trap(\"numeric cast is out of range\");\n",
                            instruction->result,
                            instruction->result);
                return;
            }
            if (source->shape == IR_TYPE_FLOAT && target_integer) {
                unsigned width = type_width(emitter, target);
                fprintf(output,
                        "    ;\n"
                        "    double cast_value_%" PRIu32
                        " = (double)v%" PRIu32 ";\n"
                        "    if (cast_value_%" PRIu32
                        " != cast_value_%" PRIu32,
                        instruction->result,
                        instruction->operands[0],
                        instruction->result,
                        instruction->result);
                if (target->shape == IR_TYPE_SIGNED_INT) {
                    if (width >= 64U) {
                        fprintf(output,
                                " || cast_value_%" PRIu32
                                " < -0x1p63 || cast_value_%" PRIu32
                                " >= 0x1p63",
                                instruction->result,
                                instruction->result);
                    } else {
                        int64_t minimum =
                            -(int64_t)(UINT64_C(1) << (width - 1U));
                        uint64_t upper =
                            UINT64_C(1) << (width - 1U);
                        fprintf(output,
                                " || cast_value_%" PRIu32
                                " < %.1f || cast_value_%" PRIu32
                                " >= %.1f",
                                instruction->result, (double)minimum,
                                instruction->result, (double)upper);
                    }
                } else {
                    uint64_t upper = target->shape == IR_TYPE_CHAR
                        ? UINT64_C(0x110000)
                        : width >= 64U ? UINT64_MAX
                        : UINT64_C(1) << width;
                    fprintf(output,
                            " || cast_value_%" PRIu32
                            " < 0.0 || cast_value_%" PRIu32 " >= ",
                            instruction->result,
                            instruction->result);
                    if (width >= 64U && target->shape != IR_TYPE_CHAR)
                        fputs("0x1p64", output);
                    else
                        fprintf(output, "%.1f", (double)upper);
                }
                fputs(")\n"
                      "        aster_trap(\"numeric cast is out of range\");\n",
                      output);
                fprintf(output,
                        "    v%" PRIu32 " = (",
                        instruction->result);
                c_backend_emit_type(emitter, instruction->result_type);
                fprintf(output,
                        ")cast_value_%" PRIu32 ";\n",
                        instruction->result);
                return;
            }
            if (!source_integer || !target_integer) {
                c_backend_unsupported(emitter, instruction->span,
                            "this numeric cast");
                return;
            }
            unsigned width = type_width(emitter, target);
            if (target->shape == IR_TYPE_SIGNED_INT) {
                uint64_t maximum = width >= 64U
                    ? (uint64_t)INT64_MAX
                    : (UINT64_C(1) << (width - 1U)) - 1U;
                int64_t minimum = width >= 64U
                    ? INT64_MIN
                    : -(int64_t)(UINT64_C(1) << (width - 1U));
                if (source->shape == IR_TYPE_SIGNED_INT) {
                    fprintf(output, "    if (v%" PRIu32 " < ",
                            instruction->operands[0]);
                    if (width >= 64U)
                        fputs("INT64_MIN", output);
                    else
                        fprintf(output, "INT64_C(%" PRId64 ")", minimum);
                    fprintf(output, " || v%" PRIu32 " > ",
                            instruction->operands[0]);
                    if (width >= 64U)
                        fputs("INT64_MAX", output);
                    else
                        fprintf(output, "INT64_C(%" PRIu64 ")", maximum);
                    fputs(")\n"
                          "        aster_trap(\"numeric cast is out of range\");\n",
                          output);
                }
                else
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " > UINT64_C(%" PRIu64 "))\n"
                            "        aster_trap(\"numeric cast is out of range\");\n",
                            instruction->operands[0], maximum);
            } else {
                uint64_t maximum = target->shape == IR_TYPE_CHAR
                    ? UINT64_C(0x10ffff)
                    : width >= 64U
                        ? UINT64_MAX
                        : (UINT64_C(1) << width) - 1U;
                if (source->shape == IR_TYPE_SIGNED_INT)
                    fprintf(output,
                            "    if (v%" PRIu32 " < 0 || "
                            "(uint64_t)v%" PRIu32
                            " > UINT64_C(%" PRIu64 "))\n"
                            "        aster_trap(\"numeric cast is out of range\");\n",
                            instruction->operands[0],
                            instruction->operands[0], maximum);
                else
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " > UINT64_C(%" PRIu64 "))\n"
                            "        aster_trap(\"numeric cast is out of range\");\n",
                            instruction->operands[0], maximum);
                if (target->shape == IR_TYPE_CHAR)
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " >= UINT64_C(0xd800) && "
                            "(uint64_t)v%" PRIu32
                            " <= UINT64_C(0xdfff))\n"
                            "        aster_trap(\"numeric cast is out of range\");\n",
                            instruction->operands[0],
                            instruction->operands[0]);
            }
            fprintf(output,
                    "    v%" PRIu32 " = (",
                    instruction->result);
            c_backend_emit_type(emitter, instruction->result_type);
            fprintf(output, ")v%" PRIu32 ";\n",
                    instruction->operands[0]);
            return;
        }
        case IR_OP_BIT_NOT:
            {
            const IrType *type =
                &emitter->ir->types[instruction->result_type];
            fprintf(output, "    v%" PRIu32 " = (",
                    instruction->result);
            c_backend_emit_type(emitter, instruction->result_type);
            fprintf(output,
                    ")aster_bits_%c("
                    "~(uint64_t)v%" PRIu32 ", %uU);\n",
                    type->shape == IR_TYPE_SIGNED_INT ? 's' : 'u',
                    instruction->operands[0],
                    type_width(emitter, type));
            }
            return;
        case IR_OP_EXCEPTION_SET:
            {
            const IrType *exception_type = &emitter->ir->types[
                function->value_types[instruction->operands[0]]];
            fprintf(output,
                    "    if (aster_exception_pending) "
                    "aster_string_drop(aster_exception_message);\n"
                    "    aster_exception_message = v%" PRIu32 ".f0;\n"
                    "    v%" PRIu32 ".f0 = NULL;\n"
                    "    aster_exception_type = \"%s\";\n"
                    "    aster_exception_pending = true;\n",
                    instruction->operands[0], instruction->operands[0],
                    exception_type->name);
            }
            return;
        case IR_OP_EXCEPTION_PENDING:
            fprintf(output, "    v%" PRIu32
                    " = aster_exception_pending;\n", instruction->result);
            return;
        case IR_OP_EXCEPTION_MATCH:
            fprintf(output,
                    "    v%" PRIu32 " = strcmp(\"%s\", \"Exception\") == 0 "
                    "|| (aster_exception_type != NULL && "
                    "(strcmp(aster_exception_type, \"%s\") == 0 || "
                    "(strcmp(\"%s\", \"OperationCanceledException\") == 0 && "
                    "strcmp(aster_exception_type, \"TaskCanceledException\") == 0)));\n",
                    instruction->result, instruction->symbol,
                    instruction->symbol, instruction->symbol);
            return;
        case IR_OP_EXCEPTION_TAKE:
            fprintf(output,
                    "    if (!aster_exception_pending) "
                    "aster_trap(\"catch entered without an exception\");\n"
                    "    v%" PRIu32 ".f0 = aster_exception_message;\n"
                    "    aster_exception_message = NULL;\n"
                    "    aster_exception_type = NULL;\n"
                    "    aster_exception_pending = false;\n",
                    instruction->result);
            return;
        case IR_OP_CALL_DIRECT:
            if (emitter->render_direct &&
                instruction->index < emitter->ir->function_count &&
                c_backend_function_supports_direct_render(
                    emitter->ir, instruction->index)) {
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
                fprintf(output, "    v%" PRIu32 " = NULL;\n",
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
        case IR_OP_FUNCTION_REF:
            fprintf(output,
                    "    v%" PRIu32 " = aster_fn_%" PRIu32 ";\n",
                    instruction->result, instruction->index);
            return;
        case IR_OP_CALL_INDIRECT:
            fprintf(output,
                    "    v%" PRIu32 " = v%" PRIu32 "(",
                    instruction->result, instruction->operands[0]);
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
            const IrType *type = &emitter->ir->types[type_id];
            fprintf(output, "    if (v%" PRIu32 " != NULL) {\n", value);
            if (type->destructor_function != IR_INVALID_ID)
                fprintf(output,
                        "        (void)aster_fn_%" PRIu32 "(v%" PRIu32 ");\n",
                        type->destructor_function, value);
            for (size_t field = type->field_count; field > 0U; --field) {
                IrTypeId field_type = type->field_types[field - 1U];
                if (c_backend_type_needs_drop(emitter, field_type))
                    fprintf(output,
                            "        aster_drop_%" PRIu32
                            "(&v%" PRIu32 "->f%zu);\n",
                            field_type, value, field - 1U);
            }
            fprintf(output,
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
            if (emit_registry_native_call(
                    emitter, function, instruction))
                return;
            if (c_backend_registry_native_symbol(instruction->symbol)) {
                c_backend_unsupported(
                    emitter, instruction->span,
                    "this registered native signature");
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Task::Delay") == 0) {
                if (instruction->operand_count != 1U &&
                    instruction->operand_count != 2U) {
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "a Task.Delay overload other than one millisecond argument");
                    return;
                }
                fprintf(output,
                        "    v%" PRIu32
                        " = aster_task_delay((int64_t)v%" PRIu32 ", ",
                        instruction->result,
                        instruction->operands[0]);
                if (instruction->operand_count == 2U)
                    fprintf(output, "v%" PRIu32,
                            instruction->operands[1]);
                else
                    fputs("NULL", output);
                fputs(");\n", output);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Task::WhenAll") == 0) {
                if (instruction->operand_count != 1U) {
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "a Task.WhenAll overload other than one List argument");
                    return;
                }
                IrTypeId input_type = function->value_types[
                    instruction->operands[0]];
                fprintf(output,
                        "    v%" PRIu32 " = aster_when_all_%" PRIu32
                        "(v%" PRIu32 ");\n",
                        instruction->result, input_type,
                        instruction->operands[0]);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Task::WhenAny") == 0) {
                if (instruction->operand_count != 1U) {
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "a Task.WhenAny overload other than one List argument");
                    return;
                }
                IrTypeId input_type = function->value_types[
                    instruction->operands[0]];
                fprintf(output,
                        "    {\n"
                        "        aster_vec_%" PRIu32 " *tasks_list = "
                        "v%" PRIu32 ";\n"
                        "        size_t tasks_count = tasks_list != NULL "
                        "? tasks_list->length : 0U;\n"
                        "        aster_task **tasks_data = tasks_list != NULL "
                        "? tasks_list->data : NULL;\n"
                        "        free(tasks_list);\n"
                        "        v%" PRIu32 " = aster_when_any_start("
                        "tasks_data, tasks_count);\n"
                        "    }\n",
                        input_type, instruction->operands[0],
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "CancellationTokenSource::New") == 0) {
                fprintf(output,
                        "    v%" PRIu32 " = aster_cancellation_new();\n",
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "CancellationToken::None") == 0) {
                fprintf(output, "    v%" PRIu32 " = NULL;\n",
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "CancellationTokenSource::Token") == 0) {
                fprintf(output, "    v%" PRIu32 " = v%" PRIu32 ";\n",
                        instruction->result, instruction->operands[0]);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "CancellationTokenSource::Cancel") == 0) {
                fprintf(output,
                        "    if (v%" PRIu32 " == NULL)\n"
                        "        aster_trap(\"CancellationTokenSource.Cancel requires a source\");\n"
                        "    v%" PRIu32 "->requested = true;\n"
                        "    aster_cancellation_drop(v%" PRIu32 ");\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->operands[0], instruction->operands[0],
                        instruction->operands[0], instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "CancellationToken::IsCancellationRequested") == 0) {
                fprintf(output,
                        "    v%" PRIu32 " = v%" PRIu32
                        " != NULL && v%" PRIu32 "->requested;\n"
                        "    aster_cancellation_drop(v%" PRIu32 ");\n",
                        instruction->result, instruction->operands[0],
                        instruction->operands[0], instruction->operands[0]);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "CancellationToken::ThrowIfCancellationRequested") == 0) {
                fprintf(output,
                        "    if (v%" PRIu32 " != NULL && "
                        "v%" PRIu32 "->requested) {\n"
                        "        if (aster_exception_pending)\n"
                        "            aster_string_drop(aster_exception_message);\n"
                        "        aster_exception_message = aster_string_from((aster_str){\n"
                        "            (const unsigned char *)\"The operation was canceled.\",\n"
                        "            sizeof(\"The operation was canceled.\") - 1U});\n"
                        "        aster_exception_type = \"OperationCanceledException\";\n"
                        "        aster_exception_pending = true;\n"
                        "    }\n"
                        "    aster_cancellation_drop(v%" PRIu32 ");\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->operands[0], instruction->operands[0],
                        instruction->operands[0], instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Arena::new") == 0) {
                fprintf(output,
                        "    v%" PRIu32 " = aster_arena_new();\n",
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "NativeUtcNowUnixMilliseconds") == 0) {
                fprintf(output,
                        "    v%" PRIu32
                        " = aster_utc_now_unix_milliseconds();\n",
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "ArenaReset") == 0) {
                fprintf(output,
                        "    aster_arena_reset(v%" PRIu32 ");\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->operands[0],
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Buffer::allocate") == 0) {
                fprintf(output,
                        "    if (v%" PRIu32 " < 0)\n"
                        "        aster_trap(\"Buffer size must be non-negative\");\n"
                        "    v%" PRIu32
                        " = aster_allocate(sizeof(*v%" PRIu32 "));\n"
                        "    v%" PRIu32 "->length = (size_t)v%" PRIu32 ";\n"
                        "    v%" PRIu32 "->data = v%" PRIu32
                        "->length == 0U ? NULL : "
                        "aster_allocate(v%" PRIu32 "->length);\n"
                        "    if (v%" PRIu32 "->length != 0U)\n"
                        "        memset(v%" PRIu32 "->data, 0, "
                        "v%" PRIu32 "->length);\n",
                        instruction->operands[0],
                        instruction->result, instruction->result,
                        instruction->result, instruction->operands[0],
                        instruction->result, instruction->result,
                        instruction->result, instruction->result,
                        instruction->result, instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "BufferAsMutSlice") == 0) {
                fprintf(output,
                        "    v%" PRIu32 " = (aster_slice_%" PRIu32
                        "){v%" PRIu32 "->data, v%" PRIu32 "->length};\n",
                        instruction->result, instruction->result_type,
                        instruction->operands[0],
                        instruction->operands[0]);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "ByteSliceLen") == 0) {
                fprintf(output,
                        "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                        ".length;\n",
                        instruction->result,
                        instruction->operands[0]);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "ByteSliceAt") == 0) {
                fprintf(output,
                        "    if (v%" PRIu32 " >= v%" PRIu32 ".length)\n"
                        "        aster_trap(\"byte slice index is out of bounds\");\n"
                        "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                        ".data[(size_t)v%" PRIu32 "];\n",
                        instruction->operands[1],
                        instruction->operands[0],
                        instruction->result,
                        instruction->operands[0],
                        instruction->operands[1]);
                return;
            }
            bool console_stdout = instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "Console::WriteLine") == 0 ||
                 strcmp(instruction->symbol, "Console::Write") == 0);
            bool console_stderr = instruction->symbol != NULL &&
                (strcmp(instruction->symbol,
                        "Console::Error::WriteLine") == 0 ||
                 strcmp(instruction->symbol,
                        "Console::Error::Write") == 0);
            if ((console_stdout || console_stderr) &&
                instruction->operand_count == 1U) {
                IrValueId argument_value = instruction->operands[0];
                IrTypeId argument_type =
                    function->value_types[argument_value];
                const IrType *argument = &emitter->ir->types[
                    argument_type];
                const char *stream = console_stdout ? "stdout" : "stderr";
                bool newline =
                    strcmp(instruction->symbol, "Console::WriteLine") == 0 ||
                    strcmp(instruction->symbol,
                           "Console::Error::WriteLine") == 0;
                if (argument->shape == IR_TYPE_STRING_VIEW)
                    fprintf(
                        output,
                        "    aster_console_write_str(%s, v%" PRIu32
                        ", %s);\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        stream, argument_value,
                        newline ? "true" : "false",
                        instruction->result);
                else if (argument->shape == IR_TYPE_BUILTIN_OBJECT &&
                         strcmp(argument->name, "string") == 0) {
                    fprintf(
                        output,
                        "    aster_console_write_string(%s, v%" PRIu32
                        ", %s);\n",
                        stream, argument_value,
                        newline ? "true" : "false");
                    if (instruction->argument_mode_count == 0U ||
                        !parameter_mode_is_reference(
                            instruction->argument_modes[0])) {
                        fputs("    ", output);
                        c_backend_emit_drop_call(
                            emitter,
                            argument_type,
                            "v", argument_value);
                        fputs(";\n", output);
                    }
                    fprintf(output,
                            "    v%" PRIu32 " = UINT8_C(0);\n",
                            instruction->result);
                }
                else if (argument->shape == IR_TYPE_BOOL) {
                    fprintf(
                        output,
                        "    fputs(v%" PRIu32
                        " ? \"true\" : \"false\", %s);\n",
                        argument_value, stream);
                    if (newline)
                        fprintf(output, "    fputc('\\n', %s);\n", stream);
                    fprintf(
                        output,
                        "    (void)fflush(%s);\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        stream,
                        instruction->result);
                }
                else if (argument->shape == IR_TYPE_SIGNED_INT) {
                    fprintf(
                        output,
                        "    fprintf(%s, \"%%\" PRId64, "
                        "v%" PRIu32 ");\n",
                        stream, argument_value);
                    if (newline)
                        fprintf(output, "    fputc('\\n', %s);\n", stream);
                    fprintf(output,
                            "    (void)fflush(%s);\n"
                            "    v%" PRIu32 " = UINT8_C(0);\n",
                            stream,
                            instruction->result);
                }
                else if (argument->shape == IR_TYPE_UNSIGNED_INT ||
                         argument->shape == IR_TYPE_CHAR) {
                    fprintf(
                        output,
                        "    fprintf(%s, \"%%\" PRIu64, "
                        "(uint64_t)v%" PRIu32 ");\n",
                        stream, argument_value);
                    if (newline)
                        fprintf(output, "    fputc('\\n', %s);\n", stream);
                    fprintf(output,
                            "    (void)fflush(%s);\n"
                            "    v%" PRIu32 " = UINT8_C(0);\n",
                            stream, instruction->result);
                }
                else if (argument->shape == IR_TYPE_FLOAT) {
                    fprintf(
                        output,
                        "    fprintf(%s, \"%%g\", "
                        "(double)v%" PRIu32 ");\n",
                        stream, argument_value);
                    if (newline)
                        fprintf(output, "    fputc('\\n', %s);\n", stream);
                    fprintf(output,
                            "    (void)fflush(%s);\n"
                            "    v%" PRIu32 " = UINT8_C(0);\n",
                            stream, instruction->result);
                }
                else
                    c_backend_unsupported(
                        emitter, instruction->span,
                        "this Console output argument");
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Dictionary::New") == 0) {
                fprintf(output,
                        "    v%" PRIu32
                        " = aster_allocate(sizeof(*v%" PRIu32 "));\n"
                        "    *v%" PRIu32
                        " = (aster_dictionary_%" PRIu32 "){0};\n",
                        instruction->result, instruction->result,
                        instruction->result, instruction->result_type);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Dictionary::Count") == 0) {
                fprintf(output,
                        "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                        "->length;\n",
                        instruction->result, instruction->operands[0]);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Dictionary::Capacity") == 0) {
                fprintf(output,
                        "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                        "->capacity;\n",
                        instruction->result, instruction->operands[0]);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "Dictionary::EnsureCapacity") == 0) {
                IrValueId dictionary = instruction->operands[0];
                IrValueId requested = instruction->operands[1];
                IrTypeId dictionary_type =
                    function->value_types[dictionary];
                fprintf(output,
                        "    if ((uint64_t)v%" PRIu32 " > (uint64_t)SIZE_MAX)\n"
                        "        aster_trap(\"Dictionary capacity overflow\");\n"
                        "    if ((size_t)v%" PRIu32 " > v%" PRIu32
                        "->capacity) {\n"
                        "        size_t capacity = (size_t)v%" PRIu32 ";\n"
                        "        if (capacity > SIZE_MAX / sizeof(*v%" PRIu32
                        "->keys) || capacity > SIZE_MAX / sizeof(*v%" PRIu32
                        "->values) || capacity > SIZE_MAX / sizeof(*v%" PRIu32
                        "->hashes))\n"
                        "            aster_trap(\"Dictionary capacity overflow\");\n"
                        "        void *keys = realloc(v%" PRIu32 "->keys,\n"
                        "            capacity * sizeof(*v%" PRIu32 "->keys));\n"
                        "        if (keys == NULL) aster_trap(\"out of memory\");\n"
                        "        v%" PRIu32 "->keys = keys;\n"
                        "        void *values = realloc(v%" PRIu32 "->values,\n"
                        "            capacity * sizeof(*v%" PRIu32 "->values));\n"
                        "        if (values == NULL) aster_trap(\"out of memory\");\n"
                        "        v%" PRIu32 "->values = values;\n"
                        "        void *hashes = realloc(v%" PRIu32 "->hashes,\n"
                        "            capacity * sizeof(*v%" PRIu32 "->hashes));\n"
                        "        if (hashes == NULL) aster_trap(\"out of memory\");\n"
                        "        v%" PRIu32 "->hashes = hashes;\n"
                        "        v%" PRIu32 "->capacity = capacity;\n"
                        "        aster_dictionary_rebuild_%" PRIu32 "(v%" PRIu32 ");\n"
                        "    }\n"
                        "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                        "->capacity;\n",
                        requested, requested, dictionary, requested,
                        dictionary, dictionary, dictionary, dictionary,
                        dictionary, dictionary, dictionary, dictionary,
                        dictionary, dictionary, dictionary, dictionary,
                        dictionary, dictionary_type, dictionary,
                        instruction->result, dictionary);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Dictionary::TrimExcess") == 0) {
                IrValueId dictionary = instruction->operands[0];
                IrTypeId dictionary_type =
                    function->value_types[dictionary];
                bool explicit_capacity = instruction->operand_count == 2U;
                if (explicit_capacity)
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " > (uint64_t)SIZE_MAX || (size_t)v%" PRIu32
                            " < v%" PRIu32 "->length)\n"
                            "        aster_trap(\"Dictionary capacity cannot be less than Count\");\n"
                            "    size_t dictionary_capacity_%" PRIu32
                            " = (size_t)v%" PRIu32 ";\n",
                            instruction->operands[1],
                            instruction->operands[1], dictionary,
                            instruction->result,
                            instruction->operands[1]);
                else
                    fprintf(output,
                            "    size_t dictionary_capacity_%" PRIu32
                            " = v%" PRIu32 "->length;\n",
                            instruction->result, dictionary);
                fprintf(output,
                        "    if (dictionary_capacity_%" PRIu32 " == 0U) {\n"
                        "        free(v%" PRIu32 "->keys);\n"
                        "        free(v%" PRIu32 "->values);\n"
                        "        free(v%" PRIu32 "->hashes);\n"
                        "        v%" PRIu32 "->keys = NULL;\n"
                        "        v%" PRIu32 "->values = NULL;\n"
                        "        v%" PRIu32 "->hashes = NULL;\n"
                        "    } else if (dictionary_capacity_%" PRIu32
                        " != v%" PRIu32 "->capacity) {\n"
                        "        void *keys = realloc(v%" PRIu32 "->keys,\n"
                        "            dictionary_capacity_%" PRIu32
                        " * sizeof(*v%" PRIu32 "->keys));\n"
                        "        if (keys == NULL) aster_trap(\"out of memory\");\n"
                        "        v%" PRIu32 "->keys = keys;\n"
                        "        void *values = realloc(v%" PRIu32 "->values,\n"
                        "            dictionary_capacity_%" PRIu32
                        " * sizeof(*v%" PRIu32 "->values));\n"
                        "        if (values == NULL) aster_trap(\"out of memory\");\n"
                        "        v%" PRIu32 "->values = values;\n"
                        "        void *hashes = realloc(v%" PRIu32 "->hashes,\n"
                        "            dictionary_capacity_%" PRIu32
                        " * sizeof(*v%" PRIu32 "->hashes));\n"
                        "        if (hashes == NULL) aster_trap(\"out of memory\");\n"
                        "        v%" PRIu32 "->hashes = hashes;\n"
                        "    }\n"
                        "    v%" PRIu32 "->capacity = dictionary_capacity_%" PRIu32
                        ";\n"
                        "    aster_dictionary_rebuild_%" PRIu32 "(v%" PRIu32 ");\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->result, dictionary, dictionary, dictionary,
                        dictionary, dictionary, dictionary,
                        instruction->result, dictionary, dictionary,
                        instruction->result, dictionary, dictionary, dictionary,
                        instruction->result, dictionary, dictionary, dictionary,
                        instruction->result, dictionary, dictionary,
                        dictionary, instruction->result, dictionary_type,
                        dictionary, instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Dictionary::Clear") == 0) {
                const IrType *dictionary = &emitter->ir->types[
                    function->value_types[instruction->operands[0]]];
                if (c_backend_type_needs_drop(
                        emitter, dictionary->element_type))
                    fprintf(output,
                            "    for (size_t i = v%" PRIu32
                            "->length; i > 0U; --i)\n"
                            "        aster_drop_%" PRIu32
                            "(&v%" PRIu32 "->keys[i - 1U]);\n",
                            instruction->operands[0],
                            dictionary->element_type,
                            instruction->operands[0]);
                if (c_backend_type_needs_drop(
                        emitter, dictionary->error_type))
                    fprintf(output,
                            "    for (size_t i = v%" PRIu32
                            "->length; i > 0U; --i)\n"
                            "        aster_drop_%" PRIu32
                            "(&v%" PRIu32 "->values[i - 1U]);\n",
                            instruction->operands[0],
                            dictionary->error_type,
                            instruction->operands[0]);
                fprintf(output,
                        "    v%" PRIu32 "->length = 0U;\n"
                        "    if (v%" PRIu32 "->buckets != NULL)\n"
                        "        memset(v%" PRIu32 "->buckets, 0, v%" PRIu32
                        "->bucket_count * sizeof(*v%" PRIu32 "->buckets));\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->operands[0], instruction->operands[0],
                        instruction->operands[0], instruction->operands[0],
                        instruction->operands[0], instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "Dictionary::Add") == 0 ||
                 strcmp(instruction->symbol, "Dictionary::TryAdd") == 0 ||
                 strcmp(instruction->symbol, "Dictionary::TryGetValue") == 0 ||
                 strcmp(instruction->symbol, "Dictionary::ContainsKey") == 0 ||
                 strcmp(instruction->symbol, "Dictionary::ContainsValue") == 0 ||
                 strcmp(instruction->symbol, "Dictionary::Remove") == 0 ||
                 strcmp(instruction->symbol, "Dictionary::Get") == 0 ||
                 strcmp(instruction->symbol, "Dictionary::Set") == 0)) {
                const IrType *dictionary = &emitter->ir->types[
                    function->value_types[instruction->operands[0]]];
                bool add = strcmp(instruction->symbol,
                                  "Dictionary::Add") == 0;
                bool try_add = strcmp(instruction->symbol,
                                      "Dictionary::TryAdd") == 0;
                bool try_get = strcmp(instruction->symbol,
                                      "Dictionary::TryGetValue") == 0;
                bool contains = strcmp(instruction->symbol,
                                       "Dictionary::ContainsKey") == 0;
                bool contains_value = strcmp(
                    instruction->symbol, "Dictionary::ContainsValue") == 0;
                bool remove = strcmp(instruction->symbol,
                                     "Dictionary::Remove") == 0;
                bool get = strcmp(instruction->symbol,
                                  "Dictionary::Get") == 0;
                bool set = strcmp(instruction->symbol,
                                  "Dictionary::Set") == 0;
                fprintf(output,
                        "    size_t dictionary_match_%" PRIu32
                        " = SIZE_MAX;\n",
                        instruction->result);
                if (contains_value) {
                    fprintf(output,
                            "    for (size_t i = 0U; i < v%" PRIu32
                            "->length; ++i) {\n"
                            "        if (",
                            instruction->operands[0]);
                    emit_dictionary_value_equality(
                        emitter,
                        &emitter->ir->types[dictionary->error_type],
                        instruction->operands[0], "i",
                        instruction->operands[1]);
                    fprintf(output,
                            ") { dictionary_match_%" PRIu32
                            " = i; break; }\n"
                            "    }\n",
                            instruction->result);
                } else {
                    IrTypeId dictionary_type =
                        function->value_types[instruction->operands[0]];
                    fprintf(output,
                            "    uint64_t dictionary_hash_%" PRIu32
                            " = aster_dictionary_hash_%" PRIu32
                            "(v%" PRIu32 ");\n"
                            "    if (v%" PRIu32 "->bucket_count != 0U) {\n"
                            "        size_t dictionary_bucket_%" PRIu32
                            " = (size_t)dictionary_hash_%" PRIu32
                            " & (v%" PRIu32 "->bucket_count - 1U);\n"
                            "        for (;;) {\n"
                            "            size_t dictionary_entry_%" PRIu32
                            " = v%" PRIu32 "->buckets[dictionary_bucket_%" PRIu32
                            "];\n"
                            "            if (dictionary_entry_%" PRIu32
                            " == 0U) break;\n"
                            "            size_t dictionary_index_%" PRIu32
                            " = dictionary_entry_%" PRIu32 " - 1U;\n"
                            "            if (v%" PRIu32
                            "->hashes[dictionary_index_%" PRIu32
                            "] == dictionary_hash_%" PRIu32 " && ",
                            instruction->result, dictionary_type,
                            instruction->operands[1], instruction->operands[0],
                            instruction->result, instruction->result,
                            instruction->operands[0], instruction->result,
                            instruction->operands[0], instruction->result,
                            instruction->result, instruction->result,
                            instruction->result, instruction->operands[0],
                            instruction->result, instruction->result);
                    char dictionary_index[64];
                    snprintf(dictionary_index, sizeof(dictionary_index),
                             "dictionary_index_%" PRIu32,
                             instruction->result);
                    emit_dictionary_key_equality(
                        emitter,
                        &emitter->ir->types[dictionary->element_type],
                        instruction->operands[0], dictionary_index,
                        instruction->operands[1]);
                    fprintf(output,
                            ") {\n"
                            "                dictionary_match_%" PRIu32
                            " = dictionary_index_%" PRIu32 ";\n"
                            "                break;\n"
                            "            }\n"
                            "            dictionary_bucket_%" PRIu32
                            " = (dictionary_bucket_%" PRIu32
                            " + 1U) & (v%" PRIu32
                            "->bucket_count - 1U);\n"
                            "        }\n"
                            "    }\n",
                            instruction->result, instruction->result,
                            instruction->result, instruction->result,
                            instruction->operands[0]);
                }
                if (contains || contains_value) {
                    fprintf(output,
                            "    v%" PRIu32 " = dictionary_match_%" PRIu32
                            " != SIZE_MAX;\n",
                            instruction->result, instruction->result);
                } else if (try_get) {
                    if (c_backend_type_needs_drop(
                            emitter, dictionary->error_type)) {
                        fprintf(output, "    aster_drop_%" PRIu32 "(",
                                dictionary->error_type);
                        emit_borrowed_call_operand(
                            emitter, function, instruction->operands[2]);
                        fputs(");\n", output);
                    }
                    fputs("    memset(", output);
                    emit_borrowed_call_operand(
                        emitter, function, instruction->operands[2]);
                    fputs(", 0, sizeof(*", output);
                    emit_borrowed_call_operand(
                        emitter, function, instruction->operands[2]);
                    fputs("));\n", output);
                    fprintf(output,
                            "    v%" PRIu32 " = dictionary_match_%" PRIu32
                            " != SIZE_MAX;\n"
                            "    if (v%" PRIu32 ") {\n",
                            instruction->result, instruction->result,
                            instruction->result);
                    fputs("        *", output);
                    emit_borrowed_call_operand(
                        emitter, function, instruction->operands[2]);
                    fputs(" = ", output);
                    if (c_backend_type_needs_drop(
                            emitter, dictionary->error_type))
                        fprintf(output, "aster_clone_%" PRIu32 "(",
                                dictionary->error_type);
                    fprintf(output,
                            "v%" PRIu32 "->values[dictionary_match_%" PRIu32 "]",
                            instruction->operands[0], instruction->result);
                    if (c_backend_type_needs_drop(
                            emitter, dictionary->error_type))
                        fputc(')', output);
                    fputs(";\n    }\n", output);
                } else if (get) {
                    fprintf(output,
                            "    if (dictionary_match_%" PRIu32
                            " == SIZE_MAX)\n"
                            "        aster_trap(\"Dictionary key was not found\");\n"
                            "    v%" PRIu32 " = ",
                            instruction->result, instruction->result);
                    if (c_backend_type_needs_drop(
                            emitter, dictionary->error_type))
                        fprintf(output, "aster_clone_%" PRIu32 "(",
                                dictionary->error_type);
                    fprintf(output,
                            "v%" PRIu32 "->values[dictionary_match_%" PRIu32 "]",
                            instruction->operands[0], instruction->result);
                    if (c_backend_type_needs_drop(
                            emitter, dictionary->error_type))
                        fputc(')', output);
                    fputs(";\n", output);
                } else if (remove) {
                    fprintf(output,
                            "    v%" PRIu32 " = dictionary_match_%" PRIu32
                            " != SIZE_MAX;\n"
                            "    if (v%" PRIu32 ") {\n",
                            instruction->result, instruction->result,
                            instruction->result);
                    if (c_backend_type_needs_drop(
                            emitter, dictionary->element_type))
                        fprintf(output,
                                "        aster_drop_%" PRIu32
                                "(&v%" PRIu32 "->keys[dictionary_match_%" PRIu32
                                "]);\n",
                                dictionary->element_type,
                                instruction->operands[0], instruction->result);
                    if (c_backend_type_needs_drop(
                            emitter, dictionary->error_type))
                        fprintf(output,
                                "        aster_drop_%" PRIu32
                                "(&v%" PRIu32 "->values[dictionary_match_%" PRIu32
                                "]);\n",
                                dictionary->error_type,
                                instruction->operands[0], instruction->result);
                    fprintf(output,
                            "        memmove(&v%" PRIu32
                            "->keys[dictionary_match_%" PRIu32 "], &v%" PRIu32
                            "->keys[dictionary_match_%" PRIu32 " + 1U],\n"
                            "            (v%" PRIu32
                            "->length - dictionary_match_%" PRIu32
                            " - 1U) * sizeof(*v%" PRIu32 "->keys));\n"
                            "        memmove(&v%" PRIu32
                            "->values[dictionary_match_%" PRIu32 "], &v%" PRIu32
                            "->values[dictionary_match_%" PRIu32 " + 1U],\n"
                            "            (v%" PRIu32
                            "->length - dictionary_match_%" PRIu32
                            " - 1U) * sizeof(*v%" PRIu32 "->values));\n"
                            "        memmove(&v%" PRIu32
                            "->hashes[dictionary_match_%" PRIu32 "], &v%" PRIu32
                            "->hashes[dictionary_match_%" PRIu32 " + 1U],\n"
                            "            (v%" PRIu32
                            "->length - dictionary_match_%" PRIu32
                            " - 1U) * sizeof(*v%" PRIu32 "->hashes));\n"
                            "        --v%" PRIu32 "->length;\n"
                            "        aster_dictionary_rebuild_%" PRIu32
                            "(v%" PRIu32 ");\n"
                            "    }\n",
                            instruction->operands[0], instruction->result,
                            instruction->operands[0], instruction->result,
                            instruction->operands[0], instruction->result,
                            instruction->operands[0],
                            instruction->operands[0], instruction->result,
                            instruction->operands[0], instruction->result,
                            instruction->operands[0], instruction->result,
                            instruction->operands[0],
                            instruction->operands[0], instruction->result,
                            instruction->operands[0], instruction->result,
                            instruction->operands[0], instruction->result,
                            instruction->operands[0],
                            instruction->operands[0],
                            function->value_types[instruction->operands[0]],
                            instruction->operands[0]);
                } else {
                    if (add)
                        fprintf(output,
                                "    if (dictionary_match_%" PRIu32
                                " != SIZE_MAX)\n"
                                "        aster_trap(\"Dictionary already contains the key\");\n",
                                instruction->result);
                    if (try_add)
                        fprintf(output,
                                "    if (dictionary_match_%" PRIu32
                                " != SIZE_MAX) {\n"
                                "        v%" PRIu32 " = false;\n",
                                instruction->result, instruction->result);
                    if (try_add && c_backend_type_needs_drop(
                            emitter, dictionary->element_type))
                        fprintf(output,
                                "        aster_drop_%" PRIu32
                                "(&v%" PRIu32 ");\n",
                                dictionary->element_type,
                                instruction->operands[1]);
                    if (try_add && c_backend_type_needs_drop(
                            emitter, dictionary->error_type))
                        fprintf(output,
                                "        aster_drop_%" PRIu32
                                "(&v%" PRIu32 ");\n",
                                dictionary->error_type,
                                instruction->operands[2]);
                    if (try_add) fputs("    } else {\n", output);
                    if (set) {
                        fprintf(output,
                                "    if (dictionary_match_%" PRIu32
                                " != SIZE_MAX) {\n",
                                instruction->result);
                        if (c_backend_type_needs_drop(
                                emitter, dictionary->element_type))
                            fprintf(output,
                                    "        aster_drop_%" PRIu32
                                    "(&v%" PRIu32 ");\n",
                                    dictionary->element_type,
                                    instruction->operands[1]);
                        if (c_backend_type_needs_drop(
                                emitter, dictionary->error_type))
                            fprintf(output,
                                    "        aster_drop_%" PRIu32
                                    "(&v%" PRIu32
                                    "->values[dictionary_match_%" PRIu32 "]);\n",
                                    dictionary->error_type,
                                    instruction->operands[0],
                                    instruction->result);
                        fprintf(output,
                                "        v%" PRIu32
                                "->values[dictionary_match_%" PRIu32
                                "] = v%" PRIu32 ";\n"
                                "    } else {\n",
                                instruction->operands[0], instruction->result,
                                instruction->operands[2]);
                    }
                    IrValueId dictionary_value = instruction->operands[0];
                    fprintf(output,
                            "    %sif (v%" PRIu32 "->length == v%" PRIu32
                            "->capacity) {\n"
                            "        size_t capacity = v%" PRIu32
                            "->capacity == 0U ? 4U : v%" PRIu32
                            "->capacity * 2U;\n"
                            "        if (capacity < v%" PRIu32 "->capacity)\n"
                            "            aster_trap(\"Dictionary capacity overflow\");\n",
                            (set || try_add) ? "    " : "", dictionary_value,
                            dictionary_value, dictionary_value,
                            dictionary_value, dictionary_value);
                    fprintf(output,
                            "        void *keys = realloc(v%" PRIu32 "->keys,\n"
                            "            capacity * sizeof(*v%" PRIu32 "->keys));\n"
                            "        if (keys == NULL) aster_trap(\"out of memory\");\n"
                            "        v%" PRIu32 "->keys = keys;\n",
                            dictionary_value, dictionary_value,
                            dictionary_value);
                    fprintf(output,
                            "        void *values = realloc(v%" PRIu32 "->values,\n"
                            "            capacity * sizeof(*v%" PRIu32 "->values));\n"
                            "        if (values == NULL) aster_trap(\"out of memory\");\n"
                            "        v%" PRIu32 "->values = values;\n"
                            "        void *hashes = realloc(v%" PRIu32 "->hashes,\n"
                            "            capacity * sizeof(*v%" PRIu32 "->hashes));\n"
                            "        if (hashes == NULL) aster_trap(\"out of memory\");\n"
                            "        v%" PRIu32 "->hashes = hashes;\n"
                            "        v%" PRIu32 "->capacity = capacity;\n"
                            "        aster_dictionary_rebuild_%" PRIu32
                            "(v%" PRIu32 ");\n"
                            "    }\n",
                            dictionary_value, dictionary_value,
                            dictionary_value, dictionary_value,
                            dictionary_value, dictionary_value,
                            dictionary_value,
                            function->value_types[dictionary_value],
                            dictionary_value);
                    fprintf(output,
                            "    v%" PRIu32 "->keys[v%" PRIu32
                            "->length] = v%" PRIu32 ";\n"
                            "    v%" PRIu32 "->values[v%" PRIu32
                            "->length] = v%" PRIu32 ";\n"
                            "    v%" PRIu32 "->hashes[v%" PRIu32
                            "->length] = dictionary_hash_%" PRIu32 ";\n"
                            "    size_t dictionary_bucket_insert_%" PRIu32
                            " = (size_t)dictionary_hash_%" PRIu32
                            " & (v%" PRIu32 "->bucket_count - 1U);\n"
                            "    while (v%" PRIu32
                            "->buckets[dictionary_bucket_insert_%" PRIu32
                            "] != 0U)\n"
                            "        dictionary_bucket_insert_%" PRIu32
                            " = (dictionary_bucket_insert_%" PRIu32
                            " + 1U) & (v%" PRIu32
                            "->bucket_count - 1U);\n"
                            "    v%" PRIu32
                            "->buckets[dictionary_bucket_insert_%" PRIu32
                            "] = v%" PRIu32 "->length + 1U;\n"
                            "    ++v%" PRIu32 "->length;\n",
                            dictionary_value, dictionary_value,
                            instruction->operands[1], dictionary_value,
                            dictionary_value, instruction->operands[2],
                            dictionary_value, dictionary_value,
                            instruction->result, instruction->result,
                            instruction->result, dictionary_value,
                            dictionary_value, instruction->result,
                            instruction->result, instruction->result,
                            dictionary_value, dictionary_value,
                            instruction->result, dictionary_value,
                            dictionary_value);
                    if (set || try_add) fputs("    }\n", output);
                    if (try_add)
                        fprintf(output,
                                "    if (dictionary_match_%" PRIu32
                                " == SIZE_MAX) v%" PRIu32 " = true;\n",
                                instruction->result, instruction->result);
                    else
                        fprintf(output,
                                "    v%" PRIu32 " = UINT8_C(0);\n",
                                instruction->result);
                    return;
                }
                IrTypeId searched_type = contains_value
                    ? dictionary->error_type : dictionary->element_type;
                if (c_backend_type_needs_drop(emitter, searched_type))
                    fprintf(output,
                            "    aster_drop_%" PRIu32 "(&v%" PRIu32 ");\n",
                            searched_type,
                            instruction->operands[1]);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Queue::New") == 0) {
                fprintf(output,
                        "    v%" PRIu32
                        " = aster_allocate(sizeof(*v%" PRIu32 "));\n"
                        "    *v%" PRIu32 " = (aster_queue_%" PRIu32
                        "){0};\n",
                        instruction->result, instruction->result,
                        instruction->result, instruction->result_type);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Queue::Enqueue") == 0) {
                IrValueId queue = instruction->operands[0];
                fprintf(output,
                        "    if (v%" PRIu32 "->length == v%" PRIu32
                        "->capacity) {\n"
                        "        size_t capacity = v%" PRIu32
                        "->capacity == 0U ? 4U : v%" PRIu32
                        "->capacity * 2U;\n"
                        "        if (capacity < v%" PRIu32 "->capacity)\n"
                        "            aster_trap(\"Queue capacity overflow\");\n"
                        "        aster_queue_resize_%" PRIu32
                        "(v%" PRIu32 ", capacity);\n"
                        "    }\n"
                        "    size_t queue_tail_%" PRIu32 " = (v%" PRIu32
                        "->head + v%" PRIu32 "->length) %% v%" PRIu32
                        "->capacity;\n"
                        "    v%" PRIu32 "->data[queue_tail_%" PRIu32
                        "] = v%" PRIu32 ";\n"
                        "    ++v%" PRIu32 "->length;\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        queue, queue, queue, queue, queue,
                        function->value_types[queue], queue,
                        instruction->result, queue, queue, queue,
                        queue, instruction->result,
                        instruction->operands[1], queue,
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "Queue::TryDequeue") == 0 ||
                 strcmp(instruction->symbol, "Queue::TryPeek") == 0)) {
                bool dequeue = strcmp(instruction->symbol,
                                      "Queue::TryDequeue") == 0;
                IrValueId queue = instruction->operands[0];
                const IrType *queue_type = &emitter->ir->types[
                    function->value_types[queue]];
                if (c_backend_type_needs_drop(
                        emitter, queue_type->element_type)) {
                    fprintf(output, "    aster_drop_%" PRIu32 "(",
                            queue_type->element_type);
                    emit_borrowed_call_operand(
                        emitter, function, instruction->operands[1]);
                    fputs(");\n", output);
                }
                fputs("    memset(", output);
                emit_borrowed_call_operand(
                    emitter, function, instruction->operands[1]);
                fputs(", 0, sizeof(*", output);
                emit_borrowed_call_operand(
                    emitter, function, instruction->operands[1]);
                fputs("));\n", output);
                fprintf(output,
                        "    v%" PRIu32 " = v%" PRIu32 "->length != 0U;\n"
                        "    if (v%" PRIu32 ") {\n",
                        instruction->result, queue, instruction->result);
                fputs("        *", output);
                emit_borrowed_call_operand(
                    emitter, function, instruction->operands[1]);
                fputs(" = ", output);
                if (!dequeue && c_backend_type_needs_drop(
                        emitter, queue_type->element_type))
                    fprintf(output, "aster_clone_%" PRIu32 "(",
                            queue_type->element_type);
                fprintf(output, "v%" PRIu32 "->data[v%" PRIu32 "->head]",
                        queue, queue);
                if (!dequeue && c_backend_type_needs_drop(
                        emitter, queue_type->element_type))
                    fputc(')', output);
                fputs(";\n", output);
                if (dequeue)
                    fprintf(output,
                            "        memset(&v%" PRIu32 "->data[v%" PRIu32
                            "->head], 0, sizeof(*v%" PRIu32 "->data));\n"
                            "        --v%" PRIu32 "->length;\n"
                            "        v%" PRIu32 "->head = v%" PRIu32
                            "->length == 0U ? 0U : (v%" PRIu32
                            "->head + 1U) %% v%" PRIu32 "->capacity;\n",
                            queue, queue, queue, queue, queue, queue,
                            queue, queue);
                fputs("    }\n", output);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "Queue::Dequeue") == 0 ||
                 strcmp(instruction->symbol, "Queue::Peek") == 0)) {
                bool dequeue =
                    strcmp(instruction->symbol, "Queue::Dequeue") == 0;
                IrValueId queue = instruction->operands[0];
                const IrType *queue_type = &emitter->ir->types[
                    function->value_types[queue]];
                fprintf(output,
                        "    if (v%" PRIu32 "->length == 0U)\n"
                        "        aster_trap(\"Queue is empty\");\n"
                        "    v%" PRIu32 " = ",
                        queue, instruction->result);
                if (!dequeue && c_backend_type_needs_drop(
                        emitter, queue_type->element_type))
                    fprintf(output, "aster_clone_%" PRIu32 "(",
                            queue_type->element_type);
                fprintf(output, "v%" PRIu32 "->data[v%" PRIu32 "->head]",
                        queue, queue);
                if (!dequeue && c_backend_type_needs_drop(
                        emitter, queue_type->element_type))
                    fputc(')', output);
                fputs(";\n", output);
                if (dequeue)
                    fprintf(output,
                            "    memset(&v%" PRIu32 "->data[v%" PRIu32
                            "->head], 0, sizeof(*v%" PRIu32 "->data));\n"
                            "    --v%" PRIu32 "->length;\n"
                            "    v%" PRIu32 "->head = v%" PRIu32
                            "->length == 0U ? 0U : (v%" PRIu32
                            "->head + 1U) %% v%" PRIu32 "->capacity;\n",
                            queue, queue, queue, queue, queue, queue,
                            queue, queue);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "Queue::Count") == 0 ||
                 strcmp(instruction->symbol, "Queue::Capacity") == 0)) {
                bool count = strcmp(instruction->symbol, "Queue::Count") == 0;
                fprintf(output,
                        "    v%" PRIu32 " = (uint64_t)v%" PRIu32 "->%s;\n",
                        instruction->result, instruction->operands[0],
                        count ? "length" : "capacity");
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Queue::Clear") == 0) {
                IrValueId queue = instruction->operands[0];
                const IrType *queue_type = &emitter->ir->types[
                    function->value_types[queue]];
                if (c_backend_type_needs_drop(emitter, queue_type->element_type))
                    fprintf(output,
                            "    for (size_t i = v%" PRIu32
                            "->length; i > 0U; --i)\n"
                            "        aster_drop_%" PRIu32
                            "(&v%" PRIu32 "->data[(v%" PRIu32
                            "->head + i - 1U) %% v%" PRIu32
                            "->capacity]);\n",
                            queue, queue_type->element_type,
                            queue, queue, queue);
                fprintf(output,
                        "    v%" PRIu32 "->length = 0U;\n"
                        "    v%" PRIu32 "->head = 0U;\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        queue, queue, instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Queue::EnsureCapacity") == 0) {
                IrValueId queue = instruction->operands[0];
                IrValueId minimum = instruction->operands[1];
                fprintf(output,
                        "    if ((uint64_t)v%" PRIu32
                        " > (uint64_t)SIZE_MAX)\n"
                        "        aster_trap(\"Queue capacity overflow\");\n"
                        "    if ((size_t)v%" PRIu32 " > v%" PRIu32
                        "->capacity) {\n"
                        "        size_t capacity = v%" PRIu32
                        "->capacity == 0U ? 4U : v%" PRIu32 "->capacity;\n"
                        "        while (capacity < (size_t)v%" PRIu32 ") {\n"
                        "            if (capacity > SIZE_MAX / 2U) {\n"
                        "                capacity = (size_t)v%" PRIu32 ";\n"
                        "                break;\n"
                        "            }\n"
                        "            capacity *= 2U;\n"
                        "        }\n"
                        "        aster_queue_resize_%" PRIu32
                        "(v%" PRIu32 ", capacity);\n"
                        "    }\n"
                        "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                        "->capacity;\n",
                        minimum, minimum, queue, queue, queue, minimum,
                        minimum, function->value_types[queue], queue,
                        instruction->result, queue);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "Queue::TrimExcess") == 0) {
                IrValueId queue = instruction->operands[0];
                fprintf(output,
                        "    size_t queue_threshold_%" PRIu32 " = "
                        "(v%" PRIu32 "->capacity / 10U) * 9U + "
                        "((v%" PRIu32 "->capacity %% 10U) * 9U) / 10U;\n"
                        "    if (v%" PRIu32 "->length < queue_threshold_%" PRIu32
                        ") aster_queue_resize_%" PRIu32 "(v%" PRIu32
                        ", v%" PRIu32 "->length);\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->result, queue, queue, queue,
                        instruction->result, function->value_types[queue],
                        queue, queue, instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "List::New") == 0 ||
                 strcmp(instruction->symbol, "Stack::New") == 0)) {
                fprintf(
                    output,
                    "    v%" PRIu32
                    " = aster_allocate(sizeof(*v%" PRIu32 "));\n"
                    "    *v%" PRIu32 " = (aster_vec_%" PRIu32 "){0};\n",
                    instruction->result, instruction->result,
                    instruction->result, instruction->result_type);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "List::Add") == 0 ||
                 strcmp(instruction->symbol, "Stack::Push") == 0)) {
                IrTypeId vector_type =
                    function->value_types[instruction->operands[0]];
                fprintf(
                    output,
                    "    if (v%" PRIu32 "->length == "
                    "v%" PRIu32 "->capacity) {\n"
                    "        size_t capacity = v%" PRIu32
                    "->capacity == 0U ? 4U : "
                    "v%" PRIu32 "->capacity * 2U;\n"
                    "        if (capacity < v%" PRIu32 "->capacity)\n"
                    "            aster_trap(\"vector capacity overflow\");\n"
                    "        void *data = realloc(v%" PRIu32 "->data,\n"
                    "            capacity * sizeof(*v%" PRIu32 "->data));\n"
                    "        if (data == NULL) aster_trap(\"out of memory\");\n"
                    "        v%" PRIu32 "->data = data;\n"
                    "        v%" PRIu32 "->capacity = capacity;\n"
                    "    }\n"
                    "    v%" PRIu32 "->data[v%" PRIu32 "->length++] = "
                    "v%" PRIu32 ";\n"
                    "    v%" PRIu32 " = UINT8_C(0);\n",
                    instruction->operands[0],
                    instruction->operands[0],
                    instruction->operands[0],
                    instruction->operands[0],
                    instruction->operands[0],
                    instruction->operands[0],
                    instruction->operands[0],
                    instruction->operands[0],
                    instruction->operands[0],
                    instruction->operands[0],
                    instruction->operands[0],
                    instruction->operands[1],
                    instruction->result);
                (void)vector_type;
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "List::Count") == 0 ||
                 strcmp(instruction->symbol, "Stack::Count") == 0)) {
                fprintf(output,
                        "    v%" PRIu32
                        " = (uint64_t)v%" PRIu32 "->length;\n",
                        instruction->result,
                        instruction->operands[0]);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "List::Capacity") == 0 ||
                 strcmp(instruction->symbol, "Stack::Capacity") == 0)) {
                fprintf(output,
                        "    v%" PRIu32
                        " = (uint64_t)v%" PRIu32 "->capacity;\n",
                        instruction->result,
                        instruction->operands[0]);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "List::Clear") == 0 ||
                 strcmp(instruction->symbol, "Stack::Clear") == 0)) {
                const IrType *list_type = &emitter->ir->types[
                    function->value_types[instruction->operands[0]]];
                if (c_backend_type_needs_drop(emitter, list_type->element_type))
                    fprintf(output,
                            "    for (size_t i = v%" PRIu32
                            "->length; i > 0U; --i)\n"
                            "        aster_drop_%" PRIu32
                            "(&v%" PRIu32 "->data[i - 1U]);\n",
                            instruction->operands[0],
                            list_type->element_type,
                            instruction->operands[0]);
                fprintf(output,
                        "    v%" PRIu32 "->length = 0U;\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->operands[0], instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "Stack::TryPop") == 0 ||
                 strcmp(instruction->symbol, "Stack::TryPeek") == 0)) {
                bool pop = strcmp(instruction->symbol,
                                  "Stack::TryPop") == 0;
                IrValueId stack = instruction->operands[0];
                const IrType *stack_type = &emitter->ir->types[
                    function->value_types[stack]];
                if (c_backend_type_needs_drop(
                        emitter, stack_type->element_type)) {
                    fprintf(output, "    aster_drop_%" PRIu32 "(",
                            stack_type->element_type);
                    emit_borrowed_call_operand(
                        emitter, function, instruction->operands[1]);
                    fputs(");\n", output);
                }
                fputs("    memset(", output);
                emit_borrowed_call_operand(
                    emitter, function, instruction->operands[1]);
                fputs(", 0, sizeof(*", output);
                emit_borrowed_call_operand(
                    emitter, function, instruction->operands[1]);
                fputs("));\n", output);
                fprintf(output,
                        "    v%" PRIu32 " = v%" PRIu32 "->length != 0U;\n"
                        "    if (v%" PRIu32 ") {\n",
                        instruction->result, stack, instruction->result);
                fputs("        *", output);
                emit_borrowed_call_operand(
                    emitter, function, instruction->operands[1]);
                fputs(" = ", output);
                if (!pop && c_backend_type_needs_drop(
                        emitter, stack_type->element_type))
                    fprintf(output, "aster_clone_%" PRIu32 "(",
                            stack_type->element_type);
                fprintf(output, "v%" PRIu32 "->data[v%" PRIu32
                        "->length - 1U]", stack, stack);
                if (!pop && c_backend_type_needs_drop(
                        emitter, stack_type->element_type))
                    fputc(')', output);
                fputs(";\n", output);
                if (pop)
                    fprintf(output,
                            "        --v%" PRIu32 "->length;\n"
                            "        memset(&v%" PRIu32 "->data[v%" PRIu32
                            "->length], 0, sizeof(*v%" PRIu32 "->data));\n",
                            stack, stack, stack, stack);
                fputs("    }\n", output);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "Stack::Pop") == 0 ||
                 strcmp(instruction->symbol, "Stack::Peek") == 0)) {
                bool pop = strcmp(instruction->symbol, "Stack::Pop") == 0;
                IrValueId stack = instruction->operands[0];
                const IrType *stack_type = &emitter->ir->types[
                    function->value_types[stack]];
                fprintf(output,
                        "    if (v%" PRIu32 "->length == 0U)\n"
                        "        aster_trap(\"Stack is empty\");\n"
                        "    v%" PRIu32 " = ",
                        stack, instruction->result);
                if (!pop && c_backend_type_needs_drop(
                        emitter, stack_type->element_type))
                    fprintf(output, "aster_clone_%" PRIu32 "(",
                            stack_type->element_type);
                fprintf(output, "v%" PRIu32 "->data[v%" PRIu32
                        "->length - 1U]", stack, stack);
                if (!pop && c_backend_type_needs_drop(
                        emitter, stack_type->element_type))
                    fputc(')', output);
                fputs(";\n", output);
                if (pop)
                    fprintf(output,
                            "    --v%" PRIu32 "->length;\n"
                            "    memset(&v%" PRIu32 "->data[v%" PRIu32
                            "->length], 0, sizeof(*v%" PRIu32 "->data));\n",
                            stack, stack, stack, stack);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "List::Insert") == 0) {
                fprintf(output,
                        "    if ((uint64_t)v%" PRIu32
                        " > (uint64_t)v%" PRIu32 "->length)\n"
                        "        aster_trap(\"List insert index out of bounds\");\n"
                        "    if (v%" PRIu32 "->length == v%" PRIu32
                        "->capacity) {\n"
                        "        size_t capacity = v%" PRIu32
                        "->capacity == 0U ? 4U : v%" PRIu32
                        "->capacity * 2U;\n"
                        "        if (capacity < v%" PRIu32 "->capacity)\n"
                        "            aster_trap(\"List capacity overflow\");\n"
                        "        void *data = realloc(v%" PRIu32 "->data,\n"
                        "            capacity * sizeof(*v%" PRIu32 "->data));\n"
                        "        if (data == NULL) aster_trap(\"out of memory\");\n"
                        "        v%" PRIu32 "->data = data;\n"
                        "        v%" PRIu32 "->capacity = capacity;\n"
                        "    }\n"
                        "    memmove(&v%" PRIu32 "->data[(size_t)v%" PRIu32
                        " + 1U], &v%" PRIu32 "->data[(size_t)v%" PRIu32 "],\n"
                        "        (v%" PRIu32 "->length - (size_t)v%" PRIu32
                        ") * sizeof(*v%" PRIu32 "->data));\n"
                        "    v%" PRIu32 "->data[(size_t)v%" PRIu32
                        "] = v%" PRIu32 ";\n"
                        "    ++v%" PRIu32 "->length;\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->operands[1], instruction->operands[0],
                        instruction->operands[0], instruction->operands[0],
                        instruction->operands[0], instruction->operands[0],
                        instruction->operands[0], instruction->operands[0],
                        instruction->operands[0], instruction->operands[0],
                        instruction->operands[0], instruction->operands[0],
                        instruction->operands[1], instruction->operands[0],
                        instruction->operands[1], instruction->operands[0],
                        instruction->operands[1], instruction->operands[0],
                        instruction->operands[0], instruction->operands[1],
                        instruction->operands[2], instruction->operands[0],
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "List::RemoveAt") == 0) {
                const IrType *list_type = &emitter->ir->types[
                    function->value_types[instruction->operands[0]]];
                fprintf(output,
                        "    if ((uint64_t)v%" PRIu32
                        " >= (uint64_t)v%" PRIu32 "->length)\n"
                        "        aster_trap(\"List index out of bounds\");\n",
                        instruction->operands[1], instruction->operands[0]);
                if (c_backend_type_needs_drop(emitter, list_type->element_type))
                    fprintf(output,
                            "    aster_drop_%" PRIu32
                            "(&v%" PRIu32 "->data[(size_t)v%" PRIu32 "]);\n",
                            list_type->element_type,
                            instruction->operands[0],
                            instruction->operands[1]);
                fprintf(output,
                        "    memmove(&v%" PRIu32 "->data[(size_t)v%" PRIu32
                        "], &v%" PRIu32 "->data[(size_t)v%" PRIu32 " + 1U],\n"
                        "        (v%" PRIu32 "->length - (size_t)v%" PRIu32
                        " - 1U) * sizeof(*v%" PRIu32 "->data));\n"
                        "    --v%" PRIu32 "->length;\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->operands[0], instruction->operands[1],
                        instruction->operands[0], instruction->operands[1],
                        instruction->operands[0], instruction->operands[1],
                        instruction->operands[0], instruction->operands[0],
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "List::Set") == 0) {
                const IrType *list_type = &emitter->ir->types[
                    function->value_types[instruction->operands[0]]];
                fprintf(output,
                        "    if ((uint64_t)v%" PRIu32
                        " >= (uint64_t)v%" PRIu32 "->length)\n"
                        "        aster_trap(\"List index out of bounds\");\n",
                        instruction->operands[1], instruction->operands[0]);
                if (c_backend_type_needs_drop(emitter, list_type->element_type))
                    fprintf(output,
                            "    aster_drop_%" PRIu32
                            "(&v%" PRIu32 "->data[(size_t)v%" PRIu32 "]);\n",
                            list_type->element_type,
                            instruction->operands[0],
                            instruction->operands[1]);
                fprintf(output,
                        "    v%" PRIu32 "->data[(size_t)v%" PRIu32
                        "] = v%" PRIu32 ";\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->operands[0], instruction->operands[1],
                        instruction->operands[2], instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "List::Contains") == 0 ||
                 strcmp(instruction->symbol, "List::IndexOf") == 0 ||
                 strcmp(instruction->symbol, "List::LastIndexOf") == 0 ||
                 strcmp(instruction->symbol, "List::Remove") == 0)) {
                const IrType *list_type = &emitter->ir->types[
                    function->value_types[instruction->operands[0]]];
                const IrType *element =
                    &emitter->ir->types[list_type->element_type];
                bool reverse =
                    strcmp(instruction->symbol, "List::LastIndexOf") == 0;
                fprintf(output,
                        "    size_t list_match_%" PRIu32 " = SIZE_MAX;\n",
                        instruction->result);
                if (reverse) {
                    fprintf(output,
                            "    for (size_t i = v%" PRIu32
                            "->length; i > 0U; --i) {\n"
                            "        if (",
                            instruction->operands[0]);
                    emit_list_element_equality(
                        emitter, element, instruction->operands[0],
                        "i - 1U", instruction->operands[1]);
                    fprintf(output,
                            ") { list_match_%" PRIu32
                            " = i - 1U; break; }\n"
                            "    }\n",
                            instruction->result);
                } else {
                    fprintf(output,
                            "    for (size_t i = 0U; i < v%" PRIu32
                            "->length; ++i) {\n"
                            "        if (",
                            instruction->operands[0]);
                    emit_list_element_equality(
                        emitter, element, instruction->operands[0],
                        "i", instruction->operands[1]);
                    fprintf(output,
                            ") { list_match_%" PRIu32
                            " = i; break; }\n"
                            "    }\n",
                            instruction->result);
                }
                if (c_backend_type_needs_drop(
                        emitter, list_type->element_type))
                    fprintf(output,
                            "    aster_drop_%" PRIu32 "(&v%" PRIu32 ");\n",
                            list_type->element_type,
                            instruction->operands[1]);
                if (strcmp(instruction->symbol, "List::Contains") == 0) {
                    fprintf(output,
                            "    v%" PRIu32 " = list_match_%" PRIu32
                            " != SIZE_MAX;\n",
                            instruction->result, instruction->result);
                } else if (strcmp(
                               instruction->symbol, "List::IndexOf") == 0 ||
                           reverse) {
                    fprintf(output,
                            "    if (list_match_%" PRIu32
                            " != SIZE_MAX && list_match_%" PRIu32
                            " > (size_t)INT32_MAX)\n"
                            "        aster_trap(\"List index exceeds int range\");\n"
                            "    v%" PRIu32 " = list_match_%" PRIu32
                            " == SIZE_MAX ? -INT32_C(1) : "
                            "(int32_t)list_match_%" PRIu32 ";\n",
                            instruction->result, instruction->result,
                            instruction->result, instruction->result,
                            instruction->result);
                } else {
                    fprintf(output,
                            "    v%" PRIu32 " = list_match_%" PRIu32
                            " != SIZE_MAX;\n"
                            "    if (v%" PRIu32 ") {\n",
                            instruction->result, instruction->result,
                            instruction->result);
                    if (c_backend_type_needs_drop(
                            emitter, list_type->element_type))
                        fprintf(output,
                                "        aster_drop_%" PRIu32
                                "(&v%" PRIu32 "->data[list_match_%" PRIu32 "]);\n",
                                list_type->element_type,
                                instruction->operands[0],
                                instruction->result);
                    fprintf(output,
                            "        memmove(&v%" PRIu32
                            "->data[list_match_%" PRIu32 "], &v%" PRIu32
                            "->data[list_match_%" PRIu32 " + 1U],\n"
                            "            (v%" PRIu32
                            "->length - list_match_%" PRIu32
                            " - 1U) * sizeof(*v%" PRIu32 "->data));\n"
                            "        --v%" PRIu32 "->length;\n"
                            "    }\n",
                            instruction->operands[0], instruction->result,
                            instruction->operands[0], instruction->result,
                            instruction->operands[0], instruction->result,
                            instruction->operands[0],
                            instruction->operands[0]);
                }
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "List::AddRange") == 0 ||
                 strcmp(instruction->symbol, "List::InsertRange") == 0)) {
                bool insert =
                    strcmp(instruction->symbol, "List::InsertRange") == 0;
                IrValueId target = instruction->operands[0];
                IrValueId source = instruction->operands[insert ? 2U : 1U];
                IrValueId index_value = insert
                                      ? instruction->operands[1] : 0U;
                IrTypeId list_type = function->value_types[target];
                fprintf(output,
                        "    if (v%" PRIu32 " == v%" PRIu32 ")\n"
                        "        v%" PRIu32 " = aster_clone_%" PRIu32
                        "(v%" PRIu32 ");\n",
                        target, source, source, list_type, source);
                if (insert)
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " > (uint64_t)v%" PRIu32 "->length)\n"
                            "        aster_trap(\"List insert index out of bounds\");\n",
                            index_value, target);
                fprintf(output,
                        "    if (v%" PRIu32 "->length > SIZE_MAX - "
                        "v%" PRIu32 "->length)\n"
                        "        aster_trap(\"List capacity overflow\");\n"
                        "    size_t list_required_%" PRIu32 " = "
                        "v%" PRIu32 "->length + v%" PRIu32 "->length;\n"
                        "    if (list_required_%" PRIu32 " > "
                        "v%" PRIu32 "->capacity) {\n"
                        "        size_t capacity = v%" PRIu32
                        "->capacity == 0U ? 4U : v%" PRIu32 "->capacity;\n"
                        "        while (capacity < list_required_%" PRIu32 ") {\n"
                        "            if (capacity > SIZE_MAX / 2U) {\n"
                        "                capacity = list_required_%" PRIu32 ";\n"
                        "                break;\n"
                        "            }\n"
                        "            capacity *= 2U;\n"
                        "        }\n"
                        "        void *data = realloc(v%" PRIu32 "->data,\n"
                        "            capacity * sizeof(*v%" PRIu32 "->data));\n"
                        "        if (data == NULL) aster_trap(\"out of memory\");\n"
                        "        v%" PRIu32 "->data = data;\n"
                        "        v%" PRIu32 "->capacity = capacity;\n"
                        "    }\n",
                        source, target,
                        instruction->result, target, source,
                        instruction->result, target,
                        target, target, instruction->result,
                        instruction->result,
                        target, target, target, target);
                if (insert)
                    fprintf(output,
                            "    size_t list_at_%" PRIu32
                            " = (size_t)v%" PRIu32 ";\n",
                            instruction->result, index_value);
                else
                    fprintf(output,
                            "    size_t list_at_%" PRIu32
                            " = v%" PRIu32 "->length;\n",
                            instruction->result, target);
                fprintf(output,
                        "    memmove(&v%" PRIu32 "->data[list_at_%" PRIu32
                        " + v%" PRIu32 "->length],\n"
                        "        &v%" PRIu32 "->data[list_at_%" PRIu32 "],\n"
                        "        (v%" PRIu32 "->length - list_at_%" PRIu32
                        ") * sizeof(*v%" PRIu32 "->data));\n"
                        "    if (v%" PRIu32 "->length != 0U)\n"
                        "        memcpy(&v%" PRIu32 "->data[list_at_%" PRIu32
                        "], v%" PRIu32 "->data,\n"
                        "            v%" PRIu32 "->length * "
                        "sizeof(*v%" PRIu32 "->data));\n"
                        "    v%" PRIu32 "->length = list_required_%" PRIu32 ";\n"
                        "    v%" PRIu32 "->length = 0U;\n"
                        "    aster_drop_%" PRIu32 "(&v%" PRIu32 ");\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        target, instruction->result, source,
                        target, instruction->result,
                        target, instruction->result, target,
                        source, target, instruction->result,
                        source, source, target,
                        target, instruction->result,
                        source, list_type, source,
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "List::RemoveRange") == 0 ||
                 strcmp(instruction->symbol, "List::GetRange") == 0)) {
                bool get =
                    strcmp(instruction->symbol, "List::GetRange") == 0;
                IrValueId source = instruction->operands[0];
                IrValueId at = instruction->operands[1];
                IrValueId count_value = instruction->operands[2];
                const IrType *list_type = &emitter->ir->types[
                    function->value_types[source]];
                fprintf(output,
                        "    if ((uint64_t)v%" PRIu32
                        " > (uint64_t)v%" PRIu32 "->length ||\n"
                        "        (uint64_t)v%" PRIu32 " > "
                        "(uint64_t)(v%" PRIu32
                        "->length - (size_t)v%" PRIu32 "))\n"
                        "        aster_trap(\"List range out of bounds\");\n",
                        at, source, count_value, source, at);
                if (!get) {
                    if (c_backend_type_needs_drop(
                            emitter, list_type->element_type))
                        fprintf(output,
                                "    for (size_t i = (size_t)v%" PRIu32
                                "; i > 0U; --i)\n"
                                "        aster_drop_%" PRIu32
                                "(&v%" PRIu32
                                "->data[(size_t)v%" PRIu32 " + i - 1U]);\n",
                                count_value, list_type->element_type,
                                source, at);
                    fprintf(output,
                            "    memmove(&v%" PRIu32
                            "->data[(size_t)v%" PRIu32 "],\n"
                            "        &v%" PRIu32 "->data[(size_t)v%" PRIu32
                            " + (size_t)v%" PRIu32 "],\n"
                            "        (v%" PRIu32 "->length - "
                            "(size_t)v%" PRIu32 " - (size_t)v%" PRIu32
                            ") * sizeof(*v%" PRIu32 "->data));\n"
                            "    v%" PRIu32 "->length -= (size_t)v%" PRIu32
                            ";\n"
                            "    v%" PRIu32 " = UINT8_C(0);\n",
                            source, at, source, at, count_value,
                            source, at, count_value, source,
                            source, count_value, instruction->result);
                } else {
                    fprintf(output,
                            "    v%" PRIu32
                            " = aster_allocate(sizeof(*v%" PRIu32 "));\n"
                            "    *v%" PRIu32 " = (aster_vec_%" PRIu32
                            "){0};\n"
                            "    v%" PRIu32 "->length = (size_t)v%" PRIu32
                            ";\n"
                            "    v%" PRIu32 "->capacity = (size_t)v%" PRIu32
                            ";\n"
                            "    if (v%" PRIu32 " != 0U)\n"
                            "        v%" PRIu32 "->data = aster_allocate(\n"
                            "            (size_t)v%" PRIu32
                            " * sizeof(*v%" PRIu32 "->data));\n",
                            instruction->result, instruction->result,
                            instruction->result, instruction->result_type,
                            instruction->result, count_value,
                            instruction->result, count_value,
                            count_value, instruction->result, count_value,
                            instruction->result);
                    fprintf(output,
                            "    for (size_t i = 0U; i < (size_t)v%" PRIu32
                            "; ++i)\n"
                            "        v%" PRIu32 "->data[i] = ",
                            count_value, instruction->result);
                    if (c_backend_type_needs_drop(
                            emitter, list_type->element_type))
                        fprintf(output, "aster_clone_%" PRIu32 "(",
                                list_type->element_type);
                    fprintf(output,
                            "v%" PRIu32 "->data[(size_t)v%" PRIu32 " + i]",
                            source, at);
                    fputs(c_backend_type_needs_drop(
                              emitter, list_type->element_type)
                              ? ");\n" : ";\n",
                          output);
                }
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "List::Reverse") == 0) {
                IrValueId list = instruction->operands[0];
                const IrType *list_type = &emitter->ir->types[
                    function->value_types[list]];
                if (instruction->operand_count == 3U) {
                    IrValueId at = instruction->operands[1];
                    IrValueId range_count = instruction->operands[2];
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " > (uint64_t)v%" PRIu32 "->length ||\n"
                            "        (uint64_t)v%" PRIu32 " > "
                            "(uint64_t)(v%" PRIu32
                            "->length - (size_t)v%" PRIu32 "))\n"
                            "        aster_trap(\"List range out of bounds\");\n"
                            "    size_t list_reverse_at_%" PRIu32
                            " = (size_t)v%" PRIu32 ";\n"
                            "    size_t list_reverse_count_%" PRIu32
                            " = (size_t)v%" PRIu32 ";\n",
                            at, list, range_count, list, at,
                            instruction->result, at,
                            instruction->result, range_count);
                } else {
                    fprintf(output,
                            "    size_t list_reverse_at_%" PRIu32 " = 0U;\n"
                            "    size_t list_reverse_count_%" PRIu32
                            " = v%" PRIu32 "->length;\n",
                            instruction->result, instruction->result, list);
                }
                fprintf(output,
                        "    for (size_t left = list_reverse_at_%" PRIu32
                        ", right = list_reverse_at_%" PRIu32
                        " + list_reverse_count_%" PRIu32 ";\n"
                        "         left < right && left < --right; ++left) {\n"
                        "        ",
                        instruction->result, instruction->result,
                        instruction->result);
                c_backend_emit_type(emitter, list_type->element_type);
                fprintf(output,
                        " temporary = v%" PRIu32 "->data[left];\n"
                        "        v%" PRIu32 "->data[left] = "
                        "v%" PRIu32 "->data[right];\n"
                        "        v%" PRIu32 "->data[right] = temporary;\n"
                        "    }\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        list, list, list, list, instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "List::EnsureCapacity") == 0 ||
                 strcmp(instruction->symbol, "Stack::EnsureCapacity") == 0)) {
                IrValueId list = instruction->operands[0];
                IrValueId minimum = instruction->operands[1];
                fprintf(output,
                        "    if ((uint64_t)v%" PRIu32
                        " > (uint64_t)SIZE_MAX)\n"
                        "        aster_trap(\"List capacity overflow\");\n"
                        "    if ((size_t)v%" PRIu32 " > v%" PRIu32
                        "->capacity) {\n"
                        "        size_t capacity = v%" PRIu32
                        "->capacity == 0U ? 4U : v%" PRIu32 "->capacity;\n"
                        "        while (capacity < (size_t)v%" PRIu32 ") {\n"
                        "            if (capacity > SIZE_MAX / 2U) {\n"
                        "                capacity = (size_t)v%" PRIu32 ";\n"
                        "                break;\n"
                        "            }\n"
                        "            capacity *= 2U;\n"
                        "        }\n"
                        "        void *data = realloc(v%" PRIu32 "->data,\n"
                        "            capacity * sizeof(*v%" PRIu32 "->data));\n"
                        "        if (data == NULL) aster_trap(\"out of memory\");\n"
                        "        v%" PRIu32 "->data = data;\n"
                        "        v%" PRIu32 "->capacity = capacity;\n"
                        "    }\n"
                        "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                        "->capacity;\n",
                        minimum, minimum, list, list, list, minimum,
                        minimum, list, list, list, list,
                        instruction->result, list);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "List::TrimExcess") == 0 ||
                 strcmp(instruction->symbol, "List::SetCapacity") == 0 ||
                 strcmp(instruction->symbol, "Stack::TrimExcess") == 0)) {
                bool trim =
                    strcmp(instruction->symbol, "List::TrimExcess") == 0 ||
                    (strcmp(instruction->symbol, "Stack::TrimExcess") == 0 &&
                     instruction->operand_count == 1U);
                IrValueId list = instruction->operands[0];
                if (trim) {
                    fprintf(output,
                            "    size_t list_threshold_%" PRIu32 " = "
                            "(v%" PRIu32 "->capacity / 10U) * 9U + "
                            "((v%" PRIu32 "->capacity %% 10U) * 9U) / 10U;\n"
                            "    size_t list_capacity_%" PRIu32 " = "
                            "v%" PRIu32 "->length < list_threshold_%" PRIu32
                            " ? v%" PRIu32 "->length : v%" PRIu32
                            "->capacity;\n",
                            instruction->result, list, list,
                            instruction->result, list, instruction->result,
                            list, list);
                } else {
                    IrValueId capacity = instruction->operands[1];
                    fprintf(output,
                            "    if ((uint64_t)v%" PRIu32
                            " > (uint64_t)SIZE_MAX || "
                            "(size_t)v%" PRIu32 " < v%" PRIu32 "->length)\n"
                            "        aster_trap(\"List Capacity cannot be less than Count\");\n"
                            "    size_t list_capacity_%" PRIu32
                            " = (size_t)v%" PRIu32 ";\n",
                            capacity, capacity, list,
                            instruction->result, capacity);
                }
                fprintf(output,
                        "    if (list_capacity_%" PRIu32 " != v%" PRIu32
                        "->capacity) {\n"
                        "        if (list_capacity_%" PRIu32 " == 0U) {\n"
                        "            free(v%" PRIu32 "->data);\n"
                        "            v%" PRIu32 "->data = NULL;\n"
                        "        } else {\n"
                        "            void *data = realloc(v%" PRIu32
                        "->data, list_capacity_%" PRIu32
                        " * sizeof(*v%" PRIu32 "->data));\n"
                        "            if (data == NULL) aster_trap(\"out of memory\");\n"
                        "            v%" PRIu32 "->data = data;\n"
                        "        }\n"
                        "        v%" PRIu32 "->capacity = list_capacity_%" PRIu32
                        ";\n"
                        "    }\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->result, list,
                        instruction->result, list, list, list,
                        instruction->result, list, list,
                        list, instruction->result, instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                (strcmp(instruction->symbol, "List::Exists") == 0 ||
                 strcmp(instruction->symbol, "List::FindAll") == 0 ||
                 strcmp(instruction->symbol, "List::FindIndex") == 0 ||
                 strcmp(instruction->symbol, "List::FindLastIndex") == 0 ||
                 strcmp(instruction->symbol, "List::RemoveAll") == 0 ||
                 strcmp(instruction->symbol, "List::ForEach") == 0 ||
                 strcmp(instruction->symbol, "List::TrueForAll") == 0)) {
                IrValueId list = instruction->operands[0];
                IrValueId callback = instruction->operands[1];
                const IrType *list_type = &emitter->ir->types[
                    function->value_types[list]];
                IrTypeId element = list_type->element_type;
                bool exists =
                    strcmp(instruction->symbol, "List::Exists") == 0;
                bool all =
                    strcmp(instruction->symbol, "List::TrueForAll") == 0;
                if (exists || all) {
                    fprintf(output,
                            "    v%" PRIu32 " = %s;\n"
                            "    for (size_t i = 0U; i < v%" PRIu32
                            "->length; ++i) {\n"
                            "        if (%s",
                            instruction->result, all ? "true" : "false",
                            list, all ? "!" : "");
                    emit_list_callback_call(
                        emitter, element, callback, list, "i");
                    fprintf(output,
                            ") { v%" PRIu32 " = %s; break; }\n"
                            "    }\n",
                            instruction->result, all ? "false" : "true");
                    return;
                }
                if (strcmp(instruction->symbol, "List::FindAll") == 0) {
                    fprintf(output,
                            "    v%" PRIu32
                            " = aster_allocate(sizeof(*v%" PRIu32 "));\n"
                            "    *v%" PRIu32 " = (aster_vec_%" PRIu32
                            "){0};\n"
                            "    for (size_t i = 0U; i < v%" PRIu32
                            "->length; ++i) {\n"
                            "        if (!",
                            instruction->result, instruction->result,
                            instruction->result, instruction->result_type,
                            list);
                    emit_list_callback_call(
                        emitter, element, callback, list, "i");
                    fprintf(output,
                            ") continue;\n"
                            "        if (v%" PRIu32 "->length == "
                            "v%" PRIu32 "->capacity) {\n"
                            "            size_t capacity = v%" PRIu32
                            "->capacity == 0U ? 4U : "
                            "v%" PRIu32 "->capacity * 2U;\n"
                            "            void *data = realloc(v%" PRIu32
                            "->data, capacity * sizeof(*v%" PRIu32
                            "->data));\n"
                            "            if (data == NULL) "
                            "aster_trap(\"out of memory\");\n"
                            "            v%" PRIu32 "->data = data;\n"
                            "            v%" PRIu32 "->capacity = capacity;\n"
                            "        }\n"
                            "        v%" PRIu32 "->data[v%" PRIu32
                            "->length++] = ",
                            instruction->result, instruction->result,
                            instruction->result, instruction->result,
                            instruction->result, instruction->result,
                            instruction->result, instruction->result,
                            instruction->result, instruction->result);
                    if (c_backend_type_needs_drop(emitter, element))
                        fprintf(output, "aster_clone_%" PRIu32 "(", element);
                    fprintf(output, "v%" PRIu32 "->data[i]", list);
                    fputs(c_backend_type_needs_drop(emitter, element)
                              ? ");\n" : ";\n",
                          output);
                    fputs("    }\n", output);
                    return;
                }
                if (strcmp(instruction->symbol, "List::FindIndex") == 0 ||
                    strcmp(instruction->symbol, "List::FindLastIndex") == 0) {
                    bool reverse = strcmp(
                        instruction->symbol, "List::FindLastIndex") == 0;
                    fprintf(output,
                            "    size_t list_found_%" PRIu32 " = SIZE_MAX;\n"
                            "    for (size_t offset = 0U; offset < v%" PRIu32
                            "->length; ++offset) {\n"
                            "        size_t at = ",
                            instruction->result, list);
                    if (reverse)
                        fprintf(output,
                                "v%" PRIu32 "->length - offset - 1U",
                                list);
                    else
                        fputs("offset", output);
                    fputs(";\n        if (", output);
                    emit_list_callback_call(
                        emitter, element, callback, list, "at");
                    fprintf(output,
                            ") { list_found_%" PRIu32 " = at; break; }\n"
                            "    }\n"
                            "    if (list_found_%" PRIu32
                            " != SIZE_MAX && list_found_%" PRIu32
                            " > (size_t)INT32_MAX)\n"
                            "        aster_trap(\"List index exceeds int range\");\n"
                            "    v%" PRIu32 " = list_found_%" PRIu32
                            " == SIZE_MAX ? -INT32_C(1) : "
                            "(int32_t)list_found_%" PRIu32 ";\n",
                            instruction->result, instruction->result,
                            instruction->result, instruction->result,
                            instruction->result, instruction->result);
                    return;
                }
                if (strcmp(instruction->symbol, "List::RemoveAll") == 0) {
                    fprintf(output,
                            "    size_t list_write_%" PRIu32 " = 0U;\n"
                            "    size_t list_removed_%" PRIu32 " = 0U;\n"
                            "    for (size_t read = 0U; read < v%" PRIu32
                            "->length; ++read) {\n"
                            "        if (",
                            instruction->result, instruction->result, list);
                    emit_list_callback_call(
                        emitter, element, callback, list, "read");
                    fputs(") {\n", output);
                    if (c_backend_type_needs_drop(emitter, element))
                        fprintf(output,
                                "            aster_drop_%" PRIu32
                                "(&v%" PRIu32 "->data[read]);\n",
                                element, list);
                    fprintf(output,
                            "            ++list_removed_%" PRIu32 ";\n"
                            "        } else {\n"
                            "            if (list_write_%" PRIu32
                            " != read) v%" PRIu32
                            "->data[list_write_%" PRIu32
                            "] = v%" PRIu32 "->data[read];\n"
                            "            ++list_write_%" PRIu32 ";\n"
                            "        }\n"
                            "    }\n"
                            "    v%" PRIu32 "->length = list_write_%" PRIu32
                            ";\n"
                            "    if (list_removed_%" PRIu32
                            " > (size_t)INT32_MAX)\n"
                            "        aster_trap(\"removed count exceeds int range\");\n"
                            "    v%" PRIu32 " = (int32_t)list_removed_%" PRIu32
                            ";\n",
                            instruction->result, instruction->result, list,
                            instruction->result, list,
                            instruction->result, list, instruction->result,
                            instruction->result, instruction->result,
                            instruction->result);
                    return;
                }
                fprintf(output,
                        "    for (size_t i = 0U; i < v%" PRIu32
                        "->length; ++i) (void)", list);
                emit_list_callback_call(
                    emitter, element, callback, list, "i");
                fprintf(output,
                        ";\n    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "List::Get") == 0) {
                const IrType *list_type = &emitter->ir->types[
                    function->value_types[instruction->operands[0]]];
                fprintf(output,
                        "    if ((uint64_t)v%" PRIu32
                        " >= (uint64_t)v%" PRIu32 "->length)\n"
                        "        aster_trap(\"List index out of bounds\");\n",
                        instruction->operands[1],
                        instruction->operands[0]);
                fprintf(output, "    v%" PRIu32 " = ",
                        instruction->result);
                if (c_backend_type_needs_drop(emitter, list_type->element_type))
                    fprintf(output, "aster_clone_%" PRIu32 "(",
                            list_type->element_type);
                fprintf(output, "v%" PRIu32
                        "->data[(size_t)v%" PRIu32 "]",
                        instruction->operands[0],
                        instruction->operands[1]);
                fputs(c_backend_type_needs_drop(emitter, list_type->element_type)
                        ? ");\n" : ";\n", output);
                return;
            }
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
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "StringBuilder::New") == 0) {
                fprintf(output,
                        "    v%" PRIu32
                        " = aster_builder_new();\n",
                        instruction->result);
                return;
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
                return;
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
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "StringBuilder::Finish") == 0) {
                fprintf(output,
                        "    v%" PRIu32
                        " = aster_builder_finish(v%" PRIu32 ");\n",
                        instruction->result,
                        instruction->operands[0]);
                return;
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
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "StringBuilder::Length") == 0) {
                fprintf(output,
                        "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                        "->length;\n",
                        instruction->result,
                        instruction->operands[0]);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol,
                       "StringBuilder::Clear") == 0) {
                fprintf(output,
                        "    v%" PRIu32 "->length = 0U;\n"
                        "    v%" PRIu32 " = UINT8_C(0);\n",
                        instruction->operands[0],
                        instruction->result);
                return;
            }
            if (instruction->symbol != NULL &&
                strcmp(instruction->symbol, "StringView") == 0) {
                fprintf(output,
                        "    v%" PRIu32
                        " = aster_string_as_str(v%" PRIu32 ");\n",
                        instruction->result,
                        instruction->operands[0]);
                return;
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
                    return;
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
                return;
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
                    return;
                }
                fprintf(output,
                        "    v%" PRIu32
                        " = aster_html_to_string(v%" PRIu32 ");\n",
                        instruction->result,
                        instruction->operands[0]);
                return;
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
                return;
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
                return;
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
                return;
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
                    return;
                }
                fprintf(
                    output,
                    "    aster_builder_append_%s(v%" PRIu32
                    ", v%" PRIu32 ");\n"
                    "    v%" PRIu32 " = UINT8_C(0);\n",
                    suffix, instruction->operands[0],
                    instruction->operands[1],
                    instruction->result);
                return;
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
                return;
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
                return;
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
                return;
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
                return;
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
                return;
            }
            c_backend_unsupported(
                emitter, instruction->span,
                "this native call");
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
                c_backend_emit_virtual_cleanup(
                    emitter, function, IR_INVALID_ID, "        ", true);
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
