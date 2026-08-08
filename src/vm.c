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

#define PUSH(value_) do { if (sp >= stack_capacity) { runtime_error(vm, instruction, "operand stack overflow"); goto fail; } stack[sp++] = (value_); } while (0)
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
    size_t stack_capacity = async_frame != NULL
        ? async_frame->stack_capacity : 1024U;
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
        if (sp >= stack_capacity) {
            vm_runtime_error_at(
                vm, call_span, "operand stack overflow on async resume");
            vm_object_free(vm, awaited);
            goto fail;
        }
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
        [OP_DEFAULT_LOCAL]=&&vm_dispatch_default_local,
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
        [OP_GET_INDEX_LOCAL_MOVE]=&&vm_dispatch_get_index_local_move,
        [OP_SET_INDEX_LOCAL]=&&vm_dispatch_set_index_local,
        [OP_MAKE_STRUCT]=&&vm_dispatch_make_struct,
        [OP_MAKE_CLASS]=&&vm_dispatch_make_class,
        [OP_DELETE_CLASS]=&&vm_dispatch_delete_class,
        [OP_GET_FIELD]=&&vm_dispatch_get_field,
        [OP_GET_FIELD_LOCAL]=&&vm_dispatch_get_field_local,
        [OP_GET_FIELD_LOCAL_MOVE]=&&vm_dispatch_get_field_local_move,
        [OP_GET_FIELD_BORROW]=&&vm_dispatch_get_field_borrow,
        [OP_SET_FIELD_LOCAL]=&&vm_dispatch_set_field_local,
        [OP_DEFAULT_FIELD_LOCAL]=&&vm_dispatch_default_field_local,
        [OP_GET_TAG]=&&vm_dispatch_get_tag,
        [OP_TAKE_PAYLOAD]=&&vm_dispatch_take_payload,
        [OP_BORROW_PAYLOAD]=&&vm_dispatch_borrow_payload,
        [OP_COLLECTION_COUNT]=&&vm_dispatch_collection_count,
        [OP_DICTIONARY_KEY_BORROW]=&&vm_dispatch_dictionary_key_borrow,
        [OP_DICTIONARY_VALUE_BORROW]=&&vm_dispatch_dictionary_value_borrow,
        [OP_LIST_ELEMENT_BORROW]=&&vm_dispatch_list_element_borrow,
        [OP_QUEUE_FRONT_BORROW]=&&vm_dispatch_queue_front_borrow,
        [OP_STACK_TOP_BORROW]=&&vm_dispatch_stack_top_borrow,
        [OP_DICTIONARY_GET_BORROW]=&&vm_dispatch_dictionary_get_borrow,
        [OP_DICTIONARY_FIND]=&&vm_dispatch_dictionary_find,
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
#include "vm_dispatch_locals.inc"
#include "vm_dispatch_arithmetic.inc"
#include "vm_dispatch_calls.inc"
#include "vm_dispatch_aggregates.inc"
#include "vm_dispatch_html.inc"
#include "vm_dispatch_iteration.inc"
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
