#include "internal.h"
#include "vm_internal.h"

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif


#define runtime_error(vm_, instruction_, message_) \
    vm_runtime_error_at((vm_), instruction_span, (message_))

static void vm_write_out_default(LangVM *vm, LangValue *output) {
    LangValueTag tag = output->tag;
    vm_value_drop_owned(vm, *output);
    *output = (LangValue){.tag=tag};
}

bool vm_unsigned_value_fits_type(uint64_t value, TypeKind kind) {
    switch (kind) {
        case TYPE_U8: return value <= UINT8_MAX;
        case TYPE_U16: return value <= UINT16_MAX;
        case TYPE_U32: return value <= UINT32_MAX;
        case TYPE_U64: case TYPE_USIZE: return true;
        default: return false;
    }
}

unsigned vm_runtime_integer_width(TypeKind kind) {
    switch (kind) {
        case TYPE_I8: case TYPE_U8: return 8U;
        case TYPE_I16: case TYPE_U16: return 16U;
        case TYPE_I32: case TYPE_U32: return 32U;
        case TYPE_I64: case TYPE_U64:
        case TYPE_ISIZE: case TYPE_USIZE: return 64U;
        default: return 0U;
    }
}

static uint64_t runtime_integer_mask(unsigned width) {
    return width == 64U
         ? UINT64_MAX
         : (UINT64_C(1) << width) - UINT64_C(1);
}

LangValue vm_runtime_value_from_integer_bits(uint64_t bits,
                                             TypeKind kind) {
    unsigned width = vm_runtime_integer_width(kind);
    uint64_t mask = runtime_integer_mask(width);
    bits &= mask;
    if (kind == TYPE_U8 || kind == TYPE_U16 || kind == TYPE_U32 ||
        kind == TYPE_U64 || kind == TYPE_USIZE)
        return (LangValue){
            .tag = LANG_VALUE_U64,
            .as.u64 = bits
        };
    uint64_t sign = UINT64_C(1) << (width - 1U);
    int64_t value;
    if ((bits & sign) == 0U) {
        value = (int64_t)bits;
    } else {
        uint64_t magnitude = ((~bits) & mask) + UINT64_C(1);
        value = magnitude == (UINT64_C(1) << 63U)
              ? INT64_MIN : -(int64_t)magnitude;
    }
    return (LangValue){
        .tag = LANG_VALUE_I64,
        .as.i64 = value
    };
}

static bool signed_type_bounds(TypeKind kind, int64_t *minimum,
                               int64_t *maximum) {
    switch (kind) {
        case TYPE_I8: *minimum = INT8_MIN; *maximum = INT8_MAX; return true;
        case TYPE_I16: *minimum = INT16_MIN; *maximum = INT16_MAX; return true;
        case TYPE_I32: *minimum = INT32_MIN; *maximum = INT32_MAX; return true;
        case TYPE_I64: case TYPE_ISIZE:
            *minimum = INT64_MIN; *maximum = INT64_MAX; return true;
        default: return false;
    }
}

