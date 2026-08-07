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
            VM_LABEL(default_field_local)
            case OP_DEFAULT_FIELD_LOCAL: {
                size_t slot = (size_t)instruction.a;
                Object *object = initialized[slot] &&
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
                                  "field default requires a struct");
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
                        LangValueTag tag = object->as.structure
                            .fields[field_index].tag;
                        vm_value_drop_owned(
                            vm, object->as.structure.fields[field_index]);
                        object->as.structure.fields[field_index] =
                            (LangValue){.tag=tag};
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
            VM_LABEL(borrow_payload)
            case OP_BORROW_PAYLOAD: {
                LangValue aggregate = POP();
                if (aggregate.tag != LANG_VALUE_OBJECT ||
                    aggregate.as.object == NULL ||
                    ((Object *)aggregate.as.object)->kind != OBJECT_STRUCT ||
                    ((Object *)aggregate.as.object)->as.structure.count != 1U) {
                    runtime_error(vm, instruction,
                                  "enum payload borrow requires one payload");
                    goto fail;
                }
                PUSH(((Object *)aggregate.as.object)->as.structure.fields[0]);
                break;
            }
            VM_LABEL(collection_count)
            case OP_COLLECTION_COUNT: {
                LangValue collection = POP();
                Object *object = collection.tag == LANG_VALUE_OBJECT
                    ? collection.as.object : NULL;
                if (object == NULL ||
                    (object->kind != OBJECT_DICTIONARY &&
                     object->kind != OBJECT_VEC &&
                     object->kind != OBJECT_QUEUE)) {
                    runtime_error(vm, instruction,
                                  "collection count requires a collection");
                    goto fail;
                }
                size_t count = object->kind == OBJECT_DICTIONARY
                    ? object->as.dictionary.count / 2U
                    : object->kind == OBJECT_QUEUE
                        ? object->as.queue.count
                        : object->as.vector.count;
                PUSH(((LangValue){
                    .tag=LANG_VALUE_U64,
                    .as.u64=(uint64_t)count
                }));
                break;
            }
            VM_LABEL(dictionary_key_borrow)
            case OP_DICTIONARY_KEY_BORROW:
            VM_LABEL(dictionary_value_borrow)
            case OP_DICTIONARY_VALUE_BORROW: {
                LangValue index_value = POP();
                LangValue collection = POP();
                Object *object = collection.tag == LANG_VALUE_OBJECT
                    ? collection.as.object : NULL;
                if (object == NULL || object->kind != OBJECT_DICTIONARY ||
                    index_value.tag != LANG_VALUE_U64 ||
                    index_value.as.u64 > (uint64_t)SIZE_MAX ||
                    (size_t)index_value.as.u64 >=
                        object->as.dictionary.count / 2U) {
                    runtime_error(vm, instruction,
                                  "dictionary copy index out of bounds");
                    goto fail;
                }
                size_t physical = (size_t)index_value.as.u64 * 2U +
                    (instruction.op == OP_DICTIONARY_VALUE_BORROW ? 1U : 0U);
                PUSH(object->as.dictionary.items[physical]);
                break;
            }
            VM_LABEL(list_element_borrow)
            case OP_LIST_ELEMENT_BORROW: {
                LangValue index_value = POP();
                LangValue collection = POP();
                Object *object = collection.tag == LANG_VALUE_OBJECT
                    ? collection.as.object : NULL;
                if (object == NULL || object->kind != OBJECT_VEC ||
                    index_value.tag != LANG_VALUE_U64 ||
                    index_value.as.u64 > (uint64_t)SIZE_MAX ||
                    (size_t)index_value.as.u64 >= object->as.vector.count) {
                    runtime_error(vm, instruction,
                                  "List index out of bounds");
                    goto fail;
                }
                PUSH(object->as.vector.items[
                    (size_t)index_value.as.u64]);
                break;
            }
            VM_LABEL(queue_front_borrow)
            case OP_QUEUE_FRONT_BORROW: {
                LangValue collection = POP();
                Object *object = collection.tag == LANG_VALUE_OBJECT
                    ? collection.as.object : NULL;
                if (object == NULL || object->kind != OBJECT_QUEUE ||
                    object->as.queue.count == 0U) {
                    runtime_error(vm, instruction, "Queue is empty");
                    goto fail;
                }
                PUSH(object->as.queue.items[object->as.queue.head]);
                break;
            }
            VM_LABEL(stack_top_borrow)
            case OP_STACK_TOP_BORROW: {
                LangValue collection = POP();
                Object *object = collection.tag == LANG_VALUE_OBJECT
                    ? collection.as.object : NULL;
                if (object == NULL || object->kind != OBJECT_VEC ||
                    object->as.vector.count == 0U) {
                    runtime_error(vm, instruction, "Stack is empty");
                    goto fail;
                }
                PUSH(object->as.vector.items[
                    object->as.vector.count - 1U]);
                break;
            }
            VM_LABEL(dictionary_get_borrow)
            case OP_DICTIONARY_GET_BORROW: {
                LangValue key = POP();
                LangValue collection = POP();
                Object *object = collection.tag == LANG_VALUE_OBJECT
                    ? collection.as.object : NULL;
                if (object == NULL || object->kind != OBJECT_DICTIONARY) {
                    vm_value_drop_owned(vm, key);
                    runtime_error(vm, instruction,
                                  "Dictionary key not found");
                    goto fail;
                }
                size_t found = vm_dictionary_find(object, key);
                vm_value_drop_owned(vm, key);
                if (found == SIZE_MAX) {
                    runtime_error(vm, instruction,
                                  "Dictionary key not found");
                    goto fail;
                }
                PUSH(object->as.dictionary.items[found + 1U]);
                break;
            }
            VM_LABEL(dictionary_find)
            case OP_DICTIONARY_FIND: {
                LangValue key = POP();
                LangValue collection = POP();
                Object *object = collection.tag == LANG_VALUE_OBJECT
                    ? collection.as.object : NULL;
                if (object == NULL || object->kind != OBJECT_DICTIONARY) {
                    vm_value_drop_owned(vm, key);
                    runtime_error(vm, instruction,
                                  "dictionary find requires a dictionary");
                    goto fail;
                }
                size_t found = vm_dictionary_find(object, key);
                vm_value_drop_owned(vm, key);
                PUSH(((LangValue){
                    .tag=LANG_VALUE_U64,
                    .as.u64=found == SIZE_MAX
                        ? UINT64_MAX : (uint64_t)(found / 2U)
                }));
                break;
            }
