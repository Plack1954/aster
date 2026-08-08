#include "internal.h"
#include "vm_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool vm_value_is_string_builder(const LangValue *value) {
    return value != NULL && value->tag == LANG_VALUE_OBJECT &&
           value->as.object != NULL &&
           ((Object *)value->as.object)->kind == OBJECT_STRING_BUILDER;
}

int64_t *vm_native_drop_log(LangVM *vm) {
    return vm != NULL ? &vm->native_drop_log : NULL;
}

size_t vm_process_argument_count(const LangVM *vm) {
    return vm != NULL ? vm->process_argument_count : 0U;
}

const char *vm_process_argument(const LangVM *vm, size_t index) {
    if (vm == NULL || index >= vm->process_argument_count)
        return NULL;
    return vm->process_arguments[index];
}

void *vm_allocate(size_t count, size_t size) {
    if (count != 0U && size > SIZE_MAX / count) return NULL;
    void *result = calloc(count, size);
    if (result == NULL && count != 0U) {
        fputs("runtime internal error: out of memory\n", stderr);
        exit(2);
    }
    return result;
}

void *vm_html_resize(void *pointer, size_t count, size_t size) {
    if (count != 0U && size > SIZE_MAX / count) return NULL;
    void *result = realloc(pointer, count * size);
    if (result == NULL && count != 0U) {
        fputs("runtime internal error: out of memory\n", stderr);
        exit(2);
    }
    return result;
}

void *vm_allocate_uninitialized(size_t count, size_t size) {
    if (count != 0U && size > SIZE_MAX / count)
        return NULL;
    void *result = malloc(count * size);
    if (result == NULL && count != 0U) {
        fputs("runtime internal error: out of memory\n", stderr);
        exit(2);
    }
    return result;
}

static char *copy_string(const char *text) {
    size_t length = strlen(text);
    char *result = vm_allocate(length + 1U, 1U);
    memcpy(result, text, length + 1U);
    return result;
}

