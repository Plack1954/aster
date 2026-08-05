#include "checker_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static Expr *clone_generic_expr(Module *module, const Expr *source);

static Stmt *clone_generic_stmt(Module *module, const Stmt *source) {
    if (source == NULL) return NULL;
    Stmt *result = lang_arena_alloc(&module->arena, sizeof(*result));
    *result = *source;
    result->exit_cleanup = (CleanupPlan){NULL, 0U};
    switch (source->kind) {
        case STMT_LET:
            result->as.let.binding_id = 0U;
            result->as.let.value =
                clone_generic_expr(module, source->as.let.value);
            break;
        case STMT_DESTRUCTURE:
            result->as.destructure.value = clone_generic_expr(
                module, source->as.destructure.value);
            result->as.destructure.checked_types = NULL;
            result->as.destructure.binding_ids = NULL;
            break;
        case STMT_EXPR:
            result->as.expression =
                clone_generic_expr(module, source->as.expression);
            break;
        case STMT_RETURN:
            result->as.return_value =
                clone_generic_expr(module, source->as.return_value);
            break;
        case STMT_IF:
            result->as.if_.condition =
                clone_generic_expr(module, source->as.if_.condition);
            result->as.if_.then_branch =
                clone_generic_stmt(module, source->as.if_.then_branch);
            result->as.if_.else_branch =
                clone_generic_stmt(module, source->as.if_.else_branch);
            break;
        case STMT_WHILE:
            result->as.while_.condition =
                clone_generic_expr(module, source->as.while_.condition);
            result->as.while_.body =
                clone_generic_stmt(module, source->as.while_.body);
            break;
        case STMT_FOR:
            result->as.for_.binding_id = 0U;
            result->as.for_.iterable =
                clone_generic_expr(module, source->as.for_.iterable);
            result->as.for_.range_end =
                clone_generic_expr(module, source->as.for_.range_end);
            result->as.for_.body =
                clone_generic_stmt(module, source->as.for_.body);
            break;
        case STMT_C_FOR:
            result->as.c_for.initializer =
                clone_generic_stmt(module, source->as.c_for.initializer);
            result->as.c_for.condition =
                clone_generic_expr(module, source->as.c_for.condition);
            result->as.c_for.increment =
                clone_generic_expr(module, source->as.c_for.increment);
            result->as.c_for.body =
                clone_generic_stmt(module, source->as.c_for.body);
            break;
        case STMT_MATCH:
            result->as.match_.value =
                clone_generic_expr(module, source->as.match_.value);
            if (source->as.match_.arm_count != 0U) {
                result->as.match_.arms = lang_arena_alloc(
                    &module->arena,
                    source->as.match_.arm_count *
                        sizeof(*result->as.match_.arms));
                for (size_t i = 0U;
                     i < source->as.match_.arm_count; ++i) {
                    result->as.match_.arms[i] =
                        source->as.match_.arms[i];
                    result->as.match_.arms[i].binding_id = 0U;
                    result->as.match_.arms[i].binding_type = NULL;
                    result->as.match_.arms[i].body = clone_generic_stmt(
                        module, source->as.match_.arms[i].body);
                }
            }
            break;
        case STMT_THROW:
            result->as.throw_value =
                clone_generic_expr(module, source->as.throw_value);
            break;
        case STMT_TRY:
            result->as.try_.body =
                clone_generic_stmt(module, source->as.try_.body);
            result->as.try_.catch_type = NULL;
            result->as.try_.catch_binding_id = 0U;
            result->as.try_.catch_body =
                clone_generic_stmt(module, source->as.try_.catch_body);
            result->as.try_.finally_body =
                clone_generic_stmt(module, source->as.try_.finally_body);
            break;
        case STMT_BLOCK:
            if (source->as.block.count != 0U) {
                result->as.block.items = lang_arena_alloc(
                    &module->arena,
                    source->as.block.count *
                        sizeof(*result->as.block.items));
                for (size_t i = 0U; i < source->as.block.count; ++i)
                    result->as.block.items[i] = clone_generic_stmt(
                        module, source->as.block.items[i]);
            }
            break;
        case STMT_UNSAFE:
            result->as.unsafe_body =
                clone_generic_stmt(module, source->as.unsafe_body);
            break;
        case STMT_BREAK:
        case STMT_CONTINUE:
            break;
    }
    return result;
}

