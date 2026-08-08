#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const char *scalar_c_type(const IrType *type) {
    switch (type->shape) {
        case IR_TYPE_UNIT: return "uint8_t";
        case IR_TYPE_BOOL: return "bool";
        case IR_TYPE_SIGNED_INT: return "int64_t";
        case IR_TYPE_UNSIGNED_INT:
        case IR_TYPE_CHAR:
            return "uint64_t";
        case IR_TYPE_ENUM:
            return "uint32_t";
        case IR_TYPE_FLOAT:
            return type->bit_width == 32U ? "float" : "double";
        default: return NULL;
    }
}

bool c_backend_type_is_vec(const IrType *type) {
    return type->shape == IR_TYPE_BUILTIN_OBJECT &&
           (strncmp(type->name, "List<", 5U) == 0 ||
            strncmp(type->name, "Stack<", 6U) == 0);
}

bool c_backend_type_is_queue(const IrType *type) {
    return type->shape == IR_TYPE_BUILTIN_OBJECT &&
           strncmp(type->name, "Queue<", 6U) == 0;
}

bool c_backend_type_is_dictionary(const IrType *type) {
    return type->shape == IR_TYPE_BUILTIN_OBJECT &&
           (strncmp(type->name, "Dictionary<", 11U) == 0 ||
            strncmp(type->name, "HashSet<", 8U) == 0);
}

bool c_backend_type_is_native_handle(const IrType *type) {
    return type->shape == IR_TYPE_BUILTIN_OBJECT &&
           strcmp(type->name, "NativeHandle") == 0;
}

bool c_backend_type_is_buffer(const IrType *type) {
    return type->shape == IR_TYPE_BUILTIN_OBJECT &&
           strcmp(type->name, "Buffer") == 0;
}

bool c_backend_type_is_arena(const IrType *type) {
    return type->shape == IR_TYPE_BUILTIN_OBJECT &&
           strcmp(type->name, "Arena") == 0;
}

bool c_backend_type_is_task(const IrType *type) {
    return type->shape == IR_TYPE_BUILTIN_OBJECT &&
           (strcmp(type->name, "Task") == 0 ||
            strncmp(type->name, "Task<", 5U) == 0);
}

bool c_backend_type_is_cancellation(const IrType *type) {
    return type->shape == IR_TYPE_BUILTIN_OBJECT &&
           (strcmp(type->name, "CancellationToken") == 0 ||
            strcmp(type->name, "CancellationTokenSource") == 0);
}

static bool builtin_object_supported(const IrType *type) {
    return type->shape == IR_TYPE_BUILTIN_OBJECT &&
           (strcmp(type->name, "string") == 0 ||
            strcmp(type->name, "StringBuilder") == 0 ||
            strcmp(type->name, "Html") == 0 ||
            strcmp(type->name, "Url") == 0 ||
            c_backend_type_is_buffer(type) ||
            c_backend_type_is_arena(type) ||
            c_backend_type_is_task(type) ||
            c_backend_type_is_cancellation(type) ||
            c_backend_type_is_vec(type) ||
            c_backend_type_is_queue(type) ||
            c_backend_type_is_dictionary(type));
}

static bool type_clone_supported_inner(
    const IrModule *ir, IrTypeId type_id, bool *visiting
) {
    if (type_id >= ir->type_count) return false;
    const IrType *type = &ir->types[type_id];
    if (!type->requires_cleanup && !type->managed) return true;
    if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
        (strcmp(type->name, "string") == 0 ||
         strcmp(type->name, "StringBuilder") == 0 ||
         strcmp(type->name, "Html") == 0 ||
         strcmp(type->name, "Url") == 0 ||
         c_backend_type_is_native_handle(type) ||
         c_backend_type_is_buffer(type) ||
         c_backend_type_is_task(type) ||
         c_backend_type_is_cancellation(type)))
        return true;
    if (visiting[type_id]) return true;
    visiting[type_id] = true;
    bool supported = false;
    if (c_backend_type_is_vec(type) || c_backend_type_is_queue(type)) {
        supported = type_clone_supported_inner(
            ir, type->element_type, visiting);
    } else if (c_backend_type_is_dictionary(type)) {
        supported = type_clone_supported_inner(
                ir, type->element_type, visiting) &&
            type_clone_supported_inner(
                ir, type->error_type, visiting);
    } else if (type->shape == IR_TYPE_STRUCT) {
        supported = true;
        for (size_t field = 0U; field < type->field_count; ++field)
            if (!type_clone_supported_inner(
                    ir, type->field_types[field], visiting)) {
                supported = false;
                break;
            }
    } else if (type->shape == IR_TYPE_ARRAY) {
        supported = type_clone_supported_inner(
            ir, type->element_type, visiting);
    } else if (type->shape == IR_TYPE_UNION) {
        supported = true;
        for (size_t variant = 0U;
             variant < type->variant_count; ++variant) {
            IrTypeId payload =
                type->variant_payload_types[variant];
            if (payload != IR_INVALID_ID &&
                !type_clone_supported_inner(
                    ir, payload, visiting)) {
                supported = false;
                break;
            }
        }
    }
    visiting[type_id] = false;
    return supported;
}