static uint64_t native_name_hash(const char *name) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (const unsigned char *byte = (const unsigned char *)name;
         *byte != 0U; ++byte) {
        hash ^= (uint64_t)*byte;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

LangVM *lang_vm_new(void) {
    /* Construction is the recoverable embedding boundary. Internal runtime
     * allocations intentionally retain their documented fail-fast policy. */
    return calloc(1U, sizeof(LangVM));
}

static size_t string_literal_hash(const char *data, size_t length) {
    uintptr_t pointer = (uintptr_t)(const void *)data;
    pointer ^= pointer >> 17U;
    pointer *= (uintptr_t)UINT64_C(0xed5ad4bb);
    pointer ^= pointer >> 11U;
    return (size_t)(pointer ^ (uintptr_t)length);
}

void vm_clear_string_literals(LangVM *vm) {
    if (vm == NULL) return;
    for (size_t i = 0U; i < vm->string_literal_capacity; ++i)
        if (vm->string_literals[i].occupied)
            free(vm->string_literals[i].object);
    free(vm->string_literals);
    vm->string_literals = NULL;
    vm->string_literal_capacity = 0U;
}

void vm_prepare_string_literals(LangVM *vm, const BytecodeModule *module) {
    vm_clear_string_literals(vm);
    if (vm == NULL || module == NULL) return;
    size_t count = 0U;
    for (size_t i = 0U; i < module->constant_count; ++i)
        if (module->constants[i].value.tag == LANG_VALUE_STRING_VIEW)
            ++count;
    if (count == 0U) return;
    size_t capacity = 8U;
    while (capacity < count * 2U) capacity *= 2U;
    vm->string_literals = vm_allocate(
        capacity, sizeof(*vm->string_literals));
    vm->string_literal_capacity = capacity;
    size_t mask = capacity - 1U;
    for (size_t i = 0U; i < module->constant_count; ++i) {
        LangValue value = module->constants[i].value;
        if (value.tag != LANG_VALUE_STRING_VIEW) continue;
        size_t bucket = string_literal_hash(
            value.as.string.data, value.as.string.length) & mask;
        while (vm->string_literals[bucket].occupied) {
            VmStringLiteral *entry = &vm->string_literals[bucket];
            if (entry->data == value.as.string.data &&
                entry->length == value.as.string.length)
                break;
            bucket = (bucket + 1U) & mask;
        }
        VmStringLiteral *entry = &vm->string_literals[bucket];
        entry->data = value.as.string.data;
        entry->length = value.as.string.length;
        entry->occupied = true;
    }
}

bool vm_owned_string_from_view(LangVM *vm, LangStringView source,
                               LangValue *result) {
    if (vm == NULL || result == NULL ||
        vm->string_literal_capacity == 0U)
        return false;
    size_t mask = vm->string_literal_capacity - 1U;
    size_t bucket = string_literal_hash(source.data, source.length) & mask;
    while (vm->string_literals[bucket].occupied) {
        VmStringLiteral *entry = &vm->string_literals[bucket];
        if (entry->data == source.data && entry->length == source.length) {
            if (entry->object == NULL) {
                entry->object = vm_allocate(1U, sizeof(*entry->object));
                entry->object->kind = OBJECT_STRING;
                entry->object->references = SIZE_MAX;
                entry->object->as.string.data = (char *)source.data;
                entry->object->as.string.length = source.length;
                entry->object->as.string.embedded_data = true;
            }
            *result = (LangValue){
                .tag=LANG_VALUE_OBJECT, .as.object=entry->object};
            return true;
        }
        bucket = (bucket + 1U) & mask;
    }
    return false;
}

void lang_vm_set_process_arguments(
    LangVM *vm, size_t argument_count, const char *const *arguments) {
    if (vm == NULL) return;
    vm->process_argument_count = argument_count;
    vm->process_arguments = arguments;
}

void lang_vm_free(LangVM *vm) {
    if (vm == NULL) return;
    if (vm->exception_pending)
        vm_value_drop_owned(vm, vm->exception_value);
    while (vm->timers != NULL) {
        VmTimer *next = vm->timers->next;
        vm_task_release_runtime(vm, vm->timers->task);
        free(vm->timers);
        vm->timers = next;
    }
    for (size_t i = 0U; i < vm->native_count; ++i) free(vm->natives[i].name);
    for (size_t i = 0U; i < vm->raw_allocation_count; ++i) {
        if (vm->raw_allocations[i]->active)
            free(vm->raw_allocations[i]->data);
        free(vm->raw_allocations[i]);
    }
    free(vm->raw_allocations);
    free(vm->natives);
    free(vm->frame_initialized);
    free(vm->frame_references);
    free(vm->frame_locals);
    free(vm->frame_stacks);
    free(vm->frame_html_objects);
    free(vm->static_fields);
    vm_clear_string_literals(vm);
    free(vm);
}

bool lang_register_native(LangVM *vm, const char *name, LangNativeFn callback,
                          size_t arity) {
    if (vm == NULL || name == NULL || callback == NULL) return false;
    uint64_t hash = native_name_hash(name);
    for (size_t i = 0U; i < vm->native_count; ++i)
        if (vm->natives[i].name_hash == hash &&
            strcmp(vm->natives[i].name, name) == 0)
            return false;
    if (vm->native_count == vm->native_capacity) {
        size_t next = vm->native_capacity == 0U ? 8U : vm->native_capacity * 2U;
        NativeEntry *entries = realloc(vm->natives, next * sizeof(*entries));
        if (entries == NULL) return false;
        vm->natives = entries;
        vm->native_capacity = next;
    }
    vm->natives[vm->native_count++] =
        (NativeEntry){copy_string(name), hash, callback, arity};
    return true;
}

LangNativeResult lang_native_result_error(const char *message) {
    LangNativeResult result = {
        .ok=false,
        .value={.tag=LANG_VALUE_UNIT},
        .error=NULL
    };
    const char *source = message != NULL ? message : "native function failed";
    size_t length = strlen(source);
    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        result.error = "could not allocate native failure diagnostic";
        return result;
    }
    memcpy(copy, source, length + 1U);
    result.value.tag = LANG_VALUE_NATIVE_ERROR;
    result.value.as.pointer = copy;
    return result;
}

const char *lang_native_result_error_message(const LangNativeResult *result) {
    if (result == NULL) return NULL;
    if (!result->ok && result->error == NULL &&
        result->value.tag == LANG_VALUE_NATIVE_ERROR)
        return result->value.as.pointer;
    return result->error;
}

void lang_native_result_drop(LangNativeResult *result) {
    if (result == NULL || result->ok) return;
    if (result->error == NULL &&
        result->value.tag == LANG_VALUE_NATIVE_ERROR)
        free(result->value.as.pointer);
    *result = (LangNativeResult){
        .ok=false, .value={.tag=LANG_VALUE_UNIT}, .error=NULL
    };
}

