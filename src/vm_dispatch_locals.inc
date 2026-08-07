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
            VM_LABEL(default_local)
            case OP_DEFAULT_LOCAL: {
                size_t slot = (size_t)instruction.a;
                if (!initialized[slot]) {
                    runtime_error(vm, instruction,
                                  "defaulting an unavailable local");
                    goto fail;
                }
                LangValueTag tag = LOCAL(slot).tag;
                vm_value_drop_owned(vm, LOCAL(slot));
                LOCAL(slot) = (LangValue){.tag=tag};
                break;
            }
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
