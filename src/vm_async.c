#include "internal.h"
#include "vm_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

static int64_t now_milliseconds(void) {
    struct timespec now;
    if (timespec_get(&now, TIME_UTC) != TIME_UTC) return -1;
    return (int64_t)now.tv_sec * INT64_C(1000) +
           (int64_t)now.tv_nsec / INT64_C(1000000);
}

static Object *new_task(void) {
    Object *task = vm_allocate(1U, sizeof(*task));
    task->kind = OBJECT_TASK;
    /* One reference belongs to Aster source and one keeps the scheduled
     * operation alive if source discards its Task before completion. */
    task->references = 2U;
    task->as.task.state = VM_TASK_PENDING;
    task->as.task.result = (LangValue){.tag=LANG_VALUE_UNIT};
    task->as.task.exception = (LangValue){.tag=LANG_VALUE_UNIT};
    return task;
}

static void free_frame(VmAsyncFrame *frame) {
    if (frame == NULL) return;
    free(frame->locals);
    free(frame->initialized);
    free(frame->references);
    free(frame->stack);
    free(frame->html_objects);
    free(frame);
}

void vm_task_destroy(LangVM *vm, Object *task) {
    if (task == NULL) return;
    vm_value_drop_owned(vm, task->as.task.result);
    vm_value_drop_owned(vm, task->as.task.exception);
    if (task->as.task.frame != NULL) {
        VmAsyncFrame *frame = task->as.task.frame;
        if (frame->awaited != NULL)
            vm_object_free(vm, frame->awaited);
        free_frame(frame);
    }
    VmTaskContinuation *continuation = task->as.task.continuations;
    while (continuation != NULL) {
        VmTaskContinuation *next = continuation->next;
        free(continuation);
        continuation = next;
    }
    free(task);
}

void vm_task_release_runtime(LangVM *vm, Object *task) {
    if (task == NULL || task->references == 0U) return;
    --task->references;
    if (task->references == 0U) vm_task_destroy(vm, task);
}

static void resume_continuations(LangVM *vm, Object *task) {
    VmTaskContinuation *continuation = task->as.task.continuations;
    task->as.task.continuations = NULL;
    while (continuation != NULL && !vm->trapped) {
        VmTaskContinuation *next = continuation->next;
        void (*callback)(LangVM *, Object *, void *) =
            continuation->callback;
        void *context = continuation->context;
        free(continuation);
        callback(vm, task, context);
        continuation = next;
    }
    while (continuation != NULL) {
        VmTaskContinuation *next = continuation->next;
        free(continuation);
        continuation = next;
    }
}

static void resume_waiting_task(LangVM *vm, Object *completed,
                                void *context) {
    (void)completed;
    vm_resume_task(vm, context);
}

void vm_task_complete(LangVM *vm, Object *task, LangValue result) {
    if (task == NULL || task->as.task.state != VM_TASK_PENDING) {
        vm_value_drop_owned(vm, result);
        return;
    }
    task->as.task.state = VM_TASK_SUCCEEDED;
    task->as.task.result = result;
    resume_continuations(vm, task);
    vm_task_release_runtime(vm, task);
}

void vm_task_fault(LangVM *vm, Object *task, LangValue exception) {
    if (task == NULL || task->as.task.state != VM_TASK_PENDING) {
        vm_value_drop_owned(vm, exception);
        return;
    }
    task->as.task.state = VM_TASK_FAULTED;
    task->as.task.exception = exception;
    resume_continuations(vm, task);
    vm_task_release_runtime(vm, task);
}

void vm_task_cancel(LangVM *vm, Object *task, LangValue exception) {
    if (task == NULL || task->as.task.state != VM_TASK_PENDING) {
        vm_value_drop_owned(vm, exception);
        return;
    }
    task->as.task.state = VM_TASK_CANCELED;
    task->as.task.exception = exception;
    resume_continuations(vm, task);
    vm_task_release_runtime(vm, task);
}

bool vm_task_suspend(LangVM *vm, Object *task, Object *awaited) {
    (void)vm;
    if (task == NULL || awaited == NULL ||
        awaited->kind != OBJECT_TASK ||
        awaited->as.task.state != VM_TASK_PENDING)
        return false;
    VmTaskContinuation *continuation =
        vm_allocate(1U, sizeof(*continuation));
    continuation->callback = resume_waiting_task;
    continuation->context = task;
    continuation->next = awaited->as.task.continuations;
    awaited->as.task.continuations = continuation;
    task->as.task.frame->awaited = awaited;
    return true;
}

