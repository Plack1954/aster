#include "ir_internal.h"

#include <stdlib.h>
#include <string.h>

static bool push_element_context(IrBuilder *builder, uint32_t local,
                                 LangSpan span) {
    if (builder->element_count >=
        sizeof(builder->elements) / sizeof(builder->elements[0])) {
        lang_diag(builder->diagnostics, span,
                  "IR element nesting limit exceeded");
        builder->failed = true;
        return false;
    }
    builder->elements[builder->element_count++] =
        (IrElementContext){local, builder->loop_count};
    return true;
}

void ir_append_element_child(IrBuilder *builder, IrValueId child,
                                 const Type *type, LangSpan span) {
    bool formatted = type != NULL &&
        (type->kind == TYPE_BOOL || type->kind == TYPE_CHAR ||
         (type->kind >= TYPE_I8 && type->kind <= TYPE_USIZE) ||
         type->kind == TYPE_F32 || type->kind == TYPE_F64);
    if (formatted) {
        if (builder->element_count == 0U) {
            lang_diag(builder->diagnostics, span,
                      "internal IR error: element child without a builder");
            builder->failed = true;
            return;
        }
        IrInstruction *append = ir_append_instruction(
            builder, IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED,
            IR_INVALID_ID, &child, 1U, span);
        if (append != NULL)
            append->index =
                builder->elements[builder->element_count - 1U].local;
        return;
    }
    if (!ir_type_produces_element_child(type)) {
        IrInstruction *discard = ir_append_instruction(
            builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
            &child, 1U, span);
        (void)discard;
        return;
    }
    if (builder->element_count == 0U) {
        lang_diag(builder->diagnostics, span,
                  "internal IR error: element child without a builder");
        builder->failed = true;
        return;
    }
    IrInstruction *append = ir_append_instruction(
        builder, IR_OP_LOCAL_ELEMENT_APPEND, IR_INVALID_ID,
        &child, 1U, span);
    if (append != NULL)
        append->index =
            builder->elements[builder->element_count - 1U].local;
}

static uint32_t begin_element_builder(IrBuilder *builder,
                                      IrTypeId result_type,
                                      const char *name,
                                      uint32_t parent_local,
                                      LangSpan span) {
    IrTypeId builder_type = ir_intern_element_builder_type(
        builder->module, result_type);
    IrInstruction *begin = ir_append_instruction(
        builder, IR_OP_ELEMENT_BEGIN, builder_type,
        NULL, 0U, span);
    if (begin == NULL) return UINT32_MAX;
    begin->symbol = name;
    begin->symbol_length = strlen(name);
    begin->index = parent_local;
    uint32_t local = ir_add_synthetic_local(
        builder, "<element-builder>", builder_type);
    IrValueId value = begin->result;
    IrInstruction *store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &value, 1U, span);
    if (store != NULL) store->index = local;
    return local;
}

static IrValueId finish_element_builder(IrBuilder *builder,
                                        uint32_t local,
                                        IrTypeId result_type,
                                        LangSpan span) {
    IrInstruction *finish = ir_append_instruction(
        builder, IR_OP_LOCAL_ELEMENT_FINISH, result_type,
        NULL, 0U, span);
    if (finish != NULL) finish->index = local;
    return finish != NULL ? finish->result : IR_INVALID_ID;
}

static IrValueId lower_borrowed_interpolation_string(
    IrBuilder *builder, const Expr *place) {
    IrTypeId string_type = ir_intern_type(
        builder->module, &ir_string_type);
    IrInstruction *load = NULL;
    if (place->kind == EXPR_NAME) {
        load = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, string_type,
            NULL, 0U, place->span);
        if (load != NULL)
            load->index = ir_find_local(
                builder, place->resolved_local_id,
                place->span);
    } else if (
        place->kind == EXPR_FIELD &&
        place->as.field.object->kind == EXPR_NAME) {
        const Expr *object = place->as.field.object;
        load = ir_append_instruction(
            builder, IR_OP_LOCAL_FIELD_BORROW,
            string_type, NULL, 0U, place->span);
        if (load != NULL) {
            load->index = ir_find_local(
                builder, object->resolved_local_id,
                object->span);
            load->auxiliary = ir_field_index(
                object->type, place->as.field.field);
            load->symbol = place->as.field.field;
            load->symbol_length = strlen(load->symbol);
        }
    }
    if (load == NULL)
        return IR_INVALID_ID;
    IrValueId owned = load->result;
    return ir_emit_synthetic_native_call(
        builder, "StringView", &ir_str_type,
        &owned, 1U, 1U, place->span);
}

