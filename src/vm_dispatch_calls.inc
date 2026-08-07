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
