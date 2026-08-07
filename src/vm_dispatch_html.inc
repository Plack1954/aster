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
                vm_html_bytes(html, value.data, value.length);
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
