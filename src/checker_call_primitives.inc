    if (strcmp(name, "Task::WhenAll") == 0 ||
        strcmp(name, "Task::WhenAny") == 0) {
        bool when_all = strcmp(name, "Task::WhenAll") == 0;
        if (expr->as.call.arguments.count != 1U) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Task.%s` expects one List<Task<T>> value",
                      when_all ? "WhenAll" : "WhenAny");
            return &type_error;
        }
        Type *list = expr->as.call.arguments.items[0]->type;
        if (list == NULL || list->kind != TYPE_VEC ||
            list->element == NULL || list->element->kind != TYPE_TASK) {
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[0]->span,
                      "`Task.%s` expects a List<Task<T>>, found `%s`",
                      when_all ? "WhenAll" : "WhenAny",
                      list != NULL ? list->name : "<unknown>");
            return &type_error;
        }
        Type *result_element = when_all ? NULL : list->element;
        if (when_all) {
            if (list->element->element->kind == TYPE_UNIT)
                result_element = &type_unit;
            else {
                size_t length = strlen(list->element->element->name) + 7U;
                char *list_name = lang_arena_alloc(
                    &checker->module->arena, length);
                (void)snprintf(list_name, length, "List<%s>",
                               list->element->element->name);
                result_element = resolve_type(checker, list_name, expr->span);
            }
        }
        if (result_element->kind == TYPE_UNIT)
            return resolve_type(checker, "Task", expr->span);
        size_t length = strlen(result_element->name) + 7U;
        char *task_name = lang_arena_alloc(&checker->module->arena, length);
        (void)snprintf(task_name, length, "Task<%s>", result_element->name);
        return resolve_type(checker, task_name, expr->span);
    }
    if (strcmp(name, "CancellationTokenSource::New") == 0) {
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`CancellationTokenSource` constructor expects no arguments");
        return &type_cancellation_token_source;
    }
    if (strcmp(name, "CancellationToken::None") == 0) {
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`CancellationToken.None` takes no arguments");
        return &type_cancellation_token;
    }
    if (strcmp(name, "CancellationTokenSource::Token") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_CANCELLATION_TOKEN_SOURCE)
            lang_diag(checker->diagnostics, expr->span,
                      "`CancellationTokenSource.Token` expects one source");
        return &type_cancellation_token;
    }
    if (strcmp(name, "CancellationTokenSource::Cancel") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_CANCELLATION_TOKEN_SOURCE)
            lang_diag(checker->diagnostics, expr->span,
                      "`CancellationTokenSource.Cancel` expects one source");
        return &type_unit;
    }
    if (strcmp(name, "CancellationToken::IsCancellationRequested") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            (expr->as.call.arguments.items[0]->type->kind !=
                 TYPE_CANCELLATION_TOKEN &&
             expr->as.call.arguments.items[0]->type->kind !=
                 TYPE_CANCELLATION_TOKEN_SOURCE))
            lang_diag(checker->diagnostics, expr->span,
                      "`IsCancellationRequested` expects a cancellation token or source");
        return &type_bool;
    }
    if (strcmp(name, "CancellationToken::ThrowIfCancellationRequested") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_CANCELLATION_TOKEN)
            lang_diag(checker->diagnostics, expr->span,
                      "`ThrowIfCancellationRequested` expects one CancellationToken");
        return &type_unit;
    }
    if (strcmp(name, "Console::WriteLine") == 0 ||
        strcmp(name, "Console::Write") == 0 ||
        strcmp(name, "Console::Error::WriteLine") == 0 ||
        strcmp(name, "Console::Error::Write") == 0) {
        if (expr->as.call.arguments.count != 1U)
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects one argument", name);
        else if (expr->as.call.arguments.items[0]->kind == EXPR_INT)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[0], &type_i64);
        else if (expr->as.call.arguments.items[0]->kind == EXPR_FLOAT)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[0], &type_f64);
        return &type_unit;
    }
    if (strcmp(name, "Html::ToHtmlString") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            !same_type(expr->as.call.arguments.items[0]->type, &type_html))
            lang_diag(checker->diagnostics, expr->span,
                      "`Html.ToHtmlString` expects one Html value");
        return &type_string;
    }
    if (strcmp(name, "TextLen") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING)
            lang_diag(checker->diagnostics, expr->span,
                      "`TextLen` expects one `string` value");
        return &type_usize;
    }
    if (strcmp(name, "StringByteAt") == 0) {
        if (expr->as.call.arguments.count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1],
                &type_usize);
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING ||
            !same_type(expr->as.call.arguments.items[1]->type,
                       &type_usize))
            lang_diag(
                checker->diagnostics, expr->span,
                "string indexing expects `(string, nuint)`");
        return &type_u8;
    }
    if (strcmp(name, "Html::UnsafeRaw") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING)
            lang_diag(checker->diagnostics, expr->span,
                      "`Html::unsafe_raw` expects one `string` value");
        return &type_html;
    }
    if (strcmp(name, "panic") == 0 || strcmp(name, "trap") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING)
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects one `string` message", name);
        return &type_never;
    }
    if (strcmp(name, "StringBuilder::New") == 0) {
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` expects no arguments");
        return &type_string_builder;
    }
    if (strcmp(name, "StringBuilder::Append") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_STRING_BUILDER ||
            (expr->as.call.arguments.items[1]->type->kind != TYPE_STRING &&
             !is_numeric(expr->as.call.arguments.items[1]->type) &&
             expr->as.call.arguments.items[1]->type->kind != TYPE_BOOL &&
             expr->as.call.arguments.items[1]->type->kind != TYPE_CHAR)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`StringBuilder.Append` expects text or a scalar value");
        } else if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *builder = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (builder != NULL && !builder->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable StringBuilder `%s`",
                          builder->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "StringBuilder::AppendByte") == 0) {
        if (expr->as.call.arguments.count == 2U &&
            expr->as.call.arguments.items[1]->kind == EXPR_INT)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_u8);
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_STRING_BUILDER ||
            expr->as.call.arguments.items[1]->type->kind != TYPE_U8) {
            lang_diag(
                checker->diagnostics, expr->span,
                "`StringBuilder.AppendByte` expects a byte");
        } else if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *builder = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (builder != NULL && !builder->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable StringBuilder `%s`",
                          builder->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "StringBuilder::Finish") == 0 ||
        strcmp(name, "StringBuilder::ToString") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_STRING_BUILDER)
            lang_diag(checker->diagnostics, expr->span,
                      "`StringBuilder.ToString` expects no arguments");
        return &type_string;
    }
    if (strcmp(name, "StringBuilder::Length") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_STRING_BUILDER)
            lang_diag(checker->diagnostics, expr->span,
                      "`StringBuilder.Length` expects no arguments");
        return &type_usize;
    }
    if (strcmp(name, "StringBuilder::Clear") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind !=
                TYPE_STRING_BUILDER)
            lang_diag(checker->diagnostics, expr->span,
                      "`StringBuilder.Clear` expects no arguments");
        else if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *builder = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (builder != NULL && !builder->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot mutate immutable StringBuilder `%s`",
                          builder->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "Url::relative") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING)
            lang_diag(checker->diagnostics, expr->span,
                      "`Url::relative` expects one `string` or owned `String` value");
        return &type_url;
    }
    if (strcmp(name, "Url::fragment") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STRING)
            lang_diag(checker->diagnostics, expr->span,
                      "`Url::fragment` expects one `string` value");
        return &type_url;
    }
