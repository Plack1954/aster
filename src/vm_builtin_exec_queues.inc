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