bool lang_vm_call_native(LangVM *vm, const char *name, const LangValue *args,
                         size_t arg_count, LangNativeResult *out_result) {
    if (vm == NULL || name == NULL || out_result == NULL) return false;
    uint64_t hash = native_name_hash(name);
    for (size_t i = 0U; i < vm->native_count; ++i) {
        NativeEntry *entry = &vm->natives[i];
        if (entry->name_hash != hash ||
            strcmp(entry->name, name) != 0)
            continue;
        if (entry->arity != arg_count) {
            *out_result = lang_native_result_error("native arity mismatch");
            return true;
        }
        *out_result = entry->callback(vm, args, arg_count);
        return true;
    }
    return false;
}

bool lang_vm_call_function(LangVM *vm, const LangValue *function,
                           const LangValue *args, size_t arg_count,
                           LangNativeResult *out_result) {
    if (vm == NULL || function == NULL || out_result == NULL ||
        (arg_count != 0U && args == NULL))
        return false;
    if (function->tag == LANG_VALUE_NATIVE_FUNCTION) {
        if (function->as.native_function.invoke == NULL) return false;
        return function->as.native_function.invoke(
            function->as.native_function.context,
            args, arg_count, out_result);
    }
    if (function->tag != LANG_VALUE_FUNCTION &&
        function->tag != LANG_VALUE_BOUND_FUNCTION)
        return false;
    if (vm->module == NULL) return false;
    vm->trapped = false;
    LangValue result = vm_invoke_function_value(
        vm, *function, args, arg_count, (LangSpan){0});
    if (vm->trapped) {
        *out_result = lang_native_result_error(
            "Aster callback execution failed");
        return true;
    }
    *out_result = (LangNativeResult){
        .ok=true, .value=result, .error=NULL
    };
    return true;
}

bool lang_native_handle_value(LangVM *vm, void *handle,
                              LangNativeHandleDropFn destructor,
                              LangValue *out_value) {
    (void)vm;
    if (handle == NULL || destructor == NULL || out_value == NULL) return false;
    Object *object = vm_allocate(1U, sizeof(*object));
    object->kind = OBJECT_NATIVE_HANDLE;
    object->references = 1U;
    object->as.native_handle.data = handle;
    object->as.native_handle.destructor = destructor;
    *out_value = (LangValue){.tag=LANG_VALUE_OBJECT, .as.object=object};
    return true;
}

void *lang_native_handle_data(const LangValue *value) {
    if (value == NULL || value->tag != LANG_VALUE_OBJECT ||
        value->as.object == NULL ||
        ((Object *)value->as.object)->kind != OBJECT_NATIVE_HANDLE)
        return NULL;
    return ((Object *)value->as.object)->as.native_handle.data;
}

bool lang_value_string_view(const LangValue *value, LangStringView *out_view) {
    if (value == NULL || out_view == NULL) return false;
    if (value->tag == LANG_VALUE_STRING_VIEW) {
        if (value->as.string.data == NULL &&
            value->as.string.length != 0U)
            return false;
        *out_view = value->as.string;
        return true;
    }
    if (value->tag == LANG_VALUE_OBJECT && value->as.object != NULL &&
        ((Object *)value->as.object)->kind == OBJECT_STRING) {
        Object *string = value->as.object;
        *out_view = (LangStringView){
            string->as.string.data, string->as.string.length
        };
        return true;
    }
    return false;
}

bool lang_value_html_view(const LangValue *value, LangStringView *out_view) {
    if (value == NULL || out_view == NULL) return false;
    /* Generated C passes the already typed Html storage as a call-scoped
     * view; the VM passes its owning Html object directly. */
    if (value->tag == LANG_VALUE_STRING_VIEW) {
        if (value->as.string.data == NULL &&
            value->as.string.length != 0U)
            return false;
        *out_view = value->as.string;
        return true;
    }
    if (value->tag == LANG_VALUE_OBJECT && value->as.object != NULL &&
        ((Object *)value->as.object)->kind == OBJECT_HTML) {
        Object *html = value->as.object;
        if (html->as.html.destination != NULL || html->as.html.open)
            return false;
        *out_view = (LangStringView){
            html->as.html.data, html->as.html.length
        };
        return true;
    }
    return false;
}

bool lang_string_value(LangVM *vm, LangStringView source,
                       LangValue *out_value) {
    (void)vm;
    if (out_value == NULL ||
        (source.data == NULL && source.length != 0U) ||
        source.length == SIZE_MAX)
        return false;
    Object *string = vm_allocate(1U, sizeof(*string));
    string->kind = OBJECT_STRING;
    string->references = 1U;
    string->as.string.data = vm_allocate(source.length + 1U, 1U);
    if (source.length != 0U)
        memcpy(string->as.string.data, source.data, source.length);
    string->as.string.data[source.length] = '\0';
    string->as.string.length = source.length;
    *out_value = (LangValue){
        .tag=LANG_VALUE_OBJECT, .as.object=string
    };
    return true;
}

