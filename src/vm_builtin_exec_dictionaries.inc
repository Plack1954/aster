    if (index == -53 && count == 0U) {
        Object *dictionary = vm_allocate(1U, sizeof(*dictionary));
        dictionary->kind = OBJECT_DICTIONARY;
        result->tag = LANG_VALUE_OBJECT;
        result->as.object = dictionary;
        return true;
    }
    if (index == -84 && count == 3U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_DICTIONARY &&
        args[2].tag == LANG_VALUE_RAW_POINTER &&
        args[2].as.pointer != NULL) {
        Object *dictionary = args[0].as.object;
        size_t found = vm_dictionary_find(dictionary, args[1]);
        result->tag = LANG_VALUE_BOOL;
        result->as.boolean = found != SIZE_MAX;
        LangValue *output = args[2].as.pointer;
        vm_write_out_default(vm, output);
        if (found != SIZE_MAX) {
            *output = vm_value_clone(
                dictionary->as.dictionary.items[found + 1U]);
        }
        return true;
    }
    if (index <= -54 && index >= -65 && count >= 1U &&
        args[0].tag == LANG_VALUE_OBJECT &&
        ((Object *)args[0].as.object)->kind == OBJECT_DICTIONARY) {
        Object *dictionary = args[0].as.object;
        if (index == -55 && count == 1U) {
            result->tag = LANG_VALUE_U64;
            result->as.u64 =
                (uint64_t)(dictionary->as.dictionary.count / 2U);
            return true;
        }
        if (index == -65 && count == 1U) {
            result->tag = LANG_VALUE_U64;
            result->as.u64 =
                (uint64_t)(dictionary->as.dictionary.capacity / 2U);
            return true;
        }
        if (index == -58 && count == 1U) {
            for (size_t i = dictionary->as.dictionary.count; i > 0U; --i) {
                vm_value_drop_owned(
                    vm, dictionary->as.dictionary.items[i - 1U]);
                dictionary->as.dictionary.items[i - 1U] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
            }
            dictionary->as.dictionary.count = 0U;
            if (!vm_dictionary_rebuild(dictionary)) {
                runtime_error(vm, instruction,
                              "could not clear Dictionary hash index");
                return false;
            }
            return true;
        }
        if (index == -63 && count == 2U &&
            args[1].tag == LANG_VALUE_U64) {
            if (args[1].as.u64 > (uint64_t)(SIZE_MAX / 2U) ||
                !vm_dictionary_reserve(
                    dictionary, (size_t)args[1].as.u64)) {
                runtime_error(vm, instruction,
                              "could not ensure Dictionary capacity");
                return false;
            }
            result->tag = LANG_VALUE_U64;
            result->as.u64 =
                (uint64_t)(dictionary->as.dictionary.capacity / 2U);
            return true;
        }
        if (index == -64 && (count == 1U || count == 2U) &&
            (count == 1U || args[1].tag == LANG_VALUE_U64)) {
            size_t logical_count = dictionary->as.dictionary.count / 2U;
            uint64_t requested = count == 1U
                ? (uint64_t)logical_count : args[1].as.u64;
            if (requested < (uint64_t)logical_count ||
                requested > (uint64_t)(SIZE_MAX / 2U)) {
                runtime_error(vm, instruction,
                              "Dictionary capacity cannot be less than Count");
                return false;
            }
            if (!vm_dictionary_set_capacity(
                    dictionary, (size_t)requested)) {
                runtime_error(vm, instruction,
                              "could not trim Dictionary capacity");
                return false;
            }
            return true;
        }
        if (count < 2U) {
            runtime_error(vm, instruction, "invalid Dictionary call");
            return false;
        }
        size_t found = SIZE_MAX;
        if (index == -62) {
            for (size_t i = 1U;
                 i < dictionary->as.dictionary.count; i += 2U)
                if (vm_list_value_equal(
                        dictionary->as.dictionary.items[i], args[1])) {
                    found = i;
                    break;
                }
        } else {
            found = vm_dictionary_find(dictionary, args[1]);
        }
        if ((index == -56 || index == -62) && count == 2U) {
            result->tag = LANG_VALUE_BOOL;
            result->as.boolean = found != SIZE_MAX;
            return true;
        }
        if (index == -57 && count == 2U) {
            result->tag = LANG_VALUE_BOOL;
            result->as.boolean = found != SIZE_MAX;
            if (found == SIZE_MAX) return true;
            vm_value_drop_owned(
                vm, dictionary->as.dictionary.items[found]);
            vm_value_drop_owned(
                vm, dictionary->as.dictionary.items[found + 1U]);
            if (!vm_dictionary_erase(dictionary, found)) {
                runtime_error(vm, instruction,
                              "could not remove Dictionary hash entry");
                return false;
            }
            return true;
        }
        if (index == -59 && count == 2U) {
            if (found == SIZE_MAX) {
                runtime_error(vm, instruction, "Dictionary key was not found");
                return false;
            }
            *result = vm_value_clone(
                dictionary->as.dictionary.items[found + 1U]);
            return true;
        }
        if ((index == -54 || index == -60 || index == -61) && count == 3U) {
            if (found != SIZE_MAX) {
                if (index == -54) {
                    runtime_error(vm, instruction,
                                  "Dictionary already contains the key");
                    return false;
                }
                if (index == -61) {
                    result->tag = LANG_VALUE_BOOL;
                    result->as.boolean = false;
                    return true;
                }
                vm_value_drop_owned(vm,
                    dictionary->as.dictionary.items[found + 1U]);
                dictionary->as.dictionary.items[found + 1U] = args[2];
                args[2] = (LangValue){.tag=LANG_VALUE_UNIT};
                return true;
            }
            if (!vm_dictionary_reserve(
                    dictionary,
                    dictionary->as.dictionary.count / 2U + 1U)) {
                runtime_error(vm, instruction,
                              "out of memory growing Dictionary");
                return false;
            }
            size_t physical_index = dictionary->as.dictionary.count;
            dictionary->as.dictionary.items[
                dictionary->as.dictionary.count++] = args[1];
            dictionary->as.dictionary.items[
                dictionary->as.dictionary.count++] = args[2];
            if (!vm_dictionary_insert_bucket(
                    dictionary, physical_index)) {
                runtime_error(vm, instruction,
                              "could not index Dictionary key");
                return false;
            }
            args[1] = (LangValue){.tag=LANG_VALUE_UNIT};
            args[2] = (LangValue){.tag=LANG_VALUE_UNIT};
            if (index == -61) {
                result->tag = LANG_VALUE_BOOL;
                result->as.boolean = true;
            }
            return true;
        }
    }
