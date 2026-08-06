#include "checker_internal.h"

#include <stdio.h>
#include <string.h>

static Decl *find_element_declaration(Checker *checker, const char *name,
                                      LangSpan use_span) {
    Decl *imported = NULL;
    Decl *first_import = NULL;
    for (size_t i = 0U; i < checker->module->count; ++i) {
        Decl *decl = checker->module->decls[i];
        if (decl->kind != DECL_ELEMENT)
            continue;
        if (checker->current_module != NULL &&
            decl->module_name != NULL &&
            strcmp(checker->current_module, decl->module_name) == 0 &&
            strcmp(name, decl->as.element.name) == 0)
            return decl;
        if (!decl->is_public ||
            !imported_declaration_matches(
                checker, name, decl->as.element.name,
                decl->module_name))
            continue;
        if (imported != NULL) {
            LangDiagnostic *diagnostic =
                lang_diag(checker->diagnostics, use_span,
                          "ambiguous imported element `%s`", name);
            lang_diag_secondary(diagnostic, first_import->span,
                                "first public candidate");
            lang_diag_secondary(diagnostic, decl->span,
                                "another public candidate");
            return imported;
        }
        imported = decl;
        first_import = decl;
    }
    return imported;
}
static bool is_element_child_type(const Type *type) {
    if (type->kind == TYPE_STRING ||
        type->kind == TYPE_HTML || type->kind == TYPE_UNIT ||
        type->kind == TYPE_ERROR)
        return true;
    if (type->kind == TYPE_BOOL || type->kind == TYPE_CHAR ||
        (type->kind >= TYPE_I8 && type->kind <= TYPE_USIZE) ||
        type->kind == TYPE_F32 || type->kind == TYPE_F64)
        return true;
    if (type->kind == TYPE_OPTION || type->kind == TYPE_VEC ||
        type->kind == TYPE_ARRAY)
        return type->element != NULL &&
               is_element_child_type(type->element);
    return false;
}