static Expr *clone_generic_expr(Module *module, const Expr *source) {
    if (source == NULL) return NULL;
    Expr *result = lang_arena_alloc(&module->arena, sizeof(*result));
    *result = *source;
    result->type = NULL;
    result->resolved_decl = NULL;
    result->resolved_local_id = 0U;
    result->error_cleanup = (CleanupPlan){NULL, 0U};
    switch (source->kind) {
        case EXPR_BINARY:
            result->as.binary.left =
                clone_generic_expr(module, source->as.binary.left);
            result->as.binary.right =
                clone_generic_expr(module, source->as.binary.right);
            break;
        case EXPR_UNARY:
            result->as.unary.operand =
                clone_generic_expr(module, source->as.unary.operand);
            break;
        case EXPR_CALL:
            result->as.call.callee =
                clone_generic_expr(module, source->as.call.callee);
            if (source->as.call.arguments.count != 0U) {
                result->as.call.arguments.items = lang_arena_alloc(
                    &module->arena,
                    source->as.call.arguments.count *
                        sizeof(*result->as.call.arguments.items));
                for (size_t i = 0U;
                     i < source->as.call.arguments.count; ++i)
                    result->as.call.arguments.items[i] =
                        clone_generic_expr(
                            module, source->as.call.arguments.items[i]);
            }
            break;
        case EXPR_ASSIGN:
            result->as.assign.target =
                clone_generic_expr(module, source->as.assign.target);
            result->as.assign.value =
                clone_generic_expr(module, source->as.assign.value);
            break;
        case EXPR_CLONE:
            result->as.clone.value =
                clone_generic_expr(module, source->as.clone.value);
            break;
        case EXPR_TRY:
            result->as.try_.value =
                clone_generic_expr(module, source->as.try_.value);
            break;
        case EXPR_AWAIT:
            result->as.try_.value =
                clone_generic_expr(module, source->as.try_.value);
            break;
        case EXPR_CAST:
            result->as.cast.value =
                clone_generic_expr(module, source->as.cast.value);
            break;
        case EXPR_ARRAY:
            if (source->as.array.count != 0U) {
                result->as.array.items = lang_arena_alloc(
                    &module->arena,
                    source->as.array.count *
                        sizeof(*result->as.array.items));
                for (size_t i = 0U; i < source->as.array.count; ++i)
                    result->as.array.items[i] = clone_generic_expr(
                        module, source->as.array.items[i]);
            }
            break;
        case EXPR_INTERPOLATION:
            if (source->as.interpolation.part_count != 0U) {
                result->as.interpolation.parts = lang_arena_alloc(
                    &module->arena,
                    source->as.interpolation.part_count *
                        sizeof(*result->as.interpolation.parts));
                for (size_t i = 0U;
                     i < source->as.interpolation.part_count; ++i) {
                    result->as.interpolation.parts[i] =
                        source->as.interpolation.parts[i];
                    result->as.interpolation.parts[i].expression =
                        clone_generic_expr(
                            module,
                            source->as.interpolation.parts[i].expression);
                }
            }
            break;
        case EXPR_INDEX:
            result->as.index.object =
                clone_generic_expr(module, source->as.index.object);
            result->as.index.index =
                clone_generic_expr(module, source->as.index.index);
            break;
        case EXPR_FIELD:
            result->as.field.object =
                clone_generic_expr(module, source->as.field.object);
            break;
        case EXPR_STRUCT:
            if (source->as.structure.field_count != 0U) {
                result->as.structure.fields = lang_arena_alloc(
                    &module->arena,
                    source->as.structure.field_count *
                        sizeof(*result->as.structure.fields));
                for (size_t i = 0U;
                     i < source->as.structure.field_count; ++i) {
                    result->as.structure.fields[i] =
                        source->as.structure.fields[i];
                    result->as.structure.fields[i].value =
                        clone_generic_expr(
                            module,
                            source->as.structure.fields[i].value);
                }
            }
            break;
        case EXPR_ELEMENT:
            if (source->as.element.property_count != 0U) {
                result->as.element.properties = lang_arena_alloc(
                    &module->arena,
                    source->as.element.property_count *
                        sizeof(*result->as.element.properties));
                for (size_t i = 0U;
                     i < source->as.element.property_count; ++i) {
                    result->as.element.properties[i] =
                        source->as.element.properties[i];
                    result->as.element.properties[i].value =
                        clone_generic_expr(
                            module,
                            source->as.element.properties[i].value);
                }
            }
            if (source->as.element.body_count != 0U) {
                result->as.element.body = lang_arena_alloc(
                    &module->arena,
                    source->as.element.body_count *
                        sizeof(*result->as.element.body));
                for (size_t i = 0U;
                     i < source->as.element.body_count; ++i) {
                    result->as.element.body[i] =
                        source->as.element.body[i];
                    if (source->as.element.body[i].is_statement)
                        result->as.element.body[i].as.statement =
                            clone_generic_stmt(
                                module,
                                source->as.element.body[i].as.statement);
                    else
                        result->as.element.body[i].as.expression =
                            clone_generic_expr(
                                module,
                                source->as.element.body[i].as.expression);
                }
            }
            break;
        case EXPR_IF:
            result->as.if_.condition =
                clone_generic_expr(module, source->as.if_.condition);
            result->as.if_.then_branch =
                clone_generic_stmt(module, source->as.if_.then_branch);
            result->as.if_.else_branch =
                clone_generic_stmt(module, source->as.if_.else_branch);
            break;
        case EXPR_MATCH:
            result->as.match_.value =
                clone_generic_expr(module, source->as.match_.value);
            if (source->as.match_.arm_count != 0U) {
                result->as.match_.arms = lang_arena_alloc(
                    &module->arena,
                    source->as.match_.arm_count *
                        sizeof(*result->as.match_.arms));
                for (size_t i = 0U;
                     i < source->as.match_.arm_count; ++i) {
                    result->as.match_.arms[i] =
                        source->as.match_.arms[i];
                    result->as.match_.arms[i].binding_id = 0U;
                    result->as.match_.arms[i].binding_type = NULL;
                    result->as.match_.arms[i].body = clone_generic_stmt(
                        module, source->as.match_.arms[i].body);
                }
            }
            break;
        case EXPR_INT:
        case EXPR_FLOAT:
        case EXPR_STRING:
        case EXPR_BOOL:
        case EXPR_NULL:
        case EXPR_NAME:
            break;
    }
    return result;
}