void vm_raise_exception_typed(LangVM *vm, const char *type,
                              const char *message) {
    const char *text = message != NULL ? message : "native function failed";
    const char *exception_type = type != NULL ? type : "Exception";
    if (vm->exception_pending)
        vm_value_drop_owned(vm, vm->exception_value);
    LangValue string = {.tag=LANG_VALUE_UNIT};
    (void)lang_string_value(
        vm, (LangStringView){text, strlen(text)}, &string);
    Object *exception = vm_allocate(1U, sizeof(*exception));
    exception->kind = OBJECT_STRUCT;
    size_t type_length = strlen(exception_type);
    exception->as.structure.metadata =
        vm_allocate(type_length + strlen("|Message") + 1U, 1U);
    memcpy(exception->as.structure.metadata, exception_type, type_length);
    memcpy(exception->as.structure.metadata + type_length, "|Message",
           strlen("|Message") + 1U);
    exception->as.structure.fields =
        vm_allocate(1U, sizeof(*exception->as.structure.fields));
    exception->as.structure.fields[0] = string;
    exception->as.structure.count = 1U;
    vm->exception_value = (LangValue){
        .tag=LANG_VALUE_OBJECT, .as.object=exception
    };
    vm->exception_pending = true;
}

void vm_raise_exception_message(LangVM *vm, const char *message) {
    vm_raise_exception_typed(vm, "Exception", message);
}

bool lang_value_byte_slice(const LangValue *value,
                           LangByteSlice *out_slice) {
    if (value == NULL || out_slice == NULL ||
        value->tag != LANG_VALUE_BYTE_SLICE ||
        (value->as.bytes.data == NULL && value->as.bytes.length != 0U))
        return false;
    *out_slice = value->as.bytes;
    return true;
}

static bool result_value(LangVM *vm, const char *variant,
                         LangValue payload, LangValue *out_value) {
    (void)vm;
    if (out_value == NULL) return false;
    Object *object = vm_allocate(1U, sizeof(*object));
    object->kind = OBJECT_STRUCT;
    object->as.structure.metadata = copy_string(variant);
    object->as.structure.count = 1U;
    object->as.structure.fields =
        vm_allocate(1U, sizeof(*object->as.structure.fields));
    object->as.structure.fields[0] = payload;
    *out_value = (LangValue){
        .tag=LANG_VALUE_OBJECT, .as.object=object
    };
    return true;
}

bool lang_result_ok_value(LangVM *vm, LangValue payload,
                          LangValue *out_value) {
    return result_value(vm, "Result::Ok", payload, out_value);
}

bool lang_result_err_value(LangVM *vm, LangValue payload,
                           LangValue *out_value) {
    return result_value(vm, "Result::Err", payload, out_value);
}

void vm_object_free(LangVM *vm, Object *object);

bool lang_result_take(LangVM *vm, LangValue *result, bool *out_is_ok,
                      LangValue *out_payload) {
    if (result == NULL || out_is_ok == NULL || out_payload == NULL ||
        result->tag != LANG_VALUE_OBJECT ||
        result->as.object == NULL)
        return false;
    Object *object = result->as.object;
    if (object->kind != OBJECT_STRUCT ||
        object->as.structure.count != 1U ||
        object->as.structure.metadata == NULL)
        return false;
    bool is_ok =
        strcmp(object->as.structure.metadata, "Result::Ok") == 0;
    if (!is_ok &&
        strcmp(object->as.structure.metadata, "Result::Err") != 0)
        return false;
    *out_is_ok = is_ok;
    *out_payload = object->as.structure.fields[0];
    object->as.structure.fields[0] =
        (LangValue){.tag=LANG_VALUE_UNIT};
    vm_object_free(vm, object);
    *result = (LangValue){.tag=LANG_VALUE_UNIT};
    return true;
}

