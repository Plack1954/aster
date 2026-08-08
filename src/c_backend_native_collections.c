#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

bool c_backend_emit_native_collections(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction
) {
    FILE *output = emitter->output;
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
        return true;
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
        return true;
    }
    if (instruction->symbol != NULL &&
        (strcmp(instruction->symbol, "List::Count") == 0 ||
         strcmp(instruction->symbol, "Stack::Count") == 0)) {
        fprintf(output,
                "    v%" PRIu32
                " = (uint64_t)v%" PRIu32 "->length;\n",
                instruction->result,
                instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        (strcmp(instruction->symbol, "List::Capacity") == 0 ||
         strcmp(instruction->symbol, "Stack::Capacity") == 0)) {
        fprintf(output,
                "    v%" PRIu32
                " = (uint64_t)v%" PRIu32 "->capacity;\n",
                instruction->result,
                instruction->operands[0]);
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
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
            return true;
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
            bool copy = c_backend_type_requires_semantic_copy(
                emitter->ir, element);
            if (copy)
                fprintf(output, "aster_clone_%" PRIu32 "(", element);
            fprintf(output, "v%" PRIu32 "->data[i]", list);
            fputs(copy ? ");\n" : ";\n",
                  output);
            fputs("    }\n", output);
            return true;
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
            return true;
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
            return true;
        }
        fprintf(output,
                "    for (size_t i = 0U; i < v%" PRIu32
                "->length; ++i) (void)", list);
        emit_list_callback_call(
            emitter, element, callback, list, "i");
        fprintf(output,
                ";\n    v%" PRIu32 " = UINT8_C(0);\n",
                instruction->result);
        return true;
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
        return true;
    }
    return false;
}