static bool element_property_names_equal(
    const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        char left_char = *left == '-' ? '_' : *left;
        char right_char = *right == '-' ? '_' : *right;
        if (left_char != right_char)
            return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static bool element_property_has_prefix(
    const char *name, const char *prefix) {
    while (*prefix != '\0') {
        char name_char = *name == '-' ? '_' : *name;
        char prefix_char = *prefix == '-' ? '_' : *prefix;
        if (name_char == '\0' || name_char != prefix_char)
            return false;
        ++name;
        ++prefix;
    }
    return *name != '\0';
}

static Type *html_global_attribute_type(const char *name) {
    static const char *text_attributes[] = {
        "id", "class", "title", "lang", "dir", "role", "style",
        "slot", "part", "exportparts", "accesskey", "autocapitalize",
        "contenteditable", "draggable", "enterkeyhint", "inputmode",
        "is", "itemid", "itemprop", "itemref", "itemtype", "nonce",
        "popover", "spellcheck", "translate", "virtualkeyboardpolicy"
    };
    for (size_t i = 0U;
         i < sizeof(text_attributes) / sizeof(text_attributes[0]); ++i)
        if (element_property_names_equal(name, text_attributes[i]))
            return &type_string;
    if (element_property_names_equal(name, "hidden") ||
        element_property_names_equal(name, "inert") ||
        element_property_names_equal(name, "itemscope") ||
        element_property_names_equal(name, "autofocus"))
        return &type_bool;
    if (element_property_names_equal(name, "tabindex"))
        return &type_i64;
    if (element_property_has_prefix(name, "data_") ||
        element_property_has_prefix(name, "aria_"))
        return &type_string;
    return NULL;
}

static bool html_event_attribute(const char *name) {
    return element_property_names_equal(name, "onclick") ||
           element_property_names_equal(name, "oninput") ||
           element_property_names_equal(name, "onchange") ||
           element_property_names_equal(name, "onsubmit");
}

static bool web_handler_scalar_type(const Type *type) {
    return type->kind == TYPE_BOOL || is_signed_integer(type) ||
           is_unsigned_integer(type) || type->kind == TYPE_UNIT;
}

static bool web_handler_parameter_type(const Type *type) {
    return web_handler_scalar_type(type) || type->kind == TYPE_STRING;
}

static bool web_handler_result_type(const Type *type) {
    return web_handler_scalar_type(type) || type->kind == TYPE_STRING ||
           type->kind == TYPE_HTML ||
           (type->kind == TYPE_NAMED && type->declaration != NULL &&
            type->declaration->kind == DECL_STRUCT);
}

static const Type *web_handler_completion_type(const Type *type) {
    return type != NULL && type->kind == TYPE_TASK ? type->element : type;
}

static char web_handler_type_code(const Type *type) {
    if (type->kind == TYPE_BOOL)
        return 'b';
    if (type->kind == TYPE_UNIT)
        return 'v';
    if (is_signed_integer(type) || is_unsigned_integer(type))
        return 'l';
    if (type->kind == TYPE_STRING)
        return 's';
    if (type->kind == TYPE_HTML)
        return 'h';
    if (type->kind == TYPE_NAMED && type->declaration != NULL &&
        type->declaration->kind == DECL_STRUCT)
        return 'a';
    return '?';
}

static void check_html_event_handler(
    Checker *checker, ElementProperty *property) {
    Type *handler_type = check_expr(checker, property->value);
    Decl *handler = (Decl *)property->value->resolved_decl;
    if (handler_type->kind != TYPE_FUNCTION || handler == NULL ||
        handler->kind != DECL_FUNCTION) {
        lang_diag(checker->diagnostics, property->value->span,
                  "event property `%s` requires a statically named function",
                  property->name);
        return;
    }
    if (!handler->is_public)
        lang_diag(checker->diagnostics, property->value->span,
                  "event handler `%s` must be public",
                  handler->as.function.name);
    handler->as.function.is_web_handler = true;
    const Type *completion =
        web_handler_completion_type(handler_type->element);
    if (handler_type->element->kind == TYPE_TASK &&
        !handler->as.function.is_async)
        lang_diag(checker->diagnostics, property->value->span,
                  "Task-returning event handler `%s` must be declared `async`",
                  handler->as.function.name);
    if (!web_handler_result_type(completion))
        lang_diag(checker->diagnostics, property->value->span,
                  "event handler `%s` must return a scalar, `String`, `Html`, struct, `void`, or `Task` of one of those types",
                  handler->as.function.name);
    for (size_t argument = 0U;
         argument < handler_type->argument_count; ++argument)
        if (!web_handler_parameter_type(
                handler_type->arguments[argument]) ||
            handler_type->arguments[argument]->kind == TYPE_UNIT)
            lang_diag(checker->diagnostics, property->value->span,
                      "event handler `%s` parameters must be scalar or `string`",
                      handler->as.function.name);

    const char *handler_name = strrchr(handler->as.function.name, ':');
    handler_name = handler_name == NULL
                 ? handler->as.function.name : handler_name + 1U;
    const char *event_name = property->name + 2U;
    size_t length = strlen(event_name) + strlen(handler_name) + 4U;
    for (size_t parameter = 0U;
         parameter < handler->as.function.param_count; ++parameter)
        length += strlen(handler->as.function.params[parameter].name) + 3U;
    char *binding = lang_arena_alloc(
        &checker->module->arena, length + 1U);
    char result_code = completion->kind == TYPE_STRING
        ? 'o' : web_handler_type_code(completion);
    if (handler_type->element->kind == TYPE_TASK &&
        result_code >= 'a' && result_code <= 'z')
        result_code = (char)(result_code - ('a' - 'A'));
    size_t offset = (size_t)snprintf(
        binding, length + 1U, "%s|%s|%c", event_name, handler_name,
        result_code);
    for (size_t parameter = 0U;
         parameter < handler->as.function.param_count; ++parameter)
        offset += (size_t)snprintf(
            binding + offset, length + 1U - offset, "|%c:%s",
            web_handler_type_code(handler_type->arguments[parameter]),
            handler->as.function.params[parameter].name);
    property->event_binding = binding;
}

static Type *check_element_attribute_value(
    Checker *checker, ElementProperty *property, Type *schema_type,
    bool allow_optional_value, bool allow_owned_text,
    bool allow_interpolated_borrow) {
    Type *previous_expected = checker->expected_type;
    bool previous_html_destination =
        checker->html_interpolation_destination;
    checker->expected_type = schema_type;
    checker->html_interpolation_destination =
        allow_owned_text &&
        property->value->kind == EXPR_INTERPOLATION;
    Type *actual = check_expr(checker, property->value);
    checker->expected_type = previous_expected;
    checker->html_interpolation_destination =
        previous_html_destination;

    Type *value_type = schema_type->kind == TYPE_OPTION
                     ? schema_type->element : schema_type;
    if (allow_interpolated_borrow &&
        property->value->kind == EXPR_INTERPOLATION &&
        value_type->kind == TYPE_STR &&
        actual->kind == TYPE_STRING) {
        property->borrow_interpolated_string = true;
        return actual;
    }
    if (same_type(actual, schema_type) ||
        same_type(actual, value_type) ||
        (allow_owned_text &&
         value_type->kind == TYPE_STR &&
         actual->kind == TYPE_STRING))
        return actual;
    if (allow_optional_value &&
        actual->kind == TYPE_OPTION &&
        actual->element != NULL &&
        same_type(actual->element, value_type))
        return actual;

    (void)coerce_literal(checker, property->value, value_type);
    actual = property->value->type;
    if (!same_type(actual, value_type))
        lang_diag(checker->diagnostics, property->value->span,
                  "property `%s` expects `%s`, found `%s`",
                  property->name,
                  type_display_name(checker, schema_type),
                  type_display_name(checker, actual));
    return actual;
}

static bool css_custom_property_value_type(const Type *type) {
    return type->kind == TYPE_STRING ||
           is_signed_integer(type) || is_unsigned_integer(type) ||
           type->kind == TYPE_F32 || type->kind == TYPE_F64;
}

static bool css_custom_property_atom(const char *data, size_t length) {
    if (length == 0U) return false;
    for (size_t i = 0U; i < length; ++i) {
        unsigned char byte = (unsigned char)data[i];
        if ((byte >= 'a' && byte <= 'z') ||
            (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') ||
            byte == '#' || byte == '_' || byte == '-' ||
            byte == '.' || byte == '%' || byte == '+')
            continue;
        return false;
    }
    return true;
}

static void check_css_custom_property(
    Checker *checker, ElementProperty *property) {
    Type *actual = check_expr(checker, property->value);
    if (property->value->kind == EXPR_INTERPOLATION) {
        lang_diag(
            checker->diagnostics, property->value->span,
            "CSS custom property values cannot use string interpolation; pass a value directly");
        return;
    }
    if (!css_custom_property_value_type(actual) &&
        actual->kind != TYPE_ERROR) {
        lang_diag(
            checker->diagnostics, property->value->span,
            "CSS custom property `%s` expects `string`, `String`, or a number; found `%s`",
            property->name, type_display_name(checker, actual));
        return;
    }
    if (property->value->kind == EXPR_STRING &&
        !css_custom_property_atom(
            property->value->as.string.data,
            property->value->as.string.length))
        lang_diag(
            checker->diagnostics, property->value->span,
            "unsafe CSS custom property value; use a single color, dimension, number, or identifier");
}

static Type *check_html_child_expression(
    Checker *checker, Expr *expression) {
    bool borrowable_place =
        expression->kind == EXPR_NAME ||
        (expression->kind == EXPR_FIELD &&
         expression->as.field.object->kind == EXPR_NAME);
    if (borrowable_place) {
        Type *result = check_place(checker, expression);
        if (result->kind == TYPE_STRING) {
            expression->borrow_html_string = true;
            return result;
        }
        return check_expr(checker, expression);
    }
    bool previous =
        checker->html_interpolation_destination;
    checker->html_interpolation_destination =
        expression->kind == EXPR_INTERPOLATION;
    Type *result = check_expr(checker, expression);
    checker->html_interpolation_destination = previous;
    return result;
}

Type *check_element(Checker *checker, Expr *expr) {
    if (strcmp(expr->as.element.name, "#fragment") == 0) {
        ++checker->depth;
        size_t start_locals = checker->local_count;
        for (size_t i = 0U;
             i < expr->as.element.body_count; ++i) {
            ElementBodyItem *item = &expr->as.element.body[i];
            if (item->is_statement) {
                (void)check_stmt(checker, item->as.statement);
            } else {
                Type *child =
                    check_html_child_expression(
                        checker, item->as.expression);
                if (!is_element_child_type(child))
                    lang_diag(
                        checker->diagnostics,
                        item->as.expression->span,
                        "fragment child must produce text, a scalar, `Html`, or `void`; found `%s`",
                        child->name);
            }
        }
        checker->local_count = start_locals;
        --checker->depth;
        return &type_html;
    }
    Function *component =
        find_function(checker, expr->as.element.name, expr->span);
    Decl *descriptor =
        find_element_declaration(checker, expr->as.element.name, expr->span);
    if (descriptor != NULL)
        component = NULL;
    if (component == NULL && descriptor == NULL) {
        lang_diag(checker->diagnostics, expr->span,
                  "unknown element `%s`", expr->as.element.name);
        return &type_error;
    }
    if (component != NULL) {
        expr->resolved_decl = function_declaration(checker, component);
        const char *component_module =
            function_module_name(checker, component);
        Type *component_result = resolve_declared_type_in_module(
            checker, component->return_type_syntax,
            component->return_type, component->span,
            component_module);
        if (component_result->kind != TYPE_HTML)
            lang_diag(checker->diagnostics, expr->span,
                      "component `%s` must return `Html`, found `%s`",
                      component->name, component_result->name);
        for (size_t i = 0U; i < expr->as.element.property_count; ++i) {
            ElementProperty *property = &expr->as.element.properties[i];
            if (property->css_custom_property) {
                (void)check_expr(checker, property->value);
                lang_diag(checker->diagnostics, property->span,
                          "CSS custom properties may only be set on native elements");
                continue;
            }
            for (size_t j = 0U; j < i; ++j)
                if (element_property_names_equal(
                        property->name,
                        expr->as.element.properties[j].name))
                    lang_diag(checker->diagnostics, property->span,
                              "duplicate property `%s`", property->name);
            Param *parameter = NULL;
            for (size_t p = 0U; p < component->param_count; ++p)
                if (element_property_names_equal(
                        component->params[p].name, property->name))
                    parameter = &component->params[p];
            if (element_property_names_equal(
                    property->name, "children"))
                lang_diag(checker->diagnostics, property->span,
                          "`children` is supplied by the element body");
            if (parameter == NULL) {
                (void)check_expr(checker, property->value);
                lang_diag(checker->diagnostics, property->span,
                          "unknown property `%s` on component `%s`",
                          property->name, component->name);
            } else {
                Type *expected = resolve_declared_type_in_module(
                    checker, parameter->type_syntax,
                    parameter->type_name, parameter->span,
                    component_module);
                Type *actual;
                if (parameter->borrowed) {
                    bool borrowable_place =
                        property->value->kind == EXPR_NAME ||
                        (property->value->kind == EXPR_FIELD &&
                         property->value->as.field.object->kind ==
                             EXPR_NAME);
                    if (!borrowable_place) {
                        lang_diag(checker->diagnostics,
                                  property->value->span,
                                  "`ref` component property must be an available place");
                        actual = check_expr(
                            checker, property->value);
                    } else {
                        actual = check_place(
                            checker, property->value);
                        if (parameter->mutable_) {
                            Expr *root = property->value;
                            while (root->kind == EXPR_FIELD)
                                root = root->as.field.object;
                            Local *local = root->kind == EXPR_NAME
                                ? find_local(checker, root->as.name)
                                : NULL;
                            if (local == NULL || !local->mutable_)
                                lang_diag(
                                    checker->diagnostics,
                                    property->value->span,
                                    "`ref` component property requires a mutable local");
                        }
                    }
                    (void)coerce_literal(
                        checker, property->value, expected);
                    actual = property->value->type;
                    if (!same_type(expected, actual))
                        lang_diag(
                            checker->diagnostics,
                            property->value->span,
                            "property `%s` expects `%s`, found `%s`",
                            property->name,
                            type_display_name(checker, expected),
                            type_display_name(checker, actual));
                } else {
                    actual = check_element_attribute_value(
                        checker, property, expected,
                        false, false, true);
                }
                (void)actual;
            }
        }
        for (size_t p = 0U; p < component->param_count; ++p) {
            if (strcmp(component->params[p].name, "children") == 0) {
                Type *children_type = resolve_declared_type_in_module(
                    checker, component->params[p].type_syntax,
                    component->params[p].type_name,
                    component->params[p].span, component_module);
                if (children_type->kind != TYPE_HTML)
                    lang_diag(checker->diagnostics, component->params[p].span,
                              "component `children` parameter must have type `Html`");
                continue;
            }
            Type *parameter_type = resolve_declared_type_in_module(
                checker, component->params[p].type_syntax,
                component->params[p].type_name,
                component->params[p].span, component_module);
            bool found = false;
            for (size_t i = 0U; i < expr->as.element.property_count; ++i)
                if (element_property_names_equal(
                        component->params[p].name,
                        expr->as.element.properties[i].name))
                    found = true;
            if (!found && parameter_type->kind != TYPE_OPTION)
                lang_diag(checker->diagnostics, expr->span,
                          "missing required property `%s`",
                          component->params[p].name);
        }
        bool accepts_children = false;
        for (size_t p = 0U; p < component->param_count; ++p)
            if (strcmp(component->params[p].name, "children") == 0)
                accepts_children = true;
        if (expr->as.element.body_count != 0U && !accepts_children)
            lang_diag(checker->diagnostics, expr->span,
                      "component `%s` does not declare a children parameter",
                      component->name);
        if (accepts_children) {
            ++checker->depth;
            size_t start_locals = checker->local_count;
            for (size_t i = 0U; i < expr->as.element.body_count; ++i) {
                ElementBodyItem *item = &expr->as.element.body[i];
                if (item->is_statement) {
                    (void)check_stmt(checker, item->as.statement);
                } else {
                    Type *child =
                        check_html_child_expression(
                            checker, item->as.expression);
                    if (!is_element_child_type(child))
                        lang_diag(checker->diagnostics,
                                  item->as.expression->span,
                                  "component child must produce text, a scalar, `Html`, or `void`; found `%s`",
                                  child->name);
                }
            }
            checker->local_count = start_locals;
            --checker->depth;
        }
        return &type_html;
    }
    if (descriptor != NULL) {
        expr->resolved_decl = descriptor;
        Type *result = resolve_declared_type_in_module(
            checker, descriptor->as.element.result_type_syntax,
            descriptor->as.element.result_type, descriptor->span,
            descriptor->module_name);
        if (result->kind != TYPE_HTML)
            lang_diag(checker->diagnostics, descriptor->span,
                      "element `%s` must produce `Html`, found `%s`",
                      descriptor->as.element.name, result->name);
        bool accepts_children = false;
        bool has_custom_property = false;
        bool has_style_property = false;
        for (size_t i = 0U; i < expr->as.element.property_count; ++i) {
            ElementProperty *property = &expr->as.element.properties[i];
            has_custom_property |= property->css_custom_property;
            has_style_property |= !property->css_custom_property &&
                element_property_names_equal(property->name, "style");
        }
        if (has_custom_property && has_style_property)
            lang_diag(
                checker->diagnostics, expr->as.element.open_span,
                "native `style` and CSS custom properties cannot be combined on one element");
        for (size_t p = 0U;
             p < descriptor->as.element.property_count; ++p) {
            FieldDecl *declared =
                &descriptor->as.element.properties[p];
            if (strcmp(declared->name, "children") == 0) {
                accepts_children = true;
                Type *children = resolve_declared_type_in_module(
                    checker, declared->type_syntax,
                    declared->type_name, declared->span,
                    descriptor->module_name);
                if (children->kind != TYPE_HTML)
                    lang_diag(checker->diagnostics, declared->span,
                              "element `children` property must have type `Html`");
            }
        }
        for (size_t i = 0U;
             i < expr->as.element.property_count; ++i) {
            ElementProperty *property =
                &expr->as.element.properties[i];
            if (property->css_custom_property) {
                for (size_t prior = 0U; prior < i; ++prior)
                    if (expr->as.element.properties[prior]
                            .css_custom_property &&
                        strcmp(
                            property->name,
                            expr->as.element.properties[prior].name) == 0)
                        lang_diag(
                            checker->diagnostics, property->span,
                            "duplicate CSS custom property `%s`",
                            property->name);
                check_css_custom_property(checker, property);
                continue;
            }
            if (element_property_names_equal(property->name, "style") &&
                property->value->kind != EXPR_STRING) {
                (void)check_expr(checker, property->value);
                lang_diag(
                    checker->diagnostics, property->value->span,
                    "dynamic `style` values are unsafe; use CSS custom properties such as `--accent=value`");
                continue;
            }
            for (size_t prior = 0U; prior < i; ++prior)
                if (element_property_names_equal(
                        property->name,
                        expr->as.element.properties[prior].name))
                    lang_diag(checker->diagnostics, property->span,
                              "duplicate property `%s`", property->name);
            FieldDecl *declared = NULL;
            for (size_t p = 0U;
                 p < descriptor->as.element.property_count; ++p)
                if (element_property_names_equal(
                        descriptor->as.element.properties[p].name,
                        property->name))
                    declared = &descriptor->as.element.properties[p];
            if (element_property_names_equal(
                    property->name, "children"))
                lang_diag(checker->diagnostics, property->span,
                          "`children` is supplied by the element body");
            if (html_event_attribute(property->name)) {
                check_html_event_handler(checker, property);
                continue;
            }
            Type *expected = declared != NULL
                ? resolve_declared_type_in_module(
                      checker, declared->type_syntax,
                      declared->type_name, declared->span,
                      descriptor->module_name)
                : html_global_attribute_type(property->name);
            if (declared == NULL) {
                if (expected == NULL) {
                    (void)check_expr(checker, property->value);
                    lang_diag(checker->diagnostics, property->span,
                              "unknown property `%s` on element `%s`",
                              property->name,
                              descriptor->as.element.name);
                    continue;
                }
            }
            (void)check_element_attribute_value(
                checker, property, expected,
                declared == NULL, true, false);
        }
        for (size_t p = 0U;
             p < descriptor->as.element.property_count; ++p) {
            FieldDecl *declared =
                &descriptor->as.element.properties[p];
            if (strcmp(declared->name, "children") == 0)
                continue;
            Type *property_type = resolve_declared_type_in_module(
                checker, declared->type_syntax,
                declared->type_name, declared->span,
                descriptor->module_name);
            if (property_type->kind == TYPE_OPTION)
                continue;
            bool found = false;
            for (size_t i = 0U;
                 i < expr->as.element.property_count; ++i)
                if (element_property_names_equal(
                        declared->name,
                        expr->as.element.properties[i].name))
                    found = true;
            if (!found)
                lang_diag(checker->diagnostics, expr->span,
                          "missing required property `%s`",
                          declared->name);
        }
        if (expr->as.element.body_count != 0U && !accepts_children)
            lang_diag(checker->diagnostics, expr->span,
                      "element `%s` does not accept children",
                      descriptor->as.element.name);
        ++checker->depth;
        size_t start_locals = checker->local_count;
        for (size_t i = 0U; i < expr->as.element.body_count; ++i) {
            ElementBodyItem *item = &expr->as.element.body[i];
            if (item->is_statement) {
                (void)check_stmt(checker, item->as.statement);
            } else {
                Type *child =
                    check_html_child_expression(
                        checker, item->as.expression);
                if (!is_element_child_type(child))
                    lang_diag(checker->diagnostics,
                              item->as.expression->span,
                              "element child must produce text, a scalar, `Html`, or `void`; found `%s`",
                              child->name);
            }
        }
        checker->local_count = start_locals;
        --checker->depth;
        return result;
    }
    return &type_error;
}