static void lower_interpolation_to_element(
    IrBuilder *builder, const Expr *expr, uint32_t local,
    const char *property_name) {
    if (property_name != NULL) {
        IrInstruction *begin = ir_append_instruction(
            builder, IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN,
            IR_INVALID_ID, NULL, 0U, expr->span);
        if (begin != NULL) {
            begin->index = local;
            begin->symbol = property_name;
            begin->symbol_length = strlen(property_name);
        }
    }
    for (size_t i = 0U;
         i < expr->as.interpolation.part_count; ++i) {
        const InterpolationPart *part =
            &expr->as.interpolation.parts[i];
        IrValueId value = IR_INVALID_ID;
        if (part->expression != NULL) {
            value = part->borrow_owned_string
                ? lower_borrowed_interpolation_string(
                      builder, part->expression)
                : ir_lower_expr(builder, part->expression);
        } else {
            IrInstruction *literal = ir_append_instruction(
                builder, IR_OP_CONST_STRING,
                ir_intern_type(builder->module, &ir_str_type),
                NULL, 0U, part->span);
            if (literal != NULL) {
                literal->symbol = part->text;
                literal->symbol_length = part->text_length;
                value = literal->result;
            }
        }
        if (value == IR_INVALID_ID ||
            ir_current_terminated(builder))
            continue;
        IrInstruction *append = ir_append_instruction(
            builder,
            property_name != NULL
                ? IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND
                : IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED,
            IR_INVALID_ID, &value, 1U, part->span);
        if (append != NULL)
            append->index = local;
    }
    if (property_name != NULL &&
        !ir_current_terminated(builder)) {
        IrInstruction *end = ir_append_instruction(
            builder, IR_OP_LOCAL_ELEMENT_PROPERTY_END,
            IR_INVALID_ID, NULL, 0U, expr->span);
        if (end != NULL) end->index = local;
    }
}

static IrValueId lower_element_body(IrBuilder *builder,
                                    const Expr *expr,
                                    const char *name) {
    IrTypeId result_type = ir_intern_type(builder->module, expr->type);
    uint32_t local = begin_element_builder(
        builder, result_type, name, IR_INVALID_ID,
        expr->as.element.open_span);
    if (local == UINT32_MAX ||
        !push_element_context(builder, local, expr->span))
        return IR_INVALID_ID;
    for (size_t i = 0U; i < expr->as.element.body_count; ++i) {
        const ElementBodyItem *item = &expr->as.element.body[i];
        if (ir_current_terminated(builder)) break;
        if (item->is_statement) {
            ir_lower_stmt(builder, item->as.statement);
        } else if (item->is_static_text) {
            IrInstruction *text = ir_append_instruction(
                builder, IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT,
                IR_INVALID_ID, NULL, 0U,
                item->as.expression->span);
            if (text != NULL) {
                text->index = local;
                text->symbol = item->as.expression->as.string.data;
                text->symbol_length =
                    item->as.expression->as.string.length;
            }
        } else if (
            item->as.expression->kind ==
                EXPR_INTERPOLATION) {
            lower_interpolation_to_element(
                builder, item->as.expression,
                local, NULL);
        } else {
            IrValueId child;
            if (item->as.expression->borrow_html_string)
                child = lower_borrowed_interpolation_string(
                    builder, item->as.expression);
            else
                child = item->as.expression->kind == EXPR_ELEMENT
                    ? ir_lower_element_with_parent(
                          builder, item->as.expression, local)
                    : ir_lower_expr(builder, item->as.expression);
            if (!ir_current_terminated(builder))
                ir_append_element_child(
                    builder, child, item->as.expression->type,
                    item->as.expression->span);
        }
    }
    --builder->element_count;
    if (ir_current_terminated(builder)) return IR_INVALID_ID;
    return finish_element_builder(
        builder, local, result_type, expr->span);
}

