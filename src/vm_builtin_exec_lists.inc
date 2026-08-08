    if (index == -33 && count == 3U &&
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
        list->as.vector.items[at] = args[2];
        args[2] = (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if ((index == -34 || index == -35 || index == -36 || index == -37) &&
        count == 2U && args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *list = args[0].as.object;
        size_t found = SIZE_MAX;
        if (index == -36) {
            for (size_t i = list->as.vector.count; i > 0U; --i) {
                if (vm_list_value_equal(
                        list->as.vector.items[i - 1U], args[1])) {
                    found = i - 1U;
                    break;
                }
            }
        } else {
            for (size_t i = 0U; i < list->as.vector.count; ++i) {
                if (vm_list_value_equal(list->as.vector.items[i], args[1])) {
                    found = i;
                    break;
                }
            }
        }
        if (index == -34) {
            result->tag = LANG_VALUE_BOOL;
            result->as.boolean = found != SIZE_MAX;
            return true;
        }
        if (index == -35 || index == -36) {
            if (found != SIZE_MAX && found > (size_t)INT32_MAX) {
                runtime_error(vm, instruction,
                              "List index exceeds int range");
                return false;
            }
            result->tag = LANG_VALUE_I64;
            result->as.i64 = found == SIZE_MAX ? -1 : (int64_t)found;
            return true;
        }
        result->tag = LANG_VALUE_BOOL;
        result->as.boolean = found != SIZE_MAX;
        if (found == SIZE_MAX) return true;
        vm_value_drop_owned(vm, list->as.vector.items[found]);
        memmove(list->as.vector.items + found,
                list->as.vector.items + found + 1U,
                (list->as.vector.count - found - 1U) *
                    sizeof(*list->as.vector.items));
        --list->as.vector.count;
        list->as.vector.items[list->as.vector.count] =
            (LangValue){.tag=LANG_VALUE_UNIT};
        return true;
    }
    if ((index == -38 || index == -39) &&
        count == (index == -38 ? 2U : 3U) &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        size_t source_argument = index == -38 ? 1U : 2U;
        if (args[source_argument].tag != LANG_VALUE_OBJECT ||
            ((Object *)args[source_argument].as.object)->kind != OBJECT_VEC) {
            runtime_error(vm, instruction, "List range source is invalid");
            return false;
        }
        Object *target = args[0].as.object;
        Object *source = args[source_argument].as.object;
        if (source == target) {
            args[source_argument] = vm_value_clone(args[source_argument]);
            source = args[source_argument].as.object;
        }
        size_t at = target->as.vector.count;
        if (index == -39) {
            if (args[1].tag != LANG_VALUE_U64 ||
                args[1].as.u64 > (uint64_t)SIZE_MAX ||
                (size_t)args[1].as.u64 > target->as.vector.count) {
                runtime_error(vm, instruction,
                              "List insert index out of bounds");
                return false;
            }
            at = (size_t)args[1].as.u64;
        }
        if (source->as.vector.count >
            SIZE_MAX - target->as.vector.count) {
            runtime_error(vm, instruction, "List capacity overflow");
            return false;
        }
        size_t required =
            target->as.vector.count + source->as.vector.count;
        if (!vm_list_reserve(target, required)) {
            runtime_error(vm, instruction, "out of memory growing List");
            return false;
        }
        size_t added = source->as.vector.count;
        memmove(target->as.vector.items + at + added,
                target->as.vector.items + at,
                (target->as.vector.count - at) *
                    sizeof(*target->as.vector.items));
        if (added != 0U)
            memcpy(target->as.vector.items + at,
                   source->as.vector.items,
                   added * sizeof(*target->as.vector.items));
        target->as.vector.count = required;
        source->as.vector.count = 0U;
        return true;
    }
    if ((index == -40 || index == -41) && count == 3U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64 &&
        args[2].tag == LANG_VALUE_U64) {
        Object *source = args[0].as.object;
        uint64_t raw_at = args[1].as.u64;
        uint64_t raw_count = args[2].as.u64;
        if (raw_at > (uint64_t)SIZE_MAX ||
            raw_count > (uint64_t)SIZE_MAX ||
            (size_t)raw_at > source->as.vector.count ||
            (size_t)raw_count >
                source->as.vector.count - (size_t)raw_at) {
            runtime_error(vm, instruction, "List range out of bounds");
            return false;
        }
        size_t at = (size_t)raw_at;
        size_t range_count = (size_t)raw_count;
        if (index == -40) {
            for (size_t i = range_count; i > 0U; --i)
                vm_value_drop_owned(
                    vm, source->as.vector.items[at + i - 1U]);
            memmove(source->as.vector.items + at,
                    source->as.vector.items + at + range_count,
                    (source->as.vector.count - at - range_count) *
                        sizeof(*source->as.vector.items));
            source->as.vector.count -= range_count;
            for (size_t i = source->as.vector.count;
                 i < source->as.vector.count + range_count; ++i)
                source->as.vector.items[i] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
            return true;
        }
        Object *range = vm_allocate(1U, sizeof(*range));
        range->kind = OBJECT_VEC;
        if (!vm_list_reserve(range, range_count)) {
            free(range);
            runtime_error(vm, instruction, "out of memory creating List range");
            return false;
        }
        range->as.vector.count = range_count;
        for (size_t i = 0U; i < range_count; ++i)
            range->as.vector.items[i] =
                vm_value_clone(source->as.vector.items[at + i]);
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = range;
        return true;
    }
    if (index == -42 && (count == 1U || count == 3U) &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *list = args[0].as.object;
        size_t at = 0U;
        size_t range_count = list->as.vector.count;
        if (count == 3U) {
            if (args[1].tag != LANG_VALUE_U64 ||
                args[2].tag != LANG_VALUE_U64 ||
                args[1].as.u64 > (uint64_t)SIZE_MAX ||
                args[2].as.u64 > (uint64_t)SIZE_MAX ||
                (size_t)args[1].as.u64 > list->as.vector.count ||
                (size_t)args[2].as.u64 >
                    list->as.vector.count - (size_t)args[1].as.u64) {
                runtime_error(vm, instruction, "List range out of bounds");
                return false;
            }
            at = (size_t)args[1].as.u64;
            range_count = (size_t)args[2].as.u64;
        }
        for (size_t left = at, right = at + range_count;
             left < right && left < --right; ++left) {
            LangValue temporary = list->as.vector.items[left];
            list->as.vector.items[left] = list->as.vector.items[right];
            list->as.vector.items[right] = temporary;
        }
        return true;
    }
    if (index == -43 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64) {
        Object *list = args[0].as.object;
        if (args[1].as.u64 > (uint64_t)SIZE_MAX ||
            !vm_list_reserve(list, (size_t)args[1].as.u64)) {
            runtime_error(vm, instruction, "could not ensure List capacity");
            return false;
        }
        result->tag = LANG_VALUE_U64;
        result->as.u64 = (uint64_t)list->as.vector.capacity;
        return true;
    }
    if (index == -44 && count == 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *list = args[0].as.object;
        if (list->as.vector.count <
            vm_list_trim_threshold(list->as.vector.capacity) &&
            !vm_list_set_capacity(list, list->as.vector.count)) {
            runtime_error(vm, instruction, "could not trim List capacity");
            return false;
        }
        return true;
    }
    if (index == -45 && count == 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC &&
        args[1].tag == LANG_VALUE_U64) {
        Object *list = args[0].as.object;
        if (args[1].as.u64 > (uint64_t)SIZE_MAX ||
            (size_t)args[1].as.u64 < list->as.vector.count) {
            runtime_error(vm, instruction,
                          "List Capacity cannot be less than Count");
            return false;
        }
        if (!vm_list_set_capacity(list, (size_t)args[1].as.u64)) {
            runtime_error(vm, instruction, "could not set List capacity");
            return false;
        }
        return true;
    }
    if (index <= -46 && index >= -52 && count >= 2U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_VEC) {
        Object *list = args[0].as.object;
        bool indexed = index == -48 || index == -49;
        size_t callback_argument = count - 1U;
        LangValue callback = args[callback_argument];
        if ((callback.tag != LANG_VALUE_FUNCTION &&
             callback.tag != LANG_VALUE_BOUND_FUNCTION) ||
            (!indexed && count != 2U) ||
            (indexed && count != 2U && count != 3U && count != 4U)) {
            runtime_error(vm, instruction, "invalid List callback");
            return false;
        }
        if (index == -46 || index == -52) {
            bool answer = index == -52;
            for (size_t i = 0U; i < list->as.vector.count; ++i) {
                bool matched = false;
                if (!vm_list_call_callback(
                        vm, callback, list->as.vector.items[i],
                        instruction_span, true, &matched)) {
                    if (!vm->trapped && !vm->exception_pending)
                        runtime_error(
                            vm, instruction, "invalid List predicate");
                    return false;
                }
                if ((index == -46 && matched) ||
                    (index == -52 && !matched)) {
                    answer = index == -46;
                    break;
                }
            }
            result->tag = LANG_VALUE_BOOL;
            result->as.boolean = answer;
            return true;
        }
        if (index == -47) {
            Object *matches = vm_allocate(1U, sizeof(*matches));
            matches->kind = OBJECT_VEC;
            for (size_t i = 0U; i < list->as.vector.count; ++i) {
                bool matched = false;
                if (!vm_list_call_callback(
                        vm, callback, list->as.vector.items[i],
                        instruction_span, true, &matched)) {
                    vm_object_free(vm, matches);
                    if (!vm->trapped && !vm->exception_pending)
                        runtime_error(
                            vm, instruction, "invalid List predicate");
                    return false;
                }
                if (!matched) continue;
                if (!vm_list_reserve(matches, matches->as.vector.count + 1U)) {
                    vm_object_free(vm, matches);
                    runtime_error(vm, instruction,
                                  "out of memory growing List");
                    return false;
                }
                LangValue copied;
                if (!vm_value_semantic_copy(
                        vm, list->as.vector.items[i], instruction_span,
                        &copied)) {
                    vm_object_free(vm, matches);
                    return false;
                }
                matches->as.vector.items[matches->as.vector.count++] = copied;
            }
            result->tag = LANG_VALUE_OBJECT;
            result->as.object = matches;
            return true;
        }
        if (index == -48 || index == -49) {
            bool reverse = index == -49;
            size_t start = reverse && list->as.vector.count != 0U
                         ? list->as.vector.count - 1U : 0U;
            size_t range_count = list->as.vector.count;
            if (count >= 3U) {
                if (args[1].tag != LANG_VALUE_U64 ||
                    args[1].as.u64 > (uint64_t)SIZE_MAX) {
                    runtime_error(vm, instruction, "List range out of bounds");
                    return false;
                }
                start = (size_t)args[1].as.u64;
                if (!reverse) {
                    if (start > list->as.vector.count) {
                        runtime_error(vm, instruction,
                                      "List range out of bounds");
                        return false;
                    }
                    range_count = count == 3U
                        ? list->as.vector.count - start : 0U;
                } else {
                    if (list->as.vector.count == 0U ||
                        start >= list->as.vector.count) {
                        runtime_error(vm, instruction,
                                      "List range out of bounds");
                        return false;
                    }
                    range_count = count == 3U ? start + 1U : 0U;
                }
            }
            if (count == 4U) {
                if (args[2].tag != LANG_VALUE_U64 ||
                    args[2].as.u64 > (uint64_t)SIZE_MAX) {
                    runtime_error(vm, instruction, "List range out of bounds");
                    return false;
                }
                range_count = (size_t)args[2].as.u64;
                bool valid = !reverse
                    ? range_count <= list->as.vector.count - start
                    : range_count <= start + 1U;
                if (!valid) {
                    runtime_error(vm, instruction, "List range out of bounds");
                    return false;
                }
            }
            size_t found = SIZE_MAX;
            for (size_t offset = 0U; offset < range_count; ++offset) {
                size_t at = reverse ? start - offset : start + offset;
                bool matched = false;
                if (!vm_list_call_callback(
                        vm, callback, list->as.vector.items[at],
                        instruction_span, true, &matched)) {
                    if (!vm->trapped && !vm->exception_pending)
                        runtime_error(
                            vm, instruction, "invalid List predicate");
                    return false;
                }
                if (matched) { found = at; break; }
            }
            if (found != SIZE_MAX && found > (size_t)INT32_MAX) {
                runtime_error(vm, instruction, "List index exceeds int range");
                return false;
            }
            result->tag = LANG_VALUE_I64;
            result->as.i64 = found == SIZE_MAX ? -1 : (int64_t)found;
            return true;
        }
        if (index == -50) {
            size_t write = 0U;
            size_t removed = 0U;
            for (size_t read = 0U; read < list->as.vector.count; ++read) {
                bool matched = false;
                if (!vm_list_call_callback(
                        vm, callback, list->as.vector.items[read],
                        instruction_span, true, &matched)) {
                    if (!vm->trapped && !vm->exception_pending)
                        runtime_error(
                            vm, instruction, "invalid List predicate");
                    return false;
                }
                if (matched) {
                    vm_value_drop_owned(vm, list->as.vector.items[read]);
                    ++removed;
                } else {
                    if (write != read)
                        list->as.vector.items[write] =
                            list->as.vector.items[read];
                    ++write;
                }
            }
            for (size_t i = write; i < list->as.vector.count; ++i)
                list->as.vector.items[i] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
            list->as.vector.count = write;
            if (removed > (size_t)INT32_MAX) {
                runtime_error(vm, instruction,
                              "removed count exceeds int range");
                return false;
            }
            result->tag = LANG_VALUE_I64;
            result->as.i64 = (int64_t)removed;
            return true;
        }
        for (size_t i = 0U; i < list->as.vector.count; ++i) {
            if (!vm_list_call_callback(
                    vm, callback, list->as.vector.items[i],
                    instruction_span, false, NULL)) {
                if (!vm->trapped && !vm->exception_pending)
                    runtime_error(vm, instruction, "invalid List action");
                return false;
            }
        }
        return true;
    }