void vm_value_drop_owned(LangVM *vm, LangValue value) {
    if (value.tag != LANG_VALUE_OBJECT || value.as.object == NULL) return;
    Object *object = value.as.object;
    if (object->language_destructor != 0U && vm != NULL &&
        vm->module != NULL) {
        size_t destructor = (size_t)(object->language_destructor - 1U);
        object->language_destructor = 0U;
        bool was_unwinding = vm->trapped;
        bool saved_exception_pending = vm->exception_pending;
        LangValue saved_exception = vm->exception_value;
        vm->exception_pending = false;
        vm->exception_value = (LangValue){.tag=LANG_VALUE_UNIT};
        vm->trapped = false;
        LangValue result = vm_execute_function(
            vm, destructor, &value, 1U, vm->active_span);
        vm_value_drop_owned(vm, result);
        bool cleanup_failed = vm->trapped || vm->exception_pending;
        if (vm->exception_pending) {
            LangValue destructor_exception = vm->exception_value;
            vm->exception_pending = false;
            vm->exception_value = (LangValue){.tag=LANG_VALUE_UNIT};
            vm_value_drop_owned(vm, destructor_exception);
            fputs("fatal: destructor attempted to throw\n", stderr);
        }
        vm->exception_pending = saved_exception_pending;
        vm->exception_value = saved_exception;
        if (cleanup_failed && was_unwinding)
            fputs("runtime internal error: destructor trapped during unwinding\n",
                  stderr);
        vm->trapped = was_unwinding || cleanup_failed;
        return;
    }
    vm_object_free(vm, object);
}

void lang_value_drop(LangVM *vm, LangValue *value) {
    (void)vm;
    if (value == NULL) return;
    vm_value_drop_owned(vm, *value);
    *value = (LangValue){.tag=LANG_VALUE_UNIT};
}

bool lang_value_copy(LangVM *vm, const LangValue *source,
                     LangValue *out_value) {
    (void)vm;
    if (source == NULL || out_value == NULL) return false;
    *out_value = vm_value_clone(*source);
    return true;
}

void vm_object_free(LangVM *vm, Object *object) {
    if (object == NULL) return;
    if ((object->kind == OBJECT_STRING ||
         object->kind == OBJECT_NATIVE_HANDLE ||
         object->kind == OBJECT_CANCELLATION ||
         object->kind == OBJECT_TASK) &&
        object->references == SIZE_MAX)
        return;
    if ((object->kind == OBJECT_STRING ||
         object->kind == OBJECT_NATIVE_HANDLE ||
         object->kind == OBJECT_CANCELLATION ||
         object->kind == OBJECT_TASK) &&
        object->references > 1U) {
        --object->references;
        return;
    }
    bool free_container = true;
    switch (object->kind) {
        case OBJECT_ARRAY:
            for (size_t i = object->as.array.count; i > 0U; --i)
                vm_value_drop_owned(vm, object->as.array.items[i - 1U]);
            free(object->as.array.items);
            break;
        case OBJECT_STRUCT:
            for (size_t i = object->as.structure.count; i > 0U; --i)
                vm_value_drop_owned(vm, object->as.structure.fields[i - 1U]);
            free(object->as.structure.fields);
            free(object->as.structure.metadata);
            break;
        case OBJECT_HTML:
            if (object->as.html.destination == NULL) {
                free(object->as.html.data);
                if (object->as.html.styles != NULL) {
                    free(object->as.html.styles->ids);
                    free(object->as.html.styles->occurrences);
                    free(object->as.html.styles);
                }
            }
            else if (object->as.html.embedded)
                free_container = false;
            break;
        case OBJECT_STRING:
            if (!object->as.string.embedded_data)
                free(object->as.string.data);
            break;
        case OBJECT_STRING_BUILDER:
            if (!object->as.string_builder.embedded_data)
                free(object->as.string_builder.data);
            break;
        case OBJECT_VEC:
            for (size_t i = object->as.vector.count; i > 0U; --i)
                vm_value_drop_owned(vm, object->as.vector.items[i - 1U]);
            free(object->as.vector.items);
            break;
        case OBJECT_DICTIONARY:
            for (size_t i = object->as.dictionary.count; i > 0U; --i)
                vm_value_drop_owned(
                    vm, object->as.dictionary.items[i - 1U]);
            free(object->as.dictionary.items);
            free(object->as.dictionary.buckets);
            break;
        case OBJECT_QUEUE:
            for (size_t i = object->as.queue.count; i > 0U; --i)
                vm_value_drop_owned(
                    vm, object->as.queue.items[
                        (object->as.queue.head + i - 1U) %
                        object->as.queue.capacity]);
            free(object->as.queue.items);
            break;
        case OBJECT_BUFFER: free(object->as.buffer.data); break;
        case OBJECT_ARENA:
            for (size_t i = object->as.arena.count; i > 0U; --i) {
                RawAllocation *allocation = object->as.arena.blocks[i - 1U];
                if (allocation->active) free(allocation->data);
                allocation->data = NULL;
                allocation->active = false;
            }
            free(object->as.arena.blocks);
            break;
        case OBJECT_NATIVE_HANDLE:
            if (object->as.native_handle.destructor != NULL)
                object->as.native_handle.destructor(object->as.native_handle.data);
            break;
        case OBJECT_CANCELLATION:
            break;
        case OBJECT_ITER:
            if (!object->as.iterator.borrowed)
                vm_object_free(vm, object->as.iterator.array);
            break;
        case OBJECT_TASK:
            vm_task_destroy(vm, object);
            return;
    }
    if (free_container) free(object);
}

