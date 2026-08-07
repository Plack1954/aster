    if ((index == -91 || index == -92 || index == -93 || index == -94) &&
        (count == 2U || (index == -91 && count == 3U))) {
        LangStringView value;
        LangStringView needle;
        if (!lang_value_string_view(&args[0], &value) ||
            !lang_value_string_view(&args[1], &needle) ||
            (count == 3U && args[2].tag != LANG_VALUE_U64)) {
            runtime_error(vm, instruction,
                          "ordinal string search received invalid arguments");
            return false;
        }
        size_t start = count == 3U
            ? (args[2].as.u64 <= (uint64_t)SIZE_MAX
               ? (size_t)args[2].as.u64 : SIZE_MAX)
            : 0U;
        if (index == -93) {
            if (needle.length > value.length) {
                *result = (LangValue){
                    .tag=LANG_VALUE_BOOL, .as.boolean=false};
                return true;
            }
            start = value.length - needle.length;
        }
        int64_t found = vm_string_index_of_ordinal(value, needle, start);
        if (index == -91)
            *result = (LangValue){.tag=LANG_VALUE_I64, .as.i64=found};
        else
            *result = (LangValue){
                .tag=LANG_VALUE_BOOL,
                .as.boolean=index == -94 ? found >= 0
                    : found == (int64_t)start};
        return true;
    }
    if ((index == -1 || index == -2 || index == -89 || index == -90) &&
        count == 1U) {
        FILE *stream = (index == -1 || index == -89) ? stdout : stderr;
        print_value(stream, args[0]);
        if (index == -1 || index == -2)
            fputc('\n', stream);
        (void)fflush(stream);
        return true;
    }
    if (index == -3 && count == 1U && args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_HTML) {
        Object *html = args[0].as.object;
        if (html->as.html.destination != NULL || html->as.html.open) {
            runtime_error(
                vm, instruction,
                "cannot convert unfinished or attached Html to string");
            return false;
        }
        return lang_string_value(
            vm,
            (LangStringView){html->as.html.data, html->as.html.length},
            result);
    }
    if (index == -4 && count == 1U && args[0].tag == LANG_VALUE_I64 &&
        args[0].as.i64 >= 0) {
        Object *buffer = vm_allocate(1U, sizeof(*buffer));
        buffer->kind = OBJECT_BUFFER;
        buffer->as.buffer.length = (size_t)args[0].as.i64;
        buffer->as.buffer.data = vm_allocate(buffer->as.buffer.length, 1U);
        result->tag = LANG_VALUE_OBJECT; result->as.object = buffer;
        return true;
    }
    if (index == -5 && count == 0U) {
        Object *arena = vm_allocate(1U, sizeof(*arena));
        arena->kind = OBJECT_ARENA;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = arena;
        return true;
    }
    if (index == -6 && count == 2U && args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_ARENA &&
        args[1].tag == LANG_VALUE_I64 && args[1].as.i64 >= 0) {
        Object *arena = args[0].as.object;
        size_t size = (size_t)args[1].as.i64;
        RawAllocation *allocation = vm_allocate(1U, sizeof(*allocation));
        allocation->data = vm_allocate(size == 0U ? 1U : size, 1U);
        allocation->length = size;
        allocation->active = true;
        if (vm->raw_allocation_count == vm->raw_allocation_capacity) {
            size_t next = vm->raw_allocation_capacity == 0U ? 16U
                                     : vm->raw_allocation_capacity * 2U;
            RawAllocation **items =
                realloc(vm->raw_allocations, next * sizeof(*items));
            if (items == NULL) {
                free(allocation->data);
                free(allocation);
                runtime_error(vm, instruction,
                              "out of memory tracking arena pointer");
                return false;
            }
            vm->raw_allocations = items;
            vm->raw_allocation_capacity = next;
        }
        vm->raw_allocations[vm->raw_allocation_count++] = allocation;
        if (arena->as.arena.count == arena->as.arena.capacity) {
            size_t next = arena->as.arena.capacity == 0U ? 8U
                                                         : arena->as.arena.capacity * 2U;
            RawAllocation **blocks =
                realloc(arena->as.arena.blocks, next * sizeof(*blocks));
            if (blocks == NULL) {
                allocation->active = false;
                free(allocation->data);
                allocation->data = NULL;
                runtime_error(vm, instruction, "out of memory growing arena");
                return false;
            }
            arena->as.arena.blocks = blocks;
            arena->as.arena.capacity = next;
        }
        arena->as.arena.blocks[arena->as.arena.count++] = allocation;
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        result->tag = LANG_VALUE_RAW_POINTER;
        result->as.pointer = allocation;
        return true;
    }
    if (index == -7 && count == 1U && args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_ARENA) {
        Object *arena = args[0].as.object;
        for (size_t i = arena->as.arena.count; i > 0U; --i) {
            RawAllocation *allocation = arena->as.arena.blocks[i - 1U];
            if (allocation->active) free(allocation->data);
            allocation->data = NULL;
            allocation->active = false;
        }
        arena->as.arena.count = 0U;
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -8 && count == 1U &&
        args[0].tag == LANG_VALUE_RAW_POINTER) {
        RawAllocation *allocation = args[0].as.pointer;
        if (allocation == NULL || !allocation->active ||
            allocation->length < sizeof(int64_t)) {
            runtime_error(vm, instruction,
                          "invalid, expired, or undersized raw pointer load");
            return false;
        }
        int64_t value;
        memcpy(&value, allocation->data, sizeof(value));
        result->tag = LANG_VALUE_I64;
        result->as.i64 = value;
        return true;
    }
    if (index == -9 && count == 2U &&
        args[0].tag == LANG_VALUE_RAW_POINTER &&
        args[1].tag == LANG_VALUE_I64) {
        RawAllocation *allocation = args[0].as.pointer;
        if (allocation == NULL || !allocation->active ||
            allocation->length < sizeof(int64_t)) {
            runtime_error(vm, instruction,
                          "invalid, expired, or undersized raw pointer store");
            return false;
        }
        memcpy(allocation->data, &args[1].as.i64, sizeof(int64_t));
        return true;
    }
    if (index == -10 && count == 1U) {
        LangStringView message;
        if (!lang_value_string_view(&args[0], &message)) {
            runtime_error(vm, instruction, "panic expects a string message");
            return false;
        }
        char rendered[257];
        size_t length = message.length < sizeof(rendered) - 1U
                      ? message.length : sizeof(rendered) - 1U;
        if (length != 0U) memcpy(rendered, message.data, length);
        rendered[length] = '\0';
        runtime_error(vm, instruction, rendered);
        return false;
    }
    if ((index == -11 || index == -15 || index == -16) && count == 1U) {
        if ((index == -11 || index == -15) &&
            args[0].tag == LANG_VALUE_OBJECT &&
            args[0].as.object != NULL &&
            ((Object *)args[0].as.object)->kind ==
                OBJECT_STRING) {
            *result = args[0];
            args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
            return true;
        }
        LangStringView source;
        if (!lang_value_string_view(&args[0], &source)) {
            runtime_error(vm, instruction,
                          "owned string constructor expects a string view");
            return false;
        }
        if (index == -11 &&
            vm_owned_string_from_view(vm, source, result))
            return true;
        bool fragment = index == -16;
        if (source.length > SIZE_MAX - (fragment ? 2U : 1U)) {
            runtime_error(vm, instruction, "string is too large");
            return false;
        }
        Object *string = vm_allocate(1U, sizeof(*string));
        string->kind = OBJECT_STRING;
        string->references = 1U;
        string->as.string.length = source.length + (fragment ? 1U : 0U);
        string->as.string.data =
            vm_allocate(string->as.string.length + 1U, 1U);
        size_t offset = 0U;
        if (fragment) string->as.string.data[offset++] = '#';
        if (source.length != 0U)
            memcpy(string->as.string.data + offset,
                   source.data, source.length);
        string->as.string.data[string->as.string.length] = '\0';
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = string;
        return true;
    }
    if (index == -12 && count == 0U) {
        Object *builder = vm_allocate(1U, sizeof(*builder));
        builder->kind = OBJECT_STRING_BUILDER;
        builder->as.string_builder.capacity = 64U;
        builder->as.string_builder.data =
            vm_allocate(builder->as.string_builder.capacity, 1U);
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = builder;
        return true;
    }
    if (index == -13 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER) {
        Object *builder = args[0].as.object;
        if (!vm_string_builder_append_value(builder, args[1])) {
            runtime_error(
                vm, instruction,
                "could not grow StringBuilder");
            return false;
        }
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -89 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER &&
        args[1].tag == LANG_VALUE_U64 && args[1].as.u64 <= 0x10ffffU &&
        !(args[1].as.u64 >= 0xd800U && args[1].as.u64 <= 0xdfffU)) {
        uint32_t scalar = (uint32_t)args[1].as.u64;
        char encoded[4];
        size_t length;
        if (scalar <= 0x7fU) {
            encoded[0] = (char)scalar; length = 1U;
        } else if (scalar <= 0x7ffU) {
            encoded[0] = (char)(0xc0U | (scalar >> 6U));
            encoded[1] = (char)(0x80U | (scalar & 0x3fU)); length = 2U;
        } else if (scalar <= 0xffffU) {
            encoded[0] = (char)(0xe0U | (scalar >> 12U));
            encoded[1] = (char)(0x80U | ((scalar >> 6U) & 0x3fU));
            encoded[2] = (char)(0x80U | (scalar & 0x3fU)); length = 3U;
        } else {
            encoded[0] = (char)(0xf0U | (scalar >> 18U));
            encoded[1] = (char)(0x80U | ((scalar >> 12U) & 0x3fU));
            encoded[2] = (char)(0x80U | ((scalar >> 6U) & 0x3fU));
            encoded[3] = (char)(0x80U | (scalar & 0x3fU)); length = 4U;
        }
        if (!vm_string_builder_append_bytes(
                args[0].as.object, encoded, length)) {
            runtime_error(vm, instruction, "could not grow StringBuilder");
            return false;
        }
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -25 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER &&
        args[1].tag == LANG_VALUE_U64) {
        unsigned char byte = (unsigned char)args[1].as.u64;
        Object *builder = args[0].as.object;
        if (!vm_string_builder_append_bytes(
                builder, (const char *)&byte, 1U)) {
            runtime_error(vm, instruction, "could not grow StringBuilder");
            return false;
        }
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -14 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER) {
        Object *builder = args[0].as.object;
        char *data = builder->as.string_builder.data;
        size_t length = builder->as.string_builder.length;
        bool embedded_data =
            builder->as.string_builder.embedded_data;
        builder->kind = OBJECT_STRING;
        builder->references = 1U;
        builder->as.string.data = data;
        builder->as.string.length = length;
        builder->as.string.embedded_data = embedded_data;
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = builder;
        return true;
    }
    if (index == -26 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER) {
        Object *builder = args[0].as.object;
        Object *string = vm_allocate(1U, sizeof(*string));
        string->kind = OBJECT_STRING;
        string->references = 1U;
        string->as.string.length = builder->as.string_builder.length;
        string->as.string.data = vm_allocate(
            string->as.string.length + 1U, 1U);
        if (string->as.string.length != 0U)
            memcpy(string->as.string.data,
                   builder->as.string_builder.data,
                   string->as.string.length);
        string->as.string.data[string->as.string.length] = '\0';
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = string;
        return true;
    }
    if (index == -27 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER) {
        result->tag = LANG_VALUE_U64;
        result->as.u64 =
            ((Object *)args[0].as.object)->as.string_builder.length;
        return true;
    }
    if (index == -28 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_STRING_BUILDER) {
        ((Object *)args[0].as.object)->as.string_builder.length = 0U;
        return true;
    }
    if (index == -17 && count == 0U) {
        Object *vector = vm_allocate(1U, sizeof(*vector));
        vector->kind = OBJECT_VEC;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = vector;
        return true;
    }
    if (index == -18 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *vector = args[0].as.object;
        if (!vm_list_reserve(vector, vector->as.vector.count + 1U)) {
            runtime_error(vm, instruction, "out of memory growing List");
            return false;
        }
        vector->as.vector.items[vector->as.vector.count++] = args[1];
        args[1] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -19 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *vector = args[0].as.object;
        result->tag = LANG_VALUE_U64;
        result->as.u64 = (uint64_t)vector->as.vector.count;
        return true;
    }
    if (index == -23 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64) {
        Object *vector = args[0].as.object;
        uint64_t index_value = args[1].as.u64;
        if (index_value > (uint64_t)SIZE_MAX ||
            (size_t)index_value >= vector->as.vector.count) {
            runtime_error(vm, instruction, "List index out of bounds");
            return false;
        }
        *result = vm_value_clone(
            vector->as.vector.items[(size_t)index_value]);
        args[1] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -29 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        result->tag = LANG_VALUE_U64;
        result->as.u64 = (uint64_t)
            ((Object *)args[0].as.object)->as.vector.capacity;
        return true;
    }
    if (index == -30 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *list = args[0].as.object;
        for (size_t i = list->as.vector.count; i > 0U; --i) {
            vm_value_drop_owned(vm, list->as.vector.items[i - 1U]);
            list->as.vector.items[i - 1U] =
                (LangValue){.tag=LANG_VALUE_UNIT};
        }
        list->as.vector.count = 0U;
        return true;
    }
    if (index == -31 && count == 3U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64) {
        Object *list = args[0].as.object;
        uint64_t raw_index = args[1].as.u64;
        if (raw_index > (uint64_t)SIZE_MAX ||
            (size_t)raw_index > list->as.vector.count) {
            runtime_error(vm, instruction, "List insert index out of bounds");
            return false;
        }
        if (!vm_list_reserve(list, list->as.vector.count + 1U)) {
            runtime_error(vm, instruction, "out of memory growing List");
            return false;
        }
        size_t at = (size_t)raw_index;
        memmove(list->as.vector.items + at + 1U,
                list->as.vector.items + at,
                (list->as.vector.count - at) * sizeof(*list->as.vector.items));
        list->as.vector.items[at] = args[2];
        ++list->as.vector.count;
        args[2] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -32 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64) {
        Object *list = args[0].as.object;
        uint64_t raw_index = args[1].as.u64;
        if (raw_index > (uint64_t)SIZE_MAX ||
            (size_t)raw_index >= list->as.vector.count) {
            runtime_error(vm, instruction, "List index out of bounds");
            return false;
        }
        size_t at = (size_t)raw_index;
        vm_value_drop_owned(vm, list->as.vector.items[at]);
        memmove(list->as.vector.items + at,
                list->as.vector.items + at + 1U,
                (list->as.vector.count - at - 1U) *
                    sizeof(*list->as.vector.items));
        --list->as.vector.count;
        list->as.vector.items[list->as.vector.count] =
            (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
