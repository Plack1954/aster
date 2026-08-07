#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

bool c_backend_emit_native_queue(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction
) {
    FILE *output = emitter->output;
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "Queue::New") == 0) {
        fprintf(output,
                "    v%" PRIu32
                " = aster_allocate(sizeof(*v%" PRIu32 "));\n"
                "    *v%" PRIu32 " = (aster_queue_%" PRIu32
                "){0};\n",
                instruction->result, instruction->result,
                instruction->result, instruction->result_type);
        return true;
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
        return true;
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
        return true;
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
        return true;
    }
    if (instruction->symbol != NULL &&
        (strcmp(instruction->symbol, "Queue::Count") == 0 ||
         strcmp(instruction->symbol, "Queue::Capacity") == 0)) {
        bool count = strcmp(instruction->symbol, "Queue::Count") == 0;
        fprintf(output,
                "    v%" PRIu32 " = (uint64_t)v%" PRIu32 "->%s;\n",
                instruction->result, instruction->operands[0],
                count ? "length" : "capacity");
        return true;
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
        return true;
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
        return true;
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
        return true;
    }
    return false;
}
