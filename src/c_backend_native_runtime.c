#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

bool c_backend_emit_native_runtime(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction
) {
    FILE *output = emitter->output;
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "Task::Delay") == 0) {
        if (instruction->operand_count != 1U &&
            instruction->operand_count != 2U) {
            c_backend_unsupported(
                emitter, instruction->span,
                "a Task.Delay overload other than one millisecond argument");
            return true;
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
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "Task::WhenAll") == 0) {
        if (instruction->operand_count != 1U) {
            c_backend_unsupported(
                emitter, instruction->span,
                "a Task.WhenAll overload other than one List argument");
            return true;
        }
        IrTypeId input_type = function->value_types[
            instruction->operands[0]];
        fprintf(output,
                "    v%" PRIu32 " = aster_when_all_%" PRIu32
                "(v%" PRIu32 ");\n",
                instruction->result, input_type,
                instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "Task::WhenAny") == 0) {
        if (instruction->operand_count != 1U) {
            c_backend_unsupported(
                emitter, instruction->span,
                "a Task.WhenAny overload other than one List argument");
            return true;
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
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol,
               "CancellationTokenSource::New") == 0) {
        fprintf(output,
                "    v%" PRIu32 " = aster_cancellation_new();\n",
                instruction->result);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol,
               "CancellationToken::None") == 0) {
        fprintf(output, "    v%" PRIu32 " = NULL;\n",
                instruction->result);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol,
               "CancellationTokenSource::Token") == 0) {
        fprintf(output, "    v%" PRIu32 " = v%" PRIu32 ";\n",
                instruction->result, instruction->operands[0]);
        return true;
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
        return true;
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
        return true;
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
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "Arena::new") == 0) {
        fprintf(output,
                "    v%" PRIu32 " = aster_arena_new();\n",
                instruction->result);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol,
               "NativeUtcNowUnixMilliseconds") == 0) {
        fprintf(output,
                "    v%" PRIu32
                " = aster_utc_now_unix_milliseconds();\n",
                instruction->result);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "ArenaReset") == 0) {
        fprintf(output,
                "    aster_arena_reset(v%" PRIu32 ");\n"
                "    v%" PRIu32 " = UINT8_C(0);\n",
                instruction->operands[0],
                instruction->result);
        return true;
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
        return true;
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
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "ByteSliceLen") == 0) {
        fprintf(output,
                "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                ".length;\n",
                instruction->result,
                instruction->operands[0]);
        return true;
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
        return true;
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
        return true;
    }
    return false;
}