static const ElementProperty *find_element_property(
    const Expr *expr, const char *name) {
    for (size_t i = 0U; i < expr->as.element.property_count; ++i) {
        const char *left =
            expr->as.element.properties[i].name;
        const char *right = name;
        while (*left != '\0' && *right != '\0' &&
               (*left == *right ||
                ((*left == '-' || *left == '_') &&
                 (*right == '-' || *right == '_')))) {
            ++left;
            ++right;
        }
        if (*left == '\0' && *right == '\0')
            return &expr->as.element.properties[i];
    }
    return NULL;
}

static IrValueId lower_option_variant(
    IrBuilder *builder, const Type *option_type,
    const char *variant, IrValueId payload, LangSpan span) {
    size_t operand_count =
        payload == IR_INVALID_ID ? 0U : 1U;
    IrInstruction *make = ir_append_instruction(
        builder, IR_OP_AGGREGATE_MAKE,
        ir_intern_type(builder->module, option_type),
        operand_count == 0U ? NULL : &payload,
        operand_count, span);
    if (make == NULL) return IR_INVALID_ID;
    make->symbol = variant;
    make->symbol_length = strlen(variant);
    make->index = strcmp(variant, "Some") == 0 ? 1U : 0U;
    return make->result;
}

static IrValueId lower_component_element(IrBuilder *builder,
                                         const Expr *expr,
                                         const Decl *decl,
                                         uint32_t parent_local) {
    const Function *component = &decl->as.function;
    size_t count = component->param_count;
    IrValueId *operands = ir_resize(
        NULL, count, sizeof(*operands));
    uint32_t *temporary_locals = ir_resize(
        NULL, count, sizeof(*temporary_locals));
    size_t temporary_count = 0U;
    for (size_t p = 0U; p < count; ++p) {
        const Param *parameter = &component->params[p];
        if (strcmp(parameter->name, "children") == 0) {
            operands[p] = lower_element_body(
                builder, expr, "#fragment");
        } else {
            const ElementProperty *property =
                find_element_property(expr, parameter->name);
            const Type *parameter_type =
                parameter->checked_type;
            if (property == NULL &&
                parameter_type != NULL &&
                parameter_type->kind == TYPE_OPTION) {
                operands[p] = lower_option_variant(
                    builder, parameter_type, "None",
                    IR_INVALID_ID, expr->span);
            } else if (
                property != NULL &&
                parameter_type != NULL &&
                parameter_type->kind == TYPE_OPTION &&
                property->value->type->kind != TYPE_OPTION) {
                IrValueId payload;
                if (property->borrow_interpolated_string) {
                    IrValueId owner =
                        ir_lower_expr(builder, property->value);
                    IrTypeId owner_type = ir_intern_type(
                        builder->module, &ir_string_type);
                    uint32_t owner_local = ir_add_synthetic_local(
                        builder, "<component-interpolation>",
                        owner_type);
                    IrInstruction *store = ir_append_instruction(
                        builder, IR_OP_LOCAL_STORE,
                        IR_INVALID_ID, &owner, 1U,
                        property->span);
                    if (store != NULL)
                        store->index = owner_local;
                    IrInstruction *load = ir_append_instruction(
                        builder, IR_OP_LOCAL_LOAD,
                        owner_type, NULL, 0U,
                        property->span);
                    if (load != NULL)
                        load->index = owner_local;
                    IrValueId loaded_owner = load != NULL
                        ? load->result : IR_INVALID_ID;
                    payload = ir_emit_synthetic_native_call(
                        builder, "StringView", &ir_str_type,
                        load != NULL ? &loaded_owner : NULL,
                        load != NULL ? 1U : 0U, 1U,
                        property->span);
                    temporary_locals[temporary_count++] =
                        owner_local;
                } else {
                    payload =
                        ir_lower_expr(builder, property->value);
                }
                operands[p] = lower_option_variant(
                    builder, parameter_type, "Some",
                    payload, property->span);
            } else {
                bool borrowed_place = property != NULL &&
                    parameter->borrowed &&
                    (property->value->kind == EXPR_NAME ||
                     (property->value->kind == EXPR_FIELD &&
                      property->value->as.field.object->kind ==
                          EXPR_NAME));
                if (borrowed_place) {
                    const Expr *place = property->value->kind == EXPR_FIELD
                                      ? property->value->as.field.object
                                      : property->value;
                    uint32_t local = ir_find_local(
                        builder, place->resolved_local_id,
                        place->span);
                    IrInstruction *load = ir_append_instruction(
                        builder,
                        property->value->kind == EXPR_FIELD
                            ? IR_OP_LOCAL_FIELD_BORROW
                            : IR_OP_LOCAL_LOAD,
                        ir_intern_type(
                            builder->module,
                            property->value->type),
                        NULL, 0U, property->value->span);
                    if (load != NULL) {
                        load->index = local;
                        if (property->value->kind == EXPR_FIELD) {
                            load->auxiliary = ir_field_index(
                                place->type,
                                property->value->as.field.field);
                            load->symbol =
                                property->value->as.field.field;
                            load->symbol_length =
                                strlen(load->symbol);
                        }
                    }
                    operands[p] = load != NULL
                        ? load->result : IR_INVALID_ID;
                } else if (property != NULL && parameter->borrowed) {
                    IrValueId owner = ir_lower_expr(
                        builder, property->value);
                    IrTypeId owner_type = ir_intern_type(
                        builder->module, property->value->type);
                    uint32_t owner_local = ir_add_synthetic_local(
                        builder, "<component-temporary>", owner_type);
                    IrInstruction *store = ir_append_instruction(
                        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                        &owner, 1U, property->span);
                    if (store != NULL) store->index = owner_local;
                    IrInstruction *load = ir_append_instruction(
                        builder, IR_OP_LOCAL_LOAD, owner_type,
                        NULL, 0U, property->span);
                    if (load != NULL) load->index = owner_local;
                    operands[p] = load != NULL
                        ? load->result : IR_INVALID_ID;
                    temporary_locals[temporary_count++] = owner_local;
                } else if (property != NULL &&
                    property->borrow_interpolated_string) {
                    IrValueId owner =
                        ir_lower_expr(builder, property->value);
                    IrTypeId owner_type = ir_intern_type(
                        builder->module, &ir_string_type);
                    uint32_t owner_local = ir_add_synthetic_local(
                        builder, "<component-interpolation>",
                        owner_type);
                    IrInstruction *store = ir_append_instruction(
                        builder, IR_OP_LOCAL_STORE,
                        IR_INVALID_ID, &owner, 1U,
                        property->span);
                    if (store != NULL)
                        store->index = owner_local;
                    IrInstruction *load = ir_append_instruction(
                        builder, IR_OP_LOCAL_LOAD,
                        owner_type, NULL, 0U,
                        property->span);
                    if (load != NULL)
                        load->index = owner_local;
                    IrValueId loaded_owner = load != NULL
                        ? load->result : IR_INVALID_ID;
                    operands[p] = ir_emit_synthetic_native_call(
                        builder, "StringView", &ir_str_type,
                        load != NULL ? &loaded_owner : NULL,
                        load != NULL ? 1U : 0U, 1U,
                        property->span);
                    temporary_locals[temporary_count++] =
                        owner_local;
                } else {
                    operands[p] = property != NULL
                        ? ir_lower_expr(builder, property->value)
                        : IR_INVALID_ID;
                }
            }
        }
    }
    bool class_component = expr->as.element.class_render != NULL;
    IrTypeId call_type = class_component
        ? ir_intern_type(builder->module, component->checked_return_type)
        : ir_intern_type(builder->module, expr->type);
    IrInstruction *call = ir_append_instruction(
        builder, IR_OP_CALL_DIRECT,
        call_type, operands, count, expr->span);
    free(operands);
    if (call == NULL) {
        free(temporary_locals);
        return IR_INVALID_ID;
    }
    call->symbol = component->name;
    call->symbol_length = strlen(component->name);
    call->index = ir_find_function(builder->module, decl);
    if (call->index < builder->module->function_count) {
        const IrFunction *target =
            &builder->module->functions[call->index];
        call->argument_mode_count = count;
        if (count != 0U)
            call->argument_modes = ir_resize(
                NULL, count, sizeof(*call->argument_modes));
        for (size_t parameter = 0U; parameter < count; ++parameter)
            call->argument_modes[parameter] =
                target->parameters[parameter].mode;
    }
    if (!class_component && parent_local != IR_INVALID_ID &&
        call->index < builder->module->function_count &&
        builder->module->functions[
            call->index].has_render_root)
        call->render_destination = parent_local;
    IrValueId call_result = call->result;
    if (class_component) {
        const Decl *render_decl = expr->as.element.class_render;
        uint32_t instance_local = ir_add_synthetic_local(
            builder, "<class-component>", call_type);
        IrInstruction *store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &call_result, 1U, expr->span);
        if (store != NULL) store->index = instance_local;
        IrInstruction *receiver = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, call_type,
            NULL, 0U, expr->span);
        if (receiver == NULL) {
            free(temporary_locals);
            return IR_INVALID_ID;
        }
        receiver->index = instance_local;
        IrValueId render_receiver = receiver->result;
        IrInstruction *render = ir_append_instruction(
            builder, IR_OP_CALL_DIRECT,
            ir_intern_type(builder->module, expr->type),
            &render_receiver, 1U, expr->span);
        if (render == NULL) {
            free(temporary_locals);
            return IR_INVALID_ID;
        }
        render->symbol = render_decl->as.function.name;
        render->symbol_length = strlen(render->symbol);
        render->index = ir_find_function(builder->module, render_decl);
        if (render->index < builder->module->function_count) {
            const IrFunction *target =
                &builder->module->functions[render->index];
            render->argument_mode_count = target->parameter_count;
            if (target->parameter_count != 0U) {
                render->argument_modes = ir_resize(
                    NULL, target->parameter_count,
                    sizeof(*render->argument_modes));
                for (size_t parameter = 0U;
                     parameter < target->parameter_count; ++parameter)
                    render->argument_modes[parameter] =
                        target->parameters[parameter].mode;
            }
            if (parent_local != IR_INVALID_ID && target->has_render_root)
                render->render_destination = parent_local;
        }
        IrInstruction *delete_load = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, call_type,
            NULL, 0U, expr->span);
        if (delete_load != NULL) delete_load->index = instance_local;
        IrValueId delete_value = delete_load != NULL
            ? delete_load->result : IR_INVALID_ID;
        IrInstruction *drop = ir_append_instruction(
            builder, IR_OP_CLASS_DELETE, IR_INVALID_ID,
            &delete_value, 1U, expr->span);
        (void)drop;
        call_result = render->result;
    }
    for (size_t i = temporary_count; i > 0U; --i) {
        IrInstruction *drop = ir_append_instruction(
            builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
            NULL, 0U, expr->span);
        if (drop != NULL)
            drop->index = temporary_locals[i - 1U];
    }
    free(temporary_locals);
    return call_result;
}

