#include "internal.h"
#include "vm_internal.h"

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool vm_decode_utf8_scalar(
    const unsigned char *bytes, size_t size, size_t *index,
    uint32_t *out_scalar) {
    if (*index >= size) return false;
    size_t cursor = *index;
    unsigned char first = bytes[cursor];
    uint32_t scalar;
    size_t length;
    if (first < 0x80U) { scalar = first; length = 1U; }
    else if (first >= 0xc2U && first <= 0xdfU) {
        scalar = first & 0x1fU; length = 2U;
    } else if (first >= 0xe0U && first <= 0xefU) {
        scalar = first & 0x0fU; length = 3U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        scalar = first & 0x07U; length = 4U;
    } else return false;
    if (cursor + length > size) return false;
    for (size_t offset = 1U; offset < length; ++offset) {
        unsigned char continuation = bytes[cursor + offset];
        if ((continuation & 0xc0U) != 0x80U) return false;
        scalar = (scalar << 6U) | (continuation & 0x3fU);
    }
    if ((length == 3U && scalar < 0x800U) ||
        (length == 4U && scalar < 0x10000U) || scalar > 0x10ffffU ||
        (scalar >= 0xd800U && scalar <= 0xdfffU)) return false;
    *index = cursor + length;
    *out_scalar = scalar;
    return true;
}
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

void vm_runtime_error_at(LangVM *vm, LangSpan instruction_span,
                             const char *message) {
    vm->trapped = true;
    size_t line = 1U, column = 1U;
    size_t line_start = 0U, line_end = 0U;
    if (vm->source != NULL) {
        lang_source_line_info(vm->source, instruction_span.start,
                              &line, &column, &line_start, &line_end);
    }
    (void)line_start;
    (void)line_end;
    fprintf(stderr, "runtime error: %s\n  at %s (%s:%zu:%zu)\n",
            message, vm->frame_count != 0U ? vm->frames[vm->frame_count - 1U] : "?",
            vm->source != NULL
                ? lang_source_path_at(vm->source,
                                      instruction_span.start)
                : (instruction_span.file != NULL
                    ? instruction_span.file : "?"),
            line, column);
    for (size_t i = vm->frame_count; i > 1U; --i) {
        LangSpan call = vm->frame_call_sites[i - 1U];
        size_t call_line = 1U;
        size_t call_column = 1U;
        size_t ignored_start = 0U;
        size_t ignored_end = 0U;
        if (vm->source != NULL)
            lang_source_line_info(
                vm->source, call.start, &call_line, &call_column,
                &ignored_start, &ignored_end);
        fprintf(
            stderr, "  at %s (%s:%zu:%zu)\n",
            vm->frames[i - 2U],
            vm->source != NULL
                ? lang_source_path_at(vm->source, call.start)
                : (call.file != NULL ? call.file : "?"),
            call_line, call_column);
    }
}

#define runtime_error(vm_, instruction_, message_) \
    vm_runtime_error_at((vm_), instruction_span, (message_))

static bool vm_is_payloadless_variant(LangValue value) {
    if (value.tag != LANG_VALUE_OBJECT || value.as.object == NULL)
        return false;
    Object *object = value.as.object;
    return object->kind == OBJECT_STRUCT &&
           object->as.structure.count == 0U;
}

static bool vm_payloadless_variants_equal(
    LangValue left, LangValue right) {
    if (!vm_is_payloadless_variant(left) ||
        !vm_is_payloadless_variant(right))
        return false;
    Object *left_object = left.as.object;
    Object *right_object = right.as.object;
    return strcmp(
        left_object->as.structure.metadata,
        right_object->as.structure.metadata) == 0;
}

bool vm_checked_add(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
    *out = a + b; return true;
}
bool vm_checked_sub(int64_t a, int64_t b, int64_t *out) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) return false;
    *out = a - b; return true;
}
bool vm_checked_mul(int64_t a, int64_t b, int64_t *out) {
    if (a == 0 || b == 0) { *out = 0; return true; }
    if ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN)) return false;
    if (a > 0) {
        if ((b > 0 && a > INT64_MAX / b) || (b < 0 && b < INT64_MIN / a)) return false;
    } else {
        if ((b > 0 && a < INT64_MIN / b) || (b < 0 && a < INT64_MAX / b)) return false;
    }
    *out = a * b; return true;
}

bool vm_signed_value_fits_type(int64_t value, TypeKind kind) {
    switch (kind) {
        case TYPE_I8: return value >= INT8_MIN && value <= INT8_MAX;
        case TYPE_I16: return value >= INT16_MIN && value <= INT16_MAX;
        case TYPE_I32: return value >= INT32_MIN && value <= INT32_MAX;
        case TYPE_I64: case TYPE_ISIZE: return true;
        default: return false;
    }
}

static void drop_runtime_value(LangVM *vm,
                               const BytecodeFunction *owner_function,
                               size_t slot, LangValue value) {
    if (owner_function != NULL &&
        owner_function->local_destructors != NULL &&
        slot < owner_function->local_count &&
        owner_function->local_destructors[slot] == -2)
        return;
    if (value.tag == LANG_VALUE_OBJECT)
        vm_value_drop_owned(vm, value);
}

#define LOCAL(slot_) \
    (*((references[(slot_)] != NULL) \
        ? references[(slot_)] : &locals[(slot_)]))

static LangValue finish_frame(LangVM *vm, const BytecodeFunction *function,
                              LangValue *locals, bool *initialized,
                              LangValue **references,
                              LangValue result,
                              uint64_t frame_instruction_count) {
    if (function->may_have_object_locals &&
        function->object_local_mask_valid) {
        uint64_t mask = function->object_local_mask;
        while (mask != 0U) {
            size_t slot = (size_t)__builtin_ctzll(mask);
            if (initialized[slot] &&
                references[slot] == NULL &&
                LOCAL(slot).tag == LANG_VALUE_OBJECT &&
                !(result.tag == LANG_VALUE_OBJECT &&
                  LOCAL(slot).as.object == result.as.object))
                drop_runtime_value(
                    vm, function, slot, LOCAL(slot));
            mask &= mask - UINT64_C(1);
        }
    } else if (function->may_have_object_locals)
        for (size_t i = function->local_count; i > 0U; --i)
            if (initialized[i - 1U] &&
                references[i - 1U] == NULL &&
                LOCAL(i - 1U).tag == LANG_VALUE_OBJECT &&
                !(result.tag == LANG_VALUE_OBJECT &&
                  LOCAL(i - 1U).as.object == result.as.object))
                drop_runtime_value(
                    vm, function, i - 1U, LOCAL(i - 1U));
    if (UINT64_MAX - vm->instruction_count < frame_instruction_count)
        vm->instruction_count = UINT64_MAX;
    else
        vm->instruction_count += frame_instruction_count;
    --vm->frame_count;
    return result;
}

#define PUSH(value_) do { if (sp >= 1024U) { runtime_error(vm, instruction, "operand stack overflow"); goto fail; } stack[sp++] = (value_); } while (0)
#define POP() stack[--sp]