bool c_backend_type_clone_supported(
    const IrModule *ir, IrTypeId type_id
) {
    bool *visiting = calloc(ir->type_count, sizeof(*visiting));
    if (visiting == NULL) return false;
    bool supported = type_clone_supported_inner(
        ir, type_id, visiting);
    free(visiting);
    return supported;
}

static bool type_is_c_supported_inner(
    const IrModule *ir, IrTypeId type_id,
    bool *visiting, bool *resolved) {
    if (type_id >= ir->type_count) return false;
    if (resolved[type_id]) return true;
    const IrType *type = &ir->types[type_id];
    if (visiting[type_id])
        return type->shape == IR_TYPE_CLASS_REFERENCE;
    if (scalar_c_type(type) != NULL) {
        resolved[type_id] = true;
        return true;
    }
    if (type->shape == IR_TYPE_STRING_VIEW ||
        c_backend_type_is_native_handle(type) ||
        type->shape == IR_TYPE_ELEMENT_BUILDER ||
        builtin_object_supported(type)) {
        resolved[type_id] = true;
        return true;
    }
    if (type->shape == IR_TYPE_SLICE) {
        visiting[type_id] = true;
        bool supported = type_is_c_supported_inner(
            ir, type->element_type, visiting, resolved);
        visiting[type_id] = false;
        resolved[type_id] = supported;
        return supported;
    }
    if (type->shape == IR_TYPE_RAW_POINTER) {
        visiting[type_id] = true;
        bool supported = type_is_c_supported_inner(
            ir, type->element_type, visiting, resolved);
        visiting[type_id] = false;
        resolved[type_id] = supported;
        return supported;
    }
    /* A class value is a pointer in the C ABI.  Its pointee is emitted and
     * validated independently, so crossing a class-reference boundary must
     * not turn legal cycles (Request -> Route -> Handler -> Request) into an
     * unsupported by-value recursive type. */
    if (type->shape == IR_TYPE_CLASS_REFERENCE) {
        resolved[type_id] = true;
        return true;
    }
    if (type->shape == IR_TYPE_FUNCTION) {
        visiting[type_id] = true;
        bool supported = type_is_c_supported_inner(
            ir, type->element_type, visiting, resolved);
        for (size_t argument = 0U;
             supported && argument < type->argument_count; ++argument)
            supported = type_is_c_supported_inner(
                ir, type->argument_types[argument],
                visiting, resolved);
        visiting[type_id] = false;
        resolved[type_id] = supported;
        return supported;
    }
    if (type->shape == IR_TYPE_ITERATOR) {
        visiting[type_id] = true;
        bool supported =
            type->argument_count == 1U &&
            (c_backend_type_is_vec(&ir->types[type->argument_types[0]]) ||
             c_backend_type_is_queue(&ir->types[type->argument_types[0]]) ||
             ir->types[type->argument_types[0]].shape ==
                 IR_TYPE_ARRAY ||
             ir->types[type->argument_types[0]].shape ==
                 IR_TYPE_SLICE ||
             (ir->types[type->argument_types[0]].shape ==
                  IR_TYPE_BUILTIN_OBJECT &&
              strcmp(ir->types[type->argument_types[0]].name,
                     "string") == 0)) &&
            type_is_c_supported_inner(
                ir, type->argument_types[0], visiting, resolved) &&
            type_is_c_supported_inner(
                ir, type->element_type, visiting, resolved);
        visiting[type_id] = false;
        resolved[type_id] = supported;
        return supported;
    }
    if (type->shape != IR_TYPE_ARRAY &&
        type->shape != IR_TYPE_STRUCT &&
        type->shape != IR_TYPE_UNION)
        return false;
    visiting[type_id] = true;
    bool supported = true;
    if (type->shape == IR_TYPE_ARRAY) {
        supported = type->array_length != 0U &&
                    type_is_c_supported_inner(
                        ir, type->element_type, visiting, resolved);
    } else if (type->shape == IR_TYPE_STRUCT ||
               type->shape == IR_TYPE_CLASS_REFERENCE) {
        supported = type->shape == IR_TYPE_CLASS_REFERENCE
                  ? type->object_layout_known
                  : type->field_count != 0U;
        for (size_t field = 0U;
             supported && field < type->field_count; ++field)
            if (!type_is_c_supported_inner(
                    ir, type->field_types[field],
                    visiting, resolved)) {
                supported = false;
                break;
            }
    } else {
        supported = type->variant_count != 0U;
        for (size_t variant = 0U;
             supported && variant < type->variant_count; ++variant) {
            IrTypeId payload =
                type->variant_payload_types[variant];
            if (payload != IR_INVALID_ID &&
                !type_is_c_supported_inner(
                    ir, payload, visiting, resolved))
                supported = false;
        }
    }
    visiting[type_id] = false;
    resolved[type_id] = supported;
    return supported;
}

