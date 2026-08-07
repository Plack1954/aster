    if (strcmp(name, "List::Add") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Add` expects a List and one element");
            return &type_unit;
        }
        Type *vector = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             vector->element);
        Type *item = expr->as.call.arguments.items[1]->type;
        if (!same_type(vector->element, item))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "`List::Add` expects `%s`, found `%s`",
                      vector->element->name, item->name);
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::Count") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC)
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Count` expects one List value");
        return &type_usize;
    }
    if (strcmp(name, "List::Capacity") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC)
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Capacity` expects one List value");
        return &type_usize;
    }
    if (strcmp(name, "List::Clear") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Clear` expects one List value");
            return &type_unit;
        }
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::Insert") == 0 ||
        strcmp(name, "List::Set") == 0) {
        const char *member = strcmp(name, "List::Insert") == 0
                           ? "Insert" : "index assignment";
        if (expr->as.call.arguments.count != 3U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.%s` expects a List, a nuint index, and an element",
                      member);
            return &type_unit;
        }
        Type *vector = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[1], &type_usize);
        if (!same_type(expr->as.call.arguments.items[1]->type, &type_usize))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "List index expects `nuint`, found `%s`",
                      expr->as.call.arguments.items[1]->type->name);
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[2], vector->element);
        if (!same_type(expr->as.call.arguments.items[2]->type,
                       vector->element))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[2]->span,
                      "List element expects `%s`, found `%s`",
                      vector->element->name,
                      expr->as.call.arguments.items[2]->type->name);
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::RemoveAt") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.RemoveAt` expects a List and a nuint index");
            return &type_unit;
        }
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[1], &type_usize);
        if (!same_type(expr->as.call.arguments.items[1]->type, &type_usize))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "List index expects `nuint`, found `%s`",
                      expr->as.call.arguments.items[1]->type->name);
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::Contains") == 0 ||
        strcmp(name, "List::IndexOf") == 0 ||
        strcmp(name, "List::LastIndexOf") == 0 ||
        strcmp(name, "List::Remove") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects a List and one element", name);
            return strcmp(name, "List::Contains") == 0 ||
                   strcmp(name, "List::Remove") == 0
                 ? &type_bool : &type_i32;
        }
        Type *vector = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[1], vector->element);
        if (!same_type(expr->as.call.arguments.items[1]->type,
                       vector->element))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "List element expects `%s`, found `%s`",
                      vector->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (!list_element_has_default_equality(vector->element))
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` requires an element type with defined equality; `%s` does not yet provide it",
                      name, vector->element->name);
        if (strcmp(name, "List::Remove") == 0 &&
            expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return strcmp(name, "List::Contains") == 0 ||
               strcmp(name, "List::Remove") == 0
             ? &type_bool : &type_i32;
    }
    if (strcmp(name, "List::AddRange") == 0 ||
        strcmp(name, "List::InsertRange") == 0) {
        bool insert = strcmp(name, "List::InsertRange") == 0;
        size_t expected_count = insert ? 3U : 2U;
        size_t source_index = insert ? 2U : 1U;
        if (expr->as.call.arguments.count != expected_count ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC ||
            expr->as.call.arguments.items[source_index]->type->kind !=
                TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects compatible List values%s",
                      name, insert ? " and a nuint index" : "");
            return &type_unit;
        }
        Type *target = expr->as.call.arguments.items[0]->type;
        Type *source = expr->as.call.arguments.items[source_index]->type;
        if (!same_type(target, source))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[source_index]->span,
                      "`%s` expects `%s`, found `%s`",
                      name, target->name, source->name);
        if (insert) {
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
            if (!same_type(expr->as.call.arguments.items[1]->type,
                           &type_usize))
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[1]->span,
                          "List index expects `nuint`, found `%s`",
                          expr->as.call.arguments.items[1]->type->name);
        }
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::RemoveRange") == 0 ||
        strcmp(name, "List::GetRange") == 0) {
        bool get = strcmp(name, "List::GetRange") == 0;
        if (expr->as.call.arguments.count != 3U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects a List, a nuint index, and a nuint count",
                      name);
            return get ? &type_error : &type_unit;
        }
        for (size_t i = 1U; i < 3U; ++i) {
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[i], &type_usize);
            if (!same_type(expr->as.call.arguments.items[i]->type,
                           &type_usize))
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[i]->span,
                          "List range expects `nuint`, found `%s`",
                          expr->as.call.arguments.items[i]->type->name);
        }
        if (!get && expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return get ? expr->as.call.arguments.items[0]->type : &type_unit;
    }
    if (strcmp(name, "List::Reverse") == 0) {
        size_t count = expr->as.call.arguments.count;
        if ((count != 1U && count != 3U) ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Reverse` expects no arguments or a nuint index and count");
            return &type_unit;
        }
        if (count == 3U) {
            for (size_t i = 1U; i < 3U; ++i) {
                (void)coerce_literal(
                    checker, expr->as.call.arguments.items[i], &type_usize);
                if (!same_type(expr->as.call.arguments.items[i]->type,
                               &type_usize))
                    lang_diag(checker->diagnostics,
                              expr->as.call.arguments.items[i]->span,
                              "List range expects `nuint`, found `%s`",
                              expr->as.call.arguments.items[i]->type->name);
            }
        }
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::EnsureCapacity") == 0 ||
        strcmp(name, "List::SetCapacity") == 0) {
        bool ensure = strcmp(name, "List::EnsureCapacity") == 0;
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects a List and a nuint capacity", name);
            return ensure ? &type_usize : &type_unit;
        }
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[1], &type_usize);
        if (!same_type(expr->as.call.arguments.items[1]->type, &type_usize))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "List capacity expects `nuint`, found `%s`",
                      expr->as.call.arguments.items[1]->type->name);
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return ensure ? &type_usize : &type_unit;
    }
    if (strcmp(name, "List::TrimExcess") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.TrimExcess` expects one List value");
            return &type_unit;
        }
        if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "List::Exists") == 0 ||
        strcmp(name, "List::FindAll") == 0 ||
        strcmp(name, "List::FindIndex") == 0 ||
        strcmp(name, "List::FindLastIndex") == 0 ||
        strcmp(name, "List::RemoveAll") == 0 ||
        strcmp(name, "List::ForEach") == 0 ||
        strcmp(name, "List::TrueForAll") == 0) {
        bool indexed = strcmp(name, "List::FindIndex") == 0 ||
                       strcmp(name, "List::FindLastIndex") == 0;
        size_t count = expr->as.call.arguments.count;
        bool arity_ok = count == 2U;
        if (!arity_ok ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` received an invalid argument list", name);
            return strcmp(name, "List::FindAll") == 0
                 ? &type_error
                 : strcmp(name, "List::ForEach") == 0
                 ? &type_unit
                 : (indexed || strcmp(name, "List::RemoveAll") == 0)
                 ? &type_i32 : &type_bool;
        }
        Type *list = expr->as.call.arguments.items[0]->type;
        size_t callback_index = count - 1U;
        Type *callback =
            expr->as.call.arguments.items[callback_index]->type;
        bool action = strcmp(name, "List::ForEach") == 0;
        if (callback->kind != TYPE_FUNCTION ||
            callback->argument_count != 1U ||
            !same_type(callback->arguments[0], list->element) ||
            !same_type(callback->element,
                       action ? &type_unit : &type_bool) ||
            (callback->argument_count != 0U &&
             callback->parameter_modes[0] != PARAMETER_MODE_VALUE))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[callback_index]->span,
                      "`%s` expects a function taking `%s` and returning `%s`",
                      name, list->element->name,
                      action ? "void" : "bool");
        for (size_t i = 1U; i < callback_index; ++i) {
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[i], &type_usize);
            if (!same_type(expr->as.call.arguments.items[i]->type,
                           &type_usize))
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[i]->span,
                          "List range expects `nuint`, found `%s`",
                          expr->as.call.arguments.items[i]->type->name);
        }
        if (strcmp(name, "List::RemoveAll") == 0 &&
            expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *local = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (local != NULL && !local->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable List `%s`", local->name);
        }
        if (strcmp(name, "List::FindAll") == 0) return list;
        if (strcmp(name, "List::ForEach") == 0) return &type_unit;
        if (indexed || strcmp(name, "List::RemoveAll") == 0)
            return &type_i32;
        return &type_bool;
    }
    if (strcmp(name, "List::Get") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`List.Get` expects a List and a nuint index");
            return &type_error;
        }
        Type *vector = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[1], &type_usize);
        if (!same_type(
                expr->as.call.arguments.items[1]->type, &type_usize))
            lang_diag(
                checker->diagnostics,
                expr->as.call.arguments.items[1]->span,
                "`List::Get` index expects `nuint`, found `%s`",
                expr->as.call.arguments.items[1]->type->name);
        if (!type_is_copyable(checker, vector->element))
            lang_diag(
                checker->diagnostics, expr->span,
                "`List::Get` cannot copy noncopyable element type `%s`",
                vector->element->name);
        return vector->element;
    }
