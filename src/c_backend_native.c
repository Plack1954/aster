#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

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
            strncmp(symbol, "NativeHttpClient", 16U) == 0 ||
            strncmp(symbol, "NativeCrypto", 12U) == 0 ||
            strncmp(symbol, "H2O", 3U) == 0 ||
            strncmp(symbol, "NativeProcess", 13U) == 0 ||
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
            strcmp(symbol, "HttpStreamChunkBytes") == 0 ||
            strcmp(symbol, "HttpStreamFinish") == 0 ||
            strncmp(symbol, "ByteSlice", 9U) == 0 ||
            strcmp(symbol, "StringAsByteSlice") == 0 ||
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

bool c_backend_registry_native_call(const IrInstruction *instruction) {
    return instruction != NULL &&
           (c_backend_registry_native_symbol(instruction->symbol) ||
            (instruction->native_call != NULL &&
             instruction->native_call->registry_dispatch));
}

static void emit_native_argument(CEmitter *emitter,
                                 const IrFunction *function,
                                 IrValueId value) {
    IrTypeId type_id = function->value_types[value];
    const IrType *type =
        &emitter->ir->types[type_id];
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
    else if (type->shape == IR_TYPE_FUNCTION)
        fprintf(emitter->output,
                "(LangValue){.tag=LANG_VALUE_NATIVE_FUNCTION,"
                ".as.native_function={aster_native_callback_%" PRIu32
                ",(void *)&v%" PRIu32 "}}",
                type_id, value);
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
    CEmitter *emitter, IrTypeId type_id, const IrType *type,
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
    } else if (type->shape == IR_TYPE_SLICE) {
        fprintf(output,
                "        if (%s.tag != LANG_VALUE_BYTE_SLICE) "
                "aster_trap(\"invalid native byte-slice result\");\n"
                "        v%" PRIu32 " = (aster_slice_%" PRIu32 "){"
                "%s.as.bytes.data,%s.as.bytes.length};\n",
                value, result, type_id,
                value, value);
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
    if (!c_backend_registry_native_call(instruction)) return false;
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
                emitter, instruction->result_type, result_type,
                instruction->result,
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


void c_backend_emit_native_instruction(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction
) {
    if (emit_registry_native_call(
            emitter, function, instruction))
        return;
    if (c_backend_registry_native_call(instruction)) {
        c_backend_unsupported(
            emitter, instruction->span,
            "this registered native signature");
        return;
    }
    if (c_backend_emit_native_runtime(emitter, function, instruction) ||
        c_backend_emit_native_dictionary(emitter, function, instruction) ||
        c_backend_emit_native_queue(emitter, function, instruction) ||
        c_backend_emit_native_collections(emitter, function, instruction) ||
        c_backend_emit_native_text(emitter, function, instruction))
        return;
    c_backend_unsupported(
        emitter, instruction->span,
        "this native call");
}