static LangValue vm_execute_function_core(
        LangVM *vm, size_t function_index,
        const LangValue *arguments, size_t argument_count,
        LangSpan call_span, Object *async_task) {
    LangValue failure = {.tag = LANG_VALUE_UNIT};
    LangSpan call_instruction_span = call_span;
    if (function_index >= vm->module->function_count) {
        vm->trapped = true;
        return failure;
    }
    if (vm->frame_count >= 128U) {
        vm_runtime_error_at(vm, call_instruction_span,
                      "maximum call depth exceeded");
        return failure;
    }
    const BytecodeFunction *function =
        &vm->module->functions[function_index];
    if (argument_count != function->arity) {
        char message[256];
        (void)snprintf(
            message, sizeof(message),
            "function `%s` expects %zu arguments, found %zu",
            function->name, function->arity, argument_count);
        vm_runtime_error_at(vm, call_instruction_span, message);
        return failure;
    }
    VmAsyncFrame *async_frame = async_task != NULL
        ? async_task->as.task.frame : NULL;
    size_t frame_offset = vm->frame_count * vm->frame_local_stride;
    LangValue *locals = async_frame != NULL
        ? async_frame->locals : vm->frame_locals + frame_offset;
    bool *initialized = async_frame != NULL
        ? async_frame->initialized : vm->frame_initialized + frame_offset;
    LangValue **references = async_frame != NULL
        ? async_frame->references : vm->frame_references + frame_offset;
    if (async_frame == NULL && function->local_count != 0U)
        memset(initialized, 0, function->local_count * sizeof(*initialized));
    if (async_frame == NULL && function->local_count != 0U)
        memset(references, 0, function->local_count * sizeof(*references));
    for (size_t i = 0U; async_frame == NULL &&
         i < argument_count && i < function->local_count; ++i) {
        if (arguments[i].tag == LANG_VALUE_RAW_POINTER &&
            parameter_mode_is_reference(function->parameter_modes[i])) {
            references[i] = (LangValue *)arguments[i].as.pointer;
        } else {
            LOCAL(i) = arguments[i];
        }
        initialized[i] = true;
    }
    LangValue *stack = async_frame != NULL
        ? async_frame->stack
        : vm->frame_stacks + vm->frame_count * 1024U;
    Object *html_objects = async_frame != NULL
        ? async_frame->html_objects
        : vm->frame_html_objects + frame_offset;
    size_t html_object_count = 0U;
    size_t sp = async_frame != NULL ? async_frame->sp : 0U;
    size_t ip = async_frame != NULL ? async_frame->ip : 0U;
    uint64_t frame_instruction_count = async_frame != NULL
        ? async_frame->instruction_count : 0U;
    bool fast_scalar_loop_enabled = true;
    vm->frames[vm->frame_count] = function->name;
    vm->frame_call_sites[vm->frame_count] = call_span;
    ++vm->frame_count;
    if (async_frame != NULL && async_frame->awaited != NULL) {
        Object *awaited = async_frame->awaited;
        async_frame->awaited = NULL;
        if (awaited->as.task.state == VM_TASK_SUCCEEDED) {
            stack[sp++] = vm_value_clone(awaited->as.task.result);
        } else if (awaited->as.task.state == VM_TASK_FAULTED ||
                   awaited->as.task.state == VM_TASK_CANCELED) {
            if (vm->exception_pending)
                vm_value_drop_owned(vm, vm->exception_value);
            vm->exception_value =
                vm_value_clone(awaited->as.task.exception);
            vm->exception_pending = true;
            stack[sp++] = (LangValue){.tag=LANG_VALUE_UNIT};
        } else {
            vm_runtime_error_at(
                vm, call_span, "await resumed from an incomplete Task");
        }
        vm_object_free(vm, awaited);
    }
#define instruction_span (function->spans[instruction_index])
#if defined(LANG_VM_COMPUTED_GOTO)
#define VM_LABEL(name_) vm_dispatch_##name_:
    static void *const dispatch_table[] = {
        [OP_CONSTANT]=&&vm_dispatch_constant,
        [OP_UNIT]=&&vm_dispatch_unit,
        [OP_TRUE]=&&vm_dispatch_true,
        [OP_FALSE]=&&vm_dispatch_false,
        [OP_POP]=&&vm_dispatch_pop,
        [OP_LOAD_LOCAL]=&&vm_dispatch_load_local,
        [OP_STORE_LOCAL]=&&vm_dispatch_store_local,
        [OP_MOVE_LOCAL]=&&vm_dispatch_move_local,
        [OP_REFERENCE_LOCAL]=&&vm_dispatch_reference_local,
        [OP_REFERENCE_FIELD_LOCAL]=&&vm_dispatch_reference_field_local,
        [OP_INVALIDATE_LOCAL]=&&vm_dispatch_invalidate_local,
        [OP_LOAD_STATIC]=&&vm_dispatch_load_static,
        [OP_STORE_STATIC]=&&vm_dispatch_store_static,
        [OP_ADD_I64]=&&vm_dispatch_integer_binary,
        [OP_SUB_I64]=&&vm_dispatch_integer_binary,
        [OP_MUL_I64]=&&vm_dispatch_integer_binary,
        [OP_DIV_I64]=&&vm_dispatch_integer_binary,
        [OP_REM_I64]=&&vm_dispatch_integer_binary,
        [OP_SHIFT_LEFT]=&&vm_dispatch_shift,
        [OP_SHIFT_RIGHT]=&&vm_dispatch_shift,
        [OP_BIT_AND]=&&vm_dispatch_bitwise,
        [OP_BIT_OR]=&&vm_dispatch_bitwise,
        [OP_BIT_XOR]=&&vm_dispatch_bitwise,
        [OP_BIT_NOT]=&&vm_dispatch_bitwise_not,
        [OP_ADD_F64]=&&vm_dispatch_float_binary,
        [OP_SUB_F64]=&&vm_dispatch_float_binary,
        [OP_MUL_F64]=&&vm_dispatch_float_binary,
        [OP_DIV_F64]=&&vm_dispatch_float_binary,
        [OP_NEG_I64]=&&vm_dispatch_neg_i64,
        [OP_NEG_F64]=&&vm_dispatch_neg_f64,
        [OP_NOT]=&&vm_dispatch_not,
        [OP_CAST]=&&vm_dispatch_cast,
        [OP_EQ]=&&vm_dispatch_compare,
        [OP_NEQ]=&&vm_dispatch_compare,
        [OP_LT_I64]=&&vm_dispatch_compare,
        [OP_LE_I64]=&&vm_dispatch_compare,
        [OP_GT_I64]=&&vm_dispatch_compare,
        [OP_GE_I64]=&&vm_dispatch_compare,
        [OP_JUMP]=&&vm_dispatch_jump,
        [OP_JUMP_IF_FALSE]=&&vm_dispatch_jump_if_false,
        [OP_FUNCTION]=&&vm_dispatch_function,
        [OP_BOUND_FUNCTION]=&&vm_dispatch_bound_function,
        [OP_CALL]=&&vm_dispatch_call,
        [OP_CALL_VIRTUAL]=&&vm_dispatch_call_virtual,
        [OP_CALL_INDIRECT]=&&vm_dispatch_call_indirect,
        [OP_CALL_NATIVE]=&&vm_dispatch_call_native,
        [OP_AWAIT]=&&vm_dispatch_await,
        [OP_TASK_DELAY]=&&vm_dispatch_task_delay,
        [OP_TASK_WHEN_ALL]=&&vm_dispatch_task_when_all,
        [OP_TASK_WHEN_ANY]=&&vm_dispatch_task_when_any,
        [OP_RETURN]=&&vm_dispatch_return,
        [OP_CANCELLATION_SOURCE_NEW]=&&vm_dispatch_cancellation_source_new,
        [OP_CANCELLATION_TOKEN_NONE]=&&vm_dispatch_cancellation_token_none,
        [OP_CANCELLATION_TOKEN_GET]=&&vm_dispatch_cancellation_token_get,
        [OP_CANCELLATION_CANCEL]=&&vm_dispatch_cancellation_cancel,
        [OP_CANCELLATION_IS_REQUESTED]=&&vm_dispatch_cancellation_is_requested,
        [OP_CANCELLATION_THROW_IF_REQUESTED]=
            &&vm_dispatch_cancellation_throw_if_requested,
        [OP_MAKE_ARRAY]=&&vm_dispatch_make_array,
        [OP_GET_INDEX]=&&vm_dispatch_get_index,
        [OP_GET_INDEX_LOCAL]=&&vm_dispatch_get_index_local,
        [OP_SET_INDEX_LOCAL]=&&vm_dispatch_set_index_local,
        [OP_MAKE_STRUCT]=&&vm_dispatch_make_struct,
        [OP_MAKE_CLASS]=&&vm_dispatch_make_class,
        [OP_DELETE_CLASS]=&&vm_dispatch_delete_class,
        [OP_GET_FIELD]=&&vm_dispatch_get_field,
        [OP_GET_FIELD_LOCAL]=&&vm_dispatch_get_field_local,
        [OP_GET_FIELD_LOCAL_MOVE]=&&vm_dispatch_get_field_local_move,
        [OP_GET_FIELD_BORROW]=&&vm_dispatch_get_field_borrow,
        [OP_SET_FIELD_LOCAL]=&&vm_dispatch_set_field_local,
        [OP_GET_TAG]=&&vm_dispatch_get_tag,
        [OP_TAKE_PAYLOAD]=&&vm_dispatch_take_payload,
        [OP_SET_LOCAL]=&&vm_dispatch_set_local,
        [OP_HTML_FRAGMENT]=&&vm_dispatch_html_fragment,
        [OP_HTML_BEGIN]=&&vm_dispatch_html_begin,
        [OP_HTML_FRAGMENT_LOCAL]=&&vm_dispatch_html_fragment_local,
        [OP_HTML_BEGIN_LOCAL]=&&vm_dispatch_html_begin_local,
        [OP_HTML_ATTR]=&&vm_dispatch_html_attr,
        [OP_HTML_TEXT]=&&vm_dispatch_html_text,
        [OP_HTML_APPEND]=&&vm_dispatch_html_append,
        [OP_HTML_END]=&&vm_dispatch_html_end,
        [OP_HTML_ATTR_LOCAL]=&&vm_dispatch_html_attr_local,
        [OP_HTML_ATTR_BEGIN_LOCAL]=&&vm_dispatch_html_attr_begin_local,
        [OP_HTML_ATTR_APPEND_LOCAL]=&&vm_dispatch_html_attr_append_local,
        [OP_HTML_CSS_VALUE_LOCAL]=&&vm_dispatch_html_css_value_local,
        [OP_HTML_ATTR_END_LOCAL]=&&vm_dispatch_html_attr_end_local,
        [OP_HTML_APPEND_LOCAL]=&&vm_dispatch_html_append_local,
        [OP_HTML_APPEND_FORMATTED_LOCAL]=&&vm_dispatch_html_append_formatted_local,
        [OP_HTML_APPEND_CONSTANT_LOCAL]=&&vm_dispatch_html_append_constant_local,
        [OP_HTML_APPEND_RAW_CONSTANT_LOCAL]=&&vm_dispatch_html_append_raw_constant_local,
        [OP_HTML_ATTR_CONSTANT_LOCAL]=&&vm_dispatch_html_attr_constant_local,
        [OP_HTML_ATTR_APPEND_CONSTANT_LOCAL]=&&vm_dispatch_html_attr_append_constant_local,
        [OP_HTML_APPEND_VALUE_LOCAL]=&&vm_dispatch_html_append_value_local,
        [OP_HTML_ATTR_APPEND_VALUE_LOCAL]=&&vm_dispatch_html_attr_append_value_local,
        [OP_HTML_FINISH_LOCAL]=&&vm_dispatch_html_finish_local,
        [OP_HTML_RENDER_LOCAL]=&&vm_dispatch_html_render_local,
        [OP_STRING_BUILDER_NEW_LOCAL]=&&vm_dispatch_string_builder_new_local,
        [OP_STRING_BUILDER_APPEND_CONSTANT_LOCAL]=&&vm_dispatch_string_builder_append_constant_local,
        [OP_STRING_BUILDER_APPEND_VALUE_LOCAL]=&&vm_dispatch_string_builder_append_value_local,
        [OP_STRING_BUILDER_FINISH_LOCAL]=&&vm_dispatch_string_builder_finish_local,
        [OP_ITER_INIT]=&&vm_dispatch_iter_init,
        [OP_ITER_BORROW_LOCAL]=&&vm_dispatch_iter_borrow_local,
        [OP_ITER_NEXT]=&&vm_dispatch_iter_next,
        [OP_ITER_HAS_NEXT_LOCAL]=&&vm_dispatch_iter_has_next_local,
        [OP_ITER_TAKE_NEXT_LOCAL]=&&vm_dispatch_iter_take_next_local,
        [OP_DROP_LOCAL]=&&vm_dispatch_drop_local,
        [OP_CLONE]=&&vm_dispatch_clone,
        [OP_TRY]=&&vm_dispatch_try,
        [OP_CONSTANT_LOCAL]=&&vm_dispatch_constant_local,
        [OP_COPY_LOCAL_TO]=&&vm_dispatch_copy_local_to,
        [OP_MOVE_LOCAL_TO]=&&vm_dispatch_move_local_to,
        [OP_BINARY_LOCALS]=&&vm_dispatch_binary_locals,
        [OP_BINARY_LOCAL_IMMEDIATE]=
            &&vm_dispatch_binary_local_immediate,
        [OP_BINARY_LOCALS_IMMEDIATE]=
            &&vm_dispatch_binary_locals_immediate,
        [OP_COMPARE_BRANCH]=&&vm_dispatch_compare_branch,
        [OP_COMPARE_LOCAL_CONSTANT_BRANCH]=
            &&vm_dispatch_compare_local_constant_branch,
        [OP_CALL_LOCAL]=&&vm_dispatch_call_local,
        [OP_CALL_LOCAL_2_COPY]=&&vm_dispatch_call_local_2_copy,
        [OP_RETURN_LOCAL]=&&vm_dispatch_return_local,
        [OP_TEXT_LEN_LOCAL]=&&vm_dispatch_text_len_local,
        [OP_STRING_SEARCH_LOCAL]=&&vm_dispatch_string_search_local,
        [OP_STRING_SEARCH_LOCAL_CONSTANT]=
            &&vm_dispatch_string_search_local_constant,
        [OP_EXCEPTION_SET]=&&vm_dispatch_exception_set,
        [OP_EXCEPTION_PENDING]=&&vm_dispatch_exception_pending,
        [OP_EXCEPTION_MATCH]=&&vm_dispatch_exception_match,
        [OP_EXCEPTION_TAKE]=&&vm_dispatch_exception_take,
        [OP_PROPAGATE_EXCEPTION]=&&vm_dispatch_propagate_exception,
        [OP_TRAP]=&&vm_dispatch_trap
    };
#else
#define VM_LABEL(name_)
#endif
    while (ip < function->code_count && !vm->trapped) {
        if (fast_scalar_loop_enabled &&
            ip == function->fast_scalar_loop_start) {
            if (!vm_execute_fast_scalar_loop(
                    vm, function, locals, initialized,
                    function->fast_scalar_loop_end, &ip,
                    &frame_instruction_count))
                goto fail;
            if (ip <= function->fast_scalar_loop_end)
                fast_scalar_loop_enabled = false;
            continue;
        }
        size_t instruction_index = ip++;
        const Instruction *current_instruction =
            &function->code[instruction_index];
#define instruction (*current_instruction)
        if (frame_instruction_count != UINT64_MAX)
            ++frame_instruction_count;
        OpCode scalar_operation = instruction.op;
        int32_t scalar_type = instruction.a;
        size_t scalar_destination = SIZE_MAX;
#define STORE_SCALAR_RESULT(value_) do { \
    LangValue scalar_result_ = (value_); \
    if (scalar_destination == SIZE_MAX) { \
        PUSH(scalar_result_); \
    } else { \
        LOCAL(scalar_destination) = scalar_result_; \
        initialized[scalar_destination] = true; \
    } \
} while (0)
#if defined(LANG_VM_COMPUTED_GOTO)
        goto *dispatch_table[(size_t)instruction.op];
#endif
        switch (instruction.op) {
            VM_LABEL(constant)
            case OP_CONSTANT: PUSH(vm->module->constants[(size_t)instruction.a].value); break;
            VM_LABEL(constant_local)
            case OP_CONSTANT_LOCAL:
                LOCAL((size_t)instruction.b) =
                    vm->module->constants[(size_t)instruction.a].value;
                initialized[(size_t)instruction.b] = true;
                break;
            VM_LABEL(unit)
            case OP_UNIT: PUSH(((LangValue){.tag=LANG_VALUE_UNIT})); break;
            VM_LABEL(true)
            case OP_TRUE: PUSH(((LangValue){.tag=LANG_VALUE_BOOL,.as.boolean=true})); break;
            VM_LABEL(false)
            case OP_FALSE: PUSH(((LangValue){.tag=LANG_VALUE_BOOL,.as.boolean=false})); break;
            VM_LABEL(pop)
            case OP_POP:
                if (sp != 0U) {
                    vm->active_span = instruction_span;
                    LangValue value = POP();
                    if (value.tag == LANG_VALUE_OBJECT)
                        vm_value_drop_owned(vm, value);
                }
                break;
            VM_LABEL(load_local)
            case OP_LOAD_LOCAL:
                if (instruction.a < 0 || !initialized[(size_t)instruction.a]) {
                    runtime_error(vm, instruction, "load of unavailable local"); goto fail;
                }
                PUSH(LOCAL((size_t)instruction.a)); break;
            VM_LABEL(store_local)
            case OP_STORE_LOCAL:
                LOCAL((size_t)instruction.a) = POP();
                initialized[(size_t)instruction.a] = true; break;
            VM_LABEL(move_local)
            case OP_MOVE_LOCAL:
                if (!initialized[(size_t)instruction.a]) {
                    runtime_error(vm, instruction, "move of unavailable local"); goto fail;
                }
                PUSH(LOCAL((size_t)instruction.a));
                initialized[(size_t)instruction.a] = false; break;
            VM_LABEL(reference_local)
            case OP_REFERENCE_LOCAL:
                if (instruction.a < 0 ||
                    !initialized[(size_t)instruction.a]) {
                    runtime_error(vm, instruction,
                                  "reference of unavailable local");
                    goto fail;
                }
                PUSH(((LangValue){
                    .tag=LANG_VALUE_RAW_POINTER,
                    .as.pointer=&LOCAL((size_t)instruction.a)
                }));
                if (instruction.b >= 0 &&
                    instruction.b != instruction.a)
                    initialized[(size_t)instruction.b] = false;
                break;
            VM_LABEL(reference_field_local)
            case OP_REFERENCE_FIELD_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *object = initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                LangStringView field =
                    vm->module->constants[(size_t)instruction.b]
                        .value.as.string;
                if (object == NULL || object->kind != OBJECT_STRUCT) {
                    runtime_error(vm, instruction,
                                  "field reference requires a struct");
                    goto fail;
                }
                char *cursor = strchr(
                    object->as.structure.metadata, '|');
                bool found = false;
                size_t field_index = 0U;
                while (cursor != NULL &&
                       field_index < object->as.structure.count) {
                    ++cursor;
                    char *end = strchr(cursor, '|');
                    size_t length = end != NULL
                        ? (size_t)(end - cursor) : strlen(cursor);
                    if (length == field.length &&
                        memcmp(cursor, field.data, length) == 0) {
                        PUSH(((LangValue){
                            .tag=LANG_VALUE_RAW_POINTER,
                            .as.pointer=&object->as.structure
                                .fields[field_index]
                        }));
                        found = true;
                        break;
                    }
                    cursor = end;
                    ++field_index;
                }
                if (!found) {
                    runtime_error(vm, instruction,
                                  "unknown struct field reference");
                    goto fail;
                }
                break;
            }
            VM_LABEL(invalidate_local)
            case OP_INVALIDATE_LOCAL:
                initialized[(size_t)instruction.a] = false;
                break;
            VM_LABEL(load_static)
            case OP_LOAD_STATIC:
                PUSH(vm->static_fields[(size_t)instruction.a]);
                break;
            VM_LABEL(store_static)
            case OP_STORE_STATIC:
                vm->static_fields[(size_t)instruction.a] = POP();
                break;
            VM_LABEL(copy_local_to)
            case OP_COPY_LOCAL_TO:
                if (!initialized[(size_t)instruction.a]) {
                    runtime_error(
                        vm, instruction,
                        "copy of unavailable local");
                    goto fail;
                }
                LOCAL((size_t)instruction.b) =
                    LOCAL((size_t)instruction.a);
                initialized[(size_t)instruction.b] = true;
                break;
            VM_LABEL(move_local_to)
            case OP_MOVE_LOCAL_TO:
                if (!initialized[(size_t)instruction.a]) {
                    runtime_error(
                        vm, instruction,
                        "move of unavailable local");
                    goto fail;
                }
                if (instruction.a == instruction.b)
                    break;
                if (initialized[(size_t)instruction.b]) {
                    vm->active_span = instruction_span;
                    drop_runtime_value(
                        vm, function, (size_t)instruction.b,
                        LOCAL((size_t)instruction.b));
                }
                LOCAL((size_t)instruction.b) =
                    LOCAL((size_t)instruction.a);
                initialized[(size_t)instruction.b] = true;
                initialized[(size_t)instruction.a] = false;
                break;
            VM_LABEL(set_local)
            case OP_SET_LOCAL:
                vm->active_span = instruction_span;
                if (initialized[(size_t)instruction.a])
                    drop_runtime_value(vm, function,
                                       (size_t)instruction.a,
                                       LOCAL((size_t)instruction.a));
                LOCAL((size_t)instruction.a) = POP();
                initialized[(size_t)instruction.a] = true;
                PUSH(((LangValue){.tag=LANG_VALUE_UNIT}));
                break;
            VM_LABEL(binary_local_immediate)
            case OP_BINARY_LOCAL_IMMEDIATE: {
                uint32_t packed = (uint32_t)instruction.b;
                size_t source =
                    (size_t)(packed & UINT32_C(0x3ff));
                scalar_destination =
                    (size_t)((packed >> 10U) &
                             UINT32_C(0x3ff));
                int64_t immediate =
                    (int64_t)((int32_t)packed >> 20U);
                if (!initialized[source] ||
                    LOCAL(source).tag != LANG_VALUE_I64) {
                    runtime_error(
                        vm, instruction,
                        "immediate binary operation used an "
                        "unavailable or invalid local");
                    goto fail;
                }
                uint32_t packed_operation =
                    (uint32_t)instruction.a;
                scalar_operation =
                    (OpCode)(packed_operation &
                             UINT32_C(0xff));
                scalar_type =
                    (int32_t)(packed_operation >> 8U);
                int64_t left = LOCAL(source).as.i64;
                int64_t result = 0;
                bool ok = true;
                if (scalar_operation == OP_ADD_I64)
                    ok = vm_checked_add(left, immediate, &result);
                else if (scalar_operation == OP_SUB_I64)
                    ok = vm_checked_sub(left, immediate, &result);
                else if (scalar_operation == OP_MUL_I64)
                    ok = vm_checked_mul(left, immediate, &result);
                else if (scalar_operation == OP_DIV_I64) {
                    if (immediate == 0) {
                        runtime_error(
                            vm, instruction,
                            "division by zero");
                        goto fail;
                    }
                    if (left == INT64_MIN && immediate == -1)
                        ok = false;
                    else
                        result = left / immediate;
                } else if (scalar_operation == OP_REM_I64) {
                    if (immediate == 0) {
                        runtime_error(
                            vm, instruction,
                            "division by zero");
                        goto fail;
                    }
                    if (left == INT64_MIN && immediate == -1)
                        result = 0;
                    else
                        result = left % immediate;
                } else {
                    ok = false;
                }
                if (ok)
                    ok = vm_signed_value_fits_type(
                        result, (TypeKind)scalar_type);
                if (!ok) {
                    runtime_error(
                        vm, instruction,
                        "integer overflow or invalid operands");
                    goto fail;
                }
                LOCAL(scalar_destination).tag =
                    LANG_VALUE_I64;
                LOCAL(scalar_destination).as.i64 = result;
                initialized[scalar_destination] = true;
                break;
            }
            VM_LABEL(binary_locals)
            case OP_BINARY_LOCALS: {
                uint32_t packed_slots = (uint32_t)instruction.b;
                size_t left_slot =
                    (size_t)(packed_slots & UINT32_C(0x3ff));
                size_t right_slot =
                    (size_t)((packed_slots >> 10U) & UINT32_C(0x3ff));
                scalar_destination =
                    (size_t)((packed_slots >> 20U) & UINT32_C(0x3ff));
                if (!initialized[left_slot] ||
                    !initialized[right_slot]) {
                    runtime_error(
                        vm, instruction,
                        "binary operation used an unavailable local");
                    goto fail;
                }
                uint32_t packed_operation =
                    (uint32_t)instruction.a;
                scalar_operation =
                    (OpCode)(packed_operation & UINT32_C(0xff));
                scalar_type =
                    (int32_t)(packed_operation >> 8U);
                if (LOCAL(left_slot).tag == LANG_VALUE_I64 &&
                    LOCAL(right_slot).tag == LANG_VALUE_I64 &&
                    scalar_operation >= OP_ADD_I64 &&
                    scalar_operation <= OP_MUL_I64) {
                    int64_t result;
                    bool ok;
                    if (scalar_operation == OP_ADD_I64)
                        ok = vm_checked_add(
                            LOCAL(left_slot).as.i64,
                            LOCAL(right_slot).as.i64,
                            &result);
                    else if (scalar_operation == OP_SUB_I64)
                        ok = vm_checked_sub(
                            LOCAL(left_slot).as.i64,
                            LOCAL(right_slot).as.i64,
                            &result);
                    else
                        ok = vm_checked_mul(
                            LOCAL(left_slot).as.i64,
                            LOCAL(right_slot).as.i64,
                            &result);
                    if (!ok || !vm_signed_value_fits_type(
                            result, (TypeKind)scalar_type)) {
                        runtime_error(
                            vm, instruction,
                            "integer overflow or invalid operands");
                        goto fail;
                    }
                    LOCAL(scalar_destination).tag =
                        LANG_VALUE_I64;
                    LOCAL(scalar_destination).as.i64 =
                        result;
                    initialized[scalar_destination] = true;
                    break;
                }
                PUSH(LOCAL(left_slot));
                PUSH(LOCAL(right_slot));
                /* Continue through the ordinary checked implementation. */
            }
#if defined(LANG_VM_COMPUTED_GOTO)
            goto vm_dispatch_integer_binary;
#else
            goto vm_switch_integer_binary;
#endif
            VM_LABEL(binary_locals_immediate)
            case OP_BINARY_LOCALS_IMMEDIATE: {
                uint32_t slots = (uint32_t)instruction.b;
                size_t left =
                    (size_t)(slots & UINT32_C(0x3ff));
                size_t right = (size_t)(
                    (slots >> 10U) & UINT32_C(0x3ff));
                size_t destination = (size_t)(
                    (slots >> 20U) & UINT32_C(0x3ff));
                uint32_t packed = (uint32_t)instruction.a;
                OpCode first =
                    (OpCode)(packed & UINT32_C(0xff));
                TypeKind type = (TypeKind)(
                    (packed >> 8U) & UINT32_C(0xff));
                OpCode second = (OpCode)(
                    (packed >> 16U) & UINT32_C(0xff));
                int64_t immediate =
                    (int64_t)(int8_t)(packed >> 24U);
                if (!initialized[left] || !initialized[right] ||
                    LOCAL(left).tag != LANG_VALUE_I64 ||
                    LOCAL(right).tag != LANG_VALUE_I64) {
                    runtime_error(
                        vm, instruction,
                        "fused binary operation used invalid locals");
                    goto fail;
                }
                int64_t intermediate = 0;
                int64_t result = 0;
                bool ok;
                if (first == OP_ADD_I64)
                    ok = vm_checked_add(
                        LOCAL(left).as.i64,
                        LOCAL(right).as.i64, &intermediate);
                else if (first == OP_SUB_I64)
                    ok = vm_checked_sub(
                        LOCAL(left).as.i64,
                        LOCAL(right).as.i64, &intermediate);
                else
                    ok = vm_checked_mul(
                        LOCAL(left).as.i64,
                        LOCAL(right).as.i64, &intermediate);
                if (ok && second == OP_ADD_I64)
                    ok = vm_checked_add(intermediate, immediate, &result);
                else if (ok && second == OP_SUB_I64)
                    ok = vm_checked_sub(intermediate, immediate, &result);
                else if (ok)
                    ok = vm_checked_mul(intermediate, immediate, &result);
                if (!ok || !vm_signed_value_fits_type(result, type)) {
                    runtime_error(
                        vm, instruction,
                        "integer overflow in fused binary operation");
                    goto fail;
                }
                LOCAL(destination) = (LangValue){
                    .tag=LANG_VALUE_I64, .as.i64=result
                };
                initialized[destination] = true;
                break;
            }
#if !defined(LANG_VM_COMPUTED_GOTO)
vm_switch_integer_binary:
#endif
            VM_LABEL(integer_binary)
            case OP_ADD_I64: case OP_SUB_I64: case OP_MUL_I64:
            case OP_DIV_I64: case OP_REM_I64: {
                LangValue right = POP(), left = POP();
                if (left.tag == LANG_VALUE_U64 &&
                    right.tag == LANG_VALUE_U64) {
                    uint64_t result = 0U;
                    bool ok = true;
                    if (scalar_operation == OP_ADD_I64) {
                        ok = left.as.u64 <= UINT64_MAX - right.as.u64;
                        if (ok) result = left.as.u64 + right.as.u64;
                    } else if (scalar_operation == OP_SUB_I64) {
                        ok = left.as.u64 >= right.as.u64;
                        if (ok) result = left.as.u64 - right.as.u64;
                    } else if (scalar_operation == OP_MUL_I64) {
                        ok = right.as.u64 == 0U ||
                             left.as.u64 <= UINT64_MAX / right.as.u64;
                        if (ok) result = left.as.u64 * right.as.u64;
                    } else {
                        if (right.as.u64 == 0U) {
                            runtime_error(vm, instruction,
                                          "division by zero");
                            goto fail;
                        }
                        result = scalar_operation == OP_DIV_I64
                               ? left.as.u64 / right.as.u64
                               : left.as.u64 % right.as.u64;
                    }
                    ok = ok && vm_unsigned_value_fits_type(
                        result, (TypeKind)scalar_type);
                    if (!ok) {
                        runtime_error(vm, instruction,
                                      "integer overflow or invalid operands");
                        goto fail;
                    }
                    STORE_SCALAR_RESULT(((LangValue){
                        .tag=LANG_VALUE_U64, .as.u64=result
                    }));
                    break;
                }
                LangValue result = {.tag=LANG_VALUE_I64};
                bool ok = left.tag == LANG_VALUE_I64 && right.tag == LANG_VALUE_I64;
                if (ok && scalar_operation == OP_ADD_I64) ok = vm_checked_add(left.as.i64, right.as.i64, &result.as.i64);
                else if (ok && scalar_operation == OP_SUB_I64) ok = vm_checked_sub(left.as.i64, right.as.i64, &result.as.i64);
                else if (ok && scalar_operation == OP_MUL_I64) ok = vm_checked_mul(left.as.i64, right.as.i64, &result.as.i64);
                else if (ok && scalar_operation == OP_DIV_I64) {
                    if (right.as.i64 == 0) { runtime_error(vm, instruction, "division by zero"); goto fail; }
                    if (left.as.i64 == INT64_MIN && right.as.i64 == -1) ok = false;
                    else result.as.i64 = left.as.i64 / right.as.i64;
                } else if (ok) {
                    if (right.as.i64 == 0) { runtime_error(vm, instruction, "division by zero"); goto fail; }
                    result.as.i64 = left.as.i64 % right.as.i64;
                }
                if (ok)
                    ok = vm_signed_value_fits_type(
                        result.as.i64, (TypeKind)scalar_type);
                if (!ok) { runtime_error(vm, instruction, "integer overflow or invalid operands"); goto fail; }
                STORE_SCALAR_RESULT(result); break;
            }
            VM_LABEL(shift)
            case OP_SHIFT_LEFT: case OP_SHIFT_RIGHT: {
                LangValue right = POP();
                LangValue left = POP();
                uint64_t count;
                if (right.tag == LANG_VALUE_I64) {
                    if (right.as.i64 < 0) {
                        runtime_error(vm, instruction,
                                      "invalid shift count");
                        goto fail;
                    }
                    count = (uint64_t)right.as.i64;
                } else if (right.tag == LANG_VALUE_U64) {
                    count = right.as.u64;
                } else {
                    runtime_error(vm, instruction,
                                  "invalid shift operands");
                    goto fail;
                }
                unsigned width = vm_runtime_integer_width(
                    (TypeKind)instruction.a);
                if (width == 0U || count >= (uint64_t)width) {
                    runtime_error(vm, instruction,
                                  "invalid shift count");
                    goto fail;
                }
                if (left.tag == LANG_VALUE_U64) {
                    uint64_t result;
                    if (instruction.op == OP_SHIFT_RIGHT) {
                        result = left.as.u64 >> (unsigned)count;
                    } else {
                        result = left.as.u64 << (unsigned)count;
                        if ((count != 0U &&
                             (result >> (unsigned)count) != left.as.u64) ||
                            !vm_unsigned_value_fits_type(
                                result, (TypeKind)instruction.a)) {
                            runtime_error(vm, instruction,
                                          "integer shift overflow");
                            goto fail;
                        }
                    }
                    PUSH(((LangValue){
                        .tag=LANG_VALUE_U64, .as.u64=result
                    }));
                    break;
                }
                if (left.tag != LANG_VALUE_I64) {
                    runtime_error(vm, instruction,
                                  "invalid shift operands");
                    goto fail;
                }
                int64_t result;
                if (instruction.op == OP_SHIFT_RIGHT) {
                    if (count == 63U) {
                        result = left.as.i64 < 0 ? -1 : 0;
                    } else {
                        int64_t divisor =
                            INT64_C(1) << (unsigned)count;
                        result = left.as.i64 / divisor;
                        if (left.as.i64 < 0 &&
                            left.as.i64 % divisor != 0)
                            --result;
                    }
                } else if (count == 63U) {
                    if (left.as.i64 != 0) {
                        runtime_error(vm, instruction,
                                      "integer shift overflow");
                        goto fail;
                    }
                    result = 0;
                } else {
                    int64_t factor =
                        INT64_C(1) << (unsigned)count;
                    if (!vm_checked_mul(left.as.i64, factor, &result) ||
                        !vm_signed_value_fits_type(
                            result, (TypeKind)instruction.a)) {
                        runtime_error(vm, instruction,
                                      "integer shift overflow");
                        goto fail;
                    }
                }
                PUSH(((LangValue){
                    .tag=LANG_VALUE_I64, .as.i64=result
                }));
                break;
            }
            VM_LABEL(bitwise)
            case OP_BIT_AND: case OP_BIT_OR: case OP_BIT_XOR: {
                LangValue right = POP();
                LangValue left = POP();
                TypeKind kind = (TypeKind)instruction.a;
                unsigned width = vm_runtime_integer_width(kind);
                bool unsigned_kind =
                    kind == TYPE_U8 || kind == TYPE_U16 ||
                    kind == TYPE_U32 || kind == TYPE_U64 ||
                    kind == TYPE_USIZE;
                if (width == 0U ||
                    (unsigned_kind &&
                     (left.tag != LANG_VALUE_U64 ||
                      right.tag != LANG_VALUE_U64)) ||
                    (!unsigned_kind &&
                     (left.tag != LANG_VALUE_I64 ||
                      right.tag != LANG_VALUE_I64))) {
                    runtime_error(
                        vm, instruction, "invalid bitwise operands");
                    goto fail;
                }
                uint64_t left_bits = unsigned_kind
                    ? left.as.u64 : (uint64_t)left.as.i64;
                uint64_t right_bits = unsigned_kind
                    ? right.as.u64 : (uint64_t)right.as.i64;
                uint64_t bits =
                    instruction.op == OP_BIT_AND
                        ? left_bits & right_bits :
                    instruction.op == OP_BIT_OR
                        ? left_bits | right_bits :
                        left_bits ^ right_bits;
                STORE_SCALAR_RESULT(
                    vm_runtime_value_from_integer_bits(bits, kind));
                break;
            }
            VM_LABEL(bitwise_not)
            case OP_BIT_NOT: {
                LangValue operand = POP();
                TypeKind kind = (TypeKind)instruction.a;
                unsigned width = vm_runtime_integer_width(kind);
                bool unsigned_kind =
                    kind == TYPE_U8 || kind == TYPE_U16 ||
                    kind == TYPE_U32 || kind == TYPE_U64 ||
                    kind == TYPE_USIZE;
                if (width == 0U ||
                    (unsigned_kind &&
                     operand.tag != LANG_VALUE_U64) ||
                    (!unsigned_kind &&
                     operand.tag != LANG_VALUE_I64)) {
                    runtime_error(
                        vm, instruction, "invalid bitwise operand");
                    goto fail;
                }
                uint64_t bits = unsigned_kind
                    ? operand.as.u64 : (uint64_t)operand.as.i64;
                STORE_SCALAR_RESULT(
                    vm_runtime_value_from_integer_bits(~bits, kind));
                break;
            }
            VM_LABEL(float_binary)
            case OP_ADD_F64: case OP_SUB_F64:
            case OP_MUL_F64: case OP_DIV_F64: {
                LangValue right = POP(), left = POP();
                if (left.tag != LANG_VALUE_F64 ||
                    right.tag != LANG_VALUE_F64) {
                    runtime_error(vm, instruction,
                                  "invalid floating-point operands");
                    goto fail;
                }
                if (instruction.op == OP_DIV_F64 && right.as.f64 == 0.0) {
                    runtime_error(vm, instruction, "division by zero");
                    goto fail;
                }
                double result;
                if (scalar_operation == OP_ADD_F64)
                    result = left.as.f64 + right.as.f64;
                else if (scalar_operation == OP_SUB_F64)
                    result = left.as.f64 - right.as.f64;
                else if (scalar_operation == OP_MUL_F64)
                    result = left.as.f64 * right.as.f64;
                else
                    result = left.as.f64 / right.as.f64;
                double maximum =
                    (TypeKind)scalar_type == TYPE_F32
                    ? (double)FLT_MAX : DBL_MAX;
                if (result > maximum || result < -maximum ||
                    result != result) {
                    runtime_error(vm, instruction,
                                  "floating-point overflow or invalid result");
                    goto fail;
                }
                if ((TypeKind)scalar_type == TYPE_F32)
                    result = (double)(float)result;
                PUSH(((LangValue){
                    .tag=LANG_VALUE_F64, .as.f64=result
                }));
                break;
            }
            VM_LABEL(neg_i64)
            case OP_NEG_I64: {
                LangValue value = POP();
                if (value.tag != LANG_VALUE_I64 || value.as.i64 == INT64_MIN) {
                    runtime_error(vm, instruction, "integer negation overflow"); goto fail;
                }
                value.as.i64 = -value.as.i64;
                if (!vm_signed_value_fits_type(
                        value.as.i64, (TypeKind)instruction.a)) {
                    runtime_error(vm, instruction,
                                  "integer negation overflow");
                    goto fail;
                }
                PUSH(value); break;
            }
            VM_LABEL(neg_f64)
            case OP_NEG_F64: {
                LangValue value = POP();
                if (value.tag != LANG_VALUE_F64) {
                    runtime_error(vm, instruction,
                                  "invalid floating-point operand");
                    goto fail;
                }
                value.as.f64 = -value.as.f64;
                PUSH(value);
                break;
            }
            VM_LABEL(not)
            case OP_NOT: {
                LangValue value = POP();
                PUSH(((LangValue){.tag=LANG_VALUE_BOOL,.as.boolean=!value.as.boolean})); break;
            }
            VM_LABEL(cast)
            case OP_CAST: {
                LangValue input = POP();
                LangValue output;
                if (!vm_cast_numeric_value(
                        input, (TypeKind)instruction.b, &output)) {
                    runtime_error(vm, instruction,
                                  "numeric cast is out of range");
                    goto fail;
                }
                PUSH(output);
                break;
            }
            VM_LABEL(compare)
            case OP_EQ: case OP_NEQ: case OP_LT_I64: case OP_LE_I64:
            case OP_GT_I64: case OP_GE_I64: {
                LangValue right = POP(), left = POP();
                bool result = false;
                if (left.tag == LANG_VALUE_I64 && right.tag == LANG_VALUE_I64) {
                    if (scalar_operation == OP_EQ) result = left.as.i64 == right.as.i64;
                    else if (scalar_operation == OP_NEQ) result = left.as.i64 != right.as.i64;
                    else if (scalar_operation == OP_LT_I64) result = left.as.i64 < right.as.i64;
                    else if (scalar_operation == OP_LE_I64) result = left.as.i64 <= right.as.i64;
                    else if (scalar_operation == OP_GT_I64) result = left.as.i64 > right.as.i64;
                    else result = left.as.i64 >= right.as.i64;
                } else if (left.tag == LANG_VALUE_U64 &&
                           right.tag == LANG_VALUE_U64) {
                    if (scalar_operation == OP_EQ)
                        result = left.as.u64 == right.as.u64;
                    else if (scalar_operation == OP_NEQ)
                        result = left.as.u64 != right.as.u64;
                    else if (scalar_operation == OP_LT_I64)
                        result = left.as.u64 < right.as.u64;
                    else if (scalar_operation == OP_LE_I64)
                        result = left.as.u64 <= right.as.u64;
                    else if (scalar_operation == OP_GT_I64)
                        result = left.as.u64 > right.as.u64;
                    else
                        result = left.as.u64 >= right.as.u64;
                } else if (left.tag == LANG_VALUE_F64 &&
                           right.tag == LANG_VALUE_F64) {
                    if (scalar_operation == OP_EQ)
                        result = left.as.f64 == right.as.f64;
                    else if (scalar_operation == OP_NEQ)
                        result = left.as.f64 != right.as.f64;
                    else if (scalar_operation == OP_LT_I64)
                        result = left.as.f64 < right.as.f64;
                    else if (scalar_operation == OP_LE_I64)
                        result = left.as.f64 <= right.as.f64;
                    else if (scalar_operation == OP_GT_I64)
                        result = left.as.f64 > right.as.f64;
                    else
                        result = left.as.f64 >= right.as.f64;
                } else if (left.tag == LANG_VALUE_BOOL && right.tag == LANG_VALUE_BOOL) {
                    result = scalar_operation == OP_EQ ? left.as.boolean == right.as.boolean
                                                    : left.as.boolean != right.as.boolean;
                } else if (left.tag == LANG_VALUE_RAW_POINTER &&
                           right.tag == LANG_VALUE_RAW_POINTER &&
                           (scalar_operation == OP_EQ ||
                            scalar_operation == OP_NEQ)) {
                    bool equal = left.as.pointer == right.as.pointer;
                    result = scalar_operation == OP_EQ ? equal : !equal;
                } else if ((scalar_operation == OP_EQ ||
                            scalar_operation == OP_NEQ) &&
                           (vm_is_payloadless_variant(left) ||
                            vm_is_payloadless_variant(right))) {
                    bool equal = vm_payloadless_variants_equal(left, right);
                    result = scalar_operation == OP_EQ ? equal : !equal;
                } else if (scalar_operation == OP_EQ ||
                           scalar_operation == OP_NEQ) {
                    LangStringView left_string;
                    LangStringView right_string;
                    bool strings =
                        lang_value_string_view(&left, &left_string) &&
                        lang_value_string_view(&right, &right_string);
                    bool equal = strings &&
                        left_string.length == right_string.length &&
                        (left_string.length == 0U ||
                         memcmp(left_string.data, right_string.data,
                                left_string.length) == 0);
                    result = scalar_operation == OP_EQ ? equal : !equal;
                }
                vm_value_drop_owned(vm, left);
                vm_value_drop_owned(vm, right);
                STORE_SCALAR_RESULT(((LangValue){
                    .tag=LANG_VALUE_BOOL,.as.boolean=result
                }));
                break;
            }
            VM_LABEL(jump)
            case OP_JUMP: ip = (size_t)instruction.a; break;
            VM_LABEL(jump_if_false)
            case OP_JUMP_IF_FALSE: {
                LangValue condition = POP();
                if (!condition.as.boolean) ip = (size_t)instruction.a;
                break;
            }
            VM_LABEL(compare_branch)
            case OP_COMPARE_BRANCH: {
                uint32_t packed = (uint32_t)instruction.b;
                size_t left_slot =
                    (size_t)(packed & UINT32_C(0x3ff));
                size_t right_slot = (size_t)(
                    (packed >> 10U) & UINT32_C(0x3ff));
                OpCode operation = (OpCode)(
                    (packed >> 20U) & UINT32_C(0x3f));
                if (!initialized[left_slot] ||
                    !initialized[right_slot]) {
                    runtime_error(
                        vm, instruction,
                        "comparison used an unavailable local");
                    goto fail;
                }
                LangValue left = LOCAL(left_slot);
                LangValue right = LOCAL(right_slot);
                initialized[left_slot] = false;
                initialized[right_slot] = false;
                bool result;
                if (left.tag == LANG_VALUE_I64 &&
                    right.tag == LANG_VALUE_I64) {
                    if (operation == OP_EQ)
                        result = left.as.i64 == right.as.i64;
                    else if (operation == OP_NEQ)
                        result = left.as.i64 != right.as.i64;
                    else if (operation == OP_LT_I64)
                        result = left.as.i64 < right.as.i64;
                    else if (operation == OP_LE_I64)
                        result = left.as.i64 <= right.as.i64;
                    else if (operation == OP_GT_I64)
                        result = left.as.i64 > right.as.i64;
                    else
                        result = left.as.i64 >= right.as.i64;
                } else if (left.tag == LANG_VALUE_U64 &&
                           right.tag == LANG_VALUE_U64) {
                    if (operation == OP_EQ)
                        result = left.as.u64 == right.as.u64;
                    else if (operation == OP_NEQ)
                        result = left.as.u64 != right.as.u64;
                    else if (operation == OP_LT_I64)
                        result = left.as.u64 < right.as.u64;
                    else if (operation == OP_LE_I64)
                        result = left.as.u64 <= right.as.u64;
                    else if (operation == OP_GT_I64)
                        result = left.as.u64 > right.as.u64;
                    else
                        result = left.as.u64 >= right.as.u64;
                } else {
                    runtime_error(
                        vm, instruction,
                        "invalid integer comparison operands");
                    goto fail;
                }
                if (!result)
                    ip = (size_t)instruction.a;
                break;
            }
            VM_LABEL(compare_local_constant_branch)
            case OP_COMPARE_LOCAL_CONSTANT_BRANCH: {
                uint32_t packed = (uint32_t)instruction.b;
                size_t local_slot =
                    (size_t)(packed & UINT32_C(0x3ff));
                size_t constant_index = (size_t)(
                    (packed >> 10U) & UINT32_C(0xffff));
                OpCode operation = (OpCode)(packed >> 26U);
                if (!initialized[local_slot]) {
                    runtime_error(
                        vm, instruction,
                        "comparison used an unavailable local");
                    goto fail;
                }
                LangValue left = LOCAL(local_slot);
                LangValue right =
                    vm->module->constants[constant_index].value;
                bool result;
                if (left.tag == LANG_VALUE_I64 &&
                    right.tag == LANG_VALUE_I64) {
                    if (operation == OP_EQ)
                        result = left.as.i64 == right.as.i64;
                    else if (operation == OP_NEQ)
                        result = left.as.i64 != right.as.i64;
                    else if (operation == OP_LT_I64)
                        result = left.as.i64 < right.as.i64;
                    else if (operation == OP_LE_I64)
                        result = left.as.i64 <= right.as.i64;
                    else if (operation == OP_GT_I64)
                        result = left.as.i64 > right.as.i64;
                    else
                        result = left.as.i64 >= right.as.i64;
                } else if (left.tag == LANG_VALUE_U64 &&
                           right.tag == LANG_VALUE_U64) {
                    if (operation == OP_EQ)
                        result = left.as.u64 == right.as.u64;
                    else if (operation == OP_NEQ)
                        result = left.as.u64 != right.as.u64;
                    else if (operation == OP_LT_I64)
                        result = left.as.u64 < right.as.u64;
                    else if (operation == OP_LE_I64)
                        result = left.as.u64 <= right.as.u64;
                    else if (operation == OP_GT_I64)
                        result = left.as.u64 > right.as.u64;
                    else
                        result = left.as.u64 >= right.as.u64;
                } else {
                    runtime_error(
                        vm, instruction,
                        "invalid integer comparison operands");
                    goto fail;
                }
                if (!result)
                    ip = (size_t)instruction.a;
                break;
            }
            VM_LABEL(function)
            case OP_FUNCTION:
                PUSH(((LangValue){
                    .tag=LANG_VALUE_FUNCTION,
                    .as.function=(size_t)instruction.a
                }));
                break;
            VM_LABEL(bound_function)
            case OP_BOUND_FUNCTION: {
                LangValue receiver = POP();
                if (receiver.tag != LANG_VALUE_RAW_POINTER ||
                    receiver.as.pointer == NULL) {
                    runtime_error(
                        vm, instruction,
                        "cannot bind an instance method to a null class reference");
                    goto fail;
                }
                size_t target = (size_t)instruction.a;
                if (instruction.b != 0) {
                    Object *object = receiver.as.pointer;
                    const char *metadata = object->as.structure.metadata;
                    size_t metadata_length = strcspn(metadata, "|");
                    const char *runtime_type = metadata;
                    const char *separator = memchr(
                        metadata, '#', metadata_length);
                    size_t module_length = separator != NULL
                        ? (size_t)(separator - metadata) : 0U;
                    if (separator != NULL) runtime_type = separator + 1U;
                    size_t type_length = metadata_length -
                        (size_t)(runtime_type - metadata);
                    for (size_t entry = 0U;
                         entry < vm->module->virtual_entry_count; ++entry) {
                        const BytecodeVirtualEntry *candidate =
                            &vm->module->virtual_entries[entry];
                        if (candidate->root_function == target &&
                            candidate->runtime_module_length == module_length &&
                            candidate->runtime_type_length == type_length &&
                            (module_length == 0U ||
                             strncmp(candidate->runtime_module, metadata,
                                     module_length) == 0) &&
                            strncmp(candidate->runtime_type, runtime_type,
                                    type_length) == 0) {
                            target = candidate->target_function;
                            break;
                        }
                    }
                }
                PUSH(((LangValue){
                    .tag=LANG_VALUE_BOUND_FUNCTION,
                    .as.bound_function={
                        .function=target,
                        .receiver=receiver.as.pointer
                    }
                }));
                break;
            }
            VM_LABEL(call)
            case OP_CALL: {
                vm->active_span = instruction_span;
                size_t count = (size_t)instruction.b;
                const BytecodeCallSite *call_site =
                    &function->call_sites[instruction_index];
                LangValue *args = &stack[sp - count];
                LangValue result;
                if (instruction.a < 0) {
                    if (!vm_call_builtin(vm, instruction.a, args, count, &result,
                                      instruction_span))
                        goto fail;
                    for (size_t i = 0U; i < count; ++i)
                        if (i >= call_site->argument_count ||
                            !parameter_mode_is_reference(
                                call_site->argument_modes[i]))
                            vm_value_drop_owned(vm, args[i]);
                } else {
                    if (vm->frame_count >= 128U) {
                        runtime_error(vm, instruction, "maximum call depth exceeded");
                        goto fail;
                    }
                    const BytecodeFunction *callee =
                        &vm->module->functions[
                            (size_t)instruction.a];
                    if (callee->fast_scalar_leaf) {
                        if (!vm_execute_fast_scalar_leaf(
                                vm, (size_t)instruction.a,
                                args, count, instruction_span,
                                &result))
                            goto fail;
                    } else {
                        result = vm_execute_function(
                            vm, (size_t)instruction.a,
                            args, count, instruction_span);
                    }
                    vm->active_span = instruction_span;
                    if (vm->trapped) goto fail;
                }
                sp -= count; PUSH(result); break;
            }
            VM_LABEL(call_virtual)
            case OP_CALL_VIRTUAL: {
                vm->active_span = instruction_span;
                size_t count = (size_t)instruction.b;
                if (count == 0U || sp < count) {
                    runtime_error(vm, instruction,
                                  "virtual call requires a receiver");
                    goto fail;
                }
                LangValue *args = &stack[sp - count];
                Object *receiver = args[0].tag == LANG_VALUE_RAW_POINTER
                    ? args[0].as.pointer : NULL;
                if (receiver == NULL || receiver->kind != OBJECT_STRUCT) {
                    runtime_error(vm, instruction,
                                  "virtual call requires a class receiver");
                    goto fail;
                }
                const char *metadata = receiver->as.structure.metadata;
                size_t metadata_length = strcspn(metadata, "|");
                const char *runtime_type = metadata;
                const char *module_separator = memchr(
                    metadata, '#', metadata_length);
                size_t runtime_module_length = module_separator != NULL
                    ? (size_t)(module_separator - metadata) : 0U;
                if (module_separator != NULL)
                    runtime_type = module_separator + 1U;
                size_t runtime_length = metadata_length -
                    (size_t)(runtime_type - metadata);
                size_t target = (size_t)instruction.a;
                for (size_t entry = 0U;
                     entry < vm->module->virtual_entry_count; ++entry) {
                    const BytecodeVirtualEntry *candidate =
                        &vm->module->virtual_entries[entry];
                    if (candidate->root_function != target ||
                        candidate->runtime_module_length !=
                            runtime_module_length ||
                        (runtime_module_length != 0U &&
                         strncmp(candidate->runtime_module, metadata,
                                 runtime_module_length) != 0) ||
                        candidate->runtime_type_length != runtime_length ||
                        strncmp(candidate->runtime_type, runtime_type,
                                runtime_length) != 0)
                        continue;
                    target = candidate->target_function;
                    break;
                }
                if (vm->frame_count >= 128U) {
                    runtime_error(vm, instruction,
                                  "maximum call depth exceeded");
                    goto fail;
                }
                LangValue result = vm_execute_function(
                    vm, target, args, count, instruction_span);
                vm->active_span = instruction_span;
                if (vm->trapped) goto fail;
                sp -= count;
                PUSH(result);
                break;
            }
            VM_LABEL(call_local)
            case OP_CALL_LOCAL: {
                vm->active_span = instruction_span;
                uint32_t encoded = (uint32_t)instruction.b;
                size_t count =
                    (size_t)(encoded & UINT32_C(0x3ff));
                size_t destination =
                    (size_t)((encoded >> 10U) &
                             UINT32_C(0x3ff));
                if (vm->frame_count >= 128U) {
                    runtime_error(
                        vm, instruction,
                        "maximum call depth exceeded");
                    goto fail;
                }
                LangValue result;
                const BytecodeFunction *callee =
                    &vm->module->functions[
                        (size_t)instruction.a];
                if (callee->fast_scalar_leaf) {
                    if (!vm_execute_fast_scalar_leaf(
                            vm, (size_t)instruction.a,
                            &stack[sp - count], count,
                            instruction_span, &result))
                        goto fail;
                } else {
                    result = vm_execute_function(
                        vm, (size_t)instruction.a,
                        &stack[sp - count], count,
                        instruction_span);
                }
                vm->active_span = instruction_span;
                if (vm->trapped)
                    goto fail;
                sp -= count;
                if (initialized[destination])
                    drop_runtime_value(
                        vm, function, destination,
                        LOCAL(destination));
                LOCAL(destination) = result;
                initialized[destination] = true;
                break;
            }
            VM_LABEL(call_local_2_copy)
            case OP_CALL_LOCAL_2_COPY: {
                vm->active_span = instruction_span;
                uint32_t packed = (uint32_t)instruction.b;
                size_t first_slot =
                    (size_t)(packed & UINT32_C(0x3ff));
                size_t second_slot = (size_t)(
                    (packed >> 10U) & UINT32_C(0x3ff));
                size_t destination = (size_t)(
                    (packed >> 20U) & UINT32_C(0x3ff));
                if (!initialized[first_slot] ||
                    !initialized[second_slot]) {
                    runtime_error(
                        vm, instruction,
                        "call copied an unavailable local");
                    goto fail;
                }
                if (vm->frame_count >= 128U) {
                    runtime_error(
                        vm, instruction,
                        "maximum call depth exceeded");
                    goto fail;
                }
                LangValue call_arguments[2] = {
                    LOCAL(first_slot), LOCAL(second_slot)
                };
                LangValue result;
                const BytecodeFunction *callee =
                    &vm->module->functions[
                        (size_t)instruction.a];
                if (callee->fast_scalar_leaf) {
                    if (!vm_execute_fast_scalar_leaf(
                            vm, (size_t)instruction.a,
                            call_arguments, 2U,
                            instruction_span, &result))
                        goto fail;
                } else {
                    result = vm_execute_function(
                        vm, (size_t)instruction.a,
                        call_arguments, 2U,
                        instruction_span);
                }
                vm->active_span = instruction_span;
                if (vm->trapped)
                    goto fail;
                if (initialized[destination])
                    drop_runtime_value(
                        vm, function, destination,
                        LOCAL(destination));
                LOCAL(destination) = result;
                initialized[destination] = true;
                break;
            }
            VM_LABEL(call_indirect)
            case OP_CALL_INDIRECT: {
                vm->active_span = instruction_span;
                size_t count = (size_t)instruction.a;
                LangValue callee = POP();
                size_t callee_function =
                    callee.tag == LANG_VALUE_FUNCTION
                        ? callee.as.function
                    : callee.tag == LANG_VALUE_BOUND_FUNCTION
                        ? callee.as.bound_function.function
                        : SIZE_MAX;
                if (callee_function >= vm->module->function_count) {
                    runtime_error(
                        vm, instruction,
                        "indirect call requires a valid function value");
                    goto fail;
                }
                if (vm->frame_count >= 128U) {
                    runtime_error(
                        vm, instruction,
                        "maximum call depth exceeded");
                    goto fail;
                }
                LangValue *args = &stack[sp - count];
                LangValue result = vm_invoke_function_value(
                    vm, callee, args, count, instruction_span);
                vm->active_span = instruction_span;
                if (vm->trapped) goto fail;
                sp -= count;
                PUSH(result);
                break;
            }
            VM_LABEL(call_native)
            case OP_CALL_NATIVE: {
                vm->active_span = instruction_span;
                size_t count = (size_t)instruction.b;
                const BytecodeCallSite *call_site =
                    &function->call_sites[instruction_index];
                LangStringView name =
                    vm->module->constants[(size_t)instruction.a].value.as.string;
                char native_name[256];
                if (name.length >= sizeof(native_name)) {
                    runtime_error(vm, instruction, "native function name is too long");
                    goto fail;
                }
                if (name.length != 0U)
                    memcpy(native_name, name.data, name.length);
                native_name[name.length] = '\0';
                LangNativeResult native_result;
                if (!lang_vm_call_native(vm, native_name, &stack[sp - count],
                                         count, &native_result)) {
                    for (size_t i = 0U; i < count; ++i)
                        if (i >= call_site->argument_count ||
                            !parameter_mode_is_reference(
                                call_site->argument_modes[i]))
                            vm_value_drop_owned(vm, stack[sp - count + i]);
                    sp -= count;
                    runtime_error(vm, instruction, "unregistered native function");
                    goto fail;
                }
                if (!native_result.ok) {
                    for (size_t i = 0U; i < count; ++i)
                        if (i >= call_site->argument_count ||
                            !parameter_mode_is_reference(
                                call_site->argument_modes[i]))
                            vm_value_drop_owned(vm, stack[sp - count + i]);
                    sp -= count;
                    const char *message =
                        lang_native_result_error_message(&native_result);
                    vm_raise_exception_message(
                        vm, message != NULL ? message
                                            : "native function failed");
                    lang_native_result_drop(&native_result);
                    PUSH(((LangValue){.tag=LANG_VALUE_UNIT}));
                    break;
                }
                for (size_t i = 0U; i < count; ++i)
                    if (i >= call_site->argument_count ||
                        !parameter_mode_is_reference(
                            call_site->argument_modes[i]))
                        vm_value_drop_owned(vm, stack[sp - count + i]);
                sp -= count;
                PUSH(native_result.value);
                break;
            }
            VM_LABEL(task_delay)
            case OP_TASK_DELAY: {
                vm->active_span = instruction_span;
                Object *cancellation = NULL;
                if (instruction.a != 0) {
                    if (sp < 2U || stack[sp - 1U].tag != LANG_VALUE_OBJECT) {
                        runtime_error(vm, instruction,
                                      "Task.Delay cancellation argument requires a CancellationToken");
                        goto fail;
                    }
                    cancellation = POP().as.object;
                    if (cancellation != NULL &&
                        cancellation->kind != OBJECT_CANCELLATION) {
                        vm_object_free(vm, cancellation);
                        runtime_error(vm, instruction,
                                      "Task.Delay requires a CancellationToken");
                        goto fail;
                    }
                }
                if (sp == 0U || stack[sp - 1U].tag != LANG_VALUE_I64) {
                    vm_object_free(vm, cancellation);
                    runtime_error(vm, instruction,
                                  "Task.Delay requires an int milliseconds argument");
                    goto fail;
                }
                int64_t milliseconds = POP().as.i64;
                LangValue delayed = vm_task_delay(
                    vm, milliseconds, cancellation, instruction_span);
                if (vm->trapped) goto fail;
                PUSH(delayed);
                break;
            }
            VM_LABEL(cancellation_source_new)
            case OP_CANCELLATION_SOURCE_NEW: {
                Object *state = vm_allocate(1U, sizeof(*state));
                state->kind = OBJECT_CANCELLATION;
                state->references = 1U;
                PUSH(((LangValue){.tag=LANG_VALUE_OBJECT,
                                  .as.object=state}));
                break;
            }
            VM_LABEL(cancellation_token_none)
            case OP_CANCELLATION_TOKEN_NONE:
                PUSH(((LangValue){.tag=LANG_VALUE_OBJECT,
                                  .as.object=NULL}));
                break;
            VM_LABEL(cancellation_token_get)
            case OP_CANCELLATION_TOKEN_GET: {
                if (sp == 0U || stack[sp - 1U].tag != LANG_VALUE_OBJECT) {
                    runtime_error(vm, instruction,
                                  "CancellationTokenSource.Token requires a source");
                    goto fail;
                }
                Object *state = stack[sp - 1U].as.object;
                if (state == NULL || state->kind != OBJECT_CANCELLATION) {
                    vm_value_drop_owned(vm, POP());
                    runtime_error(vm, instruction,
                                  "CancellationTokenSource.Token requires a source");
                    goto fail;
                }
                break;
            }
            VM_LABEL(cancellation_cancel)
            case OP_CANCELLATION_CANCEL: {
                if (sp == 0U || stack[sp - 1U].tag != LANG_VALUE_OBJECT) {
                    runtime_error(vm, instruction,
                                  "CancellationTokenSource.Cancel requires a source");
                    goto fail;
                }
                LangValue value = POP();
                Object *state = value.as.object;
                if (state == NULL || state->kind != OBJECT_CANCELLATION) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(vm, instruction,
                                  "CancellationTokenSource.Cancel requires a source");
                    goto fail;
                }
                state->as.cancellation.requested = true;
                vm_value_drop_owned(vm, value);
                PUSH(((LangValue){.tag=LANG_VALUE_UNIT}));
                break;
            }
            VM_LABEL(cancellation_is_requested)
            case OP_CANCELLATION_IS_REQUESTED: {
                if (sp == 0U || stack[sp - 1U].tag != LANG_VALUE_OBJECT) {
                    runtime_error(vm, instruction,
                                  "IsCancellationRequested requires a token");
                    goto fail;
                }
                LangValue value = POP();
                Object *state = value.as.object;
                bool requested = state != NULL &&
                    state->kind == OBJECT_CANCELLATION &&
                    state->as.cancellation.requested;
                vm_value_drop_owned(vm, value);
                PUSH(((LangValue){.tag=LANG_VALUE_BOOL,
                                  .as.boolean=requested}));
                break;
            }
            VM_LABEL(cancellation_throw_if_requested)
            case OP_CANCELLATION_THROW_IF_REQUESTED: {
                if (sp == 0U || stack[sp - 1U].tag != LANG_VALUE_OBJECT) {
                    runtime_error(vm, instruction,
                                  "ThrowIfCancellationRequested requires a token");
                    goto fail;
                }
                LangValue value = POP();
                Object *state = value.as.object;
                bool requested = state != NULL &&
                    state->kind == OBJECT_CANCELLATION &&
                    state->as.cancellation.requested;
                vm_value_drop_owned(vm, value);
                if (requested)
                    vm_raise_exception_typed(
                        vm, "OperationCanceledException",
                        "The operation was canceled.");
                PUSH(((LangValue){.tag=LANG_VALUE_UNIT}));
                break;
            }
            VM_LABEL(task_when_all)
            case OP_TASK_WHEN_ALL: {
                vm->active_span = instruction_span;
                if (sp == 0U || stack[sp - 1U].tag != LANG_VALUE_OBJECT) {
                    runtime_error(vm, instruction,
                                  "Task.WhenAll requires a List of Tasks");
                    goto fail;
                }
                Object *list = POP().as.object;
                LangValue combined = vm_task_when_all(
                    vm, list, instruction.a != 0, instruction_span);
                if (vm->trapped) goto fail;
                PUSH(combined);
                break;
            }
            VM_LABEL(task_when_any)
            case OP_TASK_WHEN_ANY: {
                vm->active_span = instruction_span;
                if (sp == 0U || stack[sp - 1U].tag != LANG_VALUE_OBJECT) {
                    runtime_error(vm, instruction,
                                  "Task.WhenAny requires a List of Tasks");
                    goto fail;
                }
                Object *list = POP().as.object;
                LangValue combined = vm_task_when_any(
                    vm, list, instruction_span);
                if (vm->trapped) goto fail;
                PUSH(combined);
                break;
            }
            VM_LABEL(await)
            case OP_AWAIT: {
                vm->active_span = instruction_span;
                if (sp == 0U) {
                    runtime_error(vm, instruction,
                                  "await requires a Task value");
                    goto fail;
                }
                LangValue value = POP();
                Object *awaited = value.tag == LANG_VALUE_OBJECT
                    ? value.as.object : NULL;
                if (awaited == NULL || awaited->kind != OBJECT_TASK) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(vm, instruction,
                                  "await requires a Task value");
                    goto fail;
                }
                if (awaited->as.task.state == VM_TASK_PENDING) {
                    if (async_task == NULL || async_frame == NULL) {
                        vm_value_drop_owned(vm, value);
                        runtime_error(vm, instruction,
                                      "only an async function may suspend");
                        goto fail;
                    }
                    async_frame->ip = ip;
                    async_frame->sp = sp;
                    async_frame->instruction_count =
                        frame_instruction_count;
                    if (!vm_task_suspend(vm, async_task, awaited)) {
                        vm_value_drop_owned(vm, value);
                        runtime_error(vm, instruction,
                                      "could not suspend async Task");
                        goto fail;
                    }
                    --vm->frame_count;
                    return failure;
                }
                if (awaited->as.task.state == VM_TASK_SUCCEEDED) {
                    PUSH(vm_value_clone(awaited->as.task.result));
                } else if (awaited->as.task.state == VM_TASK_FAULTED ||
                           awaited->as.task.state == VM_TASK_CANCELED) {
                    if (vm->exception_pending)
                        vm_value_drop_owned(vm, vm->exception_value);
                    vm->exception_value =
                        vm_value_clone(awaited->as.task.exception);
                    vm->exception_pending = true;
                    PUSH(((LangValue){.tag=LANG_VALUE_UNIT}));
                } else {
                    vm_value_drop_owned(vm, value);
                    runtime_error(vm, instruction,
                                  "awaited Task has an invalid state");
                    goto fail;
                }
                vm_value_drop_owned(vm, value);
                break;
            }
            VM_LABEL(return)
            case OP_RETURN: {
                vm->active_span = instruction_span;
                LangValue result = sp != 0U ? POP() : failure;
                result = finish_frame(
                    vm, function, locals, initialized, references, result,
                    frame_instruction_count);
                if (async_task != NULL) {
                    vm_task_complete(vm, async_task, result);
                    return failure;
                }
                return result;
            }
            VM_LABEL(return_local)
            case OP_RETURN_LOCAL: {
                size_t slot = (size_t)instruction.a;
                if (!initialized[slot]) {
                    runtime_error(
                        vm, instruction,
                        "return used an unavailable local");
                    goto fail;
                }
                LangValue result = LOCAL(slot);
                if (instruction.b != 0)
                    initialized[slot] = false;
                result = finish_frame(
                    vm, function, locals, initialized, references, result,
                    frame_instruction_count);
                if (async_task != NULL) {
                    vm_task_complete(vm, async_task, result);
                    return failure;
                }
                return result;
            }
            VM_LABEL(make_array)
            case OP_MAKE_ARRAY: {
                size_t count = (size_t)instruction.a;
                Object *array = vm_allocate(1U, sizeof(*array)); array->kind = OBJECT_ARRAY;
                array->as.array.count = count;
                array->as.array.items = vm_allocate(count, sizeof(LangValue));
                for (size_t i = count; i > 0U; --i) array->as.array.items[i - 1U] = POP();
                PUSH(((LangValue){.tag=LANG_VALUE_OBJECT,.as.object=array})); break;
            }
            VM_LABEL(get_index)
            case OP_GET_INDEX: {
                LangValue index = POP(), aggregate = POP();
                Object *array = aggregate.tag == LANG_VALUE_OBJECT
                              ? aggregate.as.object : NULL;
                bool valid_index = false;
                if (aggregate.tag == LANG_VALUE_OBJECT &&
                    array != NULL && array->kind == OBJECT_ARRAY)
                    valid_index =
                        (index.tag == LANG_VALUE_I64 && index.as.i64 >= 0 &&
                         (uint64_t)index.as.i64 < array->as.array.count) ||
                        (index.tag == LANG_VALUE_U64 &&
                         index.as.u64 < array->as.array.count);
                if (aggregate.tag != LANG_VALUE_OBJECT ||
                    array == NULL || array->kind != OBJECT_ARRAY ||
                    !valid_index) {
                    vm_value_drop_owned(vm, aggregate);
                    runtime_error(vm, instruction,
                                  "array index out of bounds");
                    goto fail;
                }
                size_t offset = index.tag == LANG_VALUE_I64
                              ? (size_t)index.as.i64 : (size_t)index.as.u64;
                LangValue item = instruction.b != 0
                    ? array->as.array.items[offset]
                    : vm_value_clone(array->as.array.items[offset]);
                if (instruction.b == 0)
                    vm_value_drop_owned(vm, aggregate);
                PUSH(item); break;
            }
            VM_LABEL(get_index_local)
            case OP_GET_INDEX_LOCAL: {
                LangValue index = POP();
                size_t slot = (size_t)instruction.a;
                Object *array =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                bool valid_index =
                    array != NULL && array->kind == OBJECT_ARRAY &&
                    ((index.tag == LANG_VALUE_I64 && index.as.i64 >= 0 &&
                      (uint64_t)index.as.i64 < array->as.array.count) ||
                     (index.tag == LANG_VALUE_U64 &&
                      index.as.u64 < array->as.array.count));
                if (!valid_index) {
                    runtime_error(vm, instruction,
                                  "array index out of bounds");
                    goto fail;
                }
                size_t offset = index.tag == LANG_VALUE_I64
                              ? (size_t)index.as.i64
                              : (size_t)index.as.u64;
                PUSH(vm_value_clone(array->as.array.items[offset]));
                break;
            }
            VM_LABEL(set_index_local)
            case OP_SET_INDEX_LOCAL: {
                LangValue value = POP();
                LangValue index = POP();
                size_t slot = (size_t)instruction.a;
                Object *array =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                bool valid_index =
                    array != NULL && array->kind == OBJECT_ARRAY &&
                    ((index.tag == LANG_VALUE_I64 && index.as.i64 >= 0 &&
                      (uint64_t)index.as.i64 < array->as.array.count) ||
                     (index.tag == LANG_VALUE_U64 &&
                      index.as.u64 < array->as.array.count));
                if (!valid_index) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(vm, instruction,
                                  "array index out of bounds");
                    goto fail;
                }
                size_t offset = index.tag == LANG_VALUE_I64
                              ? (size_t)index.as.i64
                              : (size_t)index.as.u64;
                vm_value_drop_owned(vm, array->as.array.items[offset]);
                array->as.array.items[offset] = value;
                PUSH(((LangValue){.tag=LANG_VALUE_UNIT}));
                break;
            }
            VM_LABEL(make_struct)
            case OP_MAKE_STRUCT:
            VM_LABEL(make_class)
            case OP_MAKE_CLASS: {
                bool class_reference = instruction.op == OP_MAKE_CLASS;
                size_t count = (size_t)instruction.b;
                Object *object = vm_allocate(1U, sizeof(*object)); object->kind = OBJECT_STRUCT;
                LangStringView metadata = vm->module->constants[(size_t)instruction.a].value.as.string;
                object->as.structure.metadata = vm_allocate(metadata.length + 1U, 1U);
                memcpy(object->as.structure.metadata, metadata.data, metadata.length);
                object->as.structure.metadata[metadata.length] = '\0';
                object->language_destructor =
                    vm_language_destructor_for_metadata(
                        vm, object->as.structure.metadata);
                object->as.structure.count = count;
                object->as.structure.fields = vm_allocate(count, sizeof(LangValue));
                for (size_t i = count; i > 0U; --i) object->as.structure.fields[i - 1U] = POP();
                PUSH(((LangValue){
                    .tag=class_reference
                        ? LANG_VALUE_RAW_POINTER : LANG_VALUE_OBJECT,
                    .as.object=object
                })); break;
            }
            VM_LABEL(delete_class)
            case OP_DELETE_CLASS: {
                if (sp == 0U) {
                    runtime_error(vm, instruction,
                                  "class delete requires a value");
                    goto fail;
                }
                LangValue value = POP();
                if (value.tag != LANG_VALUE_RAW_POINTER) {
                    runtime_error(vm, instruction,
                                  "class delete requires a class reference");
                    goto fail;
                }
                Object *object = value.as.pointer;
                if (object != NULL) {
                    uint32_t destructor = object->language_destructor;
                    object->language_destructor = 0U;
                    if (destructor != 0U) {
                        LangValue argument = value;
                        LangValue result = vm_execute_function(
                            vm, (size_t)(destructor - 1U),
                            &argument, 1U, vm->active_span);
                        vm_value_drop_owned(vm, result);
                        bool destructor_failed = vm->trapped;
                        vm_object_free(vm, object);
                        if (destructor_failed) goto fail;
                    } else {
                        vm_object_free(vm, object);
                    }
                }
                break;
            }
            VM_LABEL(get_field)
            case OP_GET_FIELD: {
                LangValue aggregate = POP();
                LangStringView field = vm->module->constants[(size_t)instruction.a].value.as.string;
                Object *object = aggregate.tag == LANG_VALUE_OBJECT
                               ? aggregate.as.object
                               : aggregate.tag == LANG_VALUE_RAW_POINTER
                                   ? aggregate.as.pointer : NULL;
                if (object == NULL) {
                    runtime_error(vm, instruction,
                                  "field access requires an object");
                    goto fail;
                }
                if (object->kind == OBJECT_BUFFER && field.length == 3U &&
                    memcmp(field.data, "len", 3U) == 0) {
                    LangValue length = {
                        .tag=LANG_VALUE_I64,
                        .as.i64=(int64_t)object->as.buffer.length
                    };
                    vm_value_drop_owned(vm, aggregate);
                    PUSH(length);
                    break;
                }
                if (object->kind != OBJECT_STRUCT) {
                    vm_value_drop_owned(vm, aggregate);
                    runtime_error(vm, instruction,
                                  "unknown object field");
                    goto fail;
                }
                char *cursor = strchr(object->as.structure.metadata, '|');
                bool found = false; size_t field_index = 0U;
                while (cursor != NULL && field_index < object->as.structure.count) {
                    ++cursor; char *end = strchr(cursor, '|');
                    size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
                    if (length == field.length && memcmp(cursor, field.data, length) == 0) {
                        LangValue field_value = vm_value_clone(object->as.structure.fields[field_index]);
                        vm_value_drop_owned(vm, aggregate);
                        PUSH(field_value); found = true; break;
                    }
                    cursor = end; ++field_index;
                }
                if (!found) {
                    vm_value_drop_owned(vm, aggregate);
                    runtime_error(vm, instruction,
                                  "unknown struct field");
                    goto fail;
                }
                break;
            }
            VM_LABEL(get_field_local)
            case OP_GET_FIELD_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *object =
                    initialized[slot] &&
                    (LOCAL(slot).tag == LANG_VALUE_OBJECT ||
                     LOCAL(slot).tag == LANG_VALUE_RAW_POINTER)
                    ? (LOCAL(slot).tag == LANG_VALUE_OBJECT
                        ? LOCAL(slot).as.object : LOCAL(slot).as.pointer)
                    : NULL;
                LangStringView field =
                    vm->module->constants[(size_t)instruction.b]
                        .value.as.string;
                if (object == NULL) {
                    runtime_error(vm, instruction,
                                  "field access requires an object");
                    goto fail;
                }
                if (object->kind == OBJECT_BUFFER &&
                    field.length == 3U &&
                    memcmp(field.data, "len", 3U) == 0) {
                    PUSH(((LangValue){
                        .tag=LANG_VALUE_I64,
                        .as.i64=(int64_t)object->as.buffer.length
                    }));
                    break;
                }
                if (object->kind != OBJECT_STRUCT) {
                    runtime_error(vm, instruction,
                                  "unknown object field");
                    goto fail;
                }
                char *cursor = strchr(object->as.structure.metadata, '|');
                bool found = false;
                size_t field_index = 0U;
                while (cursor != NULL &&
                       field_index < object->as.structure.count) {
                    ++cursor;
                    char *end = strchr(cursor, '|');
                    size_t length = end != NULL
                                  ? (size_t)(end - cursor)
                                  : strlen(cursor);
                    if (length == field.length &&
                        memcmp(cursor, field.data, length) == 0) {
                        PUSH(vm_value_clone(
                            object->as.structure.fields[field_index]));
                        found = true;
                        break;
                    }
                    cursor = end;
                    ++field_index;
                }
                if (!found) {
                    runtime_error(vm, instruction,
                                  "unknown struct field");
                    goto fail;
                }
                break;
            }
            VM_LABEL(get_field_local_move)
            case OP_GET_FIELD_LOCAL_MOVE: {
                size_t slot = (size_t)instruction.a;
                Object *object =
                    initialized[slot] &&
                    (LOCAL(slot).tag == LANG_VALUE_OBJECT ||
                     LOCAL(slot).tag == LANG_VALUE_RAW_POINTER)
                    ? (LOCAL(slot).tag == LANG_VALUE_OBJECT
                        ? LOCAL(slot).as.object : LOCAL(slot).as.pointer)
                    : NULL;
                LangStringView field =
                    vm->module->constants[(size_t)instruction.b]
                        .value.as.string;
                if (object == NULL || object->kind != OBJECT_STRUCT) {
                    runtime_error(vm, instruction,
                                  "field move requires a struct");
                    goto fail;
                }
                char *cursor = strchr(
                    object->as.structure.metadata, '|');
                bool found = false;
                size_t field_index = 0U;
                while (cursor != NULL &&
                       field_index < object->as.structure.count) {
                    ++cursor;
                    char *end = strchr(cursor, '|');
                    size_t length = end != NULL
                                  ? (size_t)(end - cursor)
                                  : strlen(cursor);
                    if (length == field.length &&
                        memcmp(cursor, field.data, length) == 0) {
                        LangValue field_value =
                            object->as.structure.fields[field_index];
                        object->as.structure.fields[field_index] =
                            (LangValue){.tag=LANG_VALUE_UNIT};
                        PUSH(field_value);
                        found = true;
                        break;
                    }
                    cursor = end;
                    ++field_index;
                }
                if (!found) {
                    runtime_error(vm, instruction,
                                  "unknown struct field");
                    goto fail;
                }
                break;
            }
            VM_LABEL(get_field_borrow)
            case OP_GET_FIELD_BORROW: {
                LangValue aggregate = POP();
                LangStringView field =
                    vm->module->constants[(size_t)instruction.a]
                        .value.as.string;
                Object *object = aggregate.tag == LANG_VALUE_OBJECT
                               ? aggregate.as.object
                               : aggregate.tag == LANG_VALUE_RAW_POINTER
                                   ? aggregate.as.pointer : NULL;
                if (object == NULL || object->kind != OBJECT_STRUCT) {
                    runtime_error(vm, instruction,
                                  "borrowed field access requires a struct");
                    goto fail;
                }
                char *cursor = strchr(
                    object->as.structure.metadata, '|');
                bool found = false;
                size_t field_index = 0U;
                while (cursor != NULL &&
                       field_index < object->as.structure.count) {
                    ++cursor;
                    char *end = strchr(cursor, '|');
                    size_t length = end != NULL
                                  ? (size_t)(end - cursor)
                                  : strlen(cursor);
                    if (length == field.length &&
                        memcmp(cursor, field.data, length) == 0) {
                        PUSH(object->as.structure.fields[field_index]);
                        found = true;
                        break;
                    }
                    cursor = end;
                    ++field_index;
                }
                if (!found) {
                    runtime_error(vm, instruction,
                                  "unknown struct field");
                    goto fail;
                }
                break;
            }
            VM_LABEL(set_field_local)
            case OP_SET_FIELD_LOCAL: {
                LangValue value = POP();
                size_t slot = (size_t)instruction.a;
                Object *object =
                    initialized[slot] &&
                    (LOCAL(slot).tag == LANG_VALUE_OBJECT ||
                     LOCAL(slot).tag == LANG_VALUE_RAW_POINTER)
                    ? (LOCAL(slot).tag == LANG_VALUE_OBJECT
                        ? LOCAL(slot).as.object : LOCAL(slot).as.pointer)
                    : NULL;
                LangStringView field =
                    vm->module->constants[(size_t)instruction.b]
                        .value.as.string;
                if (object == NULL || object->kind != OBJECT_STRUCT) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(vm, instruction,
                                  "field assignment requires a struct");
                    goto fail;
                }
                char *cursor = strchr(object->as.structure.metadata, '|');
                bool found = false;
                size_t field_index = 0U;
                while (cursor != NULL &&
                       field_index < object->as.structure.count) {
                    ++cursor;
                    char *end = strchr(cursor, '|');
                    size_t length = end != NULL
                                  ? (size_t)(end - cursor)
                                  : strlen(cursor);
                    if (length == field.length &&
                        memcmp(cursor, field.data, length) == 0) {
                        vm_value_drop_owned(
                            vm,
                            object->as.structure.fields[field_index]);
                        object->as.structure.fields[field_index] = value;
                        found = true;
                        break;
                    }
                    cursor = end;
                    ++field_index;
                }
                if (!found) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(vm, instruction,
                                  "unknown struct field");
                    goto fail;
                }
                PUSH(((LangValue){.tag=LANG_VALUE_UNIT}));
                break;
            }
            VM_LABEL(get_tag)
            case OP_GET_TAG: {
                LangValue aggregate = POP();
                if (aggregate.tag != LANG_VALUE_OBJECT ||
                    aggregate.as.object == NULL ||
                    ((Object *)aggregate.as.object)->kind != OBJECT_STRUCT) {
                    runtime_error(vm, instruction,
                                  "enum tag access requires a tagged value");
                    goto fail;
                }
                Object *object = aggregate.as.object;
                const char *tag = object->as.structure.metadata;
                const char *module_separator = strchr(tag, '#');
                if (module_separator != NULL) tag = module_separator + 1;
                const char *separator = strchr(tag, '|');
                size_t length = separator != NULL
                              ? (size_t)(separator - tag)
                              : strlen(tag);
                PUSH(((LangValue){
                    .tag=LANG_VALUE_STRING_VIEW,
                    .as.string={tag, length}
                }));
                break;
            }
            VM_LABEL(take_payload)
            case OP_TAKE_PAYLOAD: {
                LangValue aggregate = POP();
                if (aggregate.tag != LANG_VALUE_OBJECT ||
                    aggregate.as.object == NULL ||
                    ((Object *)aggregate.as.object)->kind != OBJECT_STRUCT ||
                    ((Object *)aggregate.as.object)->as.structure.count != 1U) {
                    runtime_error(vm, instruction,
                                  "enum payload extraction requires one payload");
                    vm_value_drop_owned(vm, aggregate);
                    goto fail;
                }
                Object *object = aggregate.as.object;
                LangValue payload = object->as.structure.fields[0];
                object->as.structure.fields[0] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
                vm_object_free(vm, object);
                PUSH(payload);
                break;
            }
            VM_LABEL(html_fragment)
            case OP_HTML_FRAGMENT: {
                Object *html = NULL;
                if (instruction.b > 0) {
                    size_t parent_slot =
                        (size_t)instruction.b - 1U;
                    Object *parent =
                        initialized[parent_slot] &&
                        LOCAL(parent_slot).tag ==
                            LANG_VALUE_OBJECT
                        ? LOCAL(parent_slot).as.object : NULL;
                    if (parent == NULL ||
                        parent->kind != OBJECT_HTML) {
                        runtime_error(
                            vm, instruction,
                            "nested fragment requires a live parent builder");
                        goto fail;
                    }
                    if (html_object_count >=
                        vm->frame_local_stride) {
                        runtime_error(
                            vm, instruction,
                            "too many nested HTML builders");
                        goto fail;
                    }
                    html = &html_objects[html_object_count++];
                    memset(html, 0, sizeof(*html));
                    html->as.html.embedded = true;
                    vm_html_ensure_open_closed(parent);
                    html->as.html.destination =
                        vm_html_destination(parent);
                } else
                    html = vm_allocate(1U, sizeof(*html));
                html->kind = OBJECT_HTML;
                html->as.html.tag = "";
                html->as.html.tag_length = 0U;
                html->as.html.open = false;
                PUSH(((LangValue){.tag=LANG_VALUE_OBJECT,.as.object=html}));
                break;
            }
            VM_LABEL(html_begin)
            case OP_HTML_BEGIN: {
                LangStringView tag = vm->module->constants[(size_t)instruction.a].value.as.string;
                Object *html = NULL;
                if (instruction.b > 0) {
                    size_t parent_slot = (size_t)instruction.b - 1U;
                    Object *parent =
                        initialized[parent_slot] &&
                        LOCAL(parent_slot).tag == LANG_VALUE_OBJECT
                        ? LOCAL(parent_slot).as.object : NULL;
                    if (parent == NULL || parent->kind != OBJECT_HTML) {
                        runtime_error(
                            vm, instruction,
                            "nested element requires a live parent builder");
                        goto fail;
                    }
                    if (html_object_count >=
                        vm->frame_local_stride) {
                        runtime_error(
                            vm, instruction,
                            "too many nested HTML builders");
                        goto fail;
                    }
                    html = &html_objects[html_object_count++];
                    memset(html, 0, sizeof(*html));
                    html->as.html.embedded = true;
                    vm_html_ensure_open_closed(parent);
                    html->as.html.destination =
                        vm_html_destination(parent);
                } else
                    html = vm_allocate(1U, sizeof(*html));
                html->kind = OBJECT_HTML;
                html->as.html.tag = tag.data;
                html->as.html.tag_length = tag.length;
                html->as.html.open = true;
                html->as.html.start = vm_html_destination(html)->as.html.length;
                vm_html_cstr(html, "<"); vm_html_bytes(html, tag.data, tag.length);
                PUSH(((LangValue){.tag=LANG_VALUE_OBJECT,.as.object=html})); break;
            }
            VM_LABEL(html_attr)
            case OP_HTML_ATTR: {
                LangValue value = POP();
                Object *html = stack[sp - 1U].as.object;
                LangStringView name =
                    vm->module->constants[
                        (size_t)instruction.a].value.as.string;
                vm_html_set_attribute(vm, html, name, value);
                break;
            }
            VM_LABEL(html_fragment_local)
            case OP_HTML_FRAGMENT_LOCAL:
            VM_LABEL(html_begin_local)
            case OP_HTML_BEGIN_LOCAL: {
                uint32_t packed = (uint32_t)instruction.b;
                size_t slot = (size_t)(packed & UINT32_C(0x3ff));
                size_t parent_encoded = (size_t)(packed >> 10U);
                Object *html = NULL;
                if (parent_encoded != 0U) {
                    size_t parent_slot = parent_encoded - 1U;
                    Object *parent =
                        initialized[parent_slot] &&
                        LOCAL(parent_slot).tag == LANG_VALUE_OBJECT
                        ? LOCAL(parent_slot).as.object : NULL;
                    if (parent == NULL || parent->kind != OBJECT_HTML) {
                        runtime_error(
                            vm, instruction,
                            "nested element requires a live parent builder");
                        goto fail;
                    }
                    if (html_object_count >= vm->frame_local_stride) {
                        runtime_error(
                            vm, instruction,
                            "too many nested HTML builders");
                        goto fail;
                    }
                    html = &html_objects[html_object_count++];
                    memset(html, 0, sizeof(*html));
                    html->as.html.embedded = true;
                    vm_html_ensure_open_closed(parent);
                    html->as.html.destination = vm_html_destination(parent);
                } else
                    html = vm_allocate(1U, sizeof(*html));
                html->kind = OBJECT_HTML;
                if (instruction.op == OP_HTML_BEGIN_LOCAL) {
                    LangStringView tag = vm->module->constants[
                        (size_t)instruction.a].value.as.string;
                    html->as.html.tag = tag.data;
                    html->as.html.tag_length = tag.length;
                    html->as.html.open = true;
                    html->as.html.start =
                        vm_html_destination(html)->as.html.length;
                    vm_html_cstr(html, "<");
                    vm_html_bytes(html, tag.data, tag.length);
                } else {
                    html->as.html.tag = "";
                    html->as.html.tag_length = 0U;
                }
                if (initialized[slot]) vm_value_drop_owned(vm, LOCAL(slot));
                LOCAL(slot) = (LangValue){
                    .tag=LANG_VALUE_OBJECT, .as.object=html
                };
                initialized[slot] = true;
                break;
            }
            VM_LABEL(html_text)
            case OP_HTML_TEXT: {
                Object *html = NULL;
                for (size_t i = sp; i > 0U; --i) {
                    if (stack[i - 1U].tag == LANG_VALUE_OBJECT &&
                        ((Object *)stack[i - 1U].as.object)->kind ==
                            OBJECT_HTML) {
                        html = stack[i - 1U].as.object;
                        break;
                    }
                }
                if (html == NULL) {
                    runtime_error(
                        vm, instruction,
                        "static HTML text has no parent builder");
                    goto fail;
                }
                vm_html_ensure_open_closed(html);
                LangStringView text = vm->module->constants[
                    (size_t)instruction.b].value.as.string;
                vm_html_append_text(html, text);
                break;
            }
            VM_LABEL(html_append)
            case OP_HTML_APPEND: {
                LangValue child = POP();
                Object *html = NULL;
                for (size_t i = sp; i > 0U; --i) {
                    if (stack[i - 1U].tag == LANG_VALUE_OBJECT &&
                        ((Object *)stack[i - 1U].as.object)->kind == OBJECT_HTML) {
                        html = stack[i - 1U].as.object;
                        break;
                    }
                }
                if (html == NULL) {
                    runtime_error(vm, instruction, "HTML child has no parent builder");
                    vm_value_drop_owned(vm, child);
                    goto fail;
                }
                vm_html_ensure_open_closed(html);
                vm_html_append_value(vm, html, child);
                break;
            }
            VM_LABEL(html_end)
            case OP_HTML_END: {
                Object *html = stack[sp - 1U].as.object;
                vm_html_finish(html);
                break;
            }
            VM_LABEL(html_attr_local)
            case OP_HTML_ATTR_LOCAL: {
                LangValue value = POP();
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL || html->kind != OBJECT_HTML) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(
                        vm, instruction,
                        "HTML property requires a live element builder");
                    goto fail;
                }
                LangStringView name =
                    vm->module->constants[
                        (size_t)instruction.b].value.as.string;
                vm_html_set_attribute(vm, html, name, value);
                break;
            }
            VM_LABEL(html_attr_begin_local)
            case OP_HTML_ATTR_BEGIN_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL ||
                    html->kind != OBJECT_HTML ||
                    !html->as.html.open) {
                    runtime_error(
                        vm, instruction,
                        "interpolated HTML property requires an open element builder");
                    goto fail;
                }
                LangStringView name =
                    vm->module->constants[
                        (size_t)instruction.b].value.as.string;
                vm_html_cstr(html, " ");
                vm_html_bytes(html, name.data, name.length);
                vm_html_cstr(html, "=\"");
                break;
            }
            VM_LABEL(html_attr_append_local)
            case OP_HTML_ATTR_APPEND_LOCAL: {
                LangValue value = POP();
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL ||
                    html->kind != OBJECT_HTML) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(
                        vm, instruction,
                        "interpolated HTML property requires a live element builder");
                    goto fail;
                }
                vm_html_append_formatted(
                    vm, html, value, true);
                break;
            }
            VM_LABEL(html_css_value_local)
            case OP_HTML_CSS_VALUE_LOCAL: {
                LangValue value = POP();
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                LangStringView string;
                bool is_string = lang_value_string_view(&value, &string);
                if (html == NULL || html->kind != OBJECT_HTML) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(
                        vm, instruction,
                        "CSS custom property requires a live element builder");
                    goto fail;
                }
                if (is_string && !vm_css_custom_property_atom(string)) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(
                        vm, instruction,
                        "unsafe CSS custom property value");
                    goto fail;
                }
                vm_html_append_formatted(vm, html, value, true);
                break;
            }
            VM_LABEL(html_attr_end_local)
            case OP_HTML_ATTR_END_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL ||
                    html->kind != OBJECT_HTML) {
                    runtime_error(
                        vm, instruction,
                        "interpolated HTML property requires a live element builder");
                    goto fail;
                }
                vm_html_cstr(html, "\"");
                break;
            }
            VM_LABEL(html_append_local)
            case OP_HTML_APPEND_LOCAL: {
                LangValue child = POP();
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL || html->kind != OBJECT_HTML) {
                    vm_value_drop_owned(vm, child);
                    runtime_error(
                        vm, instruction,
                        "HTML child requires a live element builder");
                    goto fail;
                }
                vm_html_ensure_open_closed(html);
                vm_html_append_value(vm, html, child);
                break;
            }
            VM_LABEL(html_append_formatted_local)
            case OP_HTML_APPEND_FORMATTED_LOCAL: {
                LangValue value = POP();
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL ||
                    html->kind != OBJECT_HTML) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(
                        vm, instruction,
                        "interpolated HTML text requires a live element builder");
                    goto fail;
                }
                vm_html_ensure_open_closed(html);
                vm_html_append_formatted(
                    vm, html, value, false);
                break;
            }
            VM_LABEL(html_append_constant_local)
            case OP_HTML_APPEND_CONSTANT_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL || html->kind != OBJECT_HTML) {
                    runtime_error(
                        vm, instruction,
                        "HTML constant requires a live element builder");
                    goto fail;
                }
                vm_html_ensure_open_closed(html);
                LangStringView value = vm->module->constants[
                    (size_t)instruction.b].value.as.string;
                vm_html_append_text(html, value);
                break;
            }
            VM_LABEL(html_append_raw_constant_local)
            case OP_HTML_APPEND_RAW_CONSTANT_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL || html->kind != OBJECT_HTML) {
                    runtime_error(
                        vm, instruction,
                        "static HTML text requires a live element builder");
                    goto fail;
                }
                vm_html_ensure_open_closed(html);
                LangStringView value = vm->module->constants[
                    (size_t)instruction.b].value.as.string;
                vm_html_append_text(html, value);
                break;
            }
            VM_LABEL(html_attr_constant_local)
            case OP_HTML_ATTR_CONSTANT_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL || html->kind != OBJECT_HTML) {
                    runtime_error(
                        vm, instruction,
                        "HTML constant property requires a live element builder");
                    goto fail;
                }
                uint32_t packed = (uint32_t)instruction.b;
                LangStringView value = vm->module->constants[
                    packed & UINT32_C(0xffff)].value.as.string;
                LangStringView name = vm->module->constants[
                    packed >> 16U].value.as.string;
                vm_html_cstr(html, " ");
                vm_html_bytes(html, name.data, name.length);
                vm_html_cstr(html, "=\"");
                vm_html_escape(html, value, true);
                vm_html_cstr(html, "\"");
                break;
            }
            VM_LABEL(html_attr_append_constant_local)
            case OP_HTML_ATTR_APPEND_CONSTANT_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL || html->kind != OBJECT_HTML) {
                    runtime_error(
                        vm, instruction,
                        "HTML attribute constant requires a live element builder");
                    goto fail;
                }
                LangStringView value = vm->module->constants[
                    (size_t)instruction.b].value.as.string;
                vm_html_escape(html, value, true);
                break;
            }
            VM_LABEL(html_append_value_local)
            case OP_HTML_APPEND_VALUE_LOCAL:
            VM_LABEL(html_attr_append_value_local)
            case OP_HTML_ATTR_APPEND_VALUE_LOCAL: {
                size_t slot = (size_t)instruction.a;
                size_t value_slot = (size_t)instruction.b;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL || html->kind != OBJECT_HTML ||
                    !initialized[value_slot]) {
                    runtime_error(
                        vm, instruction,
                        "HTML value requires live locals");
                    goto fail;
                }
                bool attribute =
                    instruction.op == OP_HTML_ATTR_APPEND_VALUE_LOCAL;
                if (!attribute) vm_html_ensure_open_closed(html);
                vm_html_append_formatted_value(
                    vm, html, LOCAL(value_slot), attribute);
                break;
            }
            VM_LABEL(html_finish_local)
            case OP_HTML_FINISH_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *html =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (html == NULL || html->kind != OBJECT_HTML) {
                    runtime_error(
                        vm, instruction,
                        "element finish requires a live builder");
                    goto fail;
                }
                vm_html_finish(html);
                LangValue result = LOCAL(slot);
                initialized[slot] = false;
                PUSH(result);
                break;
            }
            VM_LABEL(html_render_local)
            case OP_HTML_RENDER_LOCAL: {
                LangValue value = POP();
                size_t slot = (size_t)instruction.a;
                Object *html = value.tag == LANG_VALUE_OBJECT
                             ? value.as.object : NULL;
                if (value.tag != LANG_VALUE_OBJECT ||
                    html == NULL ||
                    html->kind != OBJECT_HTML ||
                    html->as.html.destination != NULL) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(
                        vm, instruction,
                        "html_render expects a detached Html value");
                    goto fail;
                }
                char *data = html->as.html.data;
                size_t length = html->as.html.length;
                vm_html_release_style_state(html);
                html->kind = OBJECT_STRING;
                html->references = 1U;
                html->as.string.data = data;
                html->as.string.length = length;
                html->as.string.embedded_data = false;
                if (initialized[slot]) vm_value_drop_owned(vm, LOCAL(slot));
                LOCAL(slot) = value;
                initialized[slot] = true;
                break;
            }
            VM_LABEL(string_builder_new_local)
            case OP_STRING_BUILDER_NEW_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *builder = vm_allocate_uninitialized(
                    1U, sizeof(*builder) + 64U);
                builder->kind = OBJECT_STRING_BUILDER;
                builder->language_destructor = 0U;
                builder->as.string_builder.length = 0U;
                builder->as.string_builder.capacity = 64U;
                builder->as.string_builder.data =
                    (char *)(builder + 1U);
                builder->as.string_builder.embedded_data = true;
                if (initialized[slot]) vm_value_drop_owned(vm, LOCAL(slot));
                LOCAL(slot) = (LangValue){
                    .tag=LANG_VALUE_OBJECT, .as.object=builder
                };
                initialized[slot] = true;
                break;
            }
            VM_LABEL(string_builder_append_constant_local)
            case OP_STRING_BUILDER_APPEND_CONSTANT_LOCAL:
            VM_LABEL(string_builder_append_value_local)
            case OP_STRING_BUILDER_APPEND_VALUE_LOCAL: {
                size_t builder_slot = (size_t)instruction.a;
                Object *builder =
                    initialized[builder_slot] &&
                    LOCAL(builder_slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(builder_slot).as.object : NULL;
                bool appended = false;
                if (builder != NULL &&
                    builder->kind == OBJECT_STRING_BUILDER) {
                    if (instruction.op ==
                        OP_STRING_BUILDER_APPEND_CONSTANT_LOCAL) {
                        LangStringView value = vm->module->constants[
                            (size_t)instruction.b].value.as.string;
                        appended = vm_string_builder_append_bytes(
                            builder, value.data, value.length);
                    } else {
                        size_t value_slot = (size_t)instruction.b;
                        appended = initialized[value_slot] &&
                            vm_string_builder_append_value(
                                builder, LOCAL(value_slot));
                    }
                }
                if (!appended) {
                    runtime_error(
                        vm, instruction,
                        "invalid StringBuilder append");
                    goto fail;
                }
                break;
            }
            VM_LABEL(string_builder_finish_local)
            case OP_STRING_BUILDER_FINISH_LOCAL: {
                size_t builder_slot = (size_t)instruction.a;
                size_t destination = (size_t)instruction.b;
                Object *builder =
                    initialized[builder_slot] &&
                    LOCAL(builder_slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(builder_slot).as.object : NULL;
                if (builder == NULL ||
                    builder->kind != OBJECT_STRING_BUILDER) {
                    runtime_error(
                        vm, instruction,
                        "StringBuilder finish requires a live builder");
                    goto fail;
                }
                char *data = builder->as.string_builder.data;
                size_t length = builder->as.string_builder.length;
                bool embedded_data =
                    builder->as.string_builder.embedded_data;
                builder->kind = OBJECT_STRING;
                builder->references = 1U;
                builder->as.string.data = data;
                builder->as.string.length = length;
                builder->as.string.embedded_data = embedded_data;
                if (builder_slot != destination) {
                    if (initialized[destination])
                        vm_value_drop_owned(vm, LOCAL(destination));
                    LOCAL(destination) = LOCAL(builder_slot);
                    initialized[destination] = true;
                    initialized[builder_slot] = false;
                }
                break;
            }
            VM_LABEL(iter_init)
            case OP_ITER_INIT: {
                LangValue value = POP();
                if (value.tag == LANG_VALUE_BYTE_SLICE) {
                    Object *iterator = vm_allocate(1U, sizeof(*iterator));
                    iterator->kind = OBJECT_ITER;
                    iterator->as.iterator.bytes = value.as.bytes;
                    iterator->as.iterator.index = 0U;
                    iterator->as.iterator.borrowed = true;
                    iterator->as.iterator.is_slice = true;
                    PUSH(((LangValue){
                        .tag=LANG_VALUE_OBJECT, .as.object=iterator
                    }));
                    break;
                }
                Object *iterable =
                    value.tag == LANG_VALUE_OBJECT
                    ? value.as.object : NULL;
                if (iterable == NULL ||
                    (iterable->kind != OBJECT_ARRAY &&
                     iterable->kind != OBJECT_VEC &&
                     iterable->kind != OBJECT_STRING)) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(
                        vm, instruction,
                        "iteration requires an array or vector");
                    goto fail;
                }
                Object *iterator = vm_allocate(1U, sizeof(*iterator));
                iterator->kind = OBJECT_ITER;
                iterator->as.iterator.array = iterable;
                iterator->as.iterator.index = 0U;
                iterator->as.iterator.borrowed = instruction.b != 0;
                iterator->as.iterator.is_slice = false;
                iterator->as.iterator.is_string =
                    iterable->kind == OBJECT_STRING;
                PUSH(((LangValue){
                    .tag=LANG_VALUE_OBJECT, .as.object=iterator
                }));
                break;
            }
            VM_LABEL(iter_borrow_local)
            case OP_ITER_BORROW_LOCAL: {
                size_t slot = (size_t)instruction.a;
                if (initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_BYTE_SLICE) {
                    Object *iterator = vm_allocate(1U, sizeof(*iterator));
                    iterator->kind = OBJECT_ITER;
                    iterator->as.iterator.bytes = LOCAL(slot).as.bytes;
                    iterator->as.iterator.index = 0U;
                    iterator->as.iterator.borrowed = true;
                    iterator->as.iterator.is_slice = true;
                    PUSH(((LangValue){
                        .tag=LANG_VALUE_OBJECT, .as.object=iterator
                    }));
                    break;
                }
                Object *iterable =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (iterable == NULL ||
                    (iterable->kind != OBJECT_ARRAY &&
                     iterable->kind != OBJECT_VEC &&
                     iterable->kind != OBJECT_STRING)) {
                    runtime_error(
                        vm, instruction,
                        "borrowed iteration requires an array or vector local");
                    goto fail;
                }
                Object *iterator = vm_allocate(1U, sizeof(*iterator));
                iterator->kind = OBJECT_ITER;
                iterator->as.iterator.array = iterable;
                iterator->as.iterator.index = 0U;
                iterator->as.iterator.borrowed = true;
                iterator->as.iterator.is_slice = false;
                iterator->as.iterator.is_string =
                    iterable->kind == OBJECT_STRING;
                PUSH(((LangValue){
                    .tag=LANG_VALUE_OBJECT, .as.object=iterator
                }));
                break;
            }
            VM_LABEL(iter_next)
            case OP_ITER_NEXT: {
                Object *iterator = stack[sp - 1U].as.object;
                Object *array = iterator->as.iterator.array;
                size_t count = iterator->as.iterator.is_slice
                    ? iterator->as.iterator.bytes.length
                    : iterator->as.iterator.is_string
                        ? array->as.string.length
                    : array->kind == OBJECT_VEC
                        ? array->as.vector.count
                        : array->as.array.count;
                if (iterator->as.iterator.index >= count) {
                    ip = (size_t)instruction.a;
                } else {
                    size_t slot = (size_t)instruction.b;
                    size_t index = iterator->as.iterator.index;
                    if (!iterator->as.iterator.is_string)
                        ++iterator->as.iterator.index;
                    if (iterator->as.iterator.is_string) {
                        uint32_t scalar;
                        if (!vm_decode_utf8_scalar(
                                (const unsigned char *)array->as.string.data,
                                array->as.string.length,
                                &iterator->as.iterator.index, &scalar)) {
                            runtime_error(vm, instruction,
                                          "invalid UTF-8 string iteration");
                            goto fail;
                        }
                        LOCAL(slot) = (LangValue){
                            .tag=LANG_VALUE_U64, .as.u64=scalar
                        };
                    } else if (iterator->as.iterator.is_slice) {
                        LOCAL(slot) = (LangValue){
                            .tag=LANG_VALUE_U64,
                            .as.u64=(uint64_t)
                                iterator->as.iterator.bytes.data[index]
                        };
                    } else if (array->kind == OBJECT_VEC) {
                        LOCAL(slot) = iterator->as.iterator.borrowed
                            ? vm_value_clone(array->as.vector.items[index])
                            : array->as.vector.items[index];
                        if (!iterator->as.iterator.borrowed)
                            array->as.vector.items[index] =
                                (LangValue){.tag=LANG_VALUE_UNIT};
                    } else {
                        LOCAL(slot) = iterator->as.iterator.borrowed
                            ? vm_value_clone(array->as.array.items[index])
                            : array->as.array.items[index];
                    }
                    initialized[slot] = true;
                }
                break;
            }
            VM_LABEL(iter_has_next_local)
            case OP_ITER_HAS_NEXT_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *iterator =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (iterator == NULL ||
                    iterator->kind != OBJECT_ITER ||
                    (!iterator->as.iterator.is_slice &&
                     iterator->as.iterator.array == NULL)) {
                    runtime_error(
                        vm, instruction,
                        "iterator test requires a live iterator");
                    goto fail;
                }
                Object *iterable = iterator->as.iterator.array;
                size_t count = iterator->as.iterator.is_slice
                    ? iterator->as.iterator.bytes.length
                    : iterator->as.iterator.is_string
                        ? iterable->as.string.length
                    : iterable->kind == OBJECT_VEC
                        ? iterable->as.vector.count
                        : iterable->as.array.count;
                PUSH(((LangValue){
                    .tag=LANG_VALUE_BOOL,
                    .as.boolean=iterator->as.iterator.index < count
                }));
                break;
            }
            VM_LABEL(iter_take_next_local)
            case OP_ITER_TAKE_NEXT_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *iterator =
                    initialized[slot] &&
                    LOCAL(slot).tag == LANG_VALUE_OBJECT
                    ? LOCAL(slot).as.object : NULL;
                if (iterator == NULL ||
                    iterator->kind != OBJECT_ITER ||
                    (!iterator->as.iterator.is_slice &&
                     iterator->as.iterator.array == NULL)) {
                    runtime_error(
                        vm, instruction,
                        "iterator next requires a live iterator");
                    goto fail;
                }
                Object *iterable = iterator->as.iterator.array;
                size_t count = iterator->as.iterator.is_slice
                    ? iterator->as.iterator.bytes.length
                    : iterator->as.iterator.is_string
                        ? iterable->as.string.length
                    : iterable->kind == OBJECT_VEC
                        ? iterable->as.vector.count
                        : iterable->as.array.count;
                if (iterator->as.iterator.index >= count) {
                    runtime_error(
                        vm, instruction,
                        "iterator advanced past its end");
                    goto fail;
                }
                size_t index = iterator->as.iterator.index;
                if (iterator->as.iterator.is_string) {
                    uint32_t scalar;
                    if (!vm_decode_utf8_scalar(
                            (const unsigned char *)iterable->as.string.data,
                            iterable->as.string.length,
                            &iterator->as.iterator.index, &scalar)) {
                        runtime_error(vm, instruction,
                                      "invalid UTF-8 string iteration");
                        goto fail;
                    }
                    PUSH(((LangValue){
                        .tag=LANG_VALUE_U64, .as.u64=scalar
                    }));
                    break;
                }
                ++iterator->as.iterator.index;
                if (iterator->as.iterator.is_slice) {
                    PUSH(((LangValue){
                        .tag=LANG_VALUE_U64,
                        .as.u64=(uint64_t)
                            iterator->as.iterator.bytes.data[index]
                    }));
                    break;
                }
                LangValue *item = iterable->kind == OBJECT_VEC
                    ? &iterable->as.vector.items[index]
                    : &iterable->as.array.items[index];
                if (iterator->as.iterator.borrowed) {
                    PUSH(*item);
                } else {
                    PUSH(*item);
                    *item = (LangValue){.tag=LANG_VALUE_UNIT};
                }
                break;
            }
            VM_LABEL(drop_local)
            case OP_DROP_LOCAL: {
                vm->active_span = instruction_span;
                size_t slot = (size_t)instruction.a;
                if (initialized[slot]) {
                    LangValue value = LOCAL(slot);
                    initialized[slot] = false;
                    drop_runtime_value(vm, function, slot, value);
                }
                break;
            }
            VM_LABEL(clone)
            case OP_CLONE: {
                vm->active_span = instruction_span;
                if (stack[sp - 1U].tag == LANG_VALUE_OBJECT &&
                    stack[sp - 1U].as.object != NULL &&
                    ((Object *)stack[sp - 1U].as.object)->kind == OBJECT_ARENA) {
                    runtime_error(vm, instruction,
                                  "this native resource cannot be cloned");
                    goto fail;
                }
                LangValue operand = stack[sp - 1U];
                stack[sp - 1U] = vm_value_clone(operand);
                if (instruction.a != 0)
                    vm_value_drop_owned(vm, operand);
                break;
            }
            VM_LABEL(try)
            case OP_TRY: {
                vm->active_span = instruction_span;
                LangValue result = POP();
                if (result.tag != LANG_VALUE_OBJECT ||
                    ((Object *)result.as.object)->kind != OBJECT_STRUCT) {
                    runtime_error(vm, instruction, "`try` received a malformed Result");
                    vm_value_drop_owned(vm, result);
                    goto fail;
                }
                Object *object = result.as.object;
                if (strcmp(object->as.structure.metadata, "Result::Ok") == 0 &&
                    object->as.structure.count == 1U) {
                    LangValue payload = object->as.structure.fields[0];
                    object->as.structure.fields[0] =
                        (LangValue){.tag=LANG_VALUE_UNIT};
                    vm_object_free(vm, object);
                    PUSH(payload);
                } else if (strcmp(object->as.structure.metadata, "Result::Err") == 0 &&
                           object->as.structure.count == 1U) {
                    return finish_frame(
                        vm, function, locals, initialized, references,
                        result, frame_instruction_count);
                } else {
                    runtime_error(vm, instruction, "`try` received an invalid Result tag");
                    vm_value_drop_owned(vm, result);
                    goto fail;
                }
                break;
            }
            VM_LABEL(text_len_local)
            case OP_TEXT_LEN_LOCAL: {
                size_t source = (size_t)instruction.a;
                size_t destination = (size_t)instruction.b;
                LangStringView string;
                if (!initialized[source] ||
                    !lang_value_string_view(
                        &LOCAL(source), &string)) {
                    runtime_error(
                        vm, instruction,
                        "text_len expects a string value");
                    goto fail;
                }
                if (initialized[destination])
                    vm_value_drop_owned(vm, LOCAL(destination));
                LOCAL(destination) = (LangValue){
                    .tag=LANG_VALUE_U64,
                    .as.u64=(uint64_t)string.length
                };
                initialized[destination] = true;
                break;
            }
            VM_LABEL(string_search_local)
            case OP_STRING_SEARCH_LOCAL: {
                uint32_t packed = (uint32_t)instruction.a;
                uint32_t options = (uint32_t)instruction.b;
                size_t value_slot =
                    (size_t)(packed & UINT32_C(0x3ff));
                size_t needle_slot = (size_t)(
                    (packed >> 10U) & UINT32_C(0x3ff));
                size_t destination = (size_t)(
                    (packed >> 20U) & UINT32_C(0x3ff));
                size_t start_slot =
                    (size_t)(options & UINT32_C(0x3ff));
                unsigned kind = options >> 10U;
                LangStringView value;
                LangStringView needle;
                if (!initialized[value_slot] ||
                    !initialized[needle_slot] ||
                    !lang_value_string_view(
                        &LOCAL(value_slot), &value) ||
                    !lang_value_string_view(
                        &LOCAL(needle_slot), &needle) ||
                    (kind == 1U &&
                     (!initialized[start_slot] ||
                      LOCAL(start_slot).tag != LANG_VALUE_U64))) {
                    runtime_error(
                        vm, instruction,
                        "fused ordinal string search received invalid locals");
                    goto fail;
                }
                size_t start = kind == 1U
                    ? (size_t)LOCAL(start_slot).as.u64 : 0U;
                bool oversized_suffix =
                    kind == 3U && needle.length > value.length;
                if (kind == 3U && !oversized_suffix)
                    start = value.length - needle.length;
                int64_t found = oversized_suffix ? -1
                    : vm_string_index_of_ordinal(value, needle, start);
                LangValue search_result;
                if (kind <= 1U)
                    search_result = (LangValue){
                        .tag=LANG_VALUE_I64, .as.i64=found};
                else
                    search_result = (LangValue){
                        .tag=LANG_VALUE_BOOL,
                        .as.boolean=kind == 4U ? found >= 0
                            : found == (int64_t)start};
                drop_runtime_value(
                    vm, function, value_slot, LOCAL(value_slot));
                drop_runtime_value(
                    vm, function, needle_slot, LOCAL(needle_slot));
                initialized[value_slot] = false;
                initialized[needle_slot] = false;
                if (kind == 1U)
                    initialized[start_slot] = false;
                if (initialized[destination])
                    drop_runtime_value(
                        vm, function, destination,
                        LOCAL(destination));
                LOCAL(destination) = search_result;
                initialized[destination] = true;
                break;
            }
            VM_LABEL(string_search_local_constant)
            case OP_STRING_SEARCH_LOCAL_CONSTANT: {
                uint32_t packed = (uint32_t)instruction.a;
                size_t value_slot =
                    (size_t)(packed & UINT32_C(0x3ff));
                size_t destination = (size_t)(
                    (packed >> 10U) & UINT32_C(0x3ff));
                unsigned kind = (packed >> 20U) & UINT32_C(0x7);
                LangStringView value;
                LangValue needle_value = vm->module->constants[
                    (size_t)instruction.b].value;
                LangStringView needle;
                if (!initialized[value_slot] ||
                    !lang_value_string_view(
                        &LOCAL(value_slot), &value) ||
                    !lang_value_string_view(
                        &needle_value, &needle)) {
                    runtime_error(
                        vm, instruction,
                        "fused constant string search received invalid values");
                    goto fail;
                }
                size_t start = 0U;
                bool oversized_suffix =
                    kind == 3U && needle.length > value.length;
                if (kind == 3U && !oversized_suffix)
                    start = value.length - needle.length;
                int64_t found = oversized_suffix ? -1
                    : vm_string_index_of_ordinal(value, needle, start);
                LangValue search_result = kind == 0U
                    ? (LangValue){
                        .tag=LANG_VALUE_I64, .as.i64=found}
                    : (LangValue){
                        .tag=LANG_VALUE_BOOL,
                        .as.boolean=kind == 4U ? found >= 0
                            : found == (int64_t)start};
                if (initialized[destination])
                    drop_runtime_value(
                        vm, function, destination,
                        LOCAL(destination));
                LOCAL(destination) = search_result;
                initialized[destination] = true;
                break;
            }
            VM_LABEL(trap)
            case OP_TRAP: runtime_error(vm, instruction, "explicit trap or unsupported control flow"); goto fail;
            VM_LABEL(exception_set)
            case OP_EXCEPTION_SET:
                if (vm->exception_pending) {
                    vm_value_drop_owned(vm, vm->exception_value);
                    vm->exception_value =
                        (LangValue){.tag=LANG_VALUE_UNIT};
                }
                vm->exception_value = POP();
                vm->exception_pending = true;
                break;
            VM_LABEL(exception_pending)
            case OP_EXCEPTION_PENDING:
                PUSH(((LangValue){
                    .tag=LANG_VALUE_BOOL,
                    .as.boolean=vm->exception_pending
                }));
                break;
            VM_LABEL(exception_match)
            case OP_EXCEPTION_MATCH: {
                LangValue expected = vm->module->constants[instruction.a].value;
                bool matches = false;
                if (vm->exception_pending &&
                    vm->exception_value.tag == LANG_VALUE_OBJECT &&
                    vm->exception_value.as.object != NULL &&
                    expected.tag == LANG_VALUE_STRING_VIEW) {
                    Object *exception =
                        (Object *)vm->exception_value.as.object;
                    const char *metadata =
                        exception->as.structure.metadata;
                    const char *separator = metadata != NULL
                        ? strrchr(metadata, '#') : NULL;
                    if (separator != NULL) metadata = separator + 1;
                    size_t length = expected.as.string.length;
                    matches = strcmp(expected.as.string.data, "Exception") == 0 ||
                        (metadata != NULL &&
                         strncmp(metadata, expected.as.string.data, length) == 0 &&
                         metadata[length] == '|') ||
                        (strcmp(expected.as.string.data,
                                "OperationCanceledException") == 0 &&
                         metadata != NULL &&
                         strncmp(metadata, "TaskCanceledException|",
                                 strlen("TaskCanceledException|")) == 0);
                }
                PUSH(((LangValue){
                    .tag=LANG_VALUE_BOOL, .as.boolean=matches
                }));
                break;
            }
            VM_LABEL(exception_take)
            case OP_EXCEPTION_TAKE:
                if (!vm->exception_pending) {
                    runtime_error(vm, instruction,
                                  "catch entered without a pending exception");
                    goto fail;
                }
                PUSH(vm->exception_value);
                vm->exception_value =
                    (LangValue){.tag=LANG_VALUE_UNIT};
                vm->exception_pending = false;
                break;
            VM_LABEL(propagate_exception)
            case OP_PROPAGATE_EXCEPTION:
                if (!vm->exception_pending) {
                    runtime_error(vm, instruction,
                                  "exception propagation without an exception");
                    goto fail;
                }
                goto fail;
        }
    }
