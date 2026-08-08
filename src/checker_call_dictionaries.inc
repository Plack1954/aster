    if (strcmp(name, "Dictionary::Add") == 0 ||
        strcmp(name, "Dictionary::TryAdd") == 0 ||
        strcmp(name, "Dictionary::Set") == 0) {
        bool set = strcmp(name, "Dictionary::Set") == 0;
        bool try_add = strcmp(name, "Dictionary::TryAdd") == 0;
        if (expr->as.call.arguments.count != 3U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.%s` expects a Dictionary, key, and value",
                      set ? "Set" : "Add");
            return &type_unit;
        }
        Type *dictionary = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             dictionary->element);
        (void)coerce_literal(checker, expr->as.call.arguments.items[2],
                             dictionary->error_type);
        if (!same_type(dictionary->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Dictionary key expects `%s`, found `%s`",
                      dictionary->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (!same_type(dictionary->error_type,
                       expr->as.call.arguments.items[2]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[2]->span,
                      "Dictionary value expects `%s`, found `%s`",
                      dictionary->error_type->name,
                      expr->as.call.arguments.items[2]->type->name);
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return try_add ? &type_bool : &type_unit;
    }
    if (strcmp(name, "Dictionary::Count") == 0 ||
        strcmp(name, "Dictionary::Clear") == 0) {
        bool count = strcmp(name, "Dictionary::Count") == 0;
        if (expr->as.call.arguments.count != 1U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type))
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.%s` expects one Dictionary value",
                      count ? "Count" : "Clear");
        if (!count && expr->as.call.arguments.count == 1U)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        return count ? &type_usize : &type_unit;
    }
    if (strcmp(name, "Dictionary::ContainsKey") == 0 ||
        strcmp(name, "Dictionary::Remove") == 0 ||
        strcmp(name, "Dictionary::Get") == 0) {
        bool get = strcmp(name, "Dictionary::Get") == 0;
        if (expr->as.call.arguments.count != 2U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects a Dictionary and key", name);
            return get ? &type_error : &type_bool;
        }
        Type *dictionary = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             dictionary->element);
        if (!same_type(dictionary->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Dictionary key expects `%s`, found `%s`",
                      dictionary->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (strcmp(name, "Dictionary::Remove") == 0)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        if (get && !type_is_copyable(checker, dictionary->error_type))
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary::Get` cannot copy noncopyable value type `%s`",
                      dictionary->error_type->name);
        return get ? dictionary->error_type : &type_bool;
    }
    if (strcmp(name, "Dictionary::TryGetValue") == 0) {
        if (expr->as.call.arguments.count != 3U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.TryGetValue` expects a Dictionary, key, and out value");
            return &type_bool;
        }
        Type *dictionary = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             dictionary->element);
        if (!same_type(dictionary->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Dictionary key expects `%s`, found `%s`",
                      dictionary->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (call_argument_mode(expr, 2U) != PARAMETER_MODE_OUT)
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[2]->span,
                      "argument 3 to `Dictionary::TryGetValue` must use `out`");
        if (!same_type(dictionary->error_type,
                       expr->as.call.arguments.items[2]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[2]->span,
                      "Dictionary output expects `%s`, found `%s`",
                      dictionary->error_type->name,
                      expr->as.call.arguments.items[2]->type->name);
        if (!type_is_copyable(checker, dictionary->error_type))
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary::TryGetValue` cannot copy noncopyable value type `%s`",
                      dictionary->error_type->name);
        return &type_bool;
    }
    if (strcmp(name, "Dictionary::ContainsValue") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.ContainsValue` expects a Dictionary and value");
            return &type_bool;
        }
        Type *dictionary = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             dictionary->error_type);
        if (!same_type(dictionary->error_type,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Dictionary value expects `%s`, found `%s`",
                      dictionary->error_type->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (!list_element_has_default_equality(dictionary->error_type))
            lang_diag(checker->diagnostics, expr->span,
                      "Dictionary value type `%s` does not have built-in equality",
                      dictionary->error_type->name);
        return &type_bool;
    }
    if (strcmp(name, "Dictionary::EnsureCapacity") == 0) {
        if (expr->as.call.arguments.count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
        if (expr->as.call.arguments.count != 2U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type) ||
            !same_type(expr->as.call.arguments.items[1]->type, &type_usize)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.EnsureCapacity` expects a Dictionary and nuint capacity");
            return &type_usize;
        }
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_usize;
    }
    if (strcmp(name, "Dictionary::TrimExcess") == 0) {
        size_t count = expr->as.call.arguments.count;
        if (count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
        if ((count != 1U && count != 2U) ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type) ||
            (count == 2U && !same_type(
                expr->as.call.arguments.items[1]->type, &type_usize))) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.TrimExcess` expects a Dictionary and optional nuint capacity");
            return &type_unit;
        }
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_unit;
    }
    if (strcmp(name, "Dictionary::Capacity") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            !is_hash_storage_type(expr->as.call.arguments.items[0]->type))
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.Capacity` expects one Dictionary value");
        return &type_usize;
    }
    if (strcmp(name, "Dictionary::KeyAt") == 0 ||
        strcmp(name, "Dictionary::ValueAt") == 0) {
        bool key = strcmp(name, "Dictionary::KeyAt") == 0;
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_DICTIONARY) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary.%s` expects a Dictionary and nuint index",
                      key ? "KeyAt" : "ValueAt");
            return &type_error;
        }
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             &type_usize);
        if (!same_type(expr->as.call.arguments.items[1]->type, &type_usize))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Dictionary index expects `nuint`, found `%s`",
                      expr->as.call.arguments.items[1]->type->name);
        Type *dictionary = expr->as.call.arguments.items[0]->type;
        Type *result = key ? dictionary->element : dictionary->error_type;
        if (!type_is_copyable(checker, result))
            lang_diag(checker->diagnostics, expr->span,
                      "`Dictionary::%s` cannot copy noncopyable type `%s`",
                      key ? "KeyAt" : "ValueAt", result->name);
        return result;
    }
