#ifndef ASTER_VM_INTERNAL_H
#define ASTER_VM_INTERNAL_H

#include "internal.h"

typedef enum ObjectKind {
    OBJECT_ARRAY, OBJECT_STRUCT, OBJECT_HTML, OBJECT_STRING, OBJECT_BUFFER,
    OBJECT_STRING_BUILDER, OBJECT_VEC, OBJECT_DICTIONARY, OBJECT_QUEUE,
    OBJECT_ARENA,
    OBJECT_NATIVE_HANDLE, OBJECT_CANCELLATION, OBJECT_ITER, OBJECT_TASK
} ObjectKind;

typedef enum VmTaskState {
    VM_TASK_PENDING,
    VM_TASK_SUCCEEDED,
    VM_TASK_FAULTED,
    VM_TASK_CANCELED
} VmTaskState;

typedef struct VmAsyncFrame VmAsyncFrame;
typedef struct VmTaskContinuation VmTaskContinuation;
typedef struct VmTimer VmTimer;
typedef struct Object Object;

struct VmAsyncFrame {
    size_t function_index;
    size_t ip;
    size_t sp;
    LangValue *locals;
    bool *initialized;
    LangValue **references;
    LangValue *stack;
    Object *html_objects;
    uint64_t instruction_count;
    LangSpan call_span;
    Object *awaited;
};

struct VmTaskContinuation {
    void (*callback)(LangVM *vm, Object *completed, void *context);
    void *context;
    VmTaskContinuation *next;
};

struct VmTimer {
    Object *task;
    Object *cancellation;
    int64_t deadline_milliseconds;
    VmTimer *next;
};

typedef struct RawAllocation {
    void *data;
    size_t length;
    bool active;
} RawAllocation;

typedef struct HtmlStyleOccurrence {
    const char *id;
    size_t start;
    size_t end;
} HtmlStyleOccurrence;

typedef struct HtmlStyleState {
    const char **ids;
    size_t id_count;
    size_t id_capacity;
    HtmlStyleOccurrence *occurrences;
    size_t occurrence_count;
    size_t occurrence_capacity;
} HtmlStyleState;

struct Object {
    ObjectKind kind;
    uint32_t language_destructor;
    size_t references;
    union {
        struct { LangValue *items; size_t count; } array;
        struct { char *metadata; LangValue *fields; size_t count; } structure;
        struct {
            Object *destination;
            char *data;
            size_t length;
            size_t capacity;
            const char *tag;
            size_t tag_length;
            bool open;
            bool embedded;
            bool suppressed;
            size_t start;
            const char *static_style_id;
            HtmlStyleState *styles;
        } html;
        struct {
            char *data;
            size_t length;
            bool embedded_data;
        } string;
        struct {
            char *data;
            size_t length;
            size_t capacity;
            bool embedded_data;
        } string_builder;
        struct { LangValue *items; size_t count; size_t capacity; } vector;
        struct {
            LangValue *items;
            size_t count;
            size_t capacity;
            size_t *buckets;
            size_t bucket_count;
        } dictionary;
        struct {
            LangValue *items;
            size_t count;
            size_t capacity;
            size_t head;
        } queue;
        struct { unsigned char *data; size_t length; } buffer;
        struct { RawAllocation **blocks; size_t count; size_t capacity; } arena;
        struct { void *data; LangNativeHandleDropFn destructor; } native_handle;
        struct { bool requested; } cancellation;
        struct {
            Object *array;
            LangByteSlice bytes;
            size_t index;
            bool borrowed;
            bool is_slice;
            bool is_string;
        } iterator;
        struct {
            VmTaskState state;
            LangValue result;
            LangValue exception;
            VmAsyncFrame *frame;
            VmTaskContinuation *continuations;
        } task;
    } as;
};

typedef struct NativeEntry {
    char *name;
    uint64_t name_hash;
    LangNativeFn callback;
    size_t arity;
} NativeEntry;

struct LangVM {
    NativeEntry *natives;
    size_t native_count;
    size_t native_capacity;
    const BytecodeModule *module;
    const LangSource *source;
    const char *frames[128];
    LangSpan frame_call_sites[128];
    size_t frame_count;
    LangSpan active_span;
    bool trapped;
    bool exception_pending;
    LangValue exception_value;
    int64_t native_drop_log;
    RawAllocation **raw_allocations;
    size_t raw_allocation_count;
    size_t raw_allocation_capacity;
    LangValue *frame_locals;
    bool *frame_initialized;
    LangValue **frame_references;
    size_t frame_local_stride;
    LangValue *frame_stacks;
    Object *frame_html_objects;
    uint64_t instruction_count;
    size_t process_argument_count;
    const char *const *process_arguments;
    VmTimer *timers;
};