static Type *resolve_type_with_function_arguments(
    Checker *checker, const Decl *template_decl, Type **arguments,
    const TypeSyntax *syntax, const char *name, LangSpan span) {
    const char *previous_module = checker->current_module;
    const Decl *previous_decl = checker->substitution_decl;
    Type **previous_arguments = checker->substitution_arguments;
    size_t previous_count = checker->substitution_argument_count;
    checker->current_module = template_decl->module_name;
    checker->substitution_decl = template_decl;
    checker->substitution_arguments = arguments;
    checker->substitution_argument_count = template_decl->type_param_count;
    Type *result = resolve_declared_type(checker, syntax, name, span);
    checker->current_module = previous_module;
    checker->substitution_decl = previous_decl;
    checker->substitution_arguments = previous_arguments;
    checker->substitution_argument_count = previous_count;
    return result;
}

static size_t generic_parameter_index(const Decl *template_decl,
                                      const char *name) {
    for (size_t i = 0U; i < template_decl->type_param_count; ++i)
        if (strcmp(template_decl->type_params[i], name) == 0)
            return i;
    return SIZE_MAX;
}

static bool infer_generic_pattern(
    Checker *checker, const Decl *template_decl, const char *pattern,
    Type *actual, Type **arguments, LangSpan span) {
    size_t parameter = generic_parameter_index(template_decl, pattern);
    if (parameter != SIZE_MAX) {
        if (arguments[parameter] == NULL) {
            arguments[parameter] = actual;
            return true;
        }
        if (!same_type(arguments[parameter], actual)) {
            lang_diag(
                checker->diagnostics, span,
                "conflicting inferred types for `%s`: `%s` and `%s`",
                pattern, arguments[parameter]->name, actual->name);
            return false;
        }
        return true;
    }

    char *base = NULL;
    char **patterns = NULL;
    size_t pattern_count = 0U;
    if (!split_generic_application(
            checker, pattern, &base, &patterns, &pattern_count))
        return true;

    Type **actual_arguments = NULL;
    size_t actual_count = 0U;
    if (strcmp(base, "Option") == 0 && actual->kind == TYPE_OPTION) {
        actual_arguments = &actual->element;
        actual_count = 1U;
    } else if (strcmp(base, "List") == 0 && actual->kind == TYPE_VEC) {
        actual_arguments = &actual->element;
        actual_count = 1U;
    } else if ((strcmp(base, "Span") == 0 && actual->kind == TYPE_SLICE) ||
               (strcmp(base, "ReadOnlySpan") == 0 &&
                actual->kind == TYPE_READONLY_SPAN)) {
        actual_arguments = &actual->element;
        actual_count = 1U;
    } else if (strcmp(base, "Result") == 0 &&
               actual->kind == TYPE_RESULT) {
        Type *result_arguments[2] = {
            actual->element, actual->error_type
        };
        if (pattern_count != 2U) return false;
        bool ok = true;
        for (size_t i = 0U; i < 2U; ++i)
            if (!infer_generic_pattern(
                    checker, template_decl, patterns[i],
                    result_arguments[i], arguments, span))
                ok = false;
        return ok;
    } else if (actual->kind == TYPE_NAMED &&
               actual->declaration != NULL) {
        const char *previous_module = checker->current_module;
        checker->current_module = template_decl->module_name;
        Decl *pattern_decl =
            find_type_declaration(checker, base, span);
        checker->current_module = previous_module;
        if (pattern_decl != actual->declaration)
            return true;
        actual_arguments = actual->arguments;
        actual_count = actual->argument_count;
    } else {
        return true;
    }
    if (pattern_count != actual_count) return false;
    bool ok = true;
    for (size_t i = 0U; i < pattern_count; ++i)
        if (!infer_generic_pattern(
                checker, template_decl, patterns[i],
                actual_arguments[i], arguments, span))
            ok = false;
    return ok;
}