fail:
    for (size_t i = function->local_count; i > 0U; --i)
        if (initialized[i - 1U] &&
            references[i - 1U] == NULL &&
            LOCAL(i - 1U).tag == LANG_VALUE_OBJECT)
            drop_runtime_value(vm, function, i - 1U, LOCAL(i - 1U));
    if (UINT64_MAX - vm->instruction_count < frame_instruction_count)
        vm->instruction_count = UINT64_MAX;
    else
        vm->instruction_count += frame_instruction_count;
    --vm->frame_count;
    if (async_task != NULL) {
        if (!vm->exception_pending)
            vm_raise_exception_message(
                vm, vm->trapped
                    ? "async function trapped"
                    : "async function did not complete");
        LangValue exception = vm->exception_value;
        vm->exception_value = (LangValue){.tag=LANG_VALUE_UNIT};
        vm->exception_pending = false;
        vm_task_fault(vm, async_task, exception);
    }
    return failure;
#undef STORE_SCALAR_RESULT
#undef instruction
#undef instruction_span
#undef VM_LABEL
}

LangValue vm_execute_function(LangVM *vm, size_t function_index,
                              const LangValue *arguments,
                              size_t argument_count,
                              LangSpan call_span) {
    if (function_index < vm->module->function_count &&
        vm->module->functions[function_index].is_async)
        return vm_start_async_function(
            vm, function_index, arguments, argument_count, call_span);
    return vm_execute_function_core(
        vm, function_index, arguments, argument_count, call_span, NULL);
}

