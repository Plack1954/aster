    if (index == -24) {
        char *data = NULL;
        size_t length = 0U;
        size_t capacity = 0U;
        for (size_t i = 0U; i < count; ++i) {
            char formatted[64];
            LangStringView segment;
            int formatted_length = 0;
            if (!lang_value_string_view(&args[i], &segment)) {
                if (args[i].tag == LANG_VALUE_I64)
                    formatted_length = snprintf(
                        formatted, sizeof(formatted),
                        "%" PRId64, args[i].as.i64);
                else if (args[i].tag == LANG_VALUE_U64)
                    formatted_length = snprintf(
                        formatted, sizeof(formatted),
                        "%" PRIu64, args[i].as.u64);
                else if (args[i].tag == LANG_VALUE_F64)
                    formatted_length = snprintf(
                        formatted, sizeof(formatted),
                        "%g", args[i].as.f64);
                else if (args[i].tag == LANG_VALUE_BOOL)
                    formatted_length = snprintf(
                        formatted, sizeof(formatted), "%s",
                        args[i].as.boolean
                            ? "true" : "false");
                else
                    formatted_length = -1;
                if (formatted_length < 0 ||
                    (size_t)formatted_length >=
                        sizeof(formatted)) {
                    free(data);
                    runtime_error(
                        vm, instruction,
                        "unsupported interpolation value");
                    return false;
                }
                segment = (LangStringView){
                    formatted,
                    (size_t)formatted_length
                };
            }
            if (segment.length >
                SIZE_MAX - length - 1U) {
                free(data);
                runtime_error(
                    vm, instruction,
                    "interpolated String is too large");
                return false;
            }
            size_t required =
                length + segment.length + 1U;
            if (required > capacity) {
                size_t next =
                    capacity == 0U ? 32U : capacity;
                while (next < required) {
                    if (next > SIZE_MAX / 2U) {
                        next = required;
                        break;
                    }
                    next *= 2U;
                }
                char *grown = realloc(data, next);
                if (grown == NULL) {
                    free(data);
                    runtime_error(
                        vm, instruction,
                        "out of memory growing interpolated String");
                    return false;
                }
                data = grown;
                capacity = next;
            }
            if (segment.length != 0U)
                memcpy(
                    data + length, segment.data,
                    segment.length);
            length += segment.length;
        }
        if (data == NULL)
            data = vm_allocate(1U, 1U);
        data[length] = '\0';
        Object *string = vm_allocate(1U, sizeof(*string));
        string->kind = OBJECT_STRING;
        string->references = 1U;
        string->as.string.data = data;
        string->as.string.length = length;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = string;
        return true;
    }
    if (index == -20 && count == 1U) {
        LangStringView source;
        if (!lang_value_string_view(&args[0], &source)) {
            runtime_error(vm, instruction,
                          "Html::unsafe_raw expects a string view");
            return false;
        }
        Object *html = vm_allocate(1U, sizeof(*html));
        html->kind = OBJECT_HTML;
        html->as.html.capacity = source.length;
        html->as.html.data = vm_allocate(html->as.html.capacity, 1U);
        if (source.length != 0U)
            memcpy(html->as.html.data, source.data, source.length);
        html->as.html.length = source.length;
        html->as.html.tag = "";
        html->as.html.tag_length = 0U;
        html->as.html.open = false;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = html;
        return true;
    }
    if ((index == -21 || index == -95) && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_BUFFER) {
        Object *buffer = args[0].as.object;
        result->tag = LANG_VALUE_BYTE_SLICE;
        result->as.bytes.data = buffer->as.buffer.data;
        result->as.bytes.length = buffer->as.buffer.length;
        args[0] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if (index == -22 && count == 1U) {
        LangStringView text;
        if (!lang_value_string_view(&args[0], &text)) {
            runtime_error(vm, instruction,
                          "text_len expects a string value");
            return false;
        }
        result->tag = LANG_VALUE_U64;
        result->as.u64 = (uint64_t)text.length;
        return true;
    }