static bool infer_generic_syntax(
    Checker *checker, const Decl *template_decl,
    const TypeSyntax *pattern, const char *fallback_pattern,
    Type *actual, Type **arguments, LangSpan span) {
    if (pattern == NULL)
        return infer_generic_pattern(
            checker, template_decl, fallback_pattern,
            actual, arguments, span);
    if (pattern->kind == TYPE_SYNTAX_NAMED) {
        size_t parameter = generic_parameter_index(
            template_decl, pattern->as.name);
        if (parameter == SIZE_MAX) return true;
        if (arguments[parameter] == NULL) {
            arguments[parameter] = actual;
            return true;
        }
        if (!same_type(arguments[parameter], actual)) {
            lang_diag(checker->diagnostics, span,
                      "conflicting inferred types for `%s`: `%s` and `%s`",
                      pattern->as.name, arguments[parameter]->name,
                      actual->name);
            return false;
        }
        return true;
    }
    if (pattern->kind == TYPE_SYNTAX_POINTER &&
        actual->kind == TYPE_RAW_POINTER)
        return infer_generic_syntax(
            checker, template_decl, pattern->as.pointer.element, NULL,
            actual->element, arguments, span);
    if (pattern->kind == TYPE_SYNTAX_ARRAY &&
        actual->kind == TYPE_ARRAY &&
        pattern->as.array.count == actual->array_length)
        return infer_generic_syntax(
            checker, template_decl, pattern->as.array.element, NULL,
            actual->element, arguments, span);
    if (pattern->kind == TYPE_SYNTAX_FUNCTION &&
        actual->kind == TYPE_FUNCTION) {
        if (pattern->as.function.parameter_count != actual->argument_count)
            return false;
        bool ok = infer_generic_syntax(
            checker, template_decl, pattern->as.function.return_type, NULL,
            actual->element, arguments, span);
        for (size_t i = 0U; i < actual->argument_count; ++i) {
            if (pattern->as.function.parameter_modes[i] !=
                actual->parameter_modes[i]) {
                ok = false;
                continue;
            }
            if (!infer_generic_syntax(
                    checker, template_decl,
                    pattern->as.function.parameters[i], NULL,
                    actual->arguments[i], arguments, span))
                ok = false;
        }
        return ok;
    }
    if (pattern->kind != TYPE_SYNTAX_GENERIC ||
        pattern->as.generic.base->kind != TYPE_SYNTAX_NAMED)
        return true;
    const char *base = pattern->as.generic.base->as.name;
    Type *actual_items[2] = {NULL, NULL};
    Type **actual_arguments = actual_items;
    size_t actual_count = 0U;
    if ((strcmp(base, "Option") == 0 && actual->kind == TYPE_OPTION) ||
        (strcmp(base, "List") == 0 && actual->kind == TYPE_VEC) ||
        (strcmp(base, "Span") == 0 && actual->kind == TYPE_SLICE) ||
        (strcmp(base, "ReadOnlySpan") == 0 &&
         actual->kind == TYPE_READONLY_SPAN) ||
        (strcmp(base, "HashSet") == 0 && actual->kind == TYPE_HASH_SET) ||
        (strcmp(base, "Queue") == 0 && actual->kind == TYPE_QUEUE) ||
        (strcmp(base, "Stack") == 0 && actual->kind == TYPE_STACK) ||
        (strcmp(base, "Task") == 0 && actual->kind == TYPE_TASK)) {
        actual_items[0] = actual->element;
        actual_count = 1U;
    } else if ((strcmp(base, "Result") == 0 && actual->kind == TYPE_RESULT) ||
               (strcmp(base, "Dictionary") == 0 &&
                actual->kind == TYPE_DICTIONARY)) {
        actual_items[0] = actual->element;
        actual_items[1] = actual->error_type;
        actual_count = 2U;
    } else if (actual->kind == TYPE_NAMED &&
               actual->declaration != NULL) {
        const char *previous_module = checker->current_module;
        checker->current_module = template_decl->module_name;
        Decl *pattern_decl = find_type_declaration(checker, base, span);
        checker->current_module = previous_module;
        if (pattern_decl != actual->declaration) return true;
        actual_arguments = actual->arguments;
        actual_count = actual->argument_count;
    } else {
        return true;
    }
    if (pattern->as.generic.argument_count != actual_count) return false;
    bool ok = true;
    for (size_t i = 0U; i < actual_count; ++i)
        if (!infer_generic_syntax(
                checker, template_decl,
                pattern->as.generic.arguments[i], NULL,
                actual_arguments[i], arguments, span))
            ok = false;
    return ok;
}

