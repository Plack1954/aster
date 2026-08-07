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
    FILE *output = emitter->output;
    if (emit_registry_native_call(
            emitter, function, instruction))
        return;
    if (c_backend_registry_native_call(instruction)) {
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
        (strcmp(instruction->symbol, "BufferAsMutSlice") == 0 ||
         strcmp(instruction->symbol, "BufferAsSlice") == 0)) {
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
            if (remove)
                fprintf(output,
                        "    size_t dictionary_bucket_match_%" PRIu32
                        " = SIZE_MAX;\n",
                        instruction->result);
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
                    " = dictionary_index_%" PRIu32 ";\n",
                    instruction->result, instruction->result);
            if (remove)
                fprintf(output,
                        "                dictionary_bucket_match_%" PRIu32
                        " = dictionary_bucket_%" PRIu32 ";\n",
                        instruction->result, instruction->result);
            fprintf(output,
                    "                break;\n"
                    "            }\n"
                    "            dictionary_bucket_%" PRIu32
                    " = (dictionary_bucket_%" PRIu32
                    " + 1U) & (v%" PRIu32
                    "->bucket_count - 1U);\n"
                    "        }\n"
                    "    }\n",
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
                    "        aster_dictionary_erase_%" PRIu32
                    "(v%" PRIu32 ", dictionary_match_%" PRIu32
                    ", dictionary_bucket_match_%" PRIu32 ");\n"
                    "    }\n",
                    function->value_types[instruction->operands[0]],
                    instruction->operands[0], instruction->result,
                    instruction->result);
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
        if (strcmp(instruction->symbol, "List::Add") == 0)
            fprintf(output,
                    "    ASTER_LIST_MUTATION(v%" PRIu32
                    ", 1U, v%" PRIu32 "->length - 1U, 1U);\n",
                    instruction->operands[0],
                    instruction->operands[0]);
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
        if (strcmp(instruction->symbol, "List::Clear") == 0)
            fprintf(output,
                    "    ASTER_LIST_MUTATION(v%" PRIu32
                    ", 4U, 0U, 1U);\n",
                    instruction->operands[0]);
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
        fprintf(output,
                "    ASTER_LIST_MUTATION(v%" PRIu32
                ", 5U, (size_t)v%" PRIu32 ", 1U);\n",
                instruction->operands[0],
                instruction->operands[1]);
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
        fprintf(output,
                "    ASTER_LIST_MUTATION(v%" PRIu32
                ", 3U, (size_t)v%" PRIu32 ", 1U);\n",
                instruction->operands[0],
                instruction->operands[1]);
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
        fprintf(output,
                "    ASTER_LIST_MUTATION(v%" PRIu32
                ", 2U, (size_t)v%" PRIu32 ", 1U);\n",
                instruction->operands[0],
                instruction->operands[1]);
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
        if (strcmp(instruction->symbol, "List::Remove") == 0)
            fprintf(output,
                    "    ASTER_LIST_MUTATION(v%" PRIu32
                    ", 255U, 0U, 1U);\n",
                    instruction->operands[0]);
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
        fprintf(output,
                "    ASTER_LIST_MUTATION(v%" PRIu32
                ", 255U, 0U, 1U);\n",
                target);
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
        if (!get)
            fprintf(output,
                    "    ASTER_LIST_MUTATION(v%" PRIu32
                    ", 255U, 0U, 1U);\n",
                    source);
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
        fprintf(output,
                "    ASTER_LIST_MUTATION(v%" PRIu32
                ", 255U, 0U, 1U);\n",
                list);
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
            fprintf(output,
                    "    ASTER_LIST_MUTATION(v%" PRIu32
                    ", 255U, 0U, 1U);\n",
                    list);
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
}