bool vm_cast_numeric_value(LangValue input, TypeKind target,
                           LangValue *output) {
    int64_t signed_minimum;
    int64_t signed_maximum;
    if (signed_type_bounds(target, &signed_minimum, &signed_maximum)) {
        int64_t value;
        if (input.tag == LANG_VALUE_I64) {
            value = input.as.i64;
        } else if (input.tag == LANG_VALUE_U64) {
            if (input.as.u64 > (uint64_t)signed_maximum) return false;
            value = (int64_t)input.as.u64;
        } else if (input.tag == LANG_VALUE_F64) {
            double lower = target == TYPE_I64 || target == TYPE_ISIZE
                         ? -0x1p63 : (double)signed_minimum;
            double upper = target == TYPE_I64 || target == TYPE_ISIZE
                         ? 0x1p63 : (double)signed_maximum + 1.0;
            if (input.as.f64 < lower || input.as.f64 >= upper ||
                input.as.f64 != input.as.f64)
                return false;
            value = (int64_t)input.as.f64;
        } else {
            return false;
        }
        if (!vm_signed_value_fits_type(value, target)) return false;
        *output = (LangValue){.tag=LANG_VALUE_I64, .as.i64=value};
        return true;
    }
    if (target == TYPE_U8 || target == TYPE_U16 ||
        target == TYPE_U32 || target == TYPE_U64 ||
        target == TYPE_USIZE || target == TYPE_CHAR) {
        uint64_t value;
        if (input.tag == LANG_VALUE_U64) {
            value = input.as.u64;
        } else if (input.tag == LANG_VALUE_I64) {
            if (input.as.i64 < 0) return false;
            value = (uint64_t)input.as.i64;
        } else if (input.tag == LANG_VALUE_F64) {
            double upper = target == TYPE_U64 || target == TYPE_USIZE
                         ? 0x1p64
                         : target == TYPE_CHAR ? 1114112.0
                         : target == TYPE_U32 ? 4294967296.0
                         : target == TYPE_U16 ? 65536.0 : 256.0;
            if (input.as.f64 < 0.0 || input.as.f64 >= upper ||
                input.as.f64 != input.as.f64)
                return false;
            value = (uint64_t)input.as.f64;
        } else {
            return false;
        }
        if (target == TYPE_CHAR) {
            if (value > UINT64_C(0x10ffff) ||
                (value >= UINT64_C(0xd800) &&
                 value <= UINT64_C(0xdfff)))
                return false;
        } else if (!vm_unsigned_value_fits_type(value, target)) {
            return false;
        }
        *output = (LangValue){.tag=LANG_VALUE_U64, .as.u64=value};
        return true;
    }
    if (target == TYPE_F32 || target == TYPE_F64) {
        double value;
        if (input.tag == LANG_VALUE_F64)
            value = input.as.f64;
        else if (input.tag == LANG_VALUE_I64)
            value = (double)input.as.i64;
        else if (input.tag == LANG_VALUE_U64)
            value = (double)input.as.u64;
        else
            return false;
        double maximum = target == TYPE_F32 ? (double)FLT_MAX : DBL_MAX;
        if (value > maximum || value < -maximum || value != value)
            return false;
        if (target == TYPE_F32) value = (double)(float)value;
        *output = (LangValue){.tag=LANG_VALUE_F64, .as.f64=value};
        return true;
    }
    return false;
}

static void print_value(FILE *stream, LangValue value) {
    switch (value.tag) {
        case LANG_VALUE_UNIT: fputs("unit", stream); break;
        case LANG_VALUE_BOOL: fputs(value.as.boolean ? "true" : "false", stream); break;
        case LANG_VALUE_I64: fprintf(stream, "%" PRId64, value.as.i64); break;
        case LANG_VALUE_U64: fprintf(stream, "%" PRIu64, value.as.u64); break;
        case LANG_VALUE_F64: fprintf(stream, "%g", value.as.f64); break;
        case LANG_VALUE_STRING_VIEW:
            if (value.as.string.length != 0U)
                (void)fwrite(value.as.string.data, 1U,
                             value.as.string.length, stream);
            break;
        case LANG_VALUE_BYTE_SLICE:
            fprintf(stream, "Span<u8>(%zu)", value.as.bytes.length);
            break;
        case LANG_VALUE_OBJECT: {
            Object *object = value.as.object;
            if (object->kind == OBJECT_HTML)
                fprintf(stream, "%.*s", (int)object->as.html.length, object->as.html.data);
            else if (object->kind == OBJECT_STRING)
                (void)fwrite(object->as.string.data, 1U,
                             object->as.string.length, stream);
            else if (object->kind == OBJECT_BUFFER)
                fprintf(stream, "Buffer(%zu)", object->as.buffer.length);
            else if (object->kind == OBJECT_STRING_BUILDER)
                fprintf(stream, "StringBuilder(%zu)",
                        object->as.string_builder.length);
            else fputs("<object>", stream);
            break;
        }
        case LANG_VALUE_RAW_POINTER: fprintf(stream, "%p", value.as.pointer); break;
        case LANG_VALUE_FUNCTION:
            fprintf(stream, "<fn:%zu>", value.as.function);
            break;
        case LANG_VALUE_BOUND_FUNCTION:
            fprintf(stream, "<bound-fn:%zu>",
                    value.as.bound_function.function);
            break;
        case LANG_VALUE_NATIVE_FUNCTION:
            fputs("<native-fn>", stream);
            break;
        case LANG_VALUE_NATIVE_ERROR:
            fputs("<native-error>", stream);
            break;
    }
}