static bool emit_aggregate_type(
    CEmitter *emitter, IrTypeId type_id, uint8_t *states) {
    if (states[type_id] == 2U) return true;
    if (states[type_id] == 1U) return false;
    const IrType *type = &emitter->ir->types[type_id];
    if (type->shape != IR_TYPE_ARRAY &&
        type->shape != IR_TYPE_STRUCT &&
        type->shape != IR_TYPE_CLASS_REFERENCE &&
        type->shape != IR_TYPE_UNION &&
        type->shape != IR_TYPE_FUNCTION &&
        type->shape != IR_TYPE_SLICE &&
        !c_backend_type_is_vec(type) &&
        !c_backend_type_is_queue(type) &&
        !c_backend_type_is_dictionary(type) &&
        type->shape != IR_TYPE_ITERATOR) {
        states[type_id] = 2U;
        return true;
    }
    states[type_id] = 1U;
    if (type->shape == IR_TYPE_SLICE) {
        if (!emit_aggregate_type(
                emitter, type->element_type, states))
            return false;
    } else if (type->shape == IR_TYPE_ITERATOR) {
        if (!emit_aggregate_type(
                emitter, type->argument_types[0], states) ||
            !emit_aggregate_type(
                emitter, type->element_type, states))
            return false;
    } else if (c_backend_type_is_vec(type) ||
               c_backend_type_is_queue(type)) {
        if (type->element_type != IR_INVALID_ID &&
            !emit_aggregate_type(
                emitter, type->element_type, states))
            return false;
    } else if (c_backend_type_is_dictionary(type)) {
        if (type->element_type != IR_INVALID_ID &&
            !emit_aggregate_type(emitter, type->element_type, states))
            return false;
        if (type->error_type != IR_INVALID_ID &&
            !emit_aggregate_type(emitter, type->error_type, states))
            return false;
    } else if (type->shape == IR_TYPE_FUNCTION) {
        if (!emit_aggregate_type(
                emitter, type->element_type, states))
            return false;
        for (size_t argument = 0U;
             argument < type->argument_count; ++argument)
            if (!emit_aggregate_type(
                    emitter, type->argument_types[argument],
                    states))
                return false;
    } else if (type->shape == IR_TYPE_ARRAY) {
        if (!emit_aggregate_type(
                emitter, type->element_type, states))
            return false;
    } else if (type->shape == IR_TYPE_STRUCT) {
        for (size_t field = 0U;
             field < type->field_count; ++field)
            if (!emit_aggregate_type(
                    emitter, type->field_types[field], states))
                return false;
    } else if (type->shape == IR_TYPE_CLASS_REFERENCE) {
        for (size_t field = 0U;
             field < type->field_count; ++field) {
            IrTypeId field_type = type->field_types[field];
            if (emitter->ir->types[field_type].shape !=
                    IR_TYPE_CLASS_REFERENCE &&
                !emit_aggregate_type(emitter, field_type, states))
                return false;
        }
    } else {
        for (size_t variant = 0U;
             variant < type->variant_count; ++variant) {
            IrTypeId payload =
                type->variant_payload_types[variant];
            if (payload != IR_INVALID_ID &&
                !emit_aggregate_type(
                    emitter, payload, states))
                return false;
        }
    }
    if (type->shape == IR_TYPE_SLICE) {
        fprintf(emitter->output,
                "typedef struct aster_slice_%" PRIu32 " {\n"
                "    ", type_id);
        const IrType *element =
            &emitter->ir->types[type->element_type];
        if (element->shape == IR_TYPE_UNSIGNED_INT &&
            element->bit_width == 8U)
            fputs("uint8_t", emitter->output);
        else
            c_backend_emit_type(emitter, type->element_type);
        fputs(" *data;\n"
              "    size_t length;\n",
              emitter->output);
        fprintf(emitter->output,
                "} aster_slice_%" PRIu32 ";\n\n", type_id);
        states[type_id] = 2U;
        return true;
    }
    if (c_backend_type_is_vec(type)) {
        fprintf(emitter->output,
                "typedef struct aster_vec_%" PRIu32 " {\n"
                "    ", type_id);
        c_backend_emit_type(emitter, type->element_type);
        fputs(" *data;\n"
              "    size_t length;\n"
              "    size_t capacity;\n"
              "    aster_list_mutation *mutations;\n"
              "    size_t mutation_count;\n"
              "    size_t mutation_capacity;\n"
              "    bool record_mutations;\n",
              emitter->output);
        fprintf(emitter->output,
                "} aster_vec_%" PRIu32 ";\n\n", type_id);
        states[type_id] = 2U;
        return true;
    }
    if (c_backend_type_is_queue(type)) {
        fprintf(emitter->output,
                "typedef struct aster_queue_%" PRIu32 " {\n"
                "    ", type_id);
        c_backend_emit_type(emitter, type->element_type);
        fputs(" *data;\n"
              "    size_t length;\n"
              "    size_t capacity;\n"
              "    size_t head;\n",
              emitter->output);
        fprintf(emitter->output,
                "} aster_queue_%" PRIu32 ";\n\n", type_id);
        fprintf(emitter->output,
                "static void aster_queue_resize_%" PRIu32
                "(aster_queue_%" PRIu32 " *value, size_t capacity) {\n"
                "    if (capacity < value->length)\n"
                "        aster_trap(\"Queue capacity cannot be less than Count\");\n"
                "    if (capacity == value->capacity && value->head == 0U) return;\n"
                "    if (capacity > SIZE_MAX / sizeof(*value->data))\n"
                "        aster_trap(\"Queue capacity overflow\");\n"
                "    void *memory = capacity == 0U ? NULL :\n"
                "        aster_allocate(capacity * sizeof(*value->data));\n"
                "    ",
                type_id, type_id);
        c_backend_emit_type(emitter, type->element_type);
        fputs(" *data = memory;\n"
              "    for (size_t i = 0U; i < value->length; ++i)\n"
              "        data[i] = value->data[(value->head + i) % value->capacity];\n"
              "    free(value->data);\n"
              "    value->data = data;\n"
              "    value->capacity = capacity;\n"
              "    value->head = 0U;\n"
              "}\n\n",
              emitter->output);
        states[type_id] = 2U;
        return true;
    }
    if (c_backend_type_is_dictionary(type)) {
        fprintf(emitter->output,
                "typedef struct aster_dictionary_%" PRIu32 " {\n"
                "    ", type_id);
        c_backend_emit_type(emitter, type->element_type);
        fputs(" *keys;\n    ", emitter->output);
        c_backend_emit_type(emitter, type->error_type);
        fputs(" *values;\n"
              "    uint64_t *hashes;\n"
              "    size_t *buckets;\n"
              "    size_t bucket_count;\n"
              "    size_t length;\n"
              "    size_t capacity;\n",
              emitter->output);
        fprintf(emitter->output,
                "} aster_dictionary_%" PRIu32 ";\n\n", type_id);
        fprintf(emitter->output,
                "static uint64_t aster_dictionary_mix_%" PRIu32
                "(uint64_t value) {\n"
                "    value ^= value >> 30U;\n"
                "    value *= UINT64_C(0xbf58476d1ce4e5b9);\n"
                "    value ^= value >> 27U;\n"
                "    value *= UINT64_C(0x94d049bb133111eb);\n"
                "    return value ^ (value >> 31U);\n"
                "}\n\n",
                type_id);
        fputs("static uint64_t aster_dictionary_hash_", emitter->output);
        fprintf(emitter->output, "%" PRIu32 "(", type_id);
        c_backend_emit_type(emitter, type->element_type);
        fputs(" key) {\n", emitter->output);
        const IrType *key = &emitter->ir->types[type->element_type];
        if (key->shape == IR_TYPE_BUILTIN_OBJECT &&
            strcmp(key->name, "string") == 0)
            fprintf(emitter->output,
                    "    aster_str text = aster_string_as_str(key);\n"
                    "    uint64_t hash = UINT64_C(1469598103934665603);\n"
                    "    for (size_t i = 0U; i < text.length; ++i) {\n"
                    "        hash ^= (unsigned char)text.data[i];\n"
                    "        hash *= UINT64_C(1099511628211);\n"
                    "    }\n"
                    "    return aster_dictionary_mix_%" PRIu32 "(hash);\n",
                    type_id);
        else if (key->shape == IR_TYPE_FLOAT && key->bit_width == 32U)
            fprintf(emitter->output,
                    "    uint64_t bits = 0U;\n"
                    "    double normalized = key == 0.0f ? 0.0 : (double)key;\n"
                    "    memcpy(&bits, &normalized, sizeof(bits));\n"
                    "    return aster_dictionary_mix_%" PRIu32 "(bits);\n",
                    type_id);
        else if (key->shape == IR_TYPE_FLOAT)
            fprintf(emitter->output,
                    "    uint64_t bits = 0U;\n"
                    "    double normalized = key == 0.0 ? 0.0 : key;\n"
                    "    memcpy(&bits, &normalized, sizeof(bits));\n"
                    "    return aster_dictionary_mix_%" PRIu32 "(bits);\n",
                    type_id);
        else if (key->shape == IR_TYPE_RAW_POINTER)
            fprintf(emitter->output,
                    "    return aster_dictionary_mix_%" PRIu32
                    "((uint64_t)(uintptr_t)key);\n",
                    type_id);
        else
            fprintf(emitter->output,
                    "    return aster_dictionary_mix_%" PRIu32
                    "((uint64_t)key);\n",
                    type_id);
        fputs("}\n\n", emitter->output);
        fprintf(emitter->output,
                "static void aster_dictionary_rebuild_%" PRIu32
                "(aster_dictionary_%" PRIu32 " *value) {\n"
                "    free(value->buckets);\n"
                "    value->buckets = NULL;\n"
                "    value->bucket_count = 0U;\n"
                "    if (value->capacity == 0U) return;\n"
                "    if (value->capacity > SIZE_MAX / 2U)\n"
                "        aster_trap(\"Dictionary capacity overflow\");\n"
                "    size_t required = value->capacity * 2U;\n"
                "    size_t bucket_count = 8U;\n"
                "    while (bucket_count < required) {\n"
                "        if (bucket_count > SIZE_MAX / 2U)\n"
                "            aster_trap(\"Dictionary capacity overflow\");\n"
                "        bucket_count *= 2U;\n"
                "    }\n"
                "    value->buckets = aster_allocate("
                "bucket_count * sizeof(*value->buckets));\n"
                "    memset(value->buckets, 0, "
                "bucket_count * sizeof(*value->buckets));\n"
                "    value->bucket_count = bucket_count;\n"
                "    size_t mask = bucket_count - 1U;\n"
                "    for (size_t i = 0U; i < value->length; ++i) {\n"
                "        size_t bucket = (size_t)value->hashes[i] & mask;\n"
                "        while (value->buckets[bucket] != 0U)\n"
                "            bucket = (bucket + 1U) & mask;\n"
                "        value->buckets[bucket] = i + 1U;\n"
                "    }\n"
                "}\n\n",
                type_id, type_id);
        fprintf(emitter->output,
                "static inline void aster_dictionary_erase_%" PRIu32
                "(aster_dictionary_%" PRIu32
                " *value, size_t index, size_t bucket) {\n"
                "    size_t mask = value->bucket_count - 1U;\n"
                "    value->buckets[bucket] = 0U;\n"
                "    size_t scan = (bucket + 1U) & mask;\n"
                "    while (value->buckets[scan] != 0U) {\n"
                "        size_t entry = value->buckets[scan];\n"
                "        value->buckets[scan] = 0U;\n"
                "        size_t physical = entry - 1U;\n"
                "        size_t destination = "
                "(size_t)value->hashes[physical] & mask;\n"
                "        while (value->buckets[destination] != 0U)\n"
                "            destination = (destination + 1U) & mask;\n"
                "        value->buckets[destination] = entry;\n"
                "        scan = (scan + 1U) & mask;\n"
                "    }\n"
                "    size_t last = value->length - 1U;\n"
                "    if (index != last) {\n"
                "        value->keys[index] = value->keys[last];\n"
                "        value->values[index] = value->values[last];\n"
                "        value->hashes[index] = value->hashes[last];\n"
                "        size_t moved = (size_t)value->hashes[index] & mask;\n"
                "        while (value->buckets[moved] != last + 1U)\n"
                "            moved = (moved + 1U) & mask;\n"
                "        value->buckets[moved] = index + 1U;\n"
                "    }\n"
                "    --value->length;\n"
                "}\n\n",
                type_id, type_id);
        states[type_id] = 2U;
        return true;
    }
    if (type->shape == IR_TYPE_ITERATOR) {
        const IrType *source =
            &emitter->ir->types[type->argument_types[0]];
        fprintf(emitter->output,
                "typedef struct aster_iterator_%" PRIu32 " {\n",
                type_id);
        if (c_backend_type_is_vec(source)) {
            fputs("    ", emitter->output);
            c_backend_emit_type(emitter, type->argument_types[0]);
            fputs(" vector;\n", emitter->output);
        } else if (c_backend_type_is_queue(source)) {
            fputs("    ", emitter->output);
            c_backend_emit_type(emitter, type->argument_types[0]);
            fputs(" queue;\n", emitter->output);
        } else if (source->shape == IR_TYPE_ARRAY) {
            fputs("    ", emitter->output);
            c_backend_emit_type(emitter, type->argument_types[0]);
            fputs(" owned_array;\n"
                  "    ", emitter->output);
            c_backend_emit_type(emitter, type->argument_types[0]);
            fputs(" *borrowed_array;\n", emitter->output);
        } else {
            fputs("    ", emitter->output);
            c_backend_emit_type(emitter, type->argument_types[0]);
            fputs(" slice;\n", emitter->output);
        }
        fputs("    size_t index;\n"
              "    bool borrowed;\n",
              emitter->output);
        fprintf(emitter->output,
                "} aster_iterator_%" PRIu32 ";\n\n", type_id);
        states[type_id] = 2U;
        return true;
    }
    if (type->shape == IR_TYPE_FUNCTION) {
        fprintf(emitter->output,
                "typedef struct aster_type_%" PRIu32 " {\n    ",
                type_id);
        c_backend_emit_type(emitter, type->element_type);
        fprintf(emitter->output,
                " (*invoke)(void *receiver");
        for (size_t argument = 0U;
             argument < type->argument_count; ++argument) {
            fputs(", ", emitter->output);
            c_backend_emit_type(
                emitter, type->argument_types[argument]);
            if (type->parameter_modes != NULL &&
                parameter_mode_is_reference(
                    type->parameter_modes[argument]))
                fputs(" *", emitter->output);
        }
        fputs(");\n    void *receiver;\n", emitter->output);
        fprintf(emitter->output,
                "} aster_type_%" PRIu32 ";\n\n", type_id);
        states[type_id] = 2U;
        return true;
    }
    if (type->shape == IR_TYPE_CLASS_REFERENCE)
        fprintf(emitter->output,
                "struct aster_type_%" PRIu32 " {\n", type_id);
    else
        fprintf(emitter->output,
                "typedef struct aster_type_%" PRIu32 " {\n", type_id);
    if (type->shape == IR_TYPE_ARRAY) {
        fputs("    ", emitter->output);
        c_backend_emit_type(emitter, type->element_type);
        fprintf(emitter->output, " items[%zu];\n",
                type->array_length);
    } else if (type->shape == IR_TYPE_STRUCT ||
               type->shape == IR_TYPE_CLASS_REFERENCE) {
        if (type->shape == IR_TYPE_CLASS_REFERENCE)
            fputs("    uint32_t _type_id;\n", emitter->output);
        if (type->shape == IR_TYPE_STRUCT && type->field_count == 0U)
            fputs("    uint8_t _empty;\n", emitter->output);
        for (size_t field = 0U;
             field < type->field_count; ++field) {
            fputs("    ", emitter->output);
            c_backend_emit_type(emitter, type->field_types[field]);
            fprintf(emitter->output, " f%zu;\n", field);
        }
    } else {
        fputs("    uint32_t tag;\n", emitter->output);
        bool has_payload = false;
        for (size_t variant = 0U;
             variant < type->variant_count; ++variant)
            if (type->variant_payload_types[variant] !=
                IR_INVALID_ID)
                has_payload = true;
        if (has_payload) {
            fputs("    union {\n", emitter->output);
            for (size_t variant = 0U;
                 variant < type->variant_count; ++variant) {
                IrTypeId payload =
                    type->variant_payload_types[variant];
                if (payload == IR_INVALID_ID) continue;
                fputs("        ", emitter->output);
                c_backend_emit_type(emitter, payload);
                fprintf(
                    emitter->output, " v%zu;\n", variant);
            }
            fputs("    } payload;\n", emitter->output);
        }
    }
    if (type->shape == IR_TYPE_CLASS_REFERENCE)
        fputs("};\n\n", emitter->output);
    else
        fprintf(emitter->output,
                "} aster_type_%" PRIu32 ";\n\n", type_id);
    states[type_id] = 2U;
    return true;
}