LangValue vm_invoke_function_value(
    LangVM *vm, LangValue function_value,
    const LangValue *arguments, size_t argument_count,
    LangSpan call_span
) {
    if (function_value.tag == LANG_VALUE_FUNCTION)
        return vm_execute_function(
            vm, function_value.as.function,
            arguments, argument_count, call_span);
    if (function_value.tag != LANG_VALUE_BOUND_FUNCTION ||
        function_value.as.bound_function.receiver == NULL) {
        vm_runtime_error_at(
            vm, call_span, "indirect call requires a valid function value");
        return (LangValue){.tag=LANG_VALUE_UNIT};
    }
    LangValue *bound_arguments = vm_allocate_uninitialized(
        argument_count + 1U, sizeof(*bound_arguments));
    bound_arguments[0] = (LangValue){
        .tag=LANG_VALUE_RAW_POINTER,
        .as.pointer=function_value.as.bound_function.receiver
    };
    if (argument_count != 0U)
        memcpy(bound_arguments + 1U, arguments,
               argument_count * sizeof(*arguments));
    LangValue result = vm_execute_function(
        vm, function_value.as.bound_function.function,
        bound_arguments, argument_count + 1U, call_span);
    free(bound_arguments);
    return result;
}

void vm_execute_async_task_step(LangVM *vm, Object *task) {
    VmAsyncFrame *frame = task->as.task.frame;
    const BytecodeFunction *function =
        &vm->module->functions[frame->function_index];
    (void)vm_execute_function_core(
        vm, frame->function_index, NULL, function->arity,
        frame->call_span, task);
}