bool vm_string_builder_append_bytes(
    Object *builder, const char *data, size_t length) {
    if (builder == NULL ||
        builder->kind != OBJECT_STRING_BUILDER ||
        length > SIZE_MAX -
            builder->as.string_builder.length)
        return false;
    size_t required =
        builder->as.string_builder.length + length;
    if (required > builder->as.string_builder.capacity) {
        size_t capacity =
            builder->as.string_builder.capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) {
                capacity = required;
                break;
            }
            capacity *= 2U;
        }
        char *grown;
        if (builder->as.string_builder.embedded_data) {
            grown = vm_allocate(capacity, 1U);
            if (builder->as.string_builder.length != 0U)
                memcpy(grown, builder->as.string_builder.data,
                       builder->as.string_builder.length);
            builder->as.string_builder.embedded_data = false;
        } else {
            grown = realloc(
                builder->as.string_builder.data, capacity);
        }
        if (grown == NULL)
            return false;
        builder->as.string_builder.data = grown;
        builder->as.string_builder.capacity = capacity;
    }
    if (length != 0U)
        memcpy(
            builder->as.string_builder.data +
                builder->as.string_builder.length,
            data, length);
    builder->as.string_builder.length += length;
    return true;
}

bool vm_string_builder_append_value(
    Object *builder, LangValue value) {
    LangStringView text;
    if (lang_value_string_view(&value, &text))
        return vm_string_builder_append_bytes(
            builder, text.data, text.length);
    char buffer[64];
    size_t length = 0U;
    if (value.tag == LANG_VALUE_I64)
        length = vm_format_i64(buffer, value.as.i64);
    else if (value.tag == LANG_VALUE_U64)
        length = vm_format_u64(buffer, value.as.u64);
    else if (value.tag == LANG_VALUE_BOOL) {
        const char *boolean = value.as.boolean ? "true" : "false";
        return vm_string_builder_append_bytes(
            builder, boolean, value.as.boolean ? 4U : 5U);
    } else if (value.tag == LANG_VALUE_F64) {
        int formatted = snprintf(
            buffer, sizeof(buffer), "%g", value.as.f64);
        if (formatted <= 0 || (size_t)formatted >= sizeof(buffer))
            return false;
        length = (size_t)formatted;
    } else {
        return false;
    }
    return vm_string_builder_append_bytes(builder, buffer, length);
}

static bool vm_list_reserve(Object *list, size_t required) {
    if (required <= list->as.vector.capacity) return true;
    size_t capacity = list->as.vector.capacity == 0U
                    ? 4U : list->as.vector.capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*list->as.vector.items))
        return false;
    LangValue *items = realloc(
        list->as.vector.items, capacity * sizeof(*items));
    if (items == NULL) return false;
    list->as.vector.items = items;
    list->as.vector.capacity = capacity;
    return true;
}

static bool vm_list_set_capacity(Object *list, size_t capacity) {
    if (capacity < list->as.vector.count) return false;
    if (capacity == list->as.vector.capacity) return true;
    if (capacity == 0U) {
        free(list->as.vector.items);
        list->as.vector.items = NULL;
        list->as.vector.capacity = 0U;
        return true;
    }
    if (capacity > SIZE_MAX / sizeof(*list->as.vector.items))
        return false;
    LangValue *items = realloc(
        list->as.vector.items, capacity * sizeof(*items));
    if (items == NULL) return false;
    list->as.vector.items = items;
    list->as.vector.capacity = capacity;
    return true;
}