bool c_backend_emit_aggregate_types(CEmitter *emitter) {
    uint8_t *states = calloc(
        emitter->ir->type_count, sizeof(*states));
    if (states == NULL) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    bool ok = true;
    for (size_t type = 0U; type < emitter->ir->type_count; ++type)
        if (emitter->used_types[type] &&
            emitter->ir->types[type].shape == IR_TYPE_CLASS_REFERENCE)
            fprintf(emitter->output,
                    "typedef struct aster_type_%zu aster_type_%zu;\n",
                    type, type);
    fputc('\n', emitter->output);
    for (size_t type = 0U;
         type < emitter->ir->type_count; ++type) {
        const IrType *entry = &emitter->ir->types[type];
        if (emitter->used_types[type] &&
            (entry->shape == IR_TYPE_ARRAY ||
             entry->shape == IR_TYPE_STRUCT ||
             entry->shape == IR_TYPE_CLASS_REFERENCE ||
             entry->shape == IR_TYPE_UNION ||
             entry->shape == IR_TYPE_FUNCTION ||
             entry->shape == IR_TYPE_SLICE ||
             entry->shape == IR_TYPE_ITERATOR ||
             c_backend_type_is_vec(entry) ||
             c_backend_type_is_queue(entry) ||
             c_backend_type_is_dictionary(entry)) &&
            c_backend_type_is_supported(emitter->ir, (IrTypeId)type) &&
            !emit_aggregate_type(
                emitter, (IrTypeId)type, states)) {
            ok = false;
            break;
        }
    }
    free(states);
    return ok;
}