void *vm_allocate(size_t count, size_t size);
void *vm_html_resize(void *pointer, size_t count, size_t size);
void *vm_allocate_uninitialized(size_t count, size_t size);
LangValue vm_value_clone(LangValue value);
uint32_t vm_language_destructor_for_metadata(
    const LangVM *vm, const char *metadata);
LangValue vm_execute_function(LangVM *vm, size_t function_index,
                              const LangValue *arguments,
                              size_t argument_count, LangSpan call_span);
LangValue vm_task_delay(LangVM *vm, int64_t milliseconds,
                        Object *cancellation, LangSpan call_span);
void vm_task_cancel(LangVM *vm, Object *task, LangValue exception);
LangValue vm_task_when_all(LangVM *vm, Object *list, bool returns_values,
                           LangSpan call_span);
LangValue vm_task_when_any(LangVM *vm, Object *list, LangSpan call_span);
bool vm_run_task_to_completion(LangVM *vm, LangValue task,
                               LangValue *out_result);
LangValue vm_start_async_function(LangVM *vm, size_t function_index,
                                  const LangValue *arguments,
                                  size_t argument_count,
                                  LangSpan call_span);
void vm_resume_task(LangVM *vm, Object *task);
void vm_execute_async_task_step(LangVM *vm, Object *task);
void vm_task_complete(LangVM *vm, Object *task, LangValue result);
void vm_task_fault(LangVM *vm, Object *task, LangValue exception);
bool vm_task_suspend(LangVM *vm, Object *task, Object *awaited);
void vm_task_release_runtime(LangVM *vm, Object *task);
void vm_task_destroy(LangVM *vm, Object *task);
void vm_value_drop_owned(LangVM *vm, LangValue value);
void vm_raise_exception_message(LangVM *vm, const char *message);
void vm_raise_exception_typed(LangVM *vm, const char *type,
                              const char *message);
void vm_object_free(LangVM *vm, Object *object);
bool vm_checked_add(int64_t left, int64_t right, int64_t *out);
bool vm_checked_sub(int64_t left, int64_t right, int64_t *out);
bool vm_checked_mul(int64_t left, int64_t right, int64_t *out);
bool vm_signed_value_fits_type(int64_t value, TypeKind kind);
bool vm_unsigned_value_fits_type(uint64_t value, TypeKind kind);
unsigned vm_runtime_integer_width(TypeKind kind);
LangValue vm_runtime_value_from_integer_bits(uint64_t bits,
                                             TypeKind kind);
bool vm_cast_numeric_value(LangValue input, TypeKind target,
                           LangValue *output);
void vm_runtime_error_at(LangVM *vm, LangSpan instruction_span,
                         const char *message);
bool vm_execute_fast_scalar_leaf(
    LangVM *vm, size_t function_index,
    const LangValue *arguments, size_t argument_count,
    LangSpan call_span, LangValue *out_result);
bool vm_execute_fast_scalar_loop(
    LangVM *vm, const BytecodeFunction *function,
    LangValue *locals, bool *initialized,
    size_t loop_end, size_t *io_ip, uint64_t *executed);
bool vm_string_builder_append_bytes(Object *builder,
                                    const char *bytes, size_t length);
bool vm_string_builder_append_value(Object *builder, LangValue value);
bool vm_call_builtin(LangVM *vm, int32_t index, LangValue *arguments,
                     size_t argument_count, LangValue *result,
                     LangSpan instruction_span);
bool vm_value_is_string_builder(const LangValue *value);
int64_t *vm_native_drop_log(LangVM *vm);
size_t vm_process_argument_count(const LangVM *vm);
const char *vm_process_argument(const LangVM *vm, size_t index);
bool vm_verify_bytecode_module(const BytecodeModule *module);
Object *vm_html_destination(Object *html);
void vm_html_bytes(Object *html, const char *data, size_t length);
void vm_html_cstr(Object *html, const char *text);
void vm_html_escape(Object *html, LangStringView text, bool attribute);
void vm_html_append_text(Object *html, LangStringView text);
void vm_html_ensure_open_closed(Object *html);
size_t vm_format_u64(char *buffer, uint64_t value);
size_t vm_format_i64(char *buffer, int64_t value);
void vm_html_append_formatted_value(LangVM *vm, Object *html,
                                    LangValue value, bool attribute);
void vm_html_append_formatted(LangVM *vm, Object *html,
                              LangValue value, bool attribute);
bool vm_css_custom_property_atom(LangStringView value);
void vm_html_set_attribute(LangVM *vm, Object *html,
                           LangStringView name, LangValue value);
void vm_html_finish(Object *html);
void vm_html_append_value(LangVM *vm, Object *html, LangValue child);
void vm_html_release_style_state(Object *html);

#endif