static uint32_t vm_custom_copy_for_metadata(
    const LangVM *vm, const char *metadata) {
    if (vm == NULL || vm->module == NULL || metadata == NULL) return 0U;
    const char *type_name = metadata;
    const char *module_separator = strchr(metadata, '#');
    size_t module_length = 0U;
    if (module_separator != NULL) {
        module_length = (size_t)(module_separator - metadata);
        type_name = module_separator + 1;
    }
    size_t type_length = strlen(type_name);
    const char *field_separator = strchr(type_name, '|');
    const char *variant_separator = strstr(type_name, "::");
    if (field_separator != NULL)
        type_length = (size_t)(field_separator - type_name);
    if (variant_separator != NULL &&
        (size_t)(variant_separator - type_name) < type_length)
        type_length = (size_t)(variant_separator - type_name);
    for (size_t i = 0U; i < vm->module->custom_copy_count; ++i) {
        const BytecodeCustomCopy *entry = &vm->module->custom_copies[i];
        if (entry->runtime_module_length == module_length &&
            entry->runtime_type_length == type_length &&
            (module_length == 0U ||
             memcmp(entry->runtime_module, metadata, module_length) == 0) &&
            memcmp(entry->runtime_type, type_name, type_length) == 0) {
            if (entry->copy_function >= UINT32_MAX) return 0U;
            return (uint32_t)entry->copy_function + 1U;
        }
    }
    return 0U;
}

static bool clone_value(
    LangVM *vm, LangValue value, bool semantic, LangSpan span,
    LangValue *out_value);