bool c_backend_type_is_supported(
    const IrModule *ir, IrTypeId type_id) {
    bool *visiting = calloc(ir->type_count, sizeof(*visiting));
    bool *resolved = calloc(ir->type_count, sizeof(*resolved));
    if (visiting == NULL || resolved == NULL) {
        free(visiting);
        free(resolved);
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    bool supported = type_is_c_supported_inner(
        ir, type_id, visiting, resolved);
    free(visiting);
    free(resolved);
    return supported;
}

void c_backend_emit_type(CEmitter *emitter, IrTypeId type_id) {
    const IrType *type = &emitter->ir->types[type_id];
    const char *scalar = scalar_c_type(type);
    if (scalar != NULL)
        fputs(scalar, emitter->output);
    else if (type->shape == IR_TYPE_STRING_VIEW)
        fputs("aster_str", emitter->output);
    else if (c_backend_type_is_native_handle(type))
        fputs("aster_native_handle *", emitter->output);
    else if (c_backend_type_is_buffer(type))
        fputs("aster_buffer *", emitter->output);
    else if (c_backend_type_is_arena(type))
        fputs("aster_arena *", emitter->output);
    else if (c_backend_type_is_task(type))
        fputs("aster_task *", emitter->output);
    else if (c_backend_type_is_cancellation(type))
        fputs("aster_cancellation_state *", emitter->output);
    else if (type->shape == IR_TYPE_RAW_POINTER) {
        if (!type->pointer_mutable)
            fputs("const ", emitter->output);
        c_backend_emit_type(emitter, type->element_type);
        fputs(" *", emitter->output);
    }
    else if (type->shape == IR_TYPE_SLICE)
        fprintf(emitter->output,
                "aster_slice_%" PRIu32, type_id);
    else if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
             strcmp(type->name, "string") == 0)
        fputs("aster_string *", emitter->output);
    else if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
             strcmp(type->name, "StringBuilder") == 0)
        fputs("aster_string_builder *", emitter->output);
    else if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
             strcmp(type->name, "Html") == 0)
        fputs("aster_html *", emitter->output);
    else if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
             strcmp(type->name, "Url") == 0)
        fputs("aster_url *", emitter->output);
    else if (c_backend_type_is_vec(type))
        fprintf(emitter->output,
                "aster_vec_%" PRIu32 " *", type_id);
    else if (c_backend_type_is_queue(type))
        fprintf(emitter->output,
                "aster_queue_%" PRIu32 " *", type_id);
    else if (c_backend_type_is_dictionary(type))
        fprintf(emitter->output,
                "aster_dictionary_%" PRIu32 " *", type_id);
    else if (type->shape == IR_TYPE_ITERATOR)
        fprintf(emitter->output,
                "aster_iterator_%" PRIu32, type_id);
    else if (type->shape == IR_TYPE_ELEMENT_BUILDER)
        fputs("aster_element_builder *", emitter->output);
    else if (type->shape == IR_TYPE_CLASS_REFERENCE)
        fprintf(emitter->output, "aster_type_%" PRIu32 " *", type_id);
    else
        fprintf(emitter->output, "aster_type_%" PRIu32, type_id);
}