static void record_static_css(IrBuilder *builder, const Expr *expr) {
    const char *scope = expr->as.element.css_style_attribute;
    if (scope == NULL || expr->as.element.body_count == 0U ||
        expr->as.element.body[0].is_statement ||
        expr->as.element.body[0].as.expression->kind != EXPR_STRING)
        return;
    IrFunction *function = builder->function;
    for (size_t i = 0U; i < function->static_css_count; ++i)
        if (strcmp(function->static_css[i].scope_attribute, scope) == 0)
            return;
    if (function->static_css_count == function->static_css_capacity) {
        size_t capacity = function->static_css_capacity == 0U
                        ? 4U : function->static_css_capacity * 2U;
        function->static_css = ir_resize(
            function->static_css, capacity,
            sizeof(*function->static_css));
        function->static_css_capacity = capacity;
    }
    const Expr *text = expr->as.element.body[0].as.expression;
    function->static_css[function->static_css_count++] = (IrStaticCss){
        scope, text->as.string.data, text->as.string.length, expr->span
    };
}

IrValueId ir_lower_element_with_parent(
    IrBuilder *builder, const Expr *expr, uint32_t parent_local) {
    record_static_css(builder, expr);
    const Decl *resolved = expr->resolved_decl;
    if (resolved != NULL && resolved->kind == DECL_FUNCTION)
        return lower_component_element(
            builder, expr, resolved, parent_local);

    IrTypeId result_type = ir_intern_type(builder->module, expr->type);
    const char *name =
        resolved != NULL && resolved->kind == DECL_ELEMENT
        ? resolved->as.element.name : expr->as.element.name;
    uint32_t local = begin_element_builder(
        builder, result_type, name, parent_local,
        expr->as.element.open_span);
    if (local == UINT32_MAX) return IR_INVALID_ID;
    for (size_t i = 0U; i < expr->as.element.property_count; ++i) {
        const ElementProperty *property =
            &expr->as.element.properties[i];
        if (property->css_custom_property) continue;
        if (property->projection_binding != NULL) {
            IrInstruction *binding = ir_append_instruction(
                builder, IR_OP_CONST_STRING,
                ir_intern_type(builder->module, &ir_str_type),
                NULL, 0U, property->span);
            if (binding != NULL) {
                binding->symbol = property->projection_binding;
                binding->symbol_length = strlen(property->projection_binding);
                IrValueId marker = binding->result;
                IrInstruction *set = ir_append_instruction(
                    builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
                    IR_INVALID_ID, &marker, 1U, property->span);
                if (set != NULL) {
                    set->index = local;
                    set->symbol = "data-aster-project";
                    set->symbol_length = strlen(set->symbol);
                }
            }
            if (property->projection_binding[0] != 't') {
                IrValueId value = ir_lower_expr(builder, property->value);
                IrInstruction *set = ir_append_instruction(
                    builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
                    IR_INVALID_ID, &value, 1U, property->span);
                if (set != NULL) {
                    set->index = local;
                    set->symbol = property->projection_binding[0] == 'c'
                                ? "class" : "disabled";
                    set->symbol_length = strlen(set->symbol);
                }
            }
            continue;
        }
        if (property->event_binding != NULL) {
            IrInstruction *binding = ir_append_instruction(
                builder, IR_OP_CONST_STRING,
                ir_intern_type(builder->module, &ir_str_type),
                NULL, 0U, property->span);
            if (binding != NULL) {
                binding->symbol = property->event_binding;
                binding->symbol_length =
                    strlen(property->event_binding);
                IrValueId value = binding->result;
                IrInstruction *set = ir_append_instruction(
                    builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
                    IR_INVALID_ID, &value, 1U, property->span);
                if (set != NULL) {
                    set->index = local;
                    set->symbol = "data-aster-event";
                    set->symbol_length = strlen(set->symbol);
                }
            }
            continue;
        }
        const char *property_name = property->keyed_identity
            ? "data-aster-key" : property->name;
        if (property->value->kind ==
            EXPR_INTERPOLATION) {
            lower_interpolation_to_element(
                builder, property->value, local,
                property_name);
            continue;
        }
        IrValueId value = ir_lower_expr(builder, property->value);
        IrInstruction *set = ir_append_instruction(
            builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
            IR_INVALID_ID, &value, 1U, property->span);
        if (set != NULL) {
            set->index = local;
            set->symbol = property_name;
            set->symbol_length = strlen(property_name);
        }
    }
    bool custom_property_started = false;
    for (size_t i = 0U; i < expr->as.element.property_count; ++i) {
        const ElementProperty *property =
            &expr->as.element.properties[i];
        if (!property->css_custom_property) continue;
        bool first = !custom_property_started;
        if (first) {
            IrInstruction *begin = ir_append_instruction(
                builder, IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN,
                IR_INVALID_ID, NULL, 0U, property->span);
            if (begin != NULL) {
                begin->index = local;
                begin->symbol = "style";
                begin->symbol_length = 5U;
            }
            custom_property_started = true;
        }
        const char *parts[] = {
            first ? NULL : "; ", property->name, ": "
        };
        size_t lengths[] = {2U, strlen(property->name), 2U};
        for (size_t part = 0U; part < 3U; ++part) {
            if (parts[part] == NULL) continue;
            IrInstruction *literal = ir_append_instruction(
                builder, IR_OP_CONST_STRING,
                ir_intern_type(builder->module, &ir_str_type),
                NULL, 0U, property->span);
            if (literal == NULL) continue;
            literal->symbol = parts[part];
            literal->symbol_length = lengths[part];
            IrValueId prefix_value = literal->result;
            IrInstruction *append = ir_append_instruction(
                builder, IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND,
                IR_INVALID_ID, &prefix_value, 1U, property->span);
            if (append != NULL) append->index = local;
        }
        IrValueId value = ir_lower_expr(builder, property->value);
        IrInstruction *append = ir_append_instruction(
            builder, IR_OP_LOCAL_ELEMENT_CSS_VALUE,
            IR_INVALID_ID, &value, 1U, property->span);
        if (append != NULL) append->index = local;
    }
    if (custom_property_started && !ir_current_terminated(builder)) {
        IrInstruction *end = ir_append_instruction(
            builder, IR_OP_LOCAL_ELEMENT_PROPERTY_END,
            IR_INVALID_ID, NULL, 0U, expr->as.element.open_span);
        if (end != NULL) end->index = local;
    }
    if (expr->as.element.css_style_attribute != NULL) {
        IrInstruction *constant = ir_append_instruction(
            builder, IR_OP_CONST_BOOL,
            ir_intern_type(builder->module, &ir_bool_type),
            NULL, 0U, expr->as.element.open_span);
        if (constant != NULL) constant->integer = 1U;
        IrValueId value = constant != NULL
            ? constant->result : IR_INVALID_ID;
        IrInstruction *set = ir_append_instruction(
            builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
            IR_INVALID_ID, &value, 1U,
            expr->as.element.open_span);
        if (set != NULL) {
            set->index = local;
            set->symbol = expr->as.element.css_style_attribute;
            set->symbol_length = strlen(expr->as.element.css_style_attribute);
        }
    }
    if (builder->function->css_scope_attribute != NULL &&
        strcmp(name, "#fragment") != 0 &&
        !ir_style_name(name)) {
        IrInstruction *constant = ir_append_instruction(
            builder, IR_OP_CONST_BOOL,
            ir_intern_type(builder->module, &ir_bool_type),
            NULL, 0U, expr->as.element.open_span);
        if (constant != NULL) constant->integer = 1U;
        IrValueId value = constant != NULL
            ? constant->result : IR_INVALID_ID;
        IrInstruction *set = ir_append_instruction(
            builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
            IR_INVALID_ID, &value, 1U,
            expr->as.element.open_span);
        if (set != NULL) {
            set->index = local;
            set->symbol = builder->function->css_scope_attribute;
            set->symbol_length = strlen(builder->function->css_scope_attribute);
        }
    }
    if (!push_element_context(builder, local, expr->span))
        return IR_INVALID_ID;
    for (size_t i = 0U; i < expr->as.element.body_count; ++i) {
        const ElementBodyItem *item = &expr->as.element.body[i];
        if (ir_current_terminated(builder)) break;
        if (item->is_statement) {
            ir_lower_stmt(builder, item->as.statement);
        } else if (item->is_static_text) {
            IrInstruction *text = ir_append_instruction(
                builder, IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT,
                IR_INVALID_ID, NULL, 0U,
                item->as.expression->span);
            if (text != NULL) {
                text->index = local;
                text->symbol = item->as.expression->as.string.data;
                text->symbol_length =
                    item->as.expression->as.string.length;
            }
        } else if (
            item->as.expression->kind ==
                EXPR_INTERPOLATION) {
            lower_interpolation_to_element(
                builder, item->as.expression,
                local, NULL);
        } else {
            IrValueId child;
            if (item->as.expression->borrow_html_string)
                child = lower_borrowed_interpolation_string(
                    builder, item->as.expression);
            else
                child = item->as.expression->kind == EXPR_ELEMENT
                    ? ir_lower_element_with_parent(
                          builder, item->as.expression, local)
                    : ir_lower_expr(builder, item->as.expression);
            if (!ir_current_terminated(builder))
                ir_append_element_child(
                    builder, child, item->as.expression->type,
                    item->as.expression->span);
        }
    }
    --builder->element_count;
    if (ir_current_terminated(builder)) return IR_INVALID_ID;
    return finish_element_builder(
        builder, local, result_type, expr->span);
}

IrValueId ir_lower_element(IrBuilder *builder, const Expr *expr) {
    return ir_lower_element_with_parent(
        builder, expr, IR_INVALID_ID);
}
