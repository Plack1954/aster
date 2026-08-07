#include "checker_internal.h"

#include <inttypes.h>
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

static bool projection_state_type(const Type *type) {
    if (type == NULL || type->kind != TYPE_NAMED ||
        type->declaration == NULL ||
        type->declaration->kind != DECL_STRUCT)
        return false;
    const char *name = type->declaration->as.structure.name;
    const char *suffix = "ProjectionState";
    size_t name_length = strlen(name);
    size_t suffix_length = strlen(suffix);
    return name_length >= suffix_length &&
           strcmp(name + name_length - suffix_length, suffix) == 0;
}

static bool projection_transition_type(const Type *type) {
    if (type == NULL || type->kind != TYPE_NAMED ||
        type->declaration == NULL ||
        type->declaration->kind != DECL_STRUCT)
        return false;
    const char *name = type->declaration->as.structure.name;
    const char *suffix = "ProjectionTransition";
    size_t name_length = strlen(name);
    size_t suffix_length = strlen(suffix);
    return name_length >= suffix_length &&
           strcmp(name + name_length - suffix_length, suffix) == 0;
}

static bool projection_batch_type(const Type *type) {
    return projection_state_type(type) || projection_transition_type(type);
}

static bool projection_field_part(
    const Type *type, const char *field_name, uint64_t *part_id) {
    if (!projection_state_type(type) || type->declaration == NULL)
        return false;
    const Decl *declaration = type->declaration;
    for (size_t field = 0U;
         field < declaration->as.structure.field_count; ++field)
        if (strcmp(declaration->as.structure.fields[field].name,
                   field_name) == 0) {
            *part_id = lang_projection_part_id(
                declaration->module_name,
                declaration->as.structure.name, field);
            return true;
        }
    return false;
}

static bool completion_projection_part(
    const Type *completion, const char *field_name, uint64_t *part_id) {
    if (projection_field_part(completion, field_name, part_id))
        return true;
    if (!projection_transition_type(completion) ||
        completion->declaration == NULL)
        return false;
    const Decl *transition = completion->declaration;
    for (size_t field = 0U;
         field < transition->as.structure.field_count; ++field)
        if (projection_field_part(
                transition->as.structure.fields[field].checked_type,
                field_name, part_id))
            return true;
    return false;
}

static bool web_handler_standard_html_type(
    const Type *type, const char *name) {
    return type->kind == TYPE_NAMED && type->declaration != NULL &&
           type->declaration->kind == DECL_STRUCT &&
           type->declaration->module_name != NULL &&
           strcmp(type->declaration->module_name, "Aster::Html") == 0 &&
           strcmp(type->declaration->as.structure.name, name) == 0;
}

