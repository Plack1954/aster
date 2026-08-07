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
                     iterable->kind != OBJECT_QUEUE &&
                     iterable->kind != OBJECT_STRING)) {
                    vm_value_drop_owned(vm, value);
                    runtime_error(
                        vm, instruction,
                        "iteration requires a supported collection");
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
                     iterable->kind != OBJECT_QUEUE &&
                     iterable->kind != OBJECT_STRING)) {
                    runtime_error(
                        vm, instruction,
                        "borrowed iteration requires a supported collection local");
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
                    : array->kind == OBJECT_QUEUE
                        ? array->as.queue.count
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
                    } else if (array->kind == OBJECT_QUEUE) {
                        size_t physical =
                            (array->as.queue.head + index) %
                            array->as.queue.capacity;
                        LOCAL(slot) = iterator->as.iterator.borrowed
                            ? array->as.queue.items[physical]
                            : array->as.queue.items[physical];
                        if (!iterator->as.iterator.borrowed)
                            array->as.queue.items[physical] =
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
                    : iterable->kind == OBJECT_QUEUE
                        ? iterable->as.queue.count
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
                    : iterable->kind == OBJECT_QUEUE
                        ? iterable->as.queue.count
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
                    : iterable->kind == OBJECT_QUEUE
                    ? &iterable->as.queue.items[
                        (iterable->as.queue.head + index) %
                        iterable->as.queue.capacity]
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
