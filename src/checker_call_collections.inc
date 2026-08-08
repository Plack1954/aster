    if (strcmp(name, "List::New") == 0) {
        Type *vector = checker->expected_type;
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` expects no arguments");
        if (vector != NULL && vector->kind == TYPE_STRING_BUILDER) {
            expr->as.call.callee->as.name = "StringBuilder::New";
            return &type_string_builder;
        }
        if (vector != NULL && vector->kind == TYPE_QUEUE) {
            expr->as.call.callee->as.name = "Queue::New";
            return vector;
        }
        if (vector != NULL && vector->kind == TYPE_STACK) {
            expr->as.call.callee->as.name = "Stack::New";
            return vector;
        }
        if (vector != NULL &&
            vector->kind == TYPE_CANCELLATION_TOKEN_SOURCE) {
            expr->as.call.callee->as.name =
                "CancellationTokenSource::New";
            return vector;
        }
        if (is_hash_storage_type(vector)) {
            expr->as.call.callee->as.name = "Dictionary::New";
            return vector;
        }
        if (vector == NULL || vector->kind != TYPE_VEC) {
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` requires an expected constructible type");
            return &type_error;
        }
        return vector;
    }
    if (strcmp(name, "Dictionary::New") == 0) {
        Type *dictionary = checker->expected_type;
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` expects no arguments");
        if (!is_hash_storage_type(dictionary)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` requires an expected Dictionary type");
            return &type_error;
        }
        return dictionary;
    }
    if (strcmp(name, "Queue::New") == 0) {
        Type *queue = checker->expected_type;
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` expects no arguments");
        if (queue == NULL || queue->kind != TYPE_QUEUE) {
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` requires an expected Queue type");
            return &type_error;
        }
        return queue;
    }
    if (strcmp(name, "Queue::Enqueue") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_QUEUE) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Queue.Enqueue` expects a Queue and one value");
            return &type_unit;
        }
        Type *queue = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             queue->element);
        if (!same_type(queue->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Queue value expects `%s`, found `%s`",
                      queue->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_unit;
    }
    if (strcmp(name, "Queue::Dequeue") == 0 ||
        strcmp(name, "Queue::Peek") == 0) {
        bool dequeue = strcmp(name, "Queue::Dequeue") == 0;
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_QUEUE) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Queue.%s` expects one Queue value",
                      dequeue ? "Dequeue" : "Peek");
            return &type_error;
        }
        Type *queue = expr->as.call.arguments.items[0]->type;
        if (dequeue)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        else if (!type_is_copyable(checker, queue->element))
            lang_diag(checker->diagnostics, expr->span,
                      "`Queue::Peek` cannot copy noncopyable element type `%s`",
                      queue->element->name);
        else if (!dequeue &&
                 type_moves_by_default(checker, queue->element) &&
                 checker->copy_depth == 0U &&
                 checker->borrow_depth == 0U)
            lang_diag(
                checker->diagnostics, expr->span,
                "reading non-trivial Queue element `%s` requires `copy(...)`",
                queue->element->name);
        return queue->element;
    }
    if (strcmp(name, "Queue::TryDequeue") == 0 ||
        strcmp(name, "Queue::TryPeek") == 0) {
        bool dequeue = strcmp(name, "Queue::TryDequeue") == 0;
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_QUEUE) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Queue.%s` expects a Queue and an out value",
                      dequeue ? "TryDequeue" : "TryPeek");
            return &type_bool;
        }
        Type *queue = expr->as.call.arguments.items[0]->type;
        if (call_argument_mode(expr, 1U) != PARAMETER_MODE_OUT)
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "argument 2 to `%s` must use `out`", name);
        if (!same_type(queue->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Queue output expects `%s`, found `%s`",
                      queue->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (dequeue)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        else if (!type_is_copyable(checker, queue->element))
            lang_diag(checker->diagnostics, expr->span,
                      "`Queue::TryPeek` cannot copy noncopyable element type `%s`",
                      queue->element->name);
        return &type_bool;
    }
    if (strcmp(name, "Queue::Count") == 0 ||
        strcmp(name, "Queue::Capacity") == 0 ||
        strcmp(name, "Queue::Clear") == 0 ||
        strcmp(name, "Queue::TrimExcess") == 0) {
        bool read_only = strcmp(name, "Queue::Count") == 0 ||
            strcmp(name, "Queue::Capacity") == 0;
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_QUEUE)
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects one Queue value", name);
        else if (!read_only)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        return read_only ? &type_usize : &type_unit;
    }
    if (strcmp(name, "Queue::EnsureCapacity") == 0) {
        if (expr->as.call.arguments.count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_QUEUE ||
            !same_type(expr->as.call.arguments.items[1]->type, &type_usize)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Queue.EnsureCapacity` expects a Queue and nuint capacity");
            return &type_usize;
        }
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_usize;
    }
    if (strcmp(name, "Stack::New") == 0) {
        Type *stack = checker->expected_type;
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` expects no arguments");
        if (stack == NULL || stack->kind != TYPE_STACK) {
            lang_diag(checker->diagnostics, expr->span,
                      "`new()` requires an expected Stack type");
            return &type_error;
        }
        return stack;
    }
    if (strcmp(name, "Stack::Push") == 0) {
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack.Push` expects a Stack and one value");
            return &type_unit;
        }
        Type *stack = expr->as.call.arguments.items[0]->type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[1],
                             stack->element);
        if (!same_type(stack->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Stack value expects `%s`, found `%s`",
                      stack->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_unit;
    }
    if (strcmp(name, "Stack::Pop") == 0 ||
        strcmp(name, "Stack::Peek") == 0) {
        bool pop = strcmp(name, "Stack::Pop") == 0;
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack.%s` expects one Stack value",
                      pop ? "Pop" : "Peek");
            return &type_error;
        }
        Type *stack = expr->as.call.arguments.items[0]->type;
        if (pop)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        else if (!type_is_copyable(checker, stack->element))
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack::Peek` cannot copy noncopyable element type `%s`",
                      stack->element->name);
        else if (!pop &&
                 type_moves_by_default(checker, stack->element) &&
                 checker->copy_depth == 0U &&
                 checker->borrow_depth == 0U)
            lang_diag(
                checker->diagnostics, expr->span,
                "reading non-trivial Stack element `%s` requires `copy(...)`",
                stack->element->name);
        return stack->element;
    }
    if (strcmp(name, "Stack::TryPop") == 0 ||
        strcmp(name, "Stack::TryPeek") == 0) {
        bool pop = strcmp(name, "Stack::TryPop") == 0;
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack.%s` expects a Stack and an out value",
                      pop ? "TryPop" : "TryPeek");
            return &type_bool;
        }
        Type *stack = expr->as.call.arguments.items[0]->type;
        if (call_argument_mode(expr, 1U) != PARAMETER_MODE_OUT)
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "argument 2 to `%s` must use `out`", name);
        if (!same_type(stack->element,
                       expr->as.call.arguments.items[1]->type))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[1]->span,
                      "Stack output expects `%s`, found `%s`",
                      stack->element->name,
                      expr->as.call.arguments.items[1]->type->name);
        if (pop)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        else if (!type_is_copyable(checker, stack->element))
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack::TryPeek` cannot copy noncopyable element type `%s`",
                      stack->element->name);
        return &type_bool;
    }
    if (strcmp(name, "Stack::Count") == 0 ||
        strcmp(name, "Stack::Capacity") == 0 ||
        strcmp(name, "Stack::Clear") == 0) {
        bool read_only = strcmp(name, "Stack::Count") == 0 ||
            strcmp(name, "Stack::Capacity") == 0;
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK)
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects one Stack value", name);
        else if (!read_only)
            require_mutable_dictionary(
                checker, expr->as.call.arguments.items[0]);
        return read_only ? &type_usize : &type_unit;
    }
    if (strcmp(name, "Stack::EnsureCapacity") == 0) {
        if (expr->as.call.arguments.count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK ||
            !same_type(expr->as.call.arguments.items[1]->type, &type_usize)) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack.EnsureCapacity` expects a Stack and nuint capacity");
            return &type_usize;
        }
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_usize;
    }
    if (strcmp(name, "Stack::TrimExcess") == 0) {
        size_t count = expr->as.call.arguments.count;
        if (count == 2U)
            (void)coerce_literal(
                checker, expr->as.call.arguments.items[1], &type_usize);
        if ((count != 1U && count != 2U) ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_STACK ||
            (count == 2U && !same_type(
                expr->as.call.arguments.items[1]->type, &type_usize))) {
            lang_diag(checker->diagnostics, expr->span,
                      "`Stack.TrimExcess` expects a Stack and optional nuint capacity");
            return &type_unit;
        }
        require_mutable_dictionary(
            checker, expr->as.call.arguments.items[0]);
        return &type_unit;
    }
