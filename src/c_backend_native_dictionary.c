#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

bool c_backend_emit_native_dictionary(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction
) {
    FILE *output = emitter->output;
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "Dictionary::New") == 0) {
        fprintf(output,
                "    v%" PRIu32
                " = aster_allocate(sizeof(*v%" PRIu32 "));\n"
                "    *v%" PRIu32
                " = (aster_dictionary_%" PRIu32 "){0};\n",
                instruction->result, instruction->result,
                instruction->result, instruction->result_type);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "Dictionary::Count") == 0) {
        fprintf(output,
                "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                "->length;\n",
                instruction->result, instruction->operands[0]);
        return true;
    }
    if (instruction->symbol != NULL &&
        strcmp(instruction->symbol, "Dictionary::Capacity") == 0) {
        fprintf(output,
                "    v%" PRIu32 " = (uint64_t)v%" PRIu32
                "->capacity;\n",
                instruction->result, instruction->operands[0]);
        return true;
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
        return true;
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
        return true;
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
        return true;
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
            return true;
        }
        IrTypeId searched_type = contains_value
            ? dictionary->error_type : dictionary->element_type;
        if (c_backend_type_needs_drop(emitter, searched_type))
            fprintf(output,
                    "    aster_drop_%" PRIu32 "(&v%" PRIu32 ");\n",
                    searched_type,
                    instruction->operands[1]);
        return true;
    }
    return false;
}