typedef struct VmWhenState {
    Object *output;
    Object *list;
    size_t remaining;
    bool returns_values;
    bool winner_selected;
} VmWhenState;

static void vm_when_state_release(LangVM *vm, VmWhenState *state) {
    vm_object_free(vm, state->list);
    free(state);
}

static void vm_when_all_completed(LangVM *vm, Object *completed,
                                  void *context) {
    (void)completed;
    VmWhenState *state = context;
    if (--state->remaining != 0U) return;
    for (size_t i = 0U; i < state->list->as.vector.count; ++i) {
        Object *task = state->list->as.vector.items[i].as.object;
        if (task->as.task.state == VM_TASK_FAULTED) {
            vm_task_fault(vm, state->output,
                          vm_value_clone(task->as.task.exception));
            vm_when_state_release(vm, state);
            return;
        }
    }
    for (size_t i = 0U; i < state->list->as.vector.count; ++i) {
        Object *task = state->list->as.vector.items[i].as.object;
        if (task->as.task.state == VM_TASK_CANCELED) {
            vm_task_cancel(vm, state->output,
                           vm_value_clone(task->as.task.exception));
            vm_when_state_release(vm, state);
            return;
        }
    }
    if (!state->returns_values) {
        vm_task_complete(vm, state->output,
                         (LangValue){.tag=LANG_VALUE_UNIT});
    } else {
        Object *results = vm_allocate(1U, sizeof(*results));
        results->kind = OBJECT_VEC;
        results->as.vector.count = state->list->as.vector.count;
        results->as.vector.capacity = results->as.vector.count;
        results->as.vector.items = vm_allocate(
            results->as.vector.count, sizeof(*results->as.vector.items));
        for (size_t i = 0U; i < results->as.vector.count; ++i) {
            Object *task = state->list->as.vector.items[i].as.object;
            results->as.vector.items[i] =
                vm_value_clone(task->as.task.result);
        }
        vm_task_complete(vm, state->output,
                         (LangValue){.tag=LANG_VALUE_OBJECT,
                                     .as.object=results});
    }
    vm_when_state_release(vm, state);
}

static void vm_when_any_completed(LangVM *vm, Object *completed,
                                  void *context) {
    VmWhenState *state = context;
    if (!state->winner_selected) {
        state->winner_selected = true;
        vm_task_complete(
            vm, state->output,
            vm_value_clone((LangValue){.tag=LANG_VALUE_OBJECT,
                                       .as.object=completed}));
    }
    if (--state->remaining == 0U)
        vm_when_state_release(vm, state);
}

static bool vm_validate_task_list(LangVM *vm, Object *list,
                                  LangSpan call_span) {
    if (list == NULL || list->kind != OBJECT_VEC) {
        vm_runtime_error_at(vm, call_span,
                            "Task combinator requires a List of Tasks");
        return false;
    }
    for (size_t i = 0U; i < list->as.vector.count; ++i) {
        LangValue item = list->as.vector.items[i];
        if (item.tag != LANG_VALUE_OBJECT || item.as.object == NULL ||
            ((Object *)item.as.object)->kind != OBJECT_TASK) {
            vm_runtime_error_at(vm, call_span,
                                "Task combinator list contains a non-Task value");
            return false;
        }
    }
    return true;
}