#undef PUSH
#undef POP

int lang_vm_run_module(LangVM *vm, const BytecodeModule *module,
                       const LangSource *source) {
    if (vm == NULL || !vm_verify_bytecode_module(module)) {
        fputs("runtime internal error: malformed bytecode module\n",
              stderr);
        return 2;
    }
    vm->module = module;
    vm->source = source;
    vm->trapped = false;
    if (vm->exception_pending) {
        vm_value_drop_owned(vm, vm->exception_value);
        vm->exception_value = (LangValue){.tag=LANG_VALUE_UNIT};
        vm->exception_pending = false;
    }
    vm->instruction_count = 0U;
    free(vm->static_fields);
    vm->static_fields = module->static_count != 0U
        ? vm_allocate(
              module->static_count, sizeof(*vm->static_fields))
        : NULL;
    vm->static_field_count = module->static_count;
    if (module->static_count != 0U)
        memcpy(vm->static_fields, module->static_defaults,
               module->static_count * sizeof(*vm->static_fields));
    size_t frame_local_stride = 1U;
    for (size_t i = 0U; i < module->function_count; ++i)
        if (module->functions[i].local_count > frame_local_stride)
            frame_local_stride = module->functions[i].local_count;
    if (frame_local_stride != vm->frame_local_stride) {
        if (frame_local_stride > SIZE_MAX / 128U) {
            fputs("runtime internal error: VM frame storage is too large\n",
                  stderr);
            return 2;
        }
        size_t frame_slot_count = frame_local_stride * 128U;
        LangValue *frame_locals = vm_allocate(
            frame_slot_count, sizeof(*frame_locals));
        bool *frame_initialized = vm_allocate(
            frame_slot_count, sizeof(*frame_initialized));
        LangValue **frame_references = vm_allocate(
            frame_slot_count, sizeof(*frame_references));
        Object *frame_html_objects = vm_allocate(
            frame_slot_count, sizeof(*frame_html_objects));
        free(vm->frame_locals);
        free(vm->frame_initialized);
        free(vm->frame_references);
        free(vm->frame_html_objects);
        vm->frame_locals = frame_locals;
        vm->frame_initialized = frame_initialized;
        vm->frame_references = frame_references;
        vm->frame_html_objects = frame_html_objects;
        vm->frame_local_stride = frame_local_stride;
    }
    if (vm->frame_stacks == NULL)
        vm->frame_stacks = vm_allocate(
            128U * 1024U, sizeof(*vm->frame_stacks));
    size_t main_index = module->function_count;
    for (size_t i = 0U; i < module->function_count; ++i)
        if (module->functions[i].is_entry) main_index = i;
    if (main_index == module->function_count)
        for (size_t i = 0U; i < module->function_count; ++i)
            if (strcmp(module->functions[i].name, "main") == 0)
                main_index = i;
    if (main_index == module->function_count) {
        free(vm->static_fields);
        vm->static_fields = NULL;
        vm->static_field_count = 0U;
        return 1;
    }
    LangSpan entry_span = {
        source != NULL ? source->path : NULL, 0U, 0U
    };
    if (module->functions[main_index].code_count != 0U)
        entry_span = module->functions[main_index].spans[0];
    vm_prepare_string_literals(vm, module);
    LangValue result = vm_execute_function(
        vm, main_index, NULL, 0U, entry_span);
    if (!vm->trapped && module->functions[main_index].is_async) {
        LangValue completion = {.tag=LANG_VALUE_UNIT};
        bool completed = vm_run_task_to_completion(
            vm, result, &completion);
        vm_value_drop_owned(vm, result);
        result = completed ? completion
                           : (LangValue){.tag=LANG_VALUE_UNIT};
    }
    if (vm->exception_pending) {
        fputs("unhandled Aster Exception", stderr);
        Object *exception =
            vm->exception_value.tag == LANG_VALUE_OBJECT
            ? vm->exception_value.as.object : NULL;
        LangStringView message;
        if (exception != NULL && exception->kind == OBJECT_STRUCT &&
            exception->as.structure.count >= 1U &&
            lang_value_string_view(
                &exception->as.structure.fields[0], &message)) {
            fputs(": ", stderr);
            if (message.length != 0U)
                (void)fwrite(message.data, 1U, message.length, stderr);
        }
        fputc('\n', stderr);
    }
    int status = (vm->trapped || vm->exception_pending)
        ? 1 : (result.tag == LANG_VALUE_I64 ? (int)result.as.i64 : 0);
    vm_value_drop_owned(vm, result);
    if (vm->exception_pending) {
        vm_value_drop_owned(vm, vm->exception_value);
        vm->exception_value = (LangValue){.tag=LANG_VALUE_UNIT};
        vm->exception_pending = false;
    }
    free(vm->static_fields);
    vm->static_fields = NULL;
    vm->static_field_count = 0U;
    vm_clear_string_literals(vm);
    return status;
}

uint64_t lang_vm_instruction_count(const LangVM *vm) {
    return vm != NULL ? vm->instruction_count : 0U;
}
