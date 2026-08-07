    if (strcmp(name, "Buffer::allocate") == 0) return &type_buffer;
    if (strcmp(name, "BufferAsMutSlice") == 0) {
        if (checker->unsafe_depth == 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`buffer_as_mut_slice` requires an unsafe block");
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_BUFFER) {
            lang_diag(checker->diagnostics, expr->span,
                      "`buffer_as_mut_slice` expects one Buffer local");
        } else if (expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *buffer = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (buffer != NULL && !buffer->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot create a mutable slice from immutable Buffer `%s`",
                          buffer->name);
        }
        return &type_u8_slice;
    }
    if (strcmp(name, "BufferAsSlice") == 0) {
        if (checker->unsafe_depth == 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`BufferAsSlice` requires an unsafe block");
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_BUFFER)
            lang_diag(checker->diagnostics, expr->span,
                      "`BufferAsSlice` expects one Buffer value");
        return &type_readonly_u8_span;
    }
    if (strcmp(name, "Arena::new") == 0) {
        if (expr->as.call.arguments.count != 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`Arena::new` expects no arguments");
        return &type_arena;
    }
    if (strcmp(name, "ArenaAlloc") == 0) {
        if (checker->unsafe_depth == 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`arena_alloc` requires an unsafe block");
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_ARENA ||
            expr->as.call.arguments.items[1]->type->kind != TYPE_I64)
            lang_diag(checker->diagnostics, expr->span,
                      "`arena_alloc` expects `(Arena, i64)`");
        if (expr->as.call.arguments.count >= 1U &&
            expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *arena = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (arena != NULL && !arena->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot allocate through immutable Arena `%s`",
                          arena->name);
        }
        if (checker->expected_type != NULL &&
            checker->expected_type->kind == TYPE_RAW_POINTER)
            return checker->expected_type;
        return &type_raw_pointer;
    }
    if (strcmp(name, "ArenaReset") == 0) {
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_ARENA)
            lang_diag(checker->diagnostics, expr->span,
                      "`arena_reset` expects an Arena");
        if (expr->as.call.arguments.count >= 1U &&
            expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
            Local *arena = find_local(
                checker, expr->as.call.arguments.items[0]->as.name);
            if (arena != NULL && !arena->mutable_)
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "cannot reset immutable Arena `%s`",
                          arena->name);
        }
        return &type_unit;
    }
    if (strcmp(name, "raw_load_i64") == 0) {
        if (checker->unsafe_depth == 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`raw_load_i64` requires an unsafe block");
        if (expr->as.call.arguments.count != 1U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_RAW_POINTER ||
            expr->as.call.arguments.items[0]->type->element == NULL ||
            expr->as.call.arguments.items[0]->type->element->kind != TYPE_I64)
            lang_diag(checker->diagnostics, expr->span,
                      "`raw_load_i64` expects `const long*` or `long*`");
        return &type_i64;
    }
    if (strcmp(name, "raw_store_i64") == 0) {
        if (checker->unsafe_depth == 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`raw_store_i64` requires an unsafe block");
        if (expr->as.call.arguments.count != 2U ||
            expr->as.call.arguments.items[0]->type->kind != TYPE_RAW_POINTER ||
            !expr->as.call.arguments.items[0]->type->pointer_mutable ||
            expr->as.call.arguments.items[0]->type->element == NULL ||
            expr->as.call.arguments.items[0]->type->element->kind != TYPE_I64 ||
            expr->as.call.arguments.items[1]->type->kind != TYPE_I64)
            lang_diag(checker->diagnostics, expr->span,
                      "`raw_store_i64` expects `(long*, long)`");
        return &type_unit;
    }
    if (strcmp(name, "Option::Some") == 0 ||
        strcmp(name, "Option::None") == 0) {
        Type *option = checker->expected_type;
        if (option == NULL || option->kind != TYPE_OPTION) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` requires an expected `Option<T>` type", name);
            return &type_error;
        }
        bool some = strcmp(name, "Option::Some") == 0;
        size_t expected_count = some ? 1U : 0U;
        if (!some && !expr->as.call.implicit_enum_value &&
            expr->as.call.arguments.count == 0U)
            lang_diag(checker->diagnostics, expr->span,
                      "`Option.None` is a value and must not be called");
        if (expr->as.call.arguments.count != expected_count)
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects %zu payload value(s)",
                      name, expected_count);
        if (some && expr->as.call.arguments.count == 1U) {
            (void)coerce_literal(checker,
                                 expr->as.call.arguments.items[0],
                                 option->element);
            Type *actual = expr->as.call.arguments.items[0]->type;
            if (!same_type(option->element, actual))
                lang_diag(checker->diagnostics,
                          expr->as.call.arguments.items[0]->span,
                          "`Option::Some` expects `%s`, found `%s`",
                          option->element->name, actual->name);
        }
        return option;
    }
    if (strcmp(name, "Result::Ok") == 0 || strcmp(name, "Result::Err") == 0) {
        Type *result = checker->expected_type != NULL &&
                       checker->expected_type->kind == TYPE_RESULT
                     ? checker->expected_type
                     : checker->function->checked_return_type;
        if (result == NULL || result->kind != TYPE_RESULT) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` needs a surrounding Result-returning function", name);
            return &type_error;
        }
        if (expr->as.call.arguments.count != 1U) {
            lang_diag(checker->diagnostics, expr->span,
                      "`%s` expects one payload value", name);
            return result;
        }
        Type *expected = strcmp(name, "Result::Ok") == 0
                       ? result->element : result->error_type;
        (void)coerce_literal(checker, expr->as.call.arguments.items[0],
                             expected);
        Type *actual = expr->as.call.arguments.items[0]->type;
        if (!type_assignable(expected, actual))
            lang_diag(checker->diagnostics,
                      expr->as.call.arguments.items[0]->span,
                      "`%s` payload expects `%s`, found `%s`",
                      name, expected->name, actual->name);
        return result;
    }
    const char *separator = last_path_separator(name);
    if (separator != NULL) {
        size_t enum_length = (size_t)(separator - name);
        char *enum_name = lang_arena_strndup(
            &checker->module->arena, name, enum_length);
        Type *applied_enum = NULL;
        Decl *decl = NULL;
        if (checker->expected_type != NULL &&
            checker->expected_type->kind == TYPE_NAMED &&
            checker->expected_type->declaration != NULL &&
            checker->expected_type->declaration->kind == DECL_ENUM &&
            visible_declaration_path_matches(
                checker,
                enum_name,
                checker->expected_type->declaration->as.enumeration.name,
                checker->expected_type->declaration->module_name)) {
            applied_enum = checker->expected_type;
            decl = (Decl *)applied_enum->declaration;
        } else {
            decl = find_type_declaration(
                checker, enum_name, expr->span);
        }
        if (decl != NULL && decl->kind == DECL_ENUM) {
            expr->resolved_decl = decl;
            const char *variant = separator + 2;
            for (size_t v = 0U; v < decl->as.enumeration.variant_count; ++v) {
                FieldDecl *candidate = &decl->as.enumeration.variants[v];
                if (strcmp(candidate->name, variant) == 0) {
                    Type *result = applied_enum != NULL
                                 ? applied_enum
                                 : resolve_type(
                                       checker,
                                       decl->as.enumeration.name,
                                       expr->span);
                    bool payload = strcmp(candidate->type_name, "Unit") != 0;
                    if (!payload && !expr->as.call.implicit_enum_value &&
                        expr->as.call.arguments.count == 0U)
                        lang_diag(
                            checker->diagnostics, expr->span,
                            "`%s` is a value and must not be called", name);
                    if (expr->as.call.arguments.count != (payload ? 1U : 0U))
                        lang_diag(checker->diagnostics, expr->span,
                                  "%s alternative `%s` expects %u payload value(s)",
                                  decl->as.enumeration.is_union
                                      ? "union" : "enum",
                                  name, payload ? 1U : 0U);
                    if (payload && expr->as.call.arguments.count == 1U) {
                        Type *expected =
                            resolve_type_syntax_in_applied_declaration(
                            checker, result, candidate->type_syntax,
                            candidate->type_name, candidate->span);
                        (void)coerce_literal(
                            checker, expr->as.call.arguments.items[0],
                            expected);
                        Type *actual = expr->as.call.arguments.items[0]->type;
                        if (!type_assignable(expected, actual))
                            lang_diag(checker->diagnostics,
                                      expr->as.call.arguments.items[0]->span,
                                      "enum variant `%s` expects `%s`, found `%s`",
                                      name, expected->name, actual->name);
                    }
                    return result;
                }
            }
        }
    }
    Function *function = declared_function;
    if (function == NULL) {
        const char *constructor = last_path_separator(name);
        if (constructor != NULL &&
            strcmp(constructor + 2U, "new") == 0) {
            char *type_name = lang_arena_strndup(
                &checker->module->arena, name,
                (size_t)(constructor - name));
            Decl *type = find_type_declaration(
                checker, type_name, expr->span);
            if (type != NULL && type->kind == DECL_CLASS &&
                type->as.structure.is_interface) {
                lang_diag(checker->diagnostics, expr->span,
                          "cannot instantiate interface `%s`",
                          type->as.structure.name);
                return &type_error;
            }
        }
        lang_diag(checker->diagnostics, expr->span,
                  "unknown function `%s`", name);
        return &type_error;
    }
    if (function->is_constructor &&
        function->checked_return_type != NULL &&
        function->checked_return_type->declaration != NULL &&
        function->checked_return_type->declaration->kind == DECL_CLASS &&
        function->checked_return_type->declaration
            ->as.structure.is_abstract)
        lang_diag(checker->diagnostics, expr->span,
                  "cannot instantiate abstract class `%s`",
                  function->checked_return_type->declaration
                      ->as.structure.name);
    if (function->is_deleted)
        lang_diag(checker->diagnostics, expr->span,
                  "copy constructor for `%s` is deleted",
                  function->owner_type != NULL
                      ? function->owner_type : function->name);
    if (function->param_count != expr->as.call.arguments.count) {
        lang_diag(checker->diagnostics, expr->span,
                  "function `%s` expects %zu arguments, found %zu", name,
                  function->param_count, expr->as.call.arguments.count);
    }
    size_t count = function->param_count < expr->as.call.arguments.count
                 ? function->param_count : expr->as.call.arguments.count;
    for (size_t i = 0U; i < count; ++i) {
        Type *expected = resolve_declared_type_in_module(
            checker, function->params[i].type_syntax,
            function->params[i].type_name,
            function->params[i].span, declared_module);
        (void)coerce_literal(checker, expr->as.call.arguments.items[i],
                             expected);
        Type *actual = expr->as.call.arguments.items[i]->type;
        if (!type_assignable(expected, actual))
            lang_diag(checker->diagnostics, expr->as.call.arguments.items[i]->span,
                      "argument %zu to `%s` expects `%s`, found `%s`", i + 1U,
                      name, type_display_name(checker, expected),
                      type_display_name(checker, actual));
    }
    return resolve_declared_type_in_module(
        checker, function->return_type_syntax,
        function->return_type, function->span, declared_module);