static Decl *find_function_instantiation(
    const Module *module, const Decl *template_decl,
    Type **arguments, size_t argument_count) {
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *candidate = module->decls[i];
        if (candidate->kind != DECL_FUNCTION ||
            candidate->generic_origin != template_decl ||
            candidate->generic_argument_count != argument_count)
            continue;
        bool equal = true;
        for (size_t argument = 0U; argument < argument_count; ++argument)
            if (!same_type(
                    candidate->generic_arguments[argument],
                    arguments[argument])) {
                equal = false;
                break;
            }
        if (equal) return candidate;
    }
    return NULL;
}

static Decl *instantiate_generic_function(
    Checker *checker, const Decl *template_decl,
    Type **arguments, size_t argument_count, LangSpan span) {
    Decl *existing = find_function_instantiation(
        checker->module, template_decl, arguments, argument_count);
    if (existing != NULL) return existing;
    if (checker->module->count >= 4096U) {
        lang_diag(checker->diagnostics, span,
                  "generic function instantiation limit exceeded");
        return NULL;
    }
    Decl *instance =
        lang_arena_alloc(&checker->module->arena, sizeof(*instance));
    *instance = *template_decl;
    instance->type_params = NULL;
    instance->type_param_count = 0U;
    instance->generic_origin = template_decl;
    instance->generic_argument_count = argument_count;
    instance->generic_arguments = lang_arena_alloc(
        &checker->module->arena,
        argument_count * sizeof(*instance->generic_arguments));
    memcpy(instance->generic_arguments, arguments,
           argument_count * sizeof(*instance->generic_arguments));

    size_t name_length =
        strlen(template_decl->as.function.name) + 3U;
    for (size_t i = 0U; i < argument_count; ++i)
        name_length += strlen(arguments[i]->name) +
                       (i == 0U ? 0U : 1U);
    char *name =
        lang_arena_alloc(&checker->module->arena, name_length);
    size_t offset = (size_t)snprintf(
        name, name_length, "%s<",
        template_decl->as.function.name);
    for (size_t i = 0U; i < argument_count; ++i) {
        if (i != 0U) name[offset++] = ',';
        size_t length = strlen(arguments[i]->name);
        memcpy(name + offset, arguments[i]->name, length);
        offset += length;
    }
    name[offset++] = '>';
    name[offset] = '\0';
    instance->as.function.name = name;
    instance->as.function.checked_return_type = NULL;
    instance->as.function.local_count = 0U;
    if (template_decl->as.function.param_count != 0U) {
        instance->as.function.params = lang_arena_alloc(
            &checker->module->arena,
            template_decl->as.function.param_count *
                sizeof(*instance->as.function.params));
        memcpy(instance->as.function.params,
               template_decl->as.function.params,
               template_decl->as.function.param_count *
                   sizeof(*instance->as.function.params));
        for (size_t i = 0U;
             i < instance->as.function.param_count; ++i) {
            instance->as.function.params[i].checked_type = NULL;
            instance->as.function.params[i].binding_id = 0U;
        }
    }
    instance->as.function.body = clone_generic_stmt(
        checker->module, template_decl->as.function.body);

    Decl **next = lang_arena_alloc(
        &checker->module->arena,
        (checker->module->count + 1U) * sizeof(*next));
    memcpy(next, checker->module->decls,
           checker->module->count * sizeof(*next));
    next[checker->module->count] = instance;
    checker->module->decls = next;
    ++checker->module->count;
    return instance;
}

