static_call:
    if (expr->as.call.callee->kind != EXPR_NAME) {
        lang_diag(checker->diagnostics, expr->as.call.callee->span,
                  "only statically named functions are supported");
        return &type_error;
    }
    const char *name = expr->as.call.callee->as.name;
    if (strcmp(name, "$target::new") == 0) {
        if (checker->expected_type == NULL ||
            (checker->expected_type->kind != TYPE_NAMED &&
             checker->expected_type->kind != TYPE_CLASS) ||
            checker->expected_type->declaration == NULL) {
            lang_diag(checker->diagnostics, expr->span,
                      "target-typed `new(...)` requires an expected struct or class type");
            return &type_error;
        }
        const char *owner = type_declaration_name(
            checker->expected_type->declaration);
        size_t owner_length = strlen(owner);
        char *constructor = lang_arena_alloc(
            &checker->module->arena, owner_length + sizeof("::new"));
        memcpy(constructor, owner, owner_length);
        memcpy(constructor + owner_length, "::new", sizeof("::new"));
        expr->as.call.callee->as.name = constructor;
        name = expr->as.call.callee->as.name;
    }
    Local *callee_local = find_local(checker, name);
    if (callee_local != NULL &&
        callee_local->type->kind == TYPE_FUNCTION) {
        expr->as.call.callee->resolved_local_id =
            callee_local->id;
        expr->as.call.callee->type = callee_local->type;
        Type *function_type = callee_local->type;
        if (function_type->argument_count !=
            expr->as.call.arguments.count)
            lang_diag(
                checker->diagnostics, expr->span,
                "function value `%s` expects %zu arguments, found %zu",
                name, function_type->argument_count,
                expr->as.call.arguments.count);
        size_t count =
            function_type->argument_count <
                expr->as.call.arguments.count
            ? function_type->argument_count
            : expr->as.call.arguments.count;
        for (size_t i = 0U;
             i < expr->as.call.arguments.count; ++i) {
            Type *previous_expected = checker->expected_type;
            if (i < function_type->argument_count)
                checker->expected_type =
                    function_type->arguments[i];
            ParameterMode expected_mode = function_type->parameter_modes[i];
            ParameterMode actual_mode = call_argument_mode(expr, i);
            bool borrowed = parameter_mode_is_reference(expected_mode);
            bool mutable_borrow = parameter_mode_is_mutable(expected_mode);
            bool explicit_ref =
                actual_mode == PARAMETER_MODE_MUTABLE_REFERENCE;
            bool explicit_out = actual_mode == PARAMETER_MODE_OUT;
            bool expected_out = expected_mode == PARAMETER_MODE_OUT;
            if (explicit_ref && (!mutable_borrow || expected_out))
                lang_diag(
                    checker->diagnostics,
                    expr->as.call.arguments.items[i]->span,
                    "argument %zu to function value `%s` is not a `ref` parameter",
                    i + 1U, name);
            if (explicit_out != expected_out)
                lang_diag(
                    checker->diagnostics,
                    expr->as.call.arguments.items[i]->span,
                    "argument %zu to function value `%s` %s `out`",
                    i + 1U, name,
                    expected_out ? "must use" : "must not use");
            borrowed = borrowed || explicit_ref || explicit_out;
            mutable_borrow = mutable_borrow || explicit_ref || explicit_out;
            Expr *argument = expr->as.call.arguments.items[i];
            if (borrowed) {
                bool borrowable_place =
                    argument->kind == EXPR_NAME ||
                    (argument->kind == EXPR_FIELD &&
                     argument->as.field.object->kind == EXPR_NAME);
                if (!borrowable_place && mutable_borrow) {
                    lang_diag(
                        checker->diagnostics, argument->span,
                        "`ref` argument must be an available place");
                    (void)check_expr(checker, argument);
                } else if (borrowable_place) {
                    const Expr *previous_allowed =
                        checker->allowed_unassigned_out_place;
                    if (expected_out)
                        checker->allowed_unassigned_out_place = argument;
                    (void)check_place(checker, argument);
                    checker->allowed_unassigned_out_place = previous_allowed;
                    if (mutable_borrow) {
                        Expr *root = argument;
                        while (root->kind == EXPR_FIELD)
                            root = root->as.field.object;
                        Local *local = root->kind == EXPR_NAME
                            ? find_local(checker, root->as.name) : NULL;
                        if (local == NULL || !local->mutable_)
                            lang_diag(
                                checker->diagnostics, argument->span,
                                "`ref` argument requires a mutable local");
                    }
                    if (expected_out && argument->kind == EXPR_NAME) {
                        Local *local = find_local(
                            checker, argument->as.name);
                        if (local != NULL) {
                            if (local->is_out_parameter)
                                local->definitely_assigned = true;
                            local->available = true;
                            local->moved_at = (LangSpan){0};
                        }
                    }
                } else {
                    (void)check_expr(checker, argument);
                }
            } else {
                (void)check_expr(checker, argument);
            }
            checker->expected_type = previous_expected;
        }
        for (size_t i = 0U; i < count; ++i) {
            Type *expected = function_type->arguments[i];
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[i],
                expected);
            Type *actual =
                expr->as.call.arguments.items[i]->type;
            if (!type_assignable(expected, actual))
                lang_diag(
                    checker->diagnostics,
                    expr->as.call.arguments.items[i]->span,
                    "argument %zu to function value `%s` expects `%s`, found `%s`",
                    i + 1U, name, expected->name,
                    actual->name);
        }
        return function_type->element;
    }
    if (strstr(name, "::") == NULL && checker->function != NULL &&
        checker->function->owner_type != NULL) {
        size_t owner_length = strlen(checker->function->owner_type);
        size_t name_length = strlen(name);
        char *qualified = lang_arena_alloc(
            &checker->module->arena,
            owner_length + name_length + 3U);
        memcpy(qualified, checker->function->owner_type, owner_length);
        memcpy(qualified + owner_length, "::", 2U);
        memcpy(qualified + owner_length + 2U, name, name_length + 1U);
        for (size_t i = 0U; i < checker->module->count; ++i) {
            Decl *candidate = checker->module->decls[i];
            if (candidate->kind != DECL_FUNCTION ||
                !candidate->as.function.is_static_member ||
                strcmp(candidate->as.function.name, qualified) != 0)
                continue;
            expr->as.call.callee->as.name = qualified;
            name = qualified;
            break;
        }
    }
    if (expr->as.call.implicit_receiver &&
        expr->as.call.arguments.count != 0U) {
        Expr *receiver = expr->as.call.arguments.items[0];
        Type *receiver_type = receiver->type;
        if (receiver_type != NULL && receiver_type->kind == TYPE_CLASS &&
            receiver_type->declaration != NULL) {
            const char *separator = last_path_separator(name);
            const char *short_name = separator != NULL
                ? separator + 2U : name;
            const char *requested_name = name;
            bool exact_exists = false;
            for (size_t i = 0U; i < checker->module->count; ++i) {
                Decl *candidate = checker->module->decls[i];
                if (candidate->kind == DECL_FUNCTION &&
                    strcmp(candidate->as.function.name, name) == 0) {
                    exact_exists = true;
                    break;
                }
            }
            for (const Decl *base = exact_exists
                     ? NULL
                     : receiver_type->declaration->as.structure.base_class;
                 base != NULL; base = base->as.structure.base_class) {
                size_t owner_length = strlen(base->as.structure.name);
                size_t method_length = strlen(short_name);
                char *qualified = lang_arena_alloc(
                    &checker->module->arena,
                    owner_length + method_length + 3U);
                memcpy(qualified, base->as.structure.name, owner_length);
                memcpy(qualified + owner_length, "::", 2U);
                memcpy(qualified + owner_length + 2U,
                       short_name, method_length + 1U);
                for (size_t i = 0U; i < checker->module->count; ++i) {
                    Decl *candidate = checker->module->decls[i];
                    if (candidate->kind != DECL_FUNCTION ||
                        strcmp(candidate->as.function.name,
                               qualified) != 0)
                        continue;
                    expr->as.call.callee->as.name = qualified;
                    name = qualified;
                    base = NULL;
                    break;
                }
                if (base == NULL) break;
            }
            if (!exact_exists && strcmp(name, requested_name) == 0) {
                const Decl *interfaces[256];
                size_t interface_count = 0U;
                for (const Decl *owner = receiver_type->declaration;
                     owner != NULL; owner = owner->as.structure.base_class)
                    for (size_t interface = 0U;
                         interface < owner->as.structure.interface_count &&
                         interface_count < 256U; ++interface)
                        interfaces[interface_count++] =
                            owner->as.structure.interfaces[interface];
                while (interface_count != 0U &&
                       strcmp(name, requested_name) == 0) {
                    const Decl *interface = interfaces[--interface_count];
                    size_t owner_length =
                        strlen(interface->as.structure.name);
                    size_t method_length = strlen(short_name);
                    char *qualified = lang_arena_alloc(
                        &checker->module->arena,
                        owner_length + method_length + 3U);
                    memcpy(qualified, interface->as.structure.name,
                           owner_length);
                    memcpy(qualified + owner_length, "::", 2U);
                    memcpy(qualified + owner_length + 2U,
                           short_name, method_length + 1U);
                    for (size_t i = 0U;
                         i < checker->module->count; ++i) {
                        Decl *candidate = checker->module->decls[i];
                        if (candidate->kind == DECL_FUNCTION &&
                            strcmp(candidate->as.function.name,
                                   qualified) == 0) {
                            expr->as.call.callee->as.name = qualified;
                            name = qualified;
                            break;
                        }
                    }
                    for (size_t parent = 0U;
                         parent < interface->as.structure.interface_count &&
                         interface_count < 256U; ++parent)
                        interfaces[interface_count++] =
                            interface->as.structure.interfaces[parent];
                }
            }
        }
    }
    CallOverloadSet overloads = collect_call_overloads(
        checker, name, expr, false);
    bool pending_overload = overloads.arity_count > 1U;
    const Decl *declared_declaration =
        overloads.arity_count == 1U
            ? overloads.selected
            : (overloads.arity_count == 0U
                ? overloads.first_named : NULL);
    Function *declared_function = declared_declaration != NULL
        ? (Function *)&declared_declaration->as.function : NULL;
    bool same_member_owner = declared_function != NULL &&
        declared_function->owner_type != NULL &&
        checker->function != NULL &&
        checker->function->owner_type != NULL &&
        strcmp(checker->function->owner_type,
               declared_function->owner_type) == 0 &&
        checker->current_module != NULL &&
        declared_declaration->module_name != NULL &&
        strcmp(checker->current_module,
               declared_declaration->module_name) == 0;
    if (declared_function != NULL &&
        declared_function->owner_type != NULL &&
        !declared_declaration->is_public &&
        !same_member_owner)
        lang_diag(checker->diagnostics, expr->span,
                  "member `%s` is private to class `%s`",
                  declared_function->name,
                  declared_function->owner_type);
    expr->resolved_decl = declared_declaration;
    if (declared_function != NULL &&
        expr->as.call.implicit_receiver &&
        (declared_function->is_virtual_member ||
         declared_function->is_override_member))
        expr->as.call.virtual_dispatch = true;
    if (expr->resolved_decl != NULL &&
        expr->resolved_decl->type_param_count != 0U)
        return check_generic_call(
            checker, expr, expr->resolved_decl);
    const char *declared_module = declared_declaration != NULL
        ? declared_declaration->module_name : checker->current_module;
    if (declared_function != NULL && declared_function->is_drop)
        lang_diag(checker->diagnostics, expr->span,
                  "destructor `%s` cannot be called explicitly", name);
    bool builtin_borrow =
        strcmp(name, "ArenaAlloc") == 0 ||
        strcmp(name, "ArenaReset") == 0 ||
        strcmp(name, "StringBuilder::Append") == 0 ||
        strcmp(name, "StringBuilder::AppendByte") == 0 ||
        strcmp(name, "StringBuilder::ToString") == 0 ||
        strcmp(name, "StringBuilder::Length") == 0 ||
        strcmp(name, "StringBuilder::Clear") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "List::Add") == 0 ||
        strcmp(name, "List::Count") == 0 ||
        strcmp(name, "List::Get") == 0 ||
        strcmp(name, "List::Capacity") == 0 ||
        strcmp(name, "List::Clear") == 0 ||
        strcmp(name, "List::Insert") == 0 ||
        strcmp(name, "List::RemoveAt") == 0 ||
        strcmp(name, "List::Set") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "List::Contains") == 0 ||
        strcmp(name, "List::IndexOf") == 0 ||
        strcmp(name, "List::LastIndexOf") == 0 ||
        strcmp(name, "List::Remove") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "List::AddRange") == 0 ||
        strcmp(name, "List::InsertRange") == 0 ||
        strcmp(name, "List::RemoveRange") == 0 ||
        strcmp(name, "List::GetRange") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "List::Reverse") == 0 ||
        strcmp(name, "List::EnsureCapacity") == 0 ||
        strcmp(name, "List::TrimExcess") == 0 ||
        strcmp(name, "List::SetCapacity") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "List::Exists") == 0 ||
        strcmp(name, "List::FindAll") == 0 ||
        strcmp(name, "List::FindIndex") == 0 ||
        strcmp(name, "List::FindLastIndex") == 0 ||
        strcmp(name, "List::RemoveAll") == 0 ||
        strcmp(name, "List::ForEach") == 0 ||
        strcmp(name, "List::TrueForAll") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "BufferAsMutSlice") == 0 ||
        strcmp(name, "BufferAsSlice") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Dictionary::Add") == 0 ||
        strcmp(name, "Dictionary::Count") == 0 ||
        strcmp(name, "Dictionary::ContainsKey") == 0 ||
        strcmp(name, "Dictionary::Remove") == 0 ||
        strcmp(name, "Dictionary::Clear") == 0 ||
        strcmp(name, "Dictionary::Get") == 0 ||
        strcmp(name, "Dictionary::Set") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Dictionary::TryAdd") == 0 ||
        strcmp(name, "Dictionary::TryGetValue") == 0 ||
        strcmp(name, "Dictionary::ContainsValue") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Dictionary::EnsureCapacity") == 0 ||
        strcmp(name, "Dictionary::TrimExcess") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Dictionary::Capacity") == 0 ||
        strcmp(name, "Dictionary::KeyAt") == 0 ||
        strcmp(name, "Dictionary::ValueAt") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Queue::Enqueue") == 0 ||
        strcmp(name, "Queue::Dequeue") == 0 ||
        strcmp(name, "Queue::Peek") == 0 ||
        strcmp(name, "Queue::TryDequeue") == 0 ||
        strcmp(name, "Queue::TryPeek") == 0 ||
        strcmp(name, "Queue::Count") == 0 ||
        strcmp(name, "Queue::Clear") == 0 ||
        strcmp(name, "Queue::EnsureCapacity") == 0 ||
        strcmp(name, "Queue::TrimExcess") == 0 ||
        strcmp(name, "Queue::Capacity") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "Stack::Push") == 0 ||
        strcmp(name, "Stack::Pop") == 0 ||
        strcmp(name, "Stack::Peek") == 0 ||
        strcmp(name, "Stack::TryPop") == 0 ||
        strcmp(name, "Stack::TryPeek") == 0 ||
        strcmp(name, "Stack::Count") == 0 ||
        strcmp(name, "Stack::Clear") == 0 ||
        strcmp(name, "Stack::EnsureCapacity") == 0 ||
        strcmp(name, "Stack::TrimExcess") == 0 ||
        strcmp(name, "Stack::Capacity") == 0;
    builtin_borrow = builtin_borrow ||
        strcmp(name, "CancellationTokenSource::Token") == 0 ||
        strcmp(name, "CancellationTokenSource::Cancel") == 0 ||
        strcmp(name, "CancellationToken::IsCancellationRequested") == 0 ||
        strcmp(name,
               "CancellationToken::ThrowIfCancellationRequested") == 0;
    for (size_t i = 0U; i < expr->as.call.arguments.count; ++i) {
        ParameterMode actual_mode = call_argument_mode(expr, i);
        bool explicit_call_ref =
            actual_mode == PARAMETER_MODE_MUTABLE_REFERENCE;
        bool explicit_call_out = actual_mode == PARAMETER_MODE_OUT;
        bool builtin_out =
            ((strcmp(name, "Dictionary::TryGetValue") == 0 && i == 2U) ||
             ((strcmp(name, "Queue::TryDequeue") == 0 ||
               strcmp(name, "Queue::TryPeek") == 0 ||
               strcmp(name, "Stack::TryPop") == 0 ||
               strcmp(name, "Stack::TryPeek") == 0) && i == 1U));
        if (explicit_call_ref && !pending_overload &&
            (declared_function == NULL ||
             i >= declared_function->param_count ||
             !declared_function->params[i].by_ref ||
             declared_function->params[i].by_out))
            lang_diag(
                checker->diagnostics,
                expr->as.call.arguments.items[i]->span,
                "argument %zu to `%s` is not a `ref` parameter",
                i + 1U, name);
        if (explicit_call_out && !pending_overload && !builtin_out &&
            (declared_function == NULL ||
             i >= declared_function->param_count ||
             !declared_function->params[i].by_out))
            lang_diag(
                checker->diagnostics,
                expr->as.call.arguments.items[i]->span,
                "argument %zu to `%s` is not an `out` parameter",
                i + 1U, name);
        if (!explicit_call_out && !pending_overload &&
            declared_function != NULL &&
            i < declared_function->param_count &&
            declared_function->params[i].by_out)
            lang_diag(
                checker->diagnostics,
                expr->as.call.arguments.items[i]->span,
                "argument %zu to `%s` must use `out`",
                i + 1U, name);
        bool output_place =
            (strcmp(name, "Console::WriteLine") == 0 ||
             strcmp(name, "Console::Write") == 0 ||
             strcmp(name, "Console::Error::WriteLine") == 0 ||
             strcmp(name, "Console::Error::Write") == 0) &&
            i == 0U;
        bool text_place =
            strcmp(name, "TextLen") == 0 && i == 0U;
        /* Method syntax never evaluates its receiver as an ordinary value.
         * In particular, overload resolution happens after argument typing,
         * so declared_function is temporarily NULL for overloaded methods.
         * Treating the receiver as a value here inserted a defensive clone;
         * the selected ref-self overload then mutated that clone. */
        bool implicit_receiver =
            expr->as.call.implicit_receiver && i == 0U &&
            !(declared_function != NULL &&
              declared_function->is_async);
        bool builtin_value_borrow =
            (strcmp(name, "StringBuilder::Append") == 0 && i == 1U) ||
            ((strcmp(name, "List::Contains") == 0 ||
              strcmp(name, "List::IndexOf") == 0 ||
              strcmp(name, "List::LastIndexOf") == 0 ||
              strcmp(name, "List::Remove") == 0 ||
              strcmp(name, "Dictionary::ContainsKey") == 0 ||
              strcmp(name, "Dictionary::Remove") == 0 ||
              strcmp(name, "Dictionary::Get") == 0 ||
              strcmp(name, "Dictionary::ContainsValue") == 0) &&
             i == 1U) ||
            (strcmp(name, "Dictionary::TryGetValue") == 0 && i == 1U);
        bool borrowed = explicit_call_ref || explicit_call_out ||
            implicit_receiver ||
            (builtin_borrow && i == 0U) || output_place ||
            text_place || builtin_value_borrow ||
            (pending_overload && checker_expression_is_local_place(
                checker, expr->as.call.arguments.items[i])) ||
            (declared_function != NULL &&
             i < declared_function->param_count &&
             (declared_function->params[i].by_ref ||
              declared_function->params[i].by_const_ref ||
              declared_function->params[i].borrowed));
        if (borrowed) {
            bool builtin_place =
                (strcmp(name, "List::Add") == 0 ||
                 strcmp(name, "List::Count") == 0 ||
                 strcmp(name, "List::Get") == 0 ||
                 strcmp(name, "List::Capacity") == 0 ||
                 strcmp(name, "List::Clear") == 0 ||
                 strcmp(name, "List::Insert") == 0 ||
                 strcmp(name, "List::RemoveAt") == 0 ||
                 strcmp(name, "List::Set") == 0) &&
                i == 0U;
            builtin_place = builtin_place ||
                ((strcmp(name, "List::Contains") == 0 ||
                  strcmp(name, "List::IndexOf") == 0 ||
                  strcmp(name, "List::LastIndexOf") == 0 ||
                  strcmp(name, "List::Remove") == 0) &&
                i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "List::AddRange") == 0 ||
                  strcmp(name, "List::InsertRange") == 0 ||
                  strcmp(name, "List::RemoveRange") == 0 ||
                  strcmp(name, "List::GetRange") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "List::Reverse") == 0 ||
                  strcmp(name, "List::EnsureCapacity") == 0 ||
                  strcmp(name, "List::TrimExcess") == 0 ||
                  strcmp(name, "List::SetCapacity") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "List::Exists") == 0 ||
                  strcmp(name, "List::FindAll") == 0 ||
                  strcmp(name, "List::FindIndex") == 0 ||
                  strcmp(name, "List::FindLastIndex") == 0 ||
                  strcmp(name, "List::RemoveAll") == 0 ||
                  strcmp(name, "List::ForEach") == 0 ||
                  strcmp(name, "List::TrueForAll") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "Dictionary::Add") == 0 ||
                  strcmp(name, "Dictionary::Count") == 0 ||
                  strcmp(name, "Dictionary::ContainsKey") == 0 ||
                  strcmp(name, "Dictionary::Remove") == 0 ||
                  strcmp(name, "Dictionary::Clear") == 0 ||
                  strcmp(name, "Dictionary::Get") == 0 ||
                  strcmp(name, "Dictionary::Set") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "Dictionary::TryAdd") == 0 ||
                  strcmp(name, "Dictionary::ContainsValue") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "Dictionary::EnsureCapacity") == 0 ||
                  strcmp(name, "Dictionary::TrimExcess") == 0) &&
                 i == 0U);
            builtin_place = builtin_place ||
                (strcmp(name, "Dictionary::Capacity") == 0 && i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "Dictionary::KeyAt") == 0 ||
                  strcmp(name, "Dictionary::ValueAt") == 0) && i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "Queue::Enqueue") == 0 ||
                  strcmp(name, "Queue::Dequeue") == 0 ||
                  strcmp(name, "Queue::Peek") == 0 ||
                  strcmp(name, "Queue::Count") == 0 ||
                  strcmp(name, "Queue::Clear") == 0 ||
                  strcmp(name, "Queue::EnsureCapacity") == 0 ||
                  strcmp(name, "Queue::TrimExcess") == 0 ||
                  strcmp(name, "Queue::Capacity") == 0) && i == 0U);
            builtin_place = builtin_place ||
                ((strcmp(name, "Stack::Push") == 0 ||
                  strcmp(name, "Stack::Pop") == 0 ||
                  strcmp(name, "Stack::Peek") == 0 ||
                  strcmp(name, "Stack::Count") == 0 ||
                  strcmp(name, "Stack::Clear") == 0 ||
                  strcmp(name, "Stack::EnsureCapacity") == 0 ||
                  strcmp(name, "Stack::TrimExcess") == 0 ||
                  strcmp(name, "Stack::Capacity") == 0) && i == 0U);
            bool borrowable_place =
                checker_expression_is_local_place(
                    checker, expr->as.call.arguments.items[i]);
            bool explicit_ref =
                declared_function != NULL &&
                i < declared_function->param_count &&
                declared_function->params[i].by_ref;
            if (!borrowable_place && (builtin_place || explicit_ref)) {
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[i]->span,
                          declared_function != NULL &&
                          i < declared_function->param_count &&
                          declared_function->params[i].by_out
                              ? "`out` argument must be an available place"
                              : "`ref` argument must be an available place");
                (void)check_expr(checker, expr->as.call.arguments.items[i]);
            } else if (borrowable_place) {
                Expr *argument = expr->as.call.arguments.items[i];
                bool expected_out = declared_function != NULL &&
                    i < declared_function->param_count &&
                    declared_function->params[i].by_out;
                const Expr *previous_allowed =
                    checker->allowed_unassigned_out_place;
                if (expected_out)
                    checker->allowed_unassigned_out_place = argument;
                if (builtin_place || explicit_ref || expected_out)
                    (void)check_place(checker, argument);
                else
                    (void)check_borrowed_expr(checker, argument);
                checker->allowed_unassigned_out_place = previous_allowed;
                bool mutable_borrow =
                    declared_function != NULL &&
                    i < declared_function->param_count &&
                    declared_function->params[i].by_ref &&
                    declared_function->params[i].mutable_;
                if (mutable_borrow) {
                    Expr *root = expr->as.call.arguments.items[i];
                    while (root->kind == EXPR_FIELD)
                        root = root->as.field.object;
                    Local *local = root->kind == EXPR_NAME
                                 ? find_local(
                                       checker, root->as.name)
                                 : NULL;
                    if (local == NULL || !local->mutable_)
                        lang_diag(
                            checker->diagnostics,
                            expr->as.call.arguments.items[i]->span,
                            "`ref` argument requires a mutable local");
                }
                if (expected_out && argument->kind == EXPR_NAME) {
                    Local *local = find_local(checker, argument->as.name);
                    if (local != NULL) {
                        if (local->is_out_parameter)
                            local->definitely_assigned = true;
                        local->available = true;
                        local->moved_at = (LangSpan){0};
                    }
                }
            } else {
                Type *previous_expected = checker->expected_type;
                if (declared_function != NULL &&
                    i < declared_function->param_count)
                    checker->expected_type = resolve_declared_type_in_module(
                        checker,
                        declared_function->params[i].type_syntax,
                        declared_function->params[i].type_name,
                        declared_function->params[i].span,
                        declared_module);
                (void)check_borrowed_expr(
                    checker, expr->as.call.arguments.items[i]);
                checker->expected_type = previous_expected;
            }
        } else {
            Type *previous_expected = checker->expected_type;
            if (declared_function != NULL &&
                i < declared_function->param_count)
                checker->expected_type = resolve_declared_type_in_module(
                    checker,
                    declared_function->params[i].type_syntax,
                    declared_function->params[i].type_name,
                    declared_function->params[i].span,
                    declared_module);
            else if (strcmp(name, "List::Add") == 0 &&
                     i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_VEC)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if ((strcmp(name, "List::Insert") == 0 ||
                      strcmp(name, "List::Set") == 0) &&
                     i == 2U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_VEC)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if ((strcmp(name, "List::Contains") == 0 ||
                      strcmp(name, "List::IndexOf") == 0 ||
                      strcmp(name, "List::LastIndexOf") == 0 ||
                      strcmp(name, "List::Remove") == 0) &&
                     i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_VEC)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if (strcmp(name, "List::AddRange") == 0 &&
                     i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_VEC)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type;
            else if (strcmp(name, "List::InsertRange") == 0 &&
                     i == 2U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_VEC)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type;
            else if ((strcmp(name, "Dictionary::Add") == 0 ||
                      strcmp(name, "Dictionary::TryAdd") == 0 ||
                      strcmp(name, "Dictionary::Set") == 0) &&
                     i == 2U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     is_hash_storage_type(
                         expr->as.call.arguments.items[0]->type))
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->error_type;
            else if ((strcmp(name, "Dictionary::Add") == 0 ||
                      strcmp(name, "Dictionary::TryAdd") == 0 ||
                      strcmp(name, "Dictionary::ContainsKey") == 0 ||
                      strcmp(name, "Dictionary::Remove") == 0 ||
                      strcmp(name, "Dictionary::Get") == 0 ||
                      strcmp(name, "Dictionary::Set") == 0) &&
                     i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     is_hash_storage_type(
                         expr->as.call.arguments.items[0]->type))
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if (strcmp(name, "Dictionary::ContainsValue") == 0 &&
                     i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     is_hash_storage_type(
                         expr->as.call.arguments.items[0]->type))
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->error_type;
            else if ((strcmp(name, "Dictionary::EnsureCapacity") == 0 ||
                      strcmp(name, "Dictionary::TrimExcess") == 0) &&
                     i == 1U)
                checker->expected_type = &type_usize;
            else if ((strcmp(name, "Dictionary::KeyAt") == 0 ||
                      strcmp(name, "Dictionary::ValueAt") == 0) &&
                     i == 1U)
                checker->expected_type = &type_usize;
            else if (strcmp(name, "Queue::Enqueue") == 0 && i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_QUEUE)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if (strcmp(name, "Queue::EnsureCapacity") == 0 && i == 1U)
                checker->expected_type = &type_usize;
            else if (strcmp(name, "Stack::Push") == 0 && i == 1U &&
                     expr->as.call.arguments.items[0]->type != NULL &&
                     expr->as.call.arguments.items[0]->type->kind == TYPE_STACK)
                checker->expected_type =
                    expr->as.call.arguments.items[0]->type->element;
            else if ((strcmp(name, "Stack::EnsureCapacity") == 0 ||
                      strcmp(name, "Stack::TrimExcess") == 0) && i == 1U)
                checker->expected_type = &type_usize;
            else if (strcmp(name, "Option::Some") == 0 &&
                     checker->expected_type != NULL &&
                     checker->expected_type->kind == TYPE_OPTION &&
                     i == 0U)
                checker->expected_type =
                    checker->expected_type->element;
            else if ((strcmp(name, "Result::Ok") == 0 ||
                      strcmp(name, "Result::Err") == 0) &&
                     i == 0U) {
                Type *result =
                    checker->expected_type != NULL &&
                    checker->expected_type->kind == TYPE_RESULT
                    ? checker->expected_type
                    : checker->function->checked_return_type;
                if (result != NULL && result->kind == TYPE_RESULT)
                    checker->expected_type =
                        strcmp(name, "Result::Ok") == 0
                        ? result->element : result->error_type;
            }
            (void)check_expr(
                checker, expr->as.call.arguments.items[i]);
            checker->expected_type = previous_expected;
        }
    }
    if (pending_overload) {
        overloads = collect_call_overloads(
            checker, name, expr, true);
        if (overloads.exact_count == 1U) {
            declared_declaration = overloads.selected;
            declared_function =
                (Function *)&declared_declaration->as.function;
            declared_module = declared_declaration->module_name;
            expr->resolved_decl = declared_declaration;
        } else {
            diagnose_call_overloads(
                checker, name, expr, &overloads);
            CallOverloadSet fallback = collect_call_overloads(
                checker, name, expr, false);
            declared_declaration = fallback.selected;
            declared_function = declared_declaration != NULL
                ? (Function *)&declared_declaration->as.function
                : NULL;
            declared_module = declared_declaration != NULL
                ? declared_declaration->module_name
                : checker->current_module;
            expr->resolved_decl = declared_declaration;
        }
        if (declared_function != NULL) {
            size_t parameter_count = declared_function->param_count <
                    expr->as.call.arguments.count
                ? declared_function->param_count
                : expr->as.call.arguments.count;
            for (size_t i = 0U; i < parameter_count; ++i) {
                ParameterMode mode = parameter_mode_from_param(
                    &declared_function->params[i]);
                Expr *argument = expr->as.call.arguments.items[i];
                if (parameter_mode_is_reference(mode) ||
                    argument->type == NULL ||
                    !type_moves_by_default(checker, argument->type) ||
                    type_is_copyable(checker, argument->type))
                    continue;
                if (argument->kind == EXPR_NAME) {
                    Local *local = find_local(
                        checker, argument->as.name);
                    if (local != NULL)
                        checker_move_local(
                            checker, local, argument->span);
                } else if (checker_expression_is_local_place(
                               checker, argument) &&
                           !(argument->kind == EXPR_FIELD &&
                             argument->as.field.move_out) &&
                           !(argument->kind == EXPR_INDEX &&
                             argument->as.index.move_out)) {
                    lang_diag(
                        checker->diagnostics, argument->span,
                        "passing a non-trivial projection by value requires `copy(...)`");
                }
            }
        }
    }