static size_t vm_list_trim_threshold(size_t capacity) {
    return (capacity / 10U) * 9U +
           ((capacity % 10U) * 9U) / 10U;
}

static bool vm_queue_resize(Object *queue, size_t capacity) {
    if (capacity < queue->as.queue.count ||
        capacity > SIZE_MAX / sizeof(*queue->as.queue.items))
        return false;
    if (capacity == queue->as.queue.capacity &&
        queue->as.queue.head == 0U)
        return true;
    LangValue *items = capacity == 0U ? NULL :
        vm_allocate(capacity, sizeof(*items));
    for (size_t i = 0U; i < queue->as.queue.count; ++i)
        items[i] = queue->as.queue.items[
            (queue->as.queue.head + i) % queue->as.queue.capacity];
    free(queue->as.queue.items);
    queue->as.queue.items = items;
    queue->as.queue.capacity = capacity;
    queue->as.queue.head = 0U;
    return true;
}

static bool vm_queue_reserve(Object *queue, size_t required) {
    if (required <= queue->as.queue.capacity) return true;
    size_t capacity = queue->as.queue.capacity == 0U
                    ? 4U : queue->as.queue.capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    return vm_queue_resize(queue, capacity);
}

static bool vm_list_value_equal(LangValue left, LangValue right) {
    if (left.tag == LANG_VALUE_BOOL && right.tag == LANG_VALUE_BOOL)
        return left.as.boolean == right.as.boolean;
    if (left.tag == LANG_VALUE_I64 && right.tag == LANG_VALUE_I64)
        return left.as.i64 == right.as.i64;
    if (left.tag == LANG_VALUE_U64 && right.tag == LANG_VALUE_U64)
        return left.as.u64 == right.as.u64;
    if (left.tag == LANG_VALUE_F64 && right.tag == LANG_VALUE_F64)
        return left.as.f64 == right.as.f64;
    if (left.tag == LANG_VALUE_RAW_POINTER &&
        right.tag == LANG_VALUE_RAW_POINTER)
        return left.as.pointer == right.as.pointer;
    LangStringView left_text;
    LangStringView right_text;
    return lang_value_string_view(&left, &left_text) &&
           lang_value_string_view(&right, &right_text) &&
           left_text.length == right_text.length &&
           (left_text.length == 0U ||
            memcmp(left_text.data, right_text.data,
                   left_text.length) == 0);
}

