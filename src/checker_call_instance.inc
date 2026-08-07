    if (expr->as.call.callee->kind == EXPR_FIELD) {
        Expr *field = expr->as.call.callee;
        const char *static_name = checker_static_call_path(checker, field);
        if (field->as.field.object->kind == EXPR_FIELD) {
            const char *receiver_static = checker_static_call_path(
                checker, field->as.field.object);
            Function *receiver_member = receiver_static != NULL
                ? find_function(checker, receiver_static, field->span)
                : NULL;
            if (receiver_member != NULL &&
                receiver_member->is_property_getter &&
                receiver_member->is_static_member)
                static_name = NULL;
        }
        if (static_name != NULL) {
            field->kind = EXPR_NAME;
            field->as.name = static_name;
            goto static_call;
        }
        Expr *receiver = field->as.field.object;
        Type *receiver_type = &type_error;
        if (receiver->kind == EXPR_NAME) {
            Local *local = find_local(checker, receiver->as.name);
            if (local != NULL)
                receiver_type = local->type;
            else
                lang_diag(checker->diagnostics, receiver->span,
                          "unknown method receiver `%s`",
                          receiver->as.name);
        } else if (receiver->kind == EXPR_FIELD) {
            receiver_type = check_expr(checker, receiver);
        } else {
            receiver_type = check_expr(checker, receiver);
        }

        const char *owner = receiver_type->name;
        switch (receiver_type->kind) {
            case TYPE_I8: owner = "sbyte"; break;
            case TYPE_I16: owner = "short"; break;
            case TYPE_I32: owner = "int"; break;
            case TYPE_I64: owner = "long"; break;
            case TYPE_U8: owner = "byte"; break;
            case TYPE_U16: owner = "ushort"; break;
            case TYPE_U32: owner = "uint"; break;
            case TYPE_U64: owner = "ulong"; break;
            case TYPE_F32: owner = "float"; break;
            case TYPE_F64: owner = "double"; break;
            default: break;
        }
        if ((receiver_type->kind == TYPE_NAMED ||
             receiver_type->kind == TYPE_CLASS) &&
            receiver_type->declaration != NULL)
            owner = type_declaration_name(
                receiver_type->declaration);
        else if (receiver_type->kind == TYPE_VEC)
            owner = "List";
        else if (receiver_type->kind == TYPE_DICTIONARY)
            owner = "Dictionary";
        else if (receiver_type->kind == TYPE_HASH_SET)
            owner = "HashSet";
        else if (receiver_type->kind == TYPE_QUEUE)
            owner = "Queue";
        else if (receiver_type->kind == TYPE_STACK)
            owner = "Stack";
        size_t owner_length = strcspn(owner, "<");
        size_t method_length = strlen(field->as.field.field);
        char *qualified = lang_arena_alloc(
            &checker->module->arena,
            owner_length + method_length + 3U);
        memcpy(qualified, owner, owner_length);
        memcpy(qualified + owner_length, "::", 2U);
        memcpy(
            qualified + owner_length + 2U,
            field->as.field.field, method_length + 1U);

        if (receiver_type->kind == TYPE_VEC) {
            if (strcmp(field->as.field.field, "Add") == 0)
                qualified = "List::Add";
            else if (strcmp(field->as.field.field, "Count") == 0)
                qualified = "List::Count";
            else if (strcmp(field->as.field.field, "Get") == 0)
                qualified = "List::Get";
            else if (strcmp(field->as.field.field, "Capacity") == 0)
                qualified = "List::Capacity";
            else if (strcmp(field->as.field.field, "Clear") == 0)
                qualified = "List::Clear";
            else if (strcmp(field->as.field.field, "Insert") == 0)
                qualified = "List::Insert";
            else if (strcmp(field->as.field.field, "RemoveAt") == 0)
                qualified = "List::RemoveAt";
            else if (strcmp(field->as.field.field, "Set") == 0)
                qualified = "List::Set";
            else if (strcmp(field->as.field.field, "Contains") == 0)
                qualified = "List::Contains";
            else if (strcmp(field->as.field.field, "IndexOf") == 0)
                qualified = "List::IndexOf";
            else if (strcmp(field->as.field.field, "LastIndexOf") == 0)
                qualified = "List::LastIndexOf";
            else if (strcmp(field->as.field.field, "Remove") == 0)
                qualified = "List::Remove";
            else if (strcmp(field->as.field.field, "AddRange") == 0)
                qualified = "List::AddRange";
            else if (strcmp(field->as.field.field, "InsertRange") == 0)
                qualified = "List::InsertRange";
            else if (strcmp(field->as.field.field, "RemoveRange") == 0)
                qualified = "List::RemoveRange";
            else if (strcmp(field->as.field.field, "GetRange") == 0)
                qualified = "List::GetRange";
            else if (strcmp(field->as.field.field, "Reverse") == 0)
                qualified = "List::Reverse";
            else if (strcmp(field->as.field.field, "EnsureCapacity") == 0)
                qualified = "List::EnsureCapacity";
            else if (strcmp(field->as.field.field, "TrimExcess") == 0)
                qualified = "List::TrimExcess";
            else if (strcmp(field->as.field.field, "Exists") == 0)
                qualified = "List::Exists";
            else if (strcmp(field->as.field.field, "FindAll") == 0)
                qualified = "List::FindAll";
            else if (strcmp(field->as.field.field, "FindIndex") == 0)
                qualified = "List::FindIndex";
            else if (strcmp(field->as.field.field, "FindLastIndex") == 0)
                qualified = "List::FindLastIndex";
            else if (strcmp(field->as.field.field, "RemoveAll") == 0)
                qualified = "List::RemoveAll";
            else if (strcmp(field->as.field.field, "ForEach") == 0)
                qualified = "List::ForEach";
            else if (strcmp(field->as.field.field, "TrueForAll") == 0)
                qualified = "List::TrueForAll";
        } else if (receiver_type->kind == TYPE_DICTIONARY) {
            if (strcmp(field->as.field.field, "Add") == 0)
                qualified = "Dictionary::Add";
            else if (strcmp(field->as.field.field, "ContainsKey") == 0)
                qualified = "Dictionary::ContainsKey";
            else if (strcmp(field->as.field.field, "Remove") == 0)
                qualified = "Dictionary::Remove";
            else if (strcmp(field->as.field.field, "Clear") == 0)
                qualified = "Dictionary::Clear";
            else if (strcmp(field->as.field.field, "Get") == 0)
                qualified = "Dictionary::Get";
            else if (strcmp(field->as.field.field, "Set") == 0)
                qualified = "Dictionary::Set";
            else if (strcmp(field->as.field.field, "TryAdd") == 0)
                qualified = "Dictionary::TryAdd";
            else if (strcmp(field->as.field.field, "TryGetValue") == 0)
                qualified = "Dictionary::TryGetValue";
            else if (strcmp(field->as.field.field, "ContainsValue") == 0)
                qualified = "Dictionary::ContainsValue";
            else if (strcmp(field->as.field.field, "EnsureCapacity") == 0)
                qualified = "Dictionary::EnsureCapacity";
            else if (strcmp(field->as.field.field, "TrimExcess") == 0)
                qualified = "Dictionary::TrimExcess";
            else if (strcmp(field->as.field.field, "Capacity") == 0)
                qualified = "Dictionary::Capacity";
            else if (strcmp(field->as.field.field, "KeyAt") == 0)
                qualified = "Dictionary::KeyAt";
            else if (strcmp(field->as.field.field, "ValueAt") == 0)
                qualified = "Dictionary::ValueAt";
        } else if (receiver_type->kind == TYPE_HASH_SET) {
            if (strcmp(field->as.field.field, "Add") == 0)
                qualified = "Dictionary::TryAdd";
            else if (strcmp(field->as.field.field, "Contains") == 0)
                qualified = "Dictionary::ContainsKey";
            else if (strcmp(field->as.field.field, "Remove") == 0)
                qualified = "Dictionary::Remove";
            else if (strcmp(field->as.field.field, "Clear") == 0)
                qualified = "Dictionary::Clear";
            else if (strcmp(field->as.field.field, "EnsureCapacity") == 0)
                qualified = "Dictionary::EnsureCapacity";
            else if (strcmp(field->as.field.field, "TrimExcess") == 0)
                qualified = "Dictionary::TrimExcess";
            else if (strcmp(field->as.field.field, "Capacity") == 0)
                qualified = "Dictionary::Capacity";
        } else if (receiver_type->kind == TYPE_QUEUE) {
            if (strcmp(field->as.field.field, "Enqueue") == 0)
                qualified = "Queue::Enqueue";
            else if (strcmp(field->as.field.field, "Dequeue") == 0)
                qualified = "Queue::Dequeue";
            else if (strcmp(field->as.field.field, "Peek") == 0)
                qualified = "Queue::Peek";
            else if (strcmp(field->as.field.field, "TryDequeue") == 0)
                qualified = "Queue::TryDequeue";
            else if (strcmp(field->as.field.field, "TryPeek") == 0)
                qualified = "Queue::TryPeek";
            else if (strcmp(field->as.field.field, "Clear") == 0)
                qualified = "Queue::Clear";
            else if (strcmp(field->as.field.field, "EnsureCapacity") == 0)
                qualified = "Queue::EnsureCapacity";
            else if (strcmp(field->as.field.field, "TrimExcess") == 0)
                qualified = "Queue::TrimExcess";
            else if (strcmp(field->as.field.field, "Capacity") == 0)
                qualified = "Queue::Capacity";
        } else if (receiver_type->kind == TYPE_STACK) {
            if (strcmp(field->as.field.field, "Push") == 0)
                qualified = "Stack::Push";
            else if (strcmp(field->as.field.field, "Pop") == 0)
                qualified = "Stack::Pop";
            else if (strcmp(field->as.field.field, "Peek") == 0)
                qualified = "Stack::Peek";
            else if (strcmp(field->as.field.field, "TryPop") == 0)
                qualified = "Stack::TryPop";
            else if (strcmp(field->as.field.field, "TryPeek") == 0)
                qualified = "Stack::TryPeek";
            else if (strcmp(field->as.field.field, "Clear") == 0)
                qualified = "Stack::Clear";
            else if (strcmp(field->as.field.field, "EnsureCapacity") == 0)
                qualified = "Stack::EnsureCapacity";
            else if (strcmp(field->as.field.field, "TrimExcess") == 0)
                qualified = "Stack::TrimExcess";
            else if (strcmp(field->as.field.field, "Capacity") == 0)
                qualified = "Stack::Capacity";
        }

        size_t old_count = expr->as.call.arguments.count;
        bool hash_set_add = receiver_type->kind == TYPE_HASH_SET &&
            strcmp(field->as.field.field, "Add") == 0;
        Expr **arguments = lang_arena_alloc(
            &checker->module->arena,
            (old_count + (hash_set_add ? 2U : 1U)) * sizeof(*arguments));
        arguments[0] = receiver;
        if (old_count != 0U)
            memcpy(
                arguments + 1U,
                expr->as.call.arguments.items,
                old_count * sizeof(*arguments));
        if (hash_set_add) {
            Expr *present = lang_arena_alloc(
                &checker->module->arena, sizeof(*present));
            present->kind = EXPR_BOOL;
            present->span = expr->span;
            present->as.boolean = true;
            arguments[old_count + 1U] = present;
        }
        expr->as.call.arguments.items = arguments;
        ParameterMode *argument_modes = lang_arena_alloc(
            &checker->module->arena,
            (old_count + (hash_set_add ? 2U : 1U)) *
                sizeof(*argument_modes));
        argument_modes[0] = PARAMETER_MODE_VALUE;
        for (size_t i = 0U; i < old_count; ++i)
            argument_modes[i + 1U] = call_argument_mode(expr, i);
        if (hash_set_add)
            argument_modes[old_count + 1U] = PARAMETER_MODE_VALUE;
        expr->as.call.argument_modes = argument_modes;
        expr->as.call.arguments.count =
            old_count + (hash_set_add ? 2U : 1U);
        expr->as.call.implicit_receiver =
            receiver_type->kind != TYPE_CANCELLATION_TOKEN &&
            receiver_type->kind != TYPE_CANCELLATION_TOKEN_SOURCE;
        field->kind = EXPR_NAME;
        field->as.name = qualified;
    }