LangValue vm_task_when_all(LangVM *vm, Object *list, bool returns_values,
                           LangSpan call_span) {
    if (!vm_validate_task_list(vm, list, call_span)) {
        vm_object_free(vm, list);
        return (LangValue){.tag=LANG_VALUE_UNIT};
    }
    Object *output = new_task();
    if (list->as.vector.count == 0U) {
        if (returns_values) {
            Object *results = vm_allocate(1U, sizeof(*results));
            results->kind = OBJECT_VEC;
            vm_task_complete(vm, output,
                             (LangValue){.tag=LANG_VALUE_OBJECT,
                                         .as.object=results});
        } else {
            vm_task_complete(vm, output,
                             (LangValue){.tag=LANG_VALUE_UNIT});
        }
        vm_object_free(vm, list);
        return (LangValue){.tag=LANG_VALUE_OBJECT, .as.object=output};
    }
    VmWhenState *state = vm_allocate(1U, sizeof(*state));
    state->output = output;
    state->list = list;
    state->remaining = list->as.vector.count;
    state->returns_values = returns_values;
    for (size_t i = 0U; i < list->as.vector.count; ++i) {
        Object *input = list->as.vector.items[i].as.object;
        VmTaskContinuation *continuation =
            vm_allocate(1U, sizeof(*continuation));
        continuation->callback = vm_when_all_completed;
        continuation->context = state;
        continuation->next = input->as.task.continuations;
        input->as.task.continuations = continuation;
        if (input->as.task.state != VM_TASK_PENDING) {
            input->as.task.continuations = continuation->next;
            free(continuation);
            vm_when_all_completed(vm, input, state);
        }
    }
    return (LangValue){.tag=LANG_VALUE_OBJECT, .as.object=output};
}

LangValue vm_task_when_any(LangVM *vm, Object *list, LangSpan call_span) {
    if (!vm_validate_task_list(vm, list, call_span)) {
        vm_object_free(vm, list);
        return (LangValue){.tag=LANG_VALUE_UNIT};
    }
    if (list->as.vector.count == 0U) {
        vm_object_free(vm, list);
        Object *output = new_task();
        vm_raise_exception_typed(
            vm, "ArgumentException",
            "Task.WhenAny requires at least one Task");
        LangValue exception = vm->exception_value;
        vm->exception_value = (LangValue){.tag=LANG_VALUE_UNIT};
        vm->exception_pending = false;
        vm_task_fault(vm, output, exception);
        return (LangValue){.tag=LANG_VALUE_OBJECT, .as.object=output};
    }
    Object *output = new_task();
    VmWhenState *state = vm_allocate(1U, sizeof(*state));
    state->output = output;
    state->list = list;
    state->remaining = list->as.vector.count;
    for (size_t i = 0U; i < list->as.vector.count; ++i) {
        Object *input = list->as.vector.items[i].as.object;
        VmTaskContinuation *continuation =
            vm_allocate(1U, sizeof(*continuation));
        continuation->callback = vm_when_any_completed;
        continuation->context = state;
        continuation->next = input->as.task.continuations;
        input->as.task.continuations = continuation;
        if (input->as.task.state != VM_TASK_PENDING) {
            input->as.task.continuations = continuation->next;
            free(continuation);
            vm_when_any_completed(vm, input, state);
        }
    }
    return (LangValue){.tag=LANG_VALUE_OBJECT, .as.object=output};
}

LangValue vm_start_async_function(LangVM *vm, size_t function_index,
                                  const LangValue *arguments,
                                  size_t argument_count,
                                  LangSpan call_span) {
    const BytecodeFunction *function = &vm->module->functions[function_index];
    Object *task = new_task();
    VmAsyncFrame *frame = vm_allocate(1U, sizeof(*frame));
    frame->function_index = function_index;
    frame->call_span = call_span;
    frame->locals = vm_allocate(function->local_count, sizeof(*frame->locals));
    frame->initialized = vm_allocate(
        function->local_count, sizeof(*frame->initialized));
    frame->references = vm_allocate(
        function->local_count, sizeof(*frame->references));
    frame->stack = vm_allocate(1024U, sizeof(*frame->stack));
    frame->html_objects = vm_allocate(
        function->local_count, sizeof(*frame->html_objects));
    for (size_t i = 0U; i < argument_count; ++i) {
        frame->locals[i] = arguments[i];
        frame->initialized[i] = true;
    }
    task->as.task.frame = frame;
    vm_resume_task(vm, task);
    return (LangValue){.tag=LANG_VALUE_OBJECT, .as.object=task};
}

void vm_resume_task(LangVM *vm, Object *task) {
    if (task == NULL || task->kind != OBJECT_TASK ||
        task->as.task.state != VM_TASK_PENDING ||
        task->as.task.frame == NULL)
        return;
    vm_execute_async_task_step(vm, task);
}

