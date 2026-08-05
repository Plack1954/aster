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
            fprintf(stream, "Slice<u8>(%zu)", value.as.bytes.length);
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

static size_t vm_dictionary_find(const Object *dictionary, LangValue key) {
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

static bool vm_list_call_callback(
    LangVM *vm, LangValue callback, LangValue item,
    LangSpan span, bool expects_bool, bool *matched) {
    if (callback.tag != LANG_VALUE_FUNCTION) return false;
    LangValue argument = vm_value_clone(item);
    LangValue callback_result = vm_execute_function(
        vm, callback.as.function, &argument, 1U, span);
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

bool vm_call_builtin(LangVM *vm, int32_t index, LangValue *args,
                     size_t count, LangValue *result,
                     LangSpan instruction_span) {
    *result = (LangValue){.tag = LANG_VALUE_UNIT};
    if ((index == -1 || index == -2 || index == -89 || index == -90) &&
        count == 1U) {
        FILE *stream = (index == -1 || index == -89) ? stdout : stderr;
        print_value(stream, args[0]);
        if (index == -1 || index == -2)
            fputc('\n', stream);
        (void)fflush(stream);
        return true;
    }
    if (index == -3 && count == 1U && args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_HTML) {
        Object *html = args[0].as.object;
        if (html->as.html.destination != NULL || html->as.html.open) {
            runtime_error(
                vm, instruction,
                "cannot convert unfinished or attached Html to string");
            return false;
        }
        return lang_string_value(
            vm,
            (LangStringView){html->as.html.data, html->as.html.length},
            result);
    }
    if (index == -4 && count == 1U && args[0].tag == LANG_VALUE_I64 &&
        args[0].as.i64 >= 0) {
        Object *buffer = vm_allocate(1U, sizeof(*buffer));
        buffer->kind = OBJECT_BUFFER;
        buffer->as.buffer.length = (size_t)args[0].as.i64;
        buffer->as.buffer.data = vm_allocate(buffer->as.buffer.length, 1U);
        result->tag = LANG_VALUE_OBJECT; result->as.object = buffer;
        return true;
    }
    if (index == -5 && count == 0U) {
        Object *arena = vm_allocate(1U, sizeof(*arena));
        arena->kind = OBJECT_ARENA;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = arena;
        return true;
    }
    if (index == -6 && count == 2U && args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_ARENA &&
        args[1].tag == LANG_VALUE_I64 && args[1].as.i64 >= 0) {
        Object *arena = args[0].as.object;
        size_t size = (size_t)args[1].as.i64;
        RawAllocation *allocation = vm_allocate(1U, sizeof(*allocation));
        allocation->data = vm_allocate(size == 0U ? 1U : size, 1U);
        allocation->length = size;
        allocation->active = true;
        if (vm->raw_allocation_count == vm->raw_allocation_capacity) {
            size_t next = vm->raw_allocation_capacity == 0U ? 16U
                                     : vm->raw_allocation_capacity * 2U;
            RawAllocation **items =
                realloc(vm->raw_allocations, next * sizeof(*items));
            if (items == NULL) {
                free(allocation->data);
                free(allocation);
                runtime_error(vm, instruction,
                              "out of memory tracking arena pointer");
                return false;
            }
            vm->raw_allocations = items;
            vm->raw_allocation_capacity = next;
        }
        vm->raw_allocations[vm->raw_allocation_count++] = allocation;
        if (arena->as.arena.count == arena->as.arena.capacity) {
            size_t next = arena->as.arena.capacity == 0U ? 8U
                                                         : arena->as.arena.capacity * 2U;
            RawAllocation **blocks =
                realloc(arena->as.arena.blocks, next * sizeof(*blocks));
            if (blocks == NULL) {
                allocation->active = false;
                free(allocation->data);
                allocation->data = NULL;
                runtime_error(vm, instruction, "out of memory growing arena");
                return false;
            }
            arena->as.arena.blocks = blocks;
            arena->as.arena.capacity = next;
        }
        arena->as.arena.blocks[arena->as.arena.count++] = allocation;
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        result->tag = LANG_VALUE_RAW_POINTER;
        result->as.pointer = allocation;
        return true;
    }
    if (index == -7 && count == 1U && args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_ARENA) {
        Object *arena = args[0].as.object;
        for (size_t i = arena->as.arena.count; i > 0U; --i) {
            RawAllocation *allocation = arena->as.arena.blocks[i - 1U];
            if (allocation->active) free(allocation->data);
            allocation->data = NULL;
            allocation->active = false;
        }
        arena->as.arena.count = 0U;
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -8 && count == 1U &&
        args[0].tag == LANG_VALUE_RAW_POINTER) {
        RawAllocation *allocation = args[0].as.pointer;
        if (allocation == NULL || !allocation->active ||
            allocation->length < sizeof(int64_t)) {
            runtime_error(vm, instruction,
                          "invalid, expired, or undersized raw pointer load");
            return false;
        }
        int64_t value;
        memcpy(&value, allocation->data, sizeof(value));
        result->tag = LANG_VALUE_I64;
        result->as.i64 = value;
        return true;
    }
    if (index == -9 && count == 2U &&
        args[0].tag == LANG_VALUE_RAW_POINTER &&
        args[1].tag == LANG_VALUE_I64) {
        RawAllocation *allocation = args[0].as.pointer;
        if (allocation == NULL || !allocation->active ||
            allocation->length < sizeof(int64_t)) {
            runtime_error(vm, instruction,
                          "invalid, expired, or undersized raw pointer store");
            return false;
        }
        memcpy(allocation->data, &args[1].as.i64, sizeof(int64_t));
        return true;
    }
    if (index == -10 && count == 1U) {
        LangStringView message;
        if (!lang_value_string_view(&args[0], &message)) {
            runtime_error(vm, instruction, "panic expects a string message");
            return false;
        }
        char rendered[257];
        size_t length = message.length < sizeof(rendered) - 1U
                      ? message.length : sizeof(rendered) - 1U;
        if (length != 0U) memcpy(rendered, message.data, length);
        rendered[length] = '\0';
        runtime_error(vm, instruction, rendered);
        return false;
    }
    if ((index == -11 || index == -15 || index == -16) && count == 1U) {
        if ((index == -11 || index == -15) &&
            args[0].tag == LANG_VALUE_OBJECT &&
            args[0].as.object != NULL &&
            ((Object *)args[0].as.object)->kind ==
                OBJECT_STRING) {
            *result = args[0];
            args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
            return true;
        }
        LangStringView source;
        if (!lang_value_string_view(&args[0], &source)) {
            runtime_error(vm, instruction,
                          "owned string constructor expects a string view");
            return false;
        }
        bool fragment = index == -16;
        if (source.length > SIZE_MAX - (fragment ? 2U : 1U)) {
            runtime_error(vm, instruction, "string is too large");
            return false;
        }
        Object *string = vm_allocate(1U, sizeof(*string));
        string->kind = OBJECT_STRING;
        string->references = 1U;
        string->as.string.length = source.length + (fragment ? 1U : 0U);
        string->as.string.data =
            vm_allocate(string->as.string.length + 1U, 1U);
        size_t offset = 0U;
        if (fragment) string->as.string.data[offset++] = '#';
        if (source.length != 0U)
            memcpy(string->as.string.data + offset,
                   source.data, source.length);
        string->as.string.data[string->as.string.length] = '\0';
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = string;
        return true;
    }
    if (index == -12 && count == 0U) {
        Object *builder = vm_allocate(1U, sizeof(*builder));
        builder->kind = OBJECT_STRING_BUILDER;
        builder->as.string_builder.capacity = 64U;
        builder->as.string_builder.data =
            vm_allocate(builder->as.string_builder.capacity, 1U);
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = builder;
        return true;
    }
    if (index == -13 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER) {
        Object *builder = args[0].as.object;
        if (!vm_string_builder_append_value(builder, args[1])) {
            runtime_error(
                vm, instruction,
                "could not grow StringBuilder");
            return false;
        }
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -89 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER &&
        args[1].tag == LANG_VALUE_U64 && args[1].as.u64 <= 0x10ffffU &&
        !(args[1].as.u64 >= 0xd800U && args[1].as.u64 <= 0xdfffU)) {
        uint32_t scalar = (uint32_t)args[1].as.u64;
        char encoded[4];
        size_t length;
        if (scalar <= 0x7fU) {
            encoded[0] = (char)scalar; length = 1U;
        } else if (scalar <= 0x7ffU) {
            encoded[0] = (char)(0xc0U | (scalar >> 6U));
            encoded[1] = (char)(0x80U | (scalar & 0x3fU)); length = 2U;
        } else if (scalar <= 0xffffU) {
            encoded[0] = (char)(0xe0U | (scalar >> 12U));
            encoded[1] = (char)(0x80U | ((scalar >> 6U) & 0x3fU));
            encoded[2] = (char)(0x80U | (scalar & 0x3fU)); length = 3U;
        } else {
            encoded[0] = (char)(0xf0U | (scalar >> 18U));
            encoded[1] = (char)(0x80U | ((scalar >> 12U) & 0x3fU));
            encoded[2] = (char)(0x80U | ((scalar >> 6U) & 0x3fU));
            encoded[3] = (char)(0x80U | (scalar & 0x3fU)); length = 4U;
        }
        if (!vm_string_builder_append_bytes(
                args[0].as.object, encoded, length)) {
            runtime_error(vm, instruction, "could not grow StringBuilder");
            return false;
        }
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -25 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER &&
        args[1].tag == LANG_VALUE_U64) {
        unsigned char byte = (unsigned char)args[1].as.u64;
        Object *builder = args[0].as.object;
        if (!vm_string_builder_append_bytes(
                builder, (const char *)&byte, 1U)) {
            runtime_error(vm, instruction, "could not grow StringBuilder");
            return false;
        }
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -14 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER) {
        Object *builder = args[0].as.object;
        char *data = builder->as.string_builder.data;
        size_t length = builder->as.string_builder.length;
        bool embedded_data =
            builder->as.string_builder.embedded_data;
        builder->kind = OBJECT_STRING;
        builder->references = 1U;
        builder->as.string.data = data;
        builder->as.string.length = length;
        builder->as.string.embedded_data = embedded_data;
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = builder;
        return true;
    }
    if (index == -26 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER) {
        Object *builder = args[0].as.object;
        Object *string = vm_allocate(1U, sizeof(*string));
        string->kind = OBJECT_STRING;
        string->references = 1U;
        string->as.string.length = builder->as.string_builder.length;
        string->as.string.data = vm_allocate(
            string->as.string.length + 1U, 1U);
        if (string->as.string.length != 0U)
            memcpy(string->as.string.data,
                   builder->as.string_builder.data,
                   string->as.string.length);
        string->as.string.data[string->as.string.length] = '\0';
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = string;
        return true;
    }
    if (index == -27 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER) {
        result->tag = LANG_VALUE_U64;
        result->as.u64 =
            ((Object *)args[0].as.object)->as.string_builder.length;
        return true;
    }
    if (index == -28 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER) {
        ((Object *)args[0].as.object)->as.string_builder.length = 0U;
        return true;
    }
    if (index == -17 && count == 0U) {
        Object *vector = vm_allocate(1U, sizeof(*vector));
        vector->kind = OBJECT_VEC;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = vector;
        return true;
    }
    if (index == -18 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *vector = args[0].as.object;
        if (!vm_list_reserve(vector, vector->as.vector.count + 1U)) {
            runtime_error(vm, instruction, "out of memory growing List");
            return false;
        }
        vector->as.vector.items[vector->as.vector.count++] = args[1];
        args[1] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -19 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *vector = args[0].as.object;
        result->tag = LANG_VALUE_U64;
        result->as.u64 = (uint64_t)vector->as.vector.count;
        return true;
    }
    if (index == -23 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64) {
        Object *vector = args[0].as.object;
        uint64_t index_value = args[1].as.u64;
        if (index_value > (uint64_t)SIZE_MAX ||
            (size_t)index_value >= vector->as.vector.count) {
            runtime_error(vm, instruction, "List index out of bounds");
            return false;
        }
        *result = vm_value_clone(
            vector->as.vector.items[(size_t)index_value]);
        args[1] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -29 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        result->tag = LANG_VALUE_U64;
        result->as.u64 = (uint64_t)
            ((Object *)args[0].as.object)->as.vector.capacity;
        return true;
    }
    if (index == -30 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *list = args[0].as.object;
        for (size_t i = list->as.vector.count; i > 0U; --i) {
            vm_value_drop_owned(vm, list->as.vector.items[i - 1U]);
            list->as.vector.items[i - 1U] =
                (LangValue){.tag=LANG_VALUE_UNIT};
        }
        list->as.vector.count = 0U;
        return true;
    }
    if (index == -31 && count == 3U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64) {
        Object *list = args[0].as.object;
        uint64_t raw_index = args[1].as.u64;
        if (raw_index > (uint64_t)SIZE_MAX ||
            (size_t)raw_index > list->as.vector.count) {
            runtime_error(vm, instruction, "List insert index out of bounds");
            return false;
        }
        if (!vm_list_reserve(list, list->as.vector.count + 1U)) {
            runtime_error(vm, instruction, "out of memory growing List");
            return false;
        }
        size_t at = (size_t)raw_index;
        memmove(list->as.vector.items + at + 1U,
                list->as.vector.items + at,
                (list->as.vector.count - at) * sizeof(*list->as.vector.items));
        list->as.vector.items[at] = args[2];
        ++list->as.vector.count;
        args[2] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -32 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64) {
        Object *list = args[0].as.object;
        uint64_t raw_index = args[1].as.u64;
        if (raw_index > (uint64_t)SIZE_MAX ||
            (size_t)raw_index >= list->as.vector.count) {
            runtime_error(vm, instruction, "List index out of bounds");
            return false;
        }
        size_t at = (size_t)raw_index;
        vm_value_drop_owned(vm, list->as.vector.items[at]);
        memmove(list->as.vector.items + at,
                list->as.vector.items + at + 1U,
                (list->as.vector.count - at - 1U) *
                    sizeof(*list->as.vector.items));
        --list->as.vector.count;
        list->as.vector.items[list->as.vector.count] =
            (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -33 && count == 3U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64) {
        Object *list = args[0].as.object;
        uint64_t raw_index = args[1].as.u64;
        if (raw_index > (uint64_t)SIZE_MAX ||
            (size_t)raw_index >= list->as.vector.count) {
            runtime_error(vm, instruction, "List index out of bounds");
            return false;
        }
        size_t at = (size_t)raw_index;
        vm_value_drop_owned(vm, list->as.vector.items[at]);
        list->as.vector.items[at] = args[2];
        args[2] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if ((index == -34 || index == -35 || index == -36 || index == -37) &&
        count == 2U && args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *list = args[0].as.object;
        size_t found = SIZE_MAX;
        if (index == -36) {
            for (size_t i = list->as.vector.count; i > 0U; --i) {
                if (vm_list_value_equal(
                        list->as.vector.items[i - 1U], args[1])) {
                    found = i - 1U;
                    break;
                }
            }
        } else {
            for (size_t i = 0U; i < list->as.vector.count; ++i) {
                if (vm_list_value_equal(list->as.vector.items[i], args[1])) {
                    found = i;
                    break;
                }
            }
        }
        if (index == -34) {
            result->tag = LANG_VALUE_BOOL;
            result->as.boolean = found != SIZE_MAX;
            return true;
        }
        if (index == -35 || index == -36) {
            if (found != SIZE_MAX && found > (size_t)INT32_MAX) {
                runtime_error(vm, instruction,
                              "List index exceeds int range");
                return false;
            }
            result->tag = LANG_VALUE_I64;
            result->as.i64 = found == SIZE_MAX ? -1 : (int64_t)found;
            return true;
        }
        result->tag = LANG_VALUE_BOOL;
        result->as.boolean = found != SIZE_MAX;
        if (found == SIZE_MAX) return true;
        vm_value_drop_owned(vm, list->as.vector.items[found]);
        memmove(list->as.vector.items + found,
                list->as.vector.items + found + 1U,
                (list->as.vector.count - found - 1U) *
                    sizeof(*list->as.vector.items));
        --list->as.vector.count;
        list->as.vector.items[list->as.vector.count] =
            (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if ((index == -38 || index == -39) &&
        count == (index == -38 ? 2U : 3U) &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        size_t source_argument = index == -38 ? 1U : 2U;
        if (args[source_argument].tag != LANG_VALUE_OBJECT ||
            ((Object *)args[source_argument].as.object)->kind != OBJECT_VEC) {
            runtime_error(vm, instruction, "List range source is invalid");
            return false;
        }
        Object *target = args[0].as.object;
        Object *source = args[source_argument].as.object;
        if (source == target) {
            args[source_argument] = vm_value_clone(args[source_argument]);
            source = args[source_argument].as.object;
        }
        size_t at = target->as.vector.count;
        if (index == -39) {
            if (args[1].tag != LANG_VALUE_U64 ||
                args[1].as.u64 > (uint64_t)SIZE_MAX ||
                (size_t)args[1].as.u64 > target->as.vector.count) {
                runtime_error(vm, instruction,
                              "List insert index out of bounds");
                return false;
            }
            at = (size_t)args[1].as.u64;
        }
        if (source->as.vector.count >
            SIZE_MAX - target->as.vector.count) {
            runtime_error(vm, instruction, "List capacity overflow");
            return false;
        }
        size_t required =
            target->as.vector.count + source->as.vector.count;
        if (!vm_list_reserve(target, required)) {
            runtime_error(vm, instruction, "out of memory growing List");
            return false;
        }
        size_t added = source->as.vector.count;
        memmove(target->as.vector.items + at + added,
                target->as.vector.items + at,
                (target->as.vector.count - at) *
                    sizeof(*target->as.vector.items));
        if (added != 0U)
            memcpy(target->as.vector.items + at,
                   source->as.vector.items,
                   added * sizeof(*target->as.vector.items));
        target->as.vector.count = required;
        source->as.vector.count = 0U;
        return true;
    }
    if ((index == -40 || index == -41) && count == 3U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64 &&
        args[2].tag == LANG_VALUE_U64) {
        Object *source = args[0].as.object;
        uint64_t raw_at = args[1].as.u64;
        uint64_t raw_count = args[2].as.u64;
        if (raw_at > (uint64_t)SIZE_MAX ||
            raw_count > (uint64_t)SIZE_MAX ||
            (size_t)raw_at > source->as.vector.count ||
            (size_t)raw_count >
                source->as.vector.count - (size_t)raw_at) {
            runtime_error(vm, instruction, "List range out of bounds");
            return false;
        }
        size_t at = (size_t)raw_at;
        size_t range_count = (size_t)raw_count;
        if (index == -40) {
            for (size_t i = range_count; i > 0U; --i)
                vm_value_drop_owned(
                    vm, source->as.vector.items[at + i - 1U]);
            memmove(source->as.vector.items + at,
                    source->as.vector.items + at + range_count,
                    (source->as.vector.count - at - range_count) *
                        sizeof(*source->as.vector.items));
            source->as.vector.count -= range_count;
            for (size_t i = source->as.vector.count;
                 i < source->as.vector.count + range_count; ++i)
                source->as.vector.items[i] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
            return true;
        }
        Object *range = vm_allocate(1U, sizeof(*range));
        range->kind = OBJECT_VEC;
        if (!vm_list_reserve(range, range_count)) {
            free(range);
            runtime_error(vm, instruction, "out of memory creating List range");
            return false;
        }
        range->as.vector.count = range_count;
        for (size_t i = 0U; i < range_count; ++i)
            range->as.vector.items[i] =
                vm_value_clone(source->as.vector.items[at + i]);
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = range;
        return true;
    }
    if (index == -42 && (count == 1U || count == 3U) &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *list = args[0].as.object;
        size_t at = 0U;
        size_t range_count = list->as.vector.count;
        if (count == 3U) {
            if (args[1].tag != LANG_VALUE_U64 ||
                args[2].tag != LANG_VALUE_U64 ||
                args[1].as.u64 > (uint64_t)SIZE_MAX ||
                args[2].as.u64 > (uint64_t)SIZE_MAX ||
                (size_t)args[1].as.u64 > list->as.vector.count ||
                (size_t)args[2].as.u64 >
                    list->as.vector.count - (size_t)args[1].as.u64) {
                runtime_error(vm, instruction, "List range out of bounds");
                return false;
            }
            at = (size_t)args[1].as.u64;
            range_count = (size_t)args[2].as.u64;
        }
        for (size_t left = at, right = at + range_count;
             left < right && left < --right; ++left) {
            LangValue temporary = list->as.vector.items[left];
            list->as.vector.items[left] = list->as.vector.items[right];
            list->as.vector.items[right] = temporary;
        }
        return true;
    }
    if (index == -43 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64) {
        Object *list = args[0].as.object;
        if (args[1].as.u64 > (uint64_t)SIZE_MAX ||
            !vm_list_reserve(list, (size_t)args[1].as.u64)) {
            runtime_error(vm, instruction, "could not ensure List capacity");
            return false;
        }
        result->tag = LANG_VALUE_U64;
        result->as.u64 = (uint64_t)list->as.vector.capacity;
        return true;
    }
    if (index == -44 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *list = args[0].as.object;
        if (list->as.vector.count <
            vm_list_trim_threshold(list->as.vector.capacity) &&
            !vm_list_set_capacity(list, list->as.vector.count)) {
            runtime_error(vm, instruction, "could not trim List capacity");
            return false;
        }
        return true;
    }
    if (index == -45 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64) {
        Object *list = args[0].as.object;
        if (args[1].as.u64 > (uint64_t)SIZE_MAX ||
            (size_t)args[1].as.u64 < list->as.vector.count) {
            runtime_error(vm, instruction,
                          "List Capacity cannot be less than Count");
            return false;
        }
        if (!vm_list_set_capacity(list, (size_t)args[1].as.u64)) {
            runtime_error(vm, instruction, "could not set List capacity");
            return false;
        }
        return true;
    }
    if (index <= -46 && index >= -52 && count >= 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *list = args[0].as.object;
        bool indexed = index == -48 || index == -49;
        size_t callback_argument = count - 1U;
        LangValue callback = args[callback_argument];
        if (callback.tag != LANG_VALUE_FUNCTION ||
            (!indexed && count != 2U) ||
            (indexed && count != 2U && count != 3U && count != 4U)) {
            runtime_error(vm, instruction, "invalid List callback");
            return false;
        }
        if (index == -46 || index == -52) {
            bool answer = index == -52;
            for (size_t i = 0U; i < list->as.vector.count; ++i) {
                bool matched = false;
                if (!vm_list_call_callback(
                        vm, callback, list->as.vector.items[i],
                        instruction_span, true, &matched)) {
                    runtime_error(vm, instruction, "invalid List predicate");
                    return false;
                }
                if ((index == -46 && matched) ||
                    (index == -52 && !matched)) {
                    answer = index == -46;
                    break;
                }
            }
            result->tag = LANG_VALUE_BOOL;
            result->as.boolean = answer;
            return true;
        }
        if (index == -47) {
            Object *matches = vm_allocate(1U, sizeof(*matches));
            matches->kind = OBJECT_VEC;
            for (size_t i = 0U; i < list->as.vector.count; ++i) {
                bool matched = false;
                if (!vm_list_call_callback(
                        vm, callback, list->as.vector.items[i],
                        instruction_span, true, &matched)) {
                    vm_object_free(vm, matches);
                    runtime_error(vm, instruction, "invalid List predicate");
                    return false;
                }
                if (!matched) continue;
                if (!vm_list_reserve(matches, matches->as.vector.count + 1U)) {
                    vm_object_free(vm, matches);
                    runtime_error(vm, instruction,
                                  "out of memory growing List");
                    return false;
                }
                matches->as.vector.items[matches->as.vector.count++] =
                    vm_value_clone(list->as.vector.items[i]);
            }
            result->tag = LANG_VALUE_OBJECT;
            result->as.object = matches;
            return true;
        }
        if (index == -48 || index == -49) {
            bool reverse = index == -49;
            size_t start = reverse && list->as.vector.count != 0U
                         ? list->as.vector.count - 1U : 0U;
            size_t range_count = list->as.vector.count;
            if (count >= 3U) {
                if (args[1].tag != LANG_VALUE_U64 ||
                    args[1].as.u64 > (uint64_t)SIZE_MAX) {
                    runtime_error(vm, instruction, "List range out of bounds");
                    return false;
                }
                start = (size_t)args[1].as.u64;
                if (!reverse) {
                    if (start > list->as.vector.count) {
                        runtime_error(vm, instruction,
                                      "List range out of bounds");
                        return false;
                    }
                    range_count = count == 3U
                        ? list->as.vector.count - start : 0U;
                } else {
                    if (list->as.vector.count == 0U ||
                        start >= list->as.vector.count) {
                        runtime_error(vm, instruction,
                                      "List range out of bounds");
                        return false;
                    }
                    range_count = count == 3U ? start + 1U : 0U;
                }
            }
            if (count == 4U) {
                if (args[2].tag != LANG_VALUE_U64 ||
                    args[2].as.u64 > (uint64_t)SIZE_MAX) {
                    runtime_error(vm, instruction, "List range out of bounds");
                    return false;
                }
                range_count = (size_t)args[2].as.u64;
                bool valid = !reverse
                    ? range_count <= list->as.vector.count - start
                    : range_count <= start + 1U;
                if (!valid) {
                    runtime_error(vm, instruction, "List range out of bounds");
                    return false;
                }
            }
            size_t found = SIZE_MAX;
            for (size_t offset = 0U; offset < range_count; ++offset) {
                size_t at = reverse ? start - offset : start + offset;
                bool matched = false;
                if (!vm_list_call_callback(
                        vm, callback, list->as.vector.items[at],
                        instruction_span, true, &matched)) {
                    runtime_error(vm, instruction, "invalid List predicate");
                    return false;
                }
                if (matched) { found = at; break; }
            }
            if (found != SIZE_MAX && found > (size_t)INT32_MAX) {
                runtime_error(vm, instruction, "List index exceeds int range");
                return false;
            }
            result->tag = LANG_VALUE_I64;
            result->as.i64 = found == SIZE_MAX ? -1 : (int64_t)found;
            return true;
        }
        if (index == -50) {
            size_t write = 0U;
            size_t removed = 0U;
            for (size_t read = 0U; read < list->as.vector.count; ++read) {
                bool matched = false;
                if (!vm_list_call_callback(
                        vm, callback, list->as.vector.items[read],
                        instruction_span, true, &matched)) {
                    runtime_error(vm, instruction, "invalid List predicate");
                    return false;
                }
                if (matched) {
                    vm_value_drop_owned(vm, list->as.vector.items[read]);
                    ++removed;
                } else {
                    if (write != read)
                        list->as.vector.items[write] =
                            list->as.vector.items[read];
                    ++write;
                }
            }
            for (size_t i = write; i < list->as.vector.count; ++i)
                list->as.vector.items[i] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
            list->as.vector.count = write;
            if (removed > (size_t)INT32_MAX) {
                runtime_error(vm, instruction,
                              "removed count exceeds int range");
                return false;
            }
            result->tag = LANG_VALUE_I64;
            result->as.i64 = (int64_t)removed;
            return true;
        }
        for (size_t i = 0U; i < list->as.vector.count; ++i) {
            if (!vm_list_call_callback(
                    vm, callback, list->as.vector.items[i],
                    instruction_span, false, NULL)) {
                runtime_error(vm, instruction, "invalid List action");
                return false;
            }
        }
        return true;
    }
    if (index == -53 && count == 0U) {
        Object *dictionary = vm_allocate(1U, sizeof(*dictionary));
        dictionary->kind = OBJECT_DICTIONARY;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = dictionary;
        return true;
    }
    if (index == -84 && count == 3U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_DICTIONARY &&
        args[2].tag == LANG_VALUE_RAW_POINTER &&
        args[2].as.pointer != NULL) {
        Object *dictionary = args[0].as.object;
        size_t found = vm_dictionary_find(dictionary, args[1]);
        result->tag = LANG_VALUE_BOOL;
        result->as.boolean = found != SIZE_MAX;
        LangValue *output = args[2].as.pointer;
        vm_write_out_default(vm, output);
        if (found != SIZE_MAX) {
            *output = vm_value_clone(
                dictionary->as.dictionary.items[found + 1U]);
        }
        return true;
    }
    if (index <= -54 && index >= -65 && count >= 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_DICTIONARY) {
        Object *dictionary = args[0].as.object;
        if (index == -55 && count == 1U) {
            result->tag = LANG_VALUE_U64;
            result->as.u64 =
                (uint64_t)(dictionary->as.dictionary.count / 2U);
            return true;
        }
        if (index == -65 && count == 1U) {
            result->tag = LANG_VALUE_U64;
            result->as.u64 =
                (uint64_t)(dictionary->as.dictionary.capacity / 2U);
            return true;
        }
        if (index == -58 && count == 1U) {
            for (size_t i = dictionary->as.dictionary.count; i > 0U; --i) {
                vm_value_drop_owned(
                    vm, dictionary->as.dictionary.items[i - 1U]);
                dictionary->as.dictionary.items[i - 1U] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
            }
            dictionary->as.dictionary.count = 0U;
            if (!vm_dictionary_rebuild(dictionary)) {
                runtime_error(vm, instruction,
                              "could not clear Dictionary hash index");
                return false;
            }
            return true;
        }
        if (index == -63 && count == 2U &&
            args[1].tag == LANG_VALUE_U64) {
            if (args[1].as.u64 > (uint64_t)(SIZE_MAX / 2U) ||
                !vm_dictionary_reserve(
                    dictionary, (size_t)args[1].as.u64)) {
                runtime_error(vm, instruction,
                              "could not ensure Dictionary capacity");
                return false;
            }
            result->tag = LANG_VALUE_U64;
            result->as.u64 =
                (uint64_t)(dictionary->as.dictionary.capacity / 2U);
            return true;
        }
        if (index == -64 && (count == 1U || count == 2U) &&
            (count == 1U || args[1].tag == LANG_VALUE_U64)) {
            size_t logical_count = dictionary->as.dictionary.count / 2U;
            uint64_t requested = count == 1U
                ? (uint64_t)logical_count : args[1].as.u64;
            if (requested < (uint64_t)logical_count ||
                requested > (uint64_t)(SIZE_MAX / 2U)) {
                runtime_error(vm, instruction,
                              "Dictionary capacity cannot be less than Count");
                return false;
            }
            if (!vm_dictionary_set_capacity(
                    dictionary, (size_t)requested)) {
                runtime_error(vm, instruction,
                              "could not trim Dictionary capacity");
                return false;
            }
            return true;
        }
        if (count < 2U) {
            runtime_error(vm, instruction, "invalid Dictionary call");
            return false;
        }
        size_t found = SIZE_MAX;
        if (index == -62) {
            for (size_t i = 1U;
                 i < dictionary->as.dictionary.count; i += 2U)
                if (vm_list_value_equal(
                        dictionary->as.dictionary.items[i], args[1])) {
                    found = i;
                    break;
                }
        } else {
            found = vm_dictionary_find(dictionary, args[1]);
        }
        if ((index == -56 || index == -62) && count == 2U) {
            result->tag = LANG_VALUE_BOOL;
            result->as.boolean = found != SIZE_MAX;
            return true;
        }
        if (index == -57 && count == 2U) {
            result->tag = LANG_VALUE_BOOL;
            result->as.boolean = found != SIZE_MAX;
            if (found == SIZE_MAX) return true;
            vm_value_drop_owned(
                vm, dictionary->as.dictionary.items[found]);
            vm_value_drop_owned(
                vm, dictionary->as.dictionary.items[found + 1U]);
            memmove(dictionary->as.dictionary.items + found,
                    dictionary->as.dictionary.items + found + 2U,
                    (dictionary->as.dictionary.count - found - 2U) *
                        sizeof(*dictionary->as.dictionary.items));
            dictionary->as.dictionary.count -= 2U;
            dictionary->as.dictionary.items[
                dictionary->as.dictionary.count] =
                (LangValue){.tag=LANG_VALUE_UNIT};
            dictionary->as.dictionary.items[
                dictionary->as.dictionary.count + 1U] =
                (LangValue){.tag=LANG_VALUE_UNIT};
            if (!vm_dictionary_rebuild(dictionary)) {
                runtime_error(vm, instruction,
                              "could not rebuild Dictionary hash index");
                return false;
            }
            return true;
        }
        if (index == -59 && count == 2U) {
            if (found == SIZE_MAX) {
                runtime_error(vm, instruction, "Dictionary key was not found");
                return false;
            }
            *result = vm_value_clone(
                dictionary->as.dictionary.items[found + 1U]);
            return true;
        }
        if ((index == -54 || index == -60 || index == -61) && count == 3U) {
            if (found != SIZE_MAX) {
                if (index == -54) {
                    runtime_error(vm, instruction,
                                  "Dictionary already contains the key");
                    return false;
                }
                if (index == -61) {
                    result->tag = LANG_VALUE_BOOL;
                    result->as.boolean = false;
                    return true;
                }
                vm_value_drop_owned(vm,
                    dictionary->as.dictionary.items[found + 1U]);
                dictionary->as.dictionary.items[found + 1U] = args[2];
                args[2] = (LangValue){.tag=LANG_VALUE_UNIT};
                return true;
            }
            if (!vm_dictionary_reserve(
                    dictionary,
                    dictionary->as.dictionary.count / 2U + 1U)) {
                runtime_error(vm, instruction,
                              "out of memory growing Dictionary");
                return false;
            }
            size_t physical_index = dictionary->as.dictionary.count;
            dictionary->as.dictionary.items[
                dictionary->as.dictionary.count++] = args[1];
            dictionary->as.dictionary.items[
                dictionary->as.dictionary.count++] = args[2];
            if (!vm_dictionary_insert_bucket(
                    dictionary, physical_index)) {
                runtime_error(vm, instruction,
                              "could not index Dictionary key");
                return false;
            }
            args[1] = (LangValue){.tag=LANG_VALUE_UNIT};
            args[2] = (LangValue){.tag=LANG_VALUE_UNIT};
            if (index == -61) {
                result->tag = LANG_VALUE_BOOL;
                result->as.boolean = true;
            }
            return true;
        }
    }
    if (index == -66 && count == 0U) {
        Object *queue = vm_allocate(1U, sizeof(*queue));
        queue->kind = OBJECT_QUEUE;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = queue;
        return true;
    }
    if (index == -75 && count == 0U) {
        Object *stack = vm_allocate(1U, sizeof(*stack));
        stack->kind = OBJECT_VEC;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = stack;
        return true;
    }
    if ((index == -87 || index == -88) && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_RAW_POINTER &&
        args[1].as.pointer != NULL) {
        Object *stack = args[0].as.object;
        result->tag = LANG_VALUE_BOOL;
        result->as.boolean = stack->as.vector.count != 0U;
        LangValue *output = args[1].as.pointer;
        vm_write_out_default(vm, output);
        if (stack->as.vector.count != 0U) {
            size_t top = stack->as.vector.count - 1U;
            *output = index == -87
                ? stack->as.vector.items[top]
                : vm_value_clone(stack->as.vector.items[top]);
            if (index == -87) {
                stack->as.vector.items[top] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
                --stack->as.vector.count;
            }
        }
        return true;
    }
    if (index >= -83 && index <= -76 && count >= 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *stack = args[0].as.object;
        if (index == -76 && count == 2U) {
            if (!vm_list_reserve(stack, stack->as.vector.count + 1U)) {
                runtime_error(vm, instruction, "out of memory growing Stack");
                return false;
            }
            stack->as.vector.items[stack->as.vector.count++] = args[1];
            args[1] = (LangValue){.tag=LANG_VALUE_UNIT};
            return true;
        }
        if ((index == -77 || index == -78) && count == 1U) {
            if (stack->as.vector.count == 0U) {
                runtime_error(vm, instruction, "Stack is empty");
                return false;
            }
            size_t top = stack->as.vector.count - 1U;
            *result = index == -77
                ? stack->as.vector.items[top]
                : vm_value_clone(stack->as.vector.items[top]);
            if (index == -77) {
                stack->as.vector.items[top] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
                --stack->as.vector.count;
            }
            return true;
        }
        if (index == -79 && count == 1U) {
            result->tag = LANG_VALUE_U64;
            result->as.u64 = (uint64_t)stack->as.vector.count;
            return true;
        }
        if (index == -80 && count == 1U) {
            for (size_t i = stack->as.vector.count; i > 0U; --i) {
                vm_value_drop_owned(vm, stack->as.vector.items[i - 1U]);
                stack->as.vector.items[i - 1U] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
            }
            stack->as.vector.count = 0U;
            return true;
        }
        if (index == -81 && count == 2U &&
            args[1].tag == LANG_VALUE_U64) {
            if (args[1].as.u64 > (uint64_t)SIZE_MAX ||
                !vm_list_reserve(stack, (size_t)args[1].as.u64)) {
                runtime_error(vm, instruction,
                              "could not ensure Stack capacity");
                return false;
            }
            result->tag = LANG_VALUE_U64;
            result->as.u64 = (uint64_t)stack->as.vector.capacity;
            return true;
        }
        if (index == -82 && (count == 1U || count == 2U)) {
            size_t capacity = stack->as.vector.count;
            bool resize = stack->as.vector.count <
                vm_list_trim_threshold(stack->as.vector.capacity);
            if (count == 2U) {
                if (args[1].tag != LANG_VALUE_U64 ||
                    args[1].as.u64 > (uint64_t)SIZE_MAX ||
                    (size_t)args[1].as.u64 < stack->as.vector.count) {
                    runtime_error(vm, instruction,
                                  "Stack capacity cannot be less than Count");
                    return false;
                }
                capacity = (size_t)args[1].as.u64;
                resize = capacity != stack->as.vector.capacity;
            }
            if (resize && !vm_list_set_capacity(stack, capacity)) {
                runtime_error(vm, instruction,
                              "could not trim Stack capacity");
                return false;
            }
            return true;
        }
        if (index == -83 && count == 1U) {
            result->tag = LANG_VALUE_U64;
            result->as.u64 = (uint64_t)stack->as.vector.capacity;
            return true;
        }
    }
    if (index >= -74 && index <= -67 && count >= 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_QUEUE) {
        Object *queue = args[0].as.object;
        if (index == -67 && count == 2U) {
            if (!vm_queue_reserve(queue, queue->as.queue.count + 1U)) {
                runtime_error(vm, instruction, "out of memory growing Queue");
                return false;
            }
            size_t tail = (queue->as.queue.head + queue->as.queue.count) %
                queue->as.queue.capacity;
            queue->as.queue.items[tail] = args[1];
            ++queue->as.queue.count;
            args[1] = (LangValue){.tag=LANG_VALUE_UNIT};
            return true;
        }
        if ((index == -68 || index == -69) && count == 1U) {
            if (queue->as.queue.count == 0U) {
                runtime_error(vm, instruction, "Queue is empty");
                return false;
            }
            size_t head = queue->as.queue.head;
            *result = index == -68
                ? queue->as.queue.items[head]
                : vm_value_clone(queue->as.queue.items[head]);
            if (index == -68) {
                queue->as.queue.items[head] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
                --queue->as.queue.count;
                queue->as.queue.head = queue->as.queue.count == 0U
                    ? 0U : (head + 1U) % queue->as.queue.capacity;
            }
            return true;
        }
        if (index == -70 && count == 1U) {
            result->tag = LANG_VALUE_U64;
            result->as.u64 = (uint64_t)queue->as.queue.count;
            return true;
        }
        if (index == -71 && count == 1U) {
            for (size_t i = queue->as.queue.count; i > 0U; --i) {
                size_t at = (queue->as.queue.head + i - 1U) %
                    queue->as.queue.capacity;
                vm_value_drop_owned(vm, queue->as.queue.items[at]);
                queue->as.queue.items[at] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
            }
            queue->as.queue.count = 0U;
            queue->as.queue.head = 0U;
            return true;
        }
        if (index == -72 && count == 2U &&
            args[1].tag == LANG_VALUE_U64) {
            if (args[1].as.u64 > (uint64_t)SIZE_MAX ||
                !vm_queue_reserve(queue, (size_t)args[1].as.u64)) {
                runtime_error(vm, instruction,
                              "could not ensure Queue capacity");
                return false;
            }
            result->tag = LANG_VALUE_U64;
            result->as.u64 = (uint64_t)queue->as.queue.capacity;
            return true;
        }
        if (index == -73 && count == 1U) {
            if (queue->as.queue.count <
                    vm_list_trim_threshold(queue->as.queue.capacity) &&
                !vm_queue_resize(queue, queue->as.queue.count)) {
                runtime_error(vm, instruction,
                              "could not trim Queue capacity");
                return false;
            }
            return true;
        }
        if (index == -74 && count == 1U) {
            result->tag = LANG_VALUE_U64;
            result->as.u64 = (uint64_t)queue->as.queue.capacity;
            return true;
        }
    }
    if ((index == -85 || index == -86) && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_QUEUE &&
        args[1].tag == LANG_VALUE_RAW_POINTER &&
        args[1].as.pointer != NULL) {
        Object *queue = args[0].as.object;
        result->tag = LANG_VALUE_BOOL;
        result->as.boolean = queue->as.queue.count != 0U;
        LangValue *output = args[1].as.pointer;
        vm_write_out_default(vm, output);
        if (queue->as.queue.count != 0U) {
            size_t head = queue->as.queue.head;
            *output = index == -85
                ? queue->as.queue.items[head]
                : vm_value_clone(queue->as.queue.items[head]);
            if (index == -85) {
                queue->as.queue.items[head] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
                --queue->as.queue.count;
                queue->as.queue.head = queue->as.queue.count == 0U
                    ? 0U : (head + 1U) % queue->as.queue.capacity;
            }
        }
        return true;
    }
    if (index == -24) {
        char *data = NULL;
        size_t length = 0U;
        size_t capacity = 0U;
        for (size_t i = 0U; i < count; ++i) {
            char formatted[64];
            LangStringView segment;
            int formatted_length = 0;
            if (!lang_value_string_view(&args[i], &segment)) {
                if (args[i].tag == LANG_VALUE_I64)
                    formatted_length = snprintf(
                        formatted, sizeof(formatted),
                        "%" PRId64, args[i].as.i64);
                else if (args[i].tag == LANG_VALUE_U64)
                    formatted_length = snprintf(
                        formatted, sizeof(formatted),
                        "%" PRIu64, args[i].as.u64);
                else if (args[i].tag == LANG_VALUE_F64)
                    formatted_length = snprintf(
                        formatted, sizeof(formatted),
                        "%g", args[i].as.f64);
                else if (args[i].tag == LANG_VALUE_BOOL)
                    formatted_length = snprintf(
                        formatted, sizeof(formatted), "%s",
                        args[i].as.boolean
                            ? "true" : "false");
                else
                    formatted_length = -1;
                if (formatted_length < 0 ||
                    (size_t)formatted_length >=
                        sizeof(formatted)) {
                    free(data);
                    runtime_error(
                        vm, instruction,
                        "unsupported interpolation value");
                    return false;
                }
                segment = (LangStringView){
                    formatted,
                    (size_t)formatted_length
                };
            }
            if (segment.length >
                SIZE_MAX - length - 1U) {
                free(data);
                runtime_error(
                    vm, instruction,
                    "interpolated String is too large");
                return false;
            }
            size_t required =
                length + segment.length + 1U;
            if (required > capacity) {
                size_t next =
                    capacity == 0U ? 32U : capacity;
                while (next < required) {
                    if (next > SIZE_MAX / 2U) {
                        next = required;
                        break;
                    }
                    next *= 2U;
                }
                char *grown = realloc(data, next);
                if (grown == NULL) {
                    free(data);
                    runtime_error(
                        vm, instruction,
                        "out of memory growing interpolated String");
                    return false;
                }
                data = grown;
                capacity = next;
            }
            if (segment.length != 0U)
                memcpy(
                    data + length, segment.data,
                    segment.length);
            length += segment.length;
        }
        if (data == NULL)
            data = vm_allocate(1U, 1U);
        data[length] = '\0';
        Object *string = vm_allocate(1U, sizeof(*string));
        string->kind = OBJECT_STRING;
        string->references = 1U;
        string->as.string.data = data;
        string->as.string.length = length;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = string;
        return true;
    }
    if (index == -20 && count == 1U) {
        LangStringView source;
        if (!lang_value_string_view(&args[0], &source)) {
            runtime_error(vm, instruction,
                          "Html::unsafe_raw expects a string view");
            return false;
        }
        Object *html = vm_allocate(1U, sizeof(*html));
        html->kind = OBJECT_HTML;
        html->as.html.capacity = source.length;
        html->as.html.data = vm_allocate(html->as.html.capacity, 1U);
        if (source.length != 0U)
            memcpy(html->as.html.data, source.data, source.length);
        html->as.html.length = source.length;
        html->as.html.tag = "";
        html->as.html.tag_length = 0U;
        html->as.html.open = false;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = html;
        return true;
    }
    if (index == -21 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_BUFFER) {
        Object *buffer = args[0].as.object;
        result->tag = LANG_VALUE_BYTE_SLICE;
        result->as.bytes.data = buffer->as.buffer.data;
        result->as.bytes.length = buffer->as.buffer.length;
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -22 && count == 1U) {
        LangStringView text;
        if (!lang_value_string_view(&args[0], &text)) {
            runtime_error(vm, instruction,
                          "text_len expects a string value");
            return false;
        }
        result->tag = LANG_VALUE_U64;
        result->as.u64 = (uint64_t)text.length;
        return true;
    }
    runtime_error(vm, instruction, "invalid native function call");
    return false;
}