static uint64_t vm_dictionary_mix(uint64_t value) {
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

static uint64_t vm_dictionary_hash(LangValue value) {
    if (value.tag == LANG_VALUE_BOOL)
        return vm_dictionary_mix(value.as.boolean ? 1U : 0U);
    if (value.tag == LANG_VALUE_I64)
        return vm_dictionary_mix((uint64_t)value.as.i64);
    if (value.tag == LANG_VALUE_U64)
        return vm_dictionary_mix(value.as.u64);
    if (value.tag == LANG_VALUE_F64) {
        uint64_t bits = 0U;
        double normalized = value.as.f64 == 0.0 ? 0.0 : value.as.f64;
        memcpy(&bits, &normalized, sizeof(bits));
        return vm_dictionary_mix(bits);
    }
    if (value.tag == LANG_VALUE_RAW_POINTER)
        return vm_dictionary_mix((uint64_t)(uintptr_t)value.as.pointer);
    LangStringView text;
    if (lang_value_string_view(&value, &text)) {
        uint64_t hash = UINT64_C(1469598103934665603);
        for (size_t i = 0U; i < text.length; ++i) {
            hash ^= (unsigned char)text.data[i];
            hash *= UINT64_C(1099511628211);
        }
        return vm_dictionary_mix(hash);
    }
    return 0U;
}

static bool vm_dictionary_insert_bucket(Object *dictionary,
                                        size_t physical_index) {
    if (dictionary->as.dictionary.bucket_count == 0U) return false;
    size_t mask = dictionary->as.dictionary.bucket_count - 1U;
    size_t bucket = (size_t)vm_dictionary_hash(
        dictionary->as.dictionary.items[physical_index]) & mask;
    while (dictionary->as.dictionary.buckets[bucket] != 0U)
        bucket = (bucket + 1U) & mask;
    dictionary->as.dictionary.buckets[bucket] = physical_index + 1U;
    return true;
}

static bool vm_dictionary_rebuild(Object *dictionary) {
    size_t logical_capacity = dictionary->as.dictionary.capacity / 2U;
    if (logical_capacity == 0U) {
        free(dictionary->as.dictionary.buckets);
        dictionary->as.dictionary.buckets = NULL;
        dictionary->as.dictionary.bucket_count = 0U;
        return true;
    }
    if (logical_capacity > SIZE_MAX / 2U) return false;
    size_t required = logical_capacity * 2U;
    size_t bucket_count = 8U;
    while (bucket_count < required) {
        if (bucket_count > SIZE_MAX / 2U) return false;
        bucket_count *= 2U;
    }
    size_t *buckets = calloc(bucket_count, sizeof(*buckets));
    if (buckets == NULL) return false;
    free(dictionary->as.dictionary.buckets);
    dictionary->as.dictionary.buckets = buckets;
    dictionary->as.dictionary.bucket_count = bucket_count;
    for (size_t i = 0U; i < dictionary->as.dictionary.count; i += 2U)
        if (!vm_dictionary_insert_bucket(dictionary, i)) return false;
    return true;
}

static bool vm_dictionary_reserve(Object *dictionary,
                                  size_t logical_required) {
    size_t logical_capacity = dictionary->as.dictionary.capacity / 2U;
    if (logical_required <= logical_capacity &&
        dictionary->as.dictionary.bucket_count != 0U)
        return true;
    size_t capacity = logical_capacity == 0U ? 4U : logical_capacity;
    while (capacity < logical_required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = logical_required;
            break;
        }
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / (2U * sizeof(LangValue))) return false;
    LangValue *items = realloc(
        dictionary->as.dictionary.items,
        capacity * 2U * sizeof(*items));
    if (items == NULL) return false;
    dictionary->as.dictionary.items = items;
    dictionary->as.dictionary.capacity = capacity * 2U;
    return vm_dictionary_rebuild(dictionary);
}

static bool vm_dictionary_set_capacity(Object *dictionary,
                                       size_t logical_capacity) {
    if (logical_capacity < dictionary->as.dictionary.count / 2U)
        return false;
    if (logical_capacity == 0U) {
        free(dictionary->as.dictionary.items);
        dictionary->as.dictionary.items = NULL;
        dictionary->as.dictionary.capacity = 0U;
        return vm_dictionary_rebuild(dictionary);
    }
    if (logical_capacity > SIZE_MAX / (2U * sizeof(LangValue)))
        return false;
    LangValue *items = realloc(
        dictionary->as.dictionary.items,
        logical_capacity * 2U * sizeof(*items));
    if (items == NULL) return false;
    dictionary->as.dictionary.items = items;
    dictionary->as.dictionary.capacity = logical_capacity * 2U;
    return vm_dictionary_rebuild(dictionary);
}

size_t vm_dictionary_find(const Object *dictionary, LangValue key) {
    if (dictionary->as.dictionary.bucket_count == 0U) return SIZE_MAX;
    size_t mask = dictionary->as.dictionary.bucket_count - 1U;
    size_t bucket = (size_t)vm_dictionary_hash(key) & mask;
    for (;;) {
        size_t entry = dictionary->as.dictionary.buckets[bucket];
        if (entry == 0U) return SIZE_MAX;
        size_t physical_index = entry - 1U;
        if (vm_list_value_equal(
                dictionary->as.dictionary.items[physical_index], key))
            return physical_index;
        bucket = (bucket + 1U) & mask;
    }
}