LangValue vm_task_delay(LangVM *vm, int64_t milliseconds,
                        Object *cancellation, LangSpan call_span) {
    (void)call_span;
    if (milliseconds < 0) {
        vm_raise_exception_message(
            vm, "Task.Delay requires nonnegative milliseconds");
        vm_object_free(vm, cancellation);
        return (LangValue){.tag=LANG_VALUE_UNIT};
    }
    Object *task = new_task();
    if (cancellation != NULL && cancellation->as.cancellation.requested) {
        vm_raise_exception_typed(
            vm, "TaskCanceledException", "A task was canceled.");
        LangValue exception = vm->exception_value;
        vm->exception_value = (LangValue){.tag=LANG_VALUE_UNIT};
        vm->exception_pending = false;
        vm_task_cancel(vm, task, exception);
        vm_object_free(vm, cancellation);
        return (LangValue){.tag=LANG_VALUE_OBJECT, .as.object=task};
    }
    VmTimer *timer = vm_allocate(1U, sizeof(*timer));
    int64_t now = now_milliseconds();
    if (now < 0) {
        vm_raise_exception_message(vm, "could not read clock for Task.Delay");
        vm_task_release_runtime(vm, task);
        vm_object_free(vm, task);
        vm_object_free(vm, cancellation);
        return (LangValue){.tag=LANG_VALUE_UNIT};
    }
    timer->task = task;
    timer->cancellation = cancellation;
    timer->deadline_milliseconds =
        milliseconds > INT64_MAX - now ? INT64_MAX : now + milliseconds;
    timer->next = vm->timers;
    vm->timers = timer;
    return (LangValue){.tag=LANG_VALUE_OBJECT, .as.object=task};
}

static bool run_due_timer(LangVM *vm) {
    int64_t now = now_milliseconds();
    VmTimer **link = &vm->timers;
    while (*link != NULL) {
        VmTimer *timer = *link;
        bool canceled = timer->cancellation != NULL &&
            timer->cancellation->as.cancellation.requested;
        if (!canceled && timer->deadline_milliseconds > now) {
            link = &timer->next;
            continue;
        }
        *link = timer->next;
        Object *task = timer->task;
        Object *cancellation = timer->cancellation;
        free(timer);
        if (canceled) {
            vm_raise_exception_typed(
                vm, "TaskCanceledException", "A task was canceled.");
            LangValue exception = vm->exception_value;
            vm->exception_value = (LangValue){.tag=LANG_VALUE_UNIT};
            vm->exception_pending = false;
            vm_task_cancel(vm, task, exception);
        } else {
            vm_task_complete(vm, task, (LangValue){.tag=LANG_VALUE_UNIT});
        }
        vm_object_free(vm, cancellation);
        return true;
    }
    return false;
}

static int64_t next_timer_deadline(const LangVM *vm) {
    int64_t deadline = INT64_MAX;
    for (const VmTimer *timer = vm->timers;
         timer != NULL; timer = timer->next)
        if (timer->deadline_milliseconds < deadline)
            deadline = timer->deadline_milliseconds;
    return deadline;
}

bool vm_run_task_to_completion(LangVM *vm, LangValue value,
                               LangValue *out_result) {
    if (value.tag != LANG_VALUE_OBJECT || value.as.object == NULL ||
        ((Object *)value.as.object)->kind != OBJECT_TASK)
        return false;
    Object *task = value.as.object;
    while (task->as.task.state == VM_TASK_PENDING && !vm->trapped) {
        if (run_due_timer(vm)) continue;
        int64_t deadline = next_timer_deadline(vm);
        if (deadline == INT64_MAX) {
            vm_runtime_error_at(vm, (LangSpan){0},
                                "async task has no operation able to complete it");
            break;
        }
        int64_t now = now_milliseconds();
        int64_t wait = deadline > now ? deadline - now : 0;
        struct timespec duration = {
            .tv_sec = (time_t)(wait / INT64_C(1000)),
            .tv_nsec = (long)((wait % INT64_C(1000)) * INT64_C(1000000))
        };
        (void)thrd_sleep(&duration, NULL);
    }
    if (task->as.task.state == VM_TASK_SUCCEEDED) {
        *out_result = vm_value_clone(task->as.task.result);
        return true;
    }
    if (task->as.task.state == VM_TASK_FAULTED ||
        task->as.task.state == VM_TASK_CANCELED) {
        if (vm->exception_pending)
            vm_value_drop_owned(vm, vm->exception_value);
        vm->exception_value = vm_value_clone(task->as.task.exception);
        vm->exception_pending = true;
    }
    return false;
}