static char web_handler_type_code(const Type *type) {
    if (projection_batch_type(type))
        return 'p';
    if (web_handler_standard_html_type(type, "KeyedRemove"))
        return 'r';
    if (web_handler_standard_html_type(type, "KeyedClear"))
        return 'c';
    if (web_handler_standard_html_type(type, "KeyedSwap"))
        return 'w';
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

static bool projection_property(const char *name) {
    return element_property_names_equal(name, "project_text") ||
           element_property_names_equal(name, "project_disabled") ||
           element_property_names_equal(name, "project_class");
}

static void check_projection_property(
    Checker *checker, ElementProperty *property) {
    Type *value = check_expr(checker, property->value);
    Expr *expression = property->value;
    if (expression->kind != EXPR_FIELD ||
        !projection_state_type(expression->as.field.object->type)) {
        lang_diag(checker->diagnostics, expression->span,
                  "projection `%s` requires a direct field of a `*ProjectionState` struct",
                  property->name);
        return;
    }
    bool valid = false;
    char kind = '?';
    if (element_property_names_equal(property->name, "project_text")) {
        valid = value->kind == TYPE_STRING || value->kind == TYPE_BOOL ||
                is_signed_integer(value) || is_unsigned_integer(value);
        kind = 't';
    } else if (element_property_names_equal(
                   property->name, "project_disabled")) {
        valid = value->kind == TYPE_BOOL;
        kind = 'd';
    } else if (element_property_names_equal(
                   property->name, "project_class")) {
        valid = value->kind == TYPE_STRING;
        kind = 'c';
    }
    if (!valid) {
        lang_diag(checker->diagnostics, expression->span,
                  "projection `%s` has unsupported type `%s`",
                  property->name, type_display_name(checker, value));
        return;
    }
    uint64_t part_id = 0U;
    if (!projection_field_part(
            expression->as.field.object->type,
            expression->as.field.field, &part_id)) {
        lang_diag(checker->diagnostics, expression->span,
                  "projection field `%s` has no generated part identity",
                  expression->as.field.field);
        return;
    }
    size_t length = 19U;
    char *binding = lang_arena_alloc(&checker->module->arena, length);
    (void)snprintf(
        binding, length, "%c:%016" PRIx64, kind, part_id);
    property->projection_binding = binding;
}

static Type *event_bound_method_type(
    Checker *checker, Expr *expression) {
    if (expression->kind != EXPR_FIELD) return NULL;
    Type *object = check_expr(checker, expression->as.field.object);
    if (object->kind != TYPE_CLASS || object->declaration == NULL)
        return NULL;
    Decl *selected = NULL;
    for (size_t member = 0U;
         member < object->declaration->as.structure.member_count; ++member) {
        Decl *candidate =
            object->declaration->as.structure.members[member];
        if (candidate->kind != DECL_FUNCTION ||
            candidate->as.function.is_static_member ||
            candidate->as.function.is_constructor ||
            candidate->as.function.is_drop)
            continue;
        const char *base = strrchr(candidate->as.function.name, ':');
        base = base == NULL ? candidate->as.function.name : base + 1U;
        if (strcmp(base, expression->as.field.field) != 0) continue;
        if (selected != NULL) {
            lang_diag(checker->diagnostics, expression->span,
                      "browser event method `%s` must not be overloaded",
                      expression->as.field.field);
            return NULL;
        }
        selected = candidate;
    }
    if (selected == NULL) return NULL;
    const Function *method = &selected->as.function;
    Type *delegate = lang_arena_alloc(
        &checker->module->arena, sizeof(*delegate));
    memset(delegate, 0, sizeof(*delegate));
    delegate->kind = TYPE_FUNCTION;
    delegate->name = "browser-event-method";
    delegate->element = method->checked_return_type;
    delegate->argument_count = method->param_count - 1U;
    if (delegate->argument_count != 0U) {
        delegate->arguments = lang_arena_alloc(
            &checker->module->arena,
            delegate->argument_count * sizeof(*delegate->arguments));
        delegate->parameter_modes = lang_arena_alloc(
            &checker->module->arena,
            delegate->argument_count * sizeof(*delegate->parameter_modes));
    }
    for (size_t parameter = 1U;
         parameter < method->param_count; ++parameter) {
        delegate->arguments[parameter - 1U] =
            method->params[parameter].checked_type;
        delegate->parameter_modes[parameter - 1U] =
            parameter_mode_from_param(&method->params[parameter]);
    }
    return delegate;
}

static void check_html_event_handler(
    Checker *checker, ElementProperty *property) {
    Type *previous_expected = checker->expected_type;
    Type *bound_type = event_bound_method_type(checker, property->value);
    if (bound_type != NULL) checker->expected_type = bound_type;
    Type *handler_type = check_expr(checker, property->value);
    checker->expected_type = previous_expected;
    Decl *handler = (Decl *)property->value->resolved_decl;
    if (handler_type->kind != TYPE_FUNCTION || handler == NULL ||
        handler->kind != DECL_FUNCTION) {
        lang_diag(checker->diagnostics, property->value->span,
                  "event property `%s` requires a statically named function",
                  property->name);
        return;
    }
    if (!handler->is_public && handler->as.function.owner_type == NULL)
        lang_diag(checker->diagnostics, property->value->span,
                  "event handler `%s` must be public",
                  handler->as.function.name);
    if (handler->as.function.owner_type != NULL &&
        handler->as.function.is_async)
        lang_diag(checker->diagnostics, property->value->span,
                  "browser class component handlers are synchronous in the initial prototype");
    if (handler->as.function.owner_type != NULL) {
        Decl *owner = find_type_declaration(
            checker, handler->as.function.owner_type,
            property->value->span);
        bool zero_argument_constructor = false;
        if (owner != NULL && owner->kind == DECL_CLASS)
            for (size_t member = 0U;
                 member < owner->as.structure.member_count; ++member) {
                Decl *candidate = owner->as.structure.members[member];
                if (candidate->kind != DECL_FUNCTION) continue;
                if (candidate->as.function.is_constructor &&
                    candidate->as.function.param_count == 0U)
                    zero_argument_constructor = true;
                const char *base = strrchr(
                    candidate->as.function.name, ':');
                base = base == NULL
                    ? candidate->as.function.name : base + 1U;
                if (strcmp(base, "Render") == 0)
                    candidate->as.function
                        .is_interactive_component_render = true;
            }
        if (!zero_argument_constructor)
            lang_diag(checker->diagnostics, property->value->span,
                      "interactive class component `%s` requires a zero-argument constructor",
                      handler->as.function.owner_type);
    }
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
    if (projection_batch_type(completion)) {
        if (handler_type->element->kind == TYPE_TASK)
            lang_diag(checker->diagnostics, property->value->span,
                      "projection handlers are synchronous in the experimental prototype");
        const Decl *container = completion->declaration;
        size_t state_count = projection_state_type(completion) ? 1U : 0U;
        for (size_t field = 0U;
             field < container->as.structure.field_count; ++field) {
            const FieldDecl *field_decl =
                &container->as.structure.fields[field];
            Type *field_type = resolve_declared_type_in_module(
                checker, field_decl->type_syntax, field_decl->type_name,
                field_decl->span, container->module_name);
            if (projection_transition_type(completion)) {
                if (projection_state_type(field_type)) {
                    ++state_count;
                    const Decl *state = field_type->declaration;
                    for (size_t nested = 0U;
                         nested < state->as.structure.field_count; ++nested) {
                        const FieldDecl *nested_field =
                            &state->as.structure.fields[nested];
                        Type *nested_type = resolve_declared_type_in_module(
                            checker, nested_field->type_syntax,
                            nested_field->type_name, nested_field->span,
                            state->module_name);
                        if (!(nested_type->kind == TYPE_BOOL ||
                              is_signed_integer(nested_type) ||
                              is_unsigned_integer(nested_type) ||
                              nested_type->kind == TYPE_STRING))
                            lang_diag(checker->diagnostics,
                                      nested_field->span,
                                      "projection-state field `%s` must be Boolean, integer, or `string`",
                                      nested_field->name);
                    }
                } else if (!web_handler_standard_html_type(
                               field_type, "KeyedRemove")) {
                    lang_diag(checker->diagnostics, field_decl->span,
                              "projection-transition field `%s` must be a `*ProjectionState` or `KeyedRemove`",
                              field_decl->name);
                }
            } else if (!(field_type->kind == TYPE_BOOL ||
                         is_signed_integer(field_type) ||
                         is_unsigned_integer(field_type) ||
                         field_type->kind == TYPE_STRING)) {
                lang_diag(checker->diagnostics, field_decl->span,
                          "projection-state field `%s` must be Boolean, integer, or `string`",
                          field_decl->name);
            }
        }
        if (projection_transition_type(completion) && state_count != 1U)
            lang_diag(checker->diagnostics, completion->declaration->span,
                      "projection transition must contain exactly one `*ProjectionState` field");
    }
    size_t first_parameter = handler->as.function.owner_type != NULL &&
        !handler->as.function.is_static_member ? 1U : 0U;
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
    const char *owner = first_parameter != 0U
        ? handler->as.function.owner_type : NULL;
    size_t length = strlen(event_name) + strlen(handler_name) + 4U;
    if (owner != NULL)
        length += strlen(owner) * 2U + 4U;
    for (size_t parameter = first_parameter;
         parameter < handler->as.function.param_count; ++parameter) {
        uint64_t part_id = 0U;
        const char *name = handler->as.function.params[parameter].name;
        length += completion_projection_part(
                completion, name, &part_id)
            ? 20U : strlen(name) + 3U;
    }
    char *binding = lang_arena_alloc(
        &checker->module->arena, length + 1U);
    char result_code = completion->kind == TYPE_STRING
        ? 'o' : web_handler_type_code(completion);
    if (handler_type->element->kind == TYPE_TASK &&
        result_code >= 'a' && result_code <= 'z')
        result_code = (char)(result_code - ('a' - 'A'));
    size_t offset;
    if (owner != NULL)
        offset = (size_t)snprintf(
            binding, length + 1U, "%s|%s_%s|%c|x:%s",
            event_name, owner, handler_name, result_code, owner);
    else
        offset = (size_t)snprintf(
            binding, length + 1U, "%s|%s|%c",
            event_name, handler_name, result_code);
    for (size_t parameter = first_parameter;
         parameter < handler->as.function.param_count; ++parameter) {
        uint64_t part_id = 0U;
        const char *name = handler->as.function.params[parameter].name;
        char code = web_handler_type_code(
            handler_type->arguments[parameter - first_parameter]);
        if (completion_projection_part(completion, name, &part_id))
            offset += (size_t)snprintf(
                binding + offset, length + 1U - offset,
                "|%c:@%016" PRIx64, code, part_id);
        else
            offset += (size_t)snprintf(
                binding + offset, length + 1U - offset,
                "|%c:%s", code, name);
    }
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
    const Decl *component_decl = component != NULL
        ? function_declaration(checker, component) : NULL;
    Decl *descriptor =
        find_element_declaration(checker, expr->as.element.name, expr->span);
    Decl *class_component = NULL;
    Decl *class_render = NULL;
    Function *class_constructor = NULL;
    bool multiple_class_constructors = false;
    if (descriptor != NULL) {
        component = NULL;
        component_decl = NULL;
    } else if (component == NULL) {
        Decl *candidate = find_type_declaration(
            checker, expr->as.element.name, expr->span);
        if (candidate != NULL && candidate->kind == DECL_CLASS &&
            !candidate->as.structure.is_interface) {
            class_component = candidate;
            for (size_t member = 0U;
                 member < candidate->as.structure.member_count; ++member) {
                Decl *method = candidate->as.structure.members[member];
                if (method->kind != DECL_FUNCTION) continue;
                if (method->as.function.is_constructor) {
                    if (component_decl == NULL) {
                        component_decl = method;
                        class_constructor = &method->as.function;
                    }
                    else
                        multiple_class_constructors = true;
                    continue;
                }
                const char *base = strrchr(method->as.function.name, ':');
                base = base == NULL ? method->as.function.name : base + 1U;
                if (strcmp(base, "Render") == 0 &&
                    !method->as.function.is_static_member &&
                    method->as.function.param_count == 1U &&
                    strcmp(method->as.function.params[0].name, "this") == 0)
                    class_render = method;
            }
            if (multiple_class_constructors) {
                lang_diag(checker->diagnostics, expr->span,
                          "class component `%s` must currently have exactly one constructor",
                          expr->as.element.name);
                component_decl = NULL;
            } else if (component_decl == NULL) {
                lang_diag(checker->diagnostics, expr->span,
                          "class component `%s` requires an explicit constructor",
                          expr->as.element.name);
            }
            if (class_render == NULL)
                lang_diag(checker->diagnostics, expr->span,
                          "class component `%s` requires `public Html Render()`",
                          expr->as.element.name);
            else if (!class_render->is_public)
                lang_diag(checker->diagnostics, class_render->span,
                          "class component `Render` must be public");
            if (component_decl != NULL && class_render != NULL) {
                component = class_constructor;
                expr->as.element.class_constructor = component_decl;
                expr->as.element.class_render = class_render;
            }
        }
    }
    if (component == NULL && descriptor == NULL) {
        if (class_component == NULL)
            lang_diag(checker->diagnostics, expr->span,
                      "unknown element `%s`", expr->as.element.name);
        return &type_error;
    }
    if (component != NULL) {
        expr->resolved_decl = component_decl;
        const char *component_module = class_component != NULL
            ? class_component->module_name
            : function_module_name(checker, component);
        const Function *result_function = class_render != NULL
            ? &class_render->as.function : component;
        Type *component_result = resolve_declared_type_in_module(
            checker, result_function->return_type_syntax,
            result_function->return_type, result_function->span,
            component_module);
        if (component_result->kind != TYPE_HTML)
            lang_diag(checker->diagnostics, expr->span,
                      "component `%s` must return `Html`, found `%s`",
                      expr->as.element.name, component_result->name);
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
            if (projection_property(property->name)) {
                check_projection_property(checker, property);
                continue;
            }
            if (element_property_names_equal(property->name, "key")) {
                Type *key_type = check_expr(checker, property->value);
                if (!(key_type->kind == TYPE_STRING ||
                      is_signed_integer(key_type) ||
                      is_unsigned_integer(key_type)))
                    lang_diag(checker->diagnostics, property->value->span,
                              "keyed HTML identity must be a string or integer, found `%s`",
                              type_display_name(checker, key_type));
                property->keyed_identity = true;
                continue;
            }
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