static bool vm_dictionary_erase(Object *dictionary, size_t physical_index) {
    if (dictionary->as.dictionary.bucket_count == 0U ||
        physical_index >= dictionary->as.dictionary.count)
        return false;
    size_t mask = dictionary->as.dictionary.bucket_count - 1U;
    size_t bucket = (size_t)vm_dictionary_hash(
        dictionary->as.dictionary.items[physical_index]) & mask;
    while (dictionary->as.dictionary.buckets[bucket] !=
           physical_index + 1U) {
        if (dictionary->as.dictionary.buckets[bucket] == 0U)
            return false;
        bucket = (bucket + 1U) & mask;
    }
    dictionary->as.dictionary.buckets[bucket] = 0U;
    size_t scan = (bucket + 1U) & mask;
    while (dictionary->as.dictionary.buckets[scan] != 0U) {
        size_t entry = dictionary->as.dictionary.buckets[scan];
        dictionary->as.dictionary.buckets[scan] = 0U;
        if (!vm_dictionary_insert_bucket(dictionary, entry - 1U))
            return false;
        scan = (scan + 1U) & mask;
    }

    size_t last = dictionary->as.dictionary.count - 2U;
    if (physical_index != last) {
        dictionary->as.dictionary.items[physical_index] =
            dictionary->as.dictionary.items[last];
        dictionary->as.dictionary.items[physical_index + 1U] =
            dictionary->as.dictionary.items[last + 1U];
        size_t moved = (size_t)vm_dictionary_hash(
            dictionary->as.dictionary.items[physical_index]) & mask;
        while (dictionary->as.dictionary.buckets[moved] != last + 1U)
            moved = (moved + 1U) & mask;
        dictionary->as.dictionary.buckets[moved] = physical_index + 1U;
    }
    dictionary->as.dictionary.items[last] =
        (LangValue){.tag=LANG_VALUE_UNIT};
    dictionary->as.dictionary.items[last + 1U] =
        (LangValue){.tag=LANG_VALUE_UNIT};
    dictionary->as.dictionary.count -= 2U;
    return true;
}

static bool vm_list_call_callback(
    LangVM *vm, LangValue callback, LangValue item,
    LangSpan span, bool expects_bool, bool *matched) {
    if (callback.tag != LANG_VALUE_FUNCTION &&
        callback.tag != LANG_VALUE_BOUND_FUNCTION)
        return false;
    LangValue argument = vm_value_clone(item);
    LangValue callback_result = vm_invoke_function_value(
        vm, callback, &argument, 1U, span);
    if (vm->trapped) return false;
    if (expects_bool && callback_result.tag != LANG_VALUE_BOOL) {
        vm_value_drop_owned(vm, callback_result);
        return false;
    }
    if (matched != NULL)
        *matched = expects_bool && callback_result.as.boolean;
    vm_value_drop_owned(vm, callback_result);
    return true;
}

int64_t vm_string_index_of_ordinal(
    LangStringView value, LangStringView needle, size_t start) {
    if (start > value.length) return -1;
    if (needle.length == 0U) return (int64_t)start;
    if (needle.length > value.length - start) return -1;
    const char *cursor = value.data + start;
    const char *end = value.data +
        (value.length - needle.length + 1U);
    while (cursor < end) {
        const char *candidate = memchr(
            cursor, (unsigned char)needle.data[0],
            (size_t)(end - cursor));
        if (candidate == NULL) return -1;
        if (needle.length == 1U ||
            memcmp(candidate + 1U, needle.data + 1U,
                   needle.length - 1U) == 0)
            return (int64_t)(candidate - value.data);
        cursor = candidate + 1U;
    }
    return -1;
}

bool vm_call_builtin(LangVM *vm, int32_t index, LangValue *args,
                     size_t count, LangValue *result,
                     LangSpan instruction_span) {
    *result = (LangValue){.tag = LANG_VALUE_UNIT};
    #include "vm_builtin_exec_core.inc"
    #include "vm_builtin_exec_lists.inc"
    #include "vm_builtin_exec_dictionaries.inc"
    #include "vm_builtin_exec_queues.inc"
    #include "vm_builtin_exec_text.inc"
    runtime_error(vm, instruction, "invalid native function call");
    return false;
}