static Object *object_clone(
    LangVM *vm, const Object *source, bool semantic, LangSpan span,
    bool *ok) {
    Object *copy = vm_allocate(1U, sizeof(*copy));
    memset(copy, 0, sizeof(*copy));
    copy->kind = source->kind;
    copy->language_destructor = source->language_destructor;
    switch (source->kind) {
        case OBJECT_ARRAY:
            copy->as.array.items = vm_allocate(
                source->as.array.count, sizeof(LangValue));
            for (size_t i = 0U; i < source->as.array.count; ++i) {
                if (!clone_value(vm, source->as.array.items[i], semantic,
                                 span, &copy->as.array.items[i]))
                    goto fail;
                ++copy->as.array.count;
            }
            break;
        case OBJECT_STRUCT:
            copy->as.structure.metadata = copy_string(source->as.structure.metadata);
            copy->as.structure.fields = vm_allocate(
                source->as.structure.count, sizeof(LangValue));
            for (size_t i = 0U; i < source->as.structure.count; ++i) {
                if (!clone_value(vm, source->as.structure.fields[i],
                                 semantic, span,
                                 &copy->as.structure.fields[i]))
                    goto fail;
                ++copy->as.structure.count;
            }
            break;
        case OBJECT_HTML:
            while (source->as.html.destination != NULL)
                source = source->as.html.destination;
            copy->as.html.capacity = source->as.html.length;
            copy->as.html.data = vm_allocate(copy->as.html.capacity, 1U);
            if (source->as.html.length != 0U)
                memcpy(copy->as.html.data, source->as.html.data,
                       source->as.html.length);
            copy->as.html.length = source->as.html.length;
            copy->as.html.tag = source->as.html.tag;
            copy->as.html.tag_length = source->as.html.tag_length;
            copy->as.html.open = source->as.html.open;
            if (source->as.html.styles != NULL) {
                copy->as.html.styles = vm_allocate(
                    1U, sizeof(*copy->as.html.styles));
                HtmlStyleState *target = copy->as.html.styles;
                const HtmlStyleState *styles = source->as.html.styles;
                target->id_count = styles->id_count;
                target->id_capacity = styles->id_count;
                target->ids = vm_allocate(
                    target->id_count, sizeof(*target->ids));
                if (target->id_count != 0U)
                    memcpy(target->ids, styles->ids,
                           target->id_count * sizeof(*target->ids));
                target->occurrence_count = styles->occurrence_count;
                target->occurrence_capacity = styles->occurrence_count;
                target->occurrences = vm_allocate(
                    target->occurrence_count,
                    sizeof(*target->occurrences));
                if (target->occurrence_count != 0U)
                    memcpy(target->occurrences, styles->occurrences,
                           target->occurrence_count *
                               sizeof(*target->occurrences));
            }
            break;
        case OBJECT_STRING:
            copy->as.string.length = source->as.string.length;
            copy->as.string.data = vm_allocate(copy->as.string.length + 1U, 1U);
            memcpy(copy->as.string.data, source->as.string.data,
                   copy->as.string.length + 1U);
            break;
        case OBJECT_STRING_BUILDER:
            copy->as.string_builder.length =
                source->as.string_builder.length;
            copy->as.string_builder.capacity =
                source->as.string_builder.length + 1U;
            copy->as.string_builder.data = vm_allocate(
                copy->as.string_builder.capacity, 1U);
            if (source->as.string_builder.length != 0U)
                memcpy(copy->as.string_builder.data,
                       source->as.string_builder.data,
                       source->as.string_builder.length);
            copy->as.string_builder.data[
                copy->as.string_builder.length] = '\0';
            break;
        case OBJECT_VEC:
            copy->as.vector.capacity = source->as.vector.count;
            copy->as.vector.items = vm_allocate(
                copy->as.vector.capacity, sizeof(*copy->as.vector.items));
            for (size_t i = 0U; i < source->as.vector.count; ++i) {
                if (!clone_value(vm, source->as.vector.items[i], semantic,
                                 span, &copy->as.vector.items[i]))
                    goto fail;
                ++copy->as.vector.count;
            }
            break;
        case OBJECT_DICTIONARY:
            copy->as.dictionary.capacity = source->as.dictionary.count;
            copy->as.dictionary.items = vm_allocate(
                copy->as.dictionary.capacity,
                sizeof(*copy->as.dictionary.items));
            for (size_t i = 0U; i < source->as.dictionary.count; ++i) {
                if (!clone_value(vm, source->as.dictionary.items[i],
                                 semantic, span,
                                 &copy->as.dictionary.items[i]))
                    goto fail;
                ++copy->as.dictionary.count;
            }
            copy->as.dictionary.bucket_count =
                source->as.dictionary.bucket_count;
            copy->as.dictionary.buckets = vm_allocate(
                copy->as.dictionary.bucket_count,
                sizeof(*copy->as.dictionary.buckets));
            if (copy->as.dictionary.bucket_count != 0U)
                memcpy(copy->as.dictionary.buckets,
                       source->as.dictionary.buckets,
                       copy->as.dictionary.bucket_count *
                           sizeof(*copy->as.dictionary.buckets));
            break;
        case OBJECT_QUEUE:
            copy->as.queue.capacity = source->as.queue.count;
            copy->as.queue.items = vm_allocate(
                copy->as.queue.capacity, sizeof(*copy->as.queue.items));
            for (size_t i = 0U; i < source->as.queue.count; ++i) {
                if (!clone_value(
                        vm,
                        source->as.queue.items[
                            (source->as.queue.head + i) %
                            source->as.queue.capacity],
                        semantic, span, &copy->as.queue.items[i]))
                    goto fail;
                ++copy->as.queue.count;
            }
            break;
        case OBJECT_BUFFER:
            copy->as.buffer.length = source->as.buffer.length;
            copy->as.buffer.data = vm_allocate(copy->as.buffer.length, 1U);
            if (copy->as.buffer.length != 0U)
                memcpy(copy->as.buffer.data, source->as.buffer.data,
                       copy->as.buffer.length);
            break;
        case OBJECT_ARENA:
            copy->as.arena.blocks = NULL;
            copy->as.arena.count = 0U;
            copy->as.arena.capacity = 0U;
            break;
        case OBJECT_NATIVE_HANDLE:
            copy->as.native_handle.data = source->as.native_handle.data;
            copy->as.native_handle.destructor = NULL;
            break;
        case OBJECT_CANCELLATION:
            break;
        case OBJECT_ITER:
            copy->as.iterator.array = object_clone(
                vm, source->as.iterator.array, semantic, span, ok);
            if (!*ok) goto fail;
            copy->as.iterator.bytes = source->as.iterator.bytes;
            copy->as.iterator.index = source->as.iterator.index;
            copy->as.iterator.borrowed = false;
            copy->as.iterator.is_slice = source->as.iterator.is_slice;
            break;
        case OBJECT_TASK:
            /* Tasks are identity-bearing shared operations. This branch is
             * unreachable because vm_value_clone retains them above. */
            break;
    }
    return copy;
fail:
    *ok = false;
    vm_object_free(vm, copy);
    return NULL;
}