bool c_backend_type_needs_drop(
    const CEmitter *emitter, IrTypeId type_id) {
    return type_id < emitter->ir->type_count &&
           emitter->ir->types[type_id].drop_policy != IR_DROP_TRIVIAL &&
           c_backend_type_is_supported(emitter->ir, type_id);
}

bool c_backend_local_tracks_drop(
    const CEmitter *emitter, const IrFunction *function,
    uint32_t local_index) {
    return local_index < function->local_count &&
           !(function->is_destructor && local_index == 0U) &&
           !function->locals[local_index].borrowed &&
           c_backend_type_needs_drop(
               emitter, function->locals[local_index].type);
}

bool c_backend_local_is_borrowed_alias(
    const CEmitter *emitter, const IrFunction *function,
    uint32_t local_index
) {
    if (local_index >= function->local_count ||
        !function->locals[local_index].borrowed)
        return false;
    if (local_index < function->parameter_count &&
        parameter_mode_is_reference(
            function->parameters[local_index].mode))
        return false;
    IrTypeShape shape = emitter->ir->types[
        function->locals[local_index].type].shape;
    return shape == IR_TYPE_STRUCT ||
           shape == IR_TYPE_ARRAY ||
           shape == IR_TYPE_UNION;
}

void c_backend_emit_drop_call(
    CEmitter *emitter, IrTypeId type_id,
    const char *prefix, uint32_t index) {
    fprintf(
        emitter->output,
        "aster_drop_%" PRIu32 "(&%s%" PRIu32 ")",
        type_id, prefix, index);
}