Type *check_generic_call(
    Checker *checker, Expr *expr, const Decl *template_decl) {
    Function *function = (Function *)&template_decl->as.function;
    if (function->param_count != expr->as.call.arguments.count)
        lang_diag(
            checker->diagnostics, expr->span,
            "generic function `%s` expects %zu arguments, found %zu",
            function->name, function->param_count,
            expr->as.call.arguments.count);

    Type **arguments = lang_arena_alloc(
        &checker->module->arena,
        template_decl->type_param_count * sizeof(*arguments));
    if (checker->expected_type != NULL)
        (void)infer_generic_syntax(
            checker, template_decl, function->return_type_syntax,
            function->return_type,
            checker->expected_type, arguments, expr->span);
    size_t count =
        function->param_count < expr->as.call.arguments.count
        ? function->param_count : expr->as.call.arguments.count;
    bool *argument_places = lang_arena_alloc(
        &checker->module->arena,
        expr->as.call.arguments.count * sizeof(*argument_places));
    for (size_t i = 0U; i < expr->as.call.arguments.count; ++i) {
        Expr *argument = expr->as.call.arguments.items[i];
        ParameterMode call_mode = expr->as.call.argument_modes != NULL
            ? expr->as.call.argument_modes[i]
            : PARAMETER_MODE_VALUE;
        bool call_ref =
            call_mode == PARAMETER_MODE_MUTABLE_REFERENCE;
        bool call_out = call_mode == PARAMETER_MODE_OUT;
        if (i < function->param_count) {
            bool parameter_ref = function->params[i].by_ref &&
                !function->params[i].by_out;
            if (!(i == 0U && expr->as.call.implicit_receiver) &&
                (call_ref != parameter_ref ||
                 call_out != function->params[i].by_out))
                lang_diag(
                    checker->diagnostics, argument->span,
                    "argument %zu to `%s` must use `%s`",
                    i + 1U, function->name,
                    function->params[i].by_out ? "out" :
                    parameter_ref ? "ref" : "ordinary value syntax");
        }
        argument_places[i] =
            (argument->kind == EXPR_NAME &&
             find_local(checker, argument->as.name) != NULL) ||
            (argument->kind == EXPR_FIELD &&
             argument->as.field.object->kind == EXPR_NAME);
        if (!argument_places[i] && i < function->param_count &&
            function->params[i].by_ref) {
            lang_diag(
                checker->diagnostics, argument->span,
                function->params[i].by_out
                    ? "`out` argument must be an available place"
                    : "`ref` argument must be an available place");
        }
        if (argument_places[i])
            (void)check_place(checker, argument);
        else
            (void)check_expr(checker, argument);
    }
    for (size_t i = 0U; i < count; ++i)
        (void)infer_generic_syntax(
            checker, template_decl, function->params[i].type_syntax,
            function->params[i].type_name,
            expr->as.call.arguments.items[i]->type,
            arguments, expr->as.call.arguments.items[i]->span);
    bool complete = true;
    for (size_t i = 0U; i < template_decl->type_param_count; ++i)
        if (arguments[i] == NULL) {
            lang_diag(
                checker->diagnostics, expr->span,
                "cannot infer generic type parameter `%s` for `%s`",
                template_decl->type_params[i], function->name);
            complete = false;
        }
    if (!complete) return &type_error;

    for (size_t i = 0U; i < count; ++i) {
        if (argument_places[i] && !function->params[i].by_ref)
            (void)check_expr(
                checker, expr->as.call.arguments.items[i]);
        if (function->params[i].by_ref && argument_places[i]) {
            Expr *root = expr->as.call.arguments.items[i];
            while (root->kind == EXPR_FIELD)
                root = root->as.field.object;
            Local *local = root->kind == EXPR_NAME
                ? find_local(checker, root->as.name) : NULL;
            if (local == NULL || !local->mutable_)
                lang_diag(
                    checker->diagnostics,
                    expr->as.call.arguments.items[i]->span,
                    function->params[i].by_out
                        ? "`out` argument requires a mutable local"
                        : "`ref` argument requires a mutable local");
        }
    }

    Decl *instance = instantiate_generic_function(
        checker, template_decl, arguments,
        template_decl->type_param_count, expr->span);
    if (instance == NULL) return &type_error;
    expr->resolved_decl = instance;
    for (size_t i = 0U; i < count; ++i) {
        Type *expected = resolve_type_with_function_arguments(
            checker, template_decl, arguments,
            function->params[i].type_syntax,
            function->params[i].type_name,
            function->params[i].span);
        (void)coerce_literal(
            checker, expr->as.call.arguments.items[i], expected);
        Type *actual = expr->as.call.arguments.items[i]->type;
        if (!type_assignable(expected, actual))
            lang_diag(
                checker->diagnostics,
                expr->as.call.arguments.items[i]->span,
                "argument %zu to `%s` expects `%s`, found `%s`",
                i + 1U, function->name,
                type_display_name(checker, expected),
                type_display_name(checker, actual));
    }
    return resolve_type_with_function_arguments(
        checker, template_decl, arguments,
        function->return_type_syntax,
        function->return_type, function->span);
}