static bool clone_value(
    LangVM *vm, LangValue value, bool semantic, LangSpan span,
    LangValue *out_value) {
    if (out_value == NULL) return false;
    if (value.tag != LANG_VALUE_OBJECT || value.as.object == NULL) {
        *out_value = value;
        return true;
    }
    Object *object = value.as.object;
    if (semantic && object->kind == OBJECT_STRUCT) {
        uint32_t copy_function = vm_custom_copy_for_metadata(
            vm, object->as.structure.metadata);
        if (copy_function != 0U) {
            LangValue source = value;
            LangValue reference = {
                .tag=LANG_VALUE_RAW_POINTER,
                .as.pointer=&source
            };
            *out_value = vm_execute_function(
                vm, (size_t)(copy_function - 1U), &reference, 1U, span);
            return !vm->trapped && !vm->exception_pending;
        }
    }
    if (object->kind == OBJECT_STRING ||
        object->kind == OBJECT_NATIVE_HANDLE ||
        object->kind == OBJECT_CANCELLATION ||
        object->kind == OBJECT_TASK) {
        if (object->references != SIZE_MAX) {
            if (object->references == 0U) object->references = 1U;
            ++object->references;
        }
        *out_value = value;
        return true;
    }
    bool ok = true;
    value.as.object = object_clone(vm, object, semantic, span, &ok);
    if (!ok) return false;
    *out_value = value;
    return true;
}

LangValue vm_value_clone(LangValue value) {
    LangValue result = (LangValue){.tag=LANG_VALUE_UNIT};
    (void)clone_value(NULL, value, false, (LangSpan){0}, &result);
    return result;
}

bool vm_value_semantic_copy(
    LangVM *vm, LangValue value, LangSpan span, LangValue *out_value) {
    return clone_value(vm, value, true, span, out_value);
}

uint32_t vm_language_destructor_for_metadata(
    const LangVM *vm, const char *metadata) {
    if (vm == NULL || vm->module == NULL || metadata == NULL) return 0U;
    const char *type_name = metadata;
    const char *module_separator = strchr(metadata, '#');
    size_t module_length = 0U;
    if (module_separator != NULL) {
        module_length = (size_t)(module_separator - metadata);
        type_name = module_separator + 1;
    }
    size_t type_length = strlen(type_name);
    const char *field_separator = strchr(type_name, '|');
    const char *variant_separator = strstr(type_name, "::");
    if (field_separator != NULL)
        type_length = (size_t)(field_separator - type_name);
    if (variant_separator != NULL &&
        (size_t)(variant_separator - type_name) < type_length)
        type_length = (size_t)(variant_separator - type_name);
    for (size_t i = 0U;
         i < vm->module->class_destructor_count; ++i) {
        const BytecodeClassDestructor *entry =
            &vm->module->class_destructors[i];
        if (entry->runtime_module_length == module_length &&
            entry->runtime_type_length == type_length &&
            (module_length == 0U ||
             memcmp(entry->runtime_module, metadata, module_length) == 0) &&
            memcmp(entry->runtime_type, type_name, type_length) == 0) {
            if (entry->destructor_function >= UINT32_MAX) return 0U;
            return (uint32_t)entry->destructor_function + 1U;
        }
    }
    static const char suffix[] = "::drop";
    for (size_t i = 0U; i < vm->module->function_count; ++i) {
        const char *name = vm->module->functions[i].name;
        const char *function_module =
            vm->module->functions[i].module_name;
        bool module_matches =
            module_separator == NULL ||
            (function_module != NULL &&
             strlen(function_module) == module_length &&
             memcmp(function_module, metadata, module_length) == 0);
        if (strlen(name) == type_length + sizeof(suffix) - 1U &&
            module_matches &&
            memcmp(name, type_name, type_length) == 0 &&
            memcmp(name + type_length, suffix, sizeof(suffix)) == 0) {
            if (i >= UINT32_MAX) return 0U;
            return (uint32_t)i + 1U;
        }
    }
    return 0U;
}
