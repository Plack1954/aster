#include "ir_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool push_element_context(IrBuilder *builder, uint32_t local,
                                 bool keyed_item, LangSpan span) {
    if (builder->element_count >=
        sizeof(builder->elements) / sizeof(builder->elements[0])) {
        lang_diag(builder->diagnostics, span,
                  "IR element nesting limit exceeded");
        builder->failed = true;
        return false;
    }
    builder->elements[builder->element_count++] =
        (IrElementContext){local, builder->loop_count, keyed_item};
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
        !push_element_context(builder, local, false, expr->span))
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

static uint64_t keyed_part_id(
    IrBuilder *builder, const Expr *expression, char kind, LangSpan span) {
    if (expression != NULL && expression->kind == EXPR_FIELD &&
        expression->as.field.object->type != NULL &&
        expression->as.field.object->type->declaration != NULL) {
        const Decl *declaration =
            expression->as.field.object->type->declaration;
        if (declaration->kind == DECL_STRUCT ||
            declaration->kind == DECL_CLASS)
            for (size_t field = 0U;
                 field < declaration->as.structure.field_count; ++field)
                if (strcmp(declaration->as.structure.fields[field].name,
                           expression->as.field.field) == 0)
                    return lang_projection_part_id(
                        declaration->module_name,
                        declaration->as.structure.name, field);
    }
    return lang_projection_part_id(
        builder->function->module_name,
        builder->function->name,
        span.start * 8U + (size_t)(
            kind == 't' ? 1U : kind == 'c' ? 2U : kind == 'd' ? 3U :
            kind == 'h' ? 4U : 5U));
}

static void emit_keyed_part_marker(
    IrBuilder *builder, uint32_t local, const Expr *expression,
    char kind, LangSpan span) {
    uint64_t part_id = keyed_part_id(
        builder, expression, kind, span);
    char *identifier = lang_arena_alloc(
        &builder->module->lowering_module->arena, 17U);
    (void)snprintf(identifier, 17U, "%016" PRIx64, part_id);
    IrInstruction *constant = ir_append_instruction(
        builder, IR_OP_CONST_STRING,
        ir_intern_type(builder->module, &ir_str_type),
        NULL, 0U, span);
    if (constant == NULL) return;
    constant->symbol = identifier;
    constant->symbol_length = 16U;
    IrValueId value = constant->result;
    IrInstruction *set = ir_append_instruction(
        builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
        IR_INVALID_ID, &value, 1U, span);
    if (set == NULL) return;
    set->index = local;
    set->symbol = kind == 't' ? "data-aster-part-t"
                : kind == 'c' ? "data-aster-part-c"
                : kind == 'd' ? "data-aster-part-d"
                : kind == 'h' ? "data-aster-part-h"
                              : "data-aster-part-a";
    set->symbol_length = strlen(set->symbol);

    if (builder->function->owner_type == NULL ||
        expression == NULL || expression->kind != EXPR_FIELD ||
        expression->as.field.object->type == NULL ||
        expression->as.field.object->type->declaration == NULL ||
        expression->as.field.object->type->declaration->kind != DECL_STRUCT)
        return;
    char *state_name = lang_arena_alloc(
        &builder->module->lowering_module->arena, 40U);
    char *state_marker_name = lang_arena_alloc(
        &builder->module->lowering_module->arena, 48U);
    (void)snprintf(
        state_name, 40U, "data-aster-state-%016" PRIx64, part_id);
    (void)snprintf(
        state_marker_name, 48U,
        "data-aster-state-field-%016" PRIx64, part_id);
    IrInstruction *state_marker = ir_append_instruction(
        builder, IR_OP_CONST_BOOL,
        ir_intern_type(builder->module, &ir_bool_type),
        NULL, 0U, span);
    if (state_marker != NULL) state_marker->integer = 1U;
    IrValueId state_marker_value = state_marker != NULL
        ? state_marker->result : IR_INVALID_ID;
    IrInstruction *set_state_marker = ir_append_instruction(
        builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
        IR_INVALID_ID, &state_marker_value, 1U, span);
    if (set_state_marker != NULL) {
        set_state_marker->index = local;
        set_state_marker->symbol = state_marker_name;
        set_state_marker->symbol_length = strlen(state_marker_name);
    }
    IrValueId state_value = ir_lower_expr(builder, expression);
    IrInstruction *set_state = ir_append_instruction(
        builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
        IR_INVALID_ID, &state_value, 1U, span);
    if (set_state != NULL) {
        set_state->index = local;
        set_state->symbol = state_name;
        set_state->symbol_length = strlen(state_name);
    }
}

static void emit_component_constructor_markers(
    IrBuilder *builder, uint32_t local, LangSpan span) {
    const IrFunction *constructor = NULL;
    for (size_t function = 0U;
         function < builder->module->function_count; ++function) {
        const IrFunction *candidate = &builder->module->functions[function];
        if (candidate->is_constructor && candidate->owner_type != NULL &&
            strcmp(candidate->owner_type,
                   builder->function->owner_type) == 0) {
            constructor = candidate;
            break;
        }
    }
    if (constructor == NULL ||
        builder->function->parameter_count == 0U)
        return;
    const IrType *instance = &builder->module->types[
        builder->function->parameters[0].type];
    for (size_t parameter = 0U;
         parameter < constructor->parameter_count; ++parameter) {
        size_t field = instance->field_count;
        for (size_t candidate = 0U;
             candidate < instance->field_count; ++candidate)
            if (strcmp(instance->field_names[candidate],
                       constructor->parameters[parameter].name) == 0) {
                field = candidate;
                break;
            }
        if (field == instance->field_count) continue;
        const IrType *type = &builder->module->types[
            constructor->parameters[parameter].type];
        char code = type->shape == IR_TYPE_BOOL ? 'b'
                  : type->shape == IR_TYPE_SIGNED_INT ||
                    type->shape == IR_TYPE_UNSIGNED_INT ? 'l' : 's';
        char *type_name = lang_arena_alloc(
            &builder->module->lowering_module->arena, 64U);
        char *value_name = lang_arena_alloc(
            &builder->module->lowering_module->arena, 64U);
        (void)snprintf(type_name, 64U,
                       "data-aster-component-param-%zu", parameter);
        (void)snprintf(value_name, 64U,
                       "data-aster-component-arg-%zu", parameter);
        IrInstruction *type_value = ir_append_instruction(
            builder, IR_OP_CONST_STRING,
            ir_intern_type(builder->module, &ir_str_type),
            NULL, 0U, span);
        if (type_value != NULL) {
            char *encoded = lang_arena_alloc(
                &builder->module->lowering_module->arena, 2U);
            encoded[0] = code;
            encoded[1] = '\0';
            type_value->symbol = encoded;
            type_value->symbol_length = 1U;
            IrValueId marker = type_value->result;
            IrInstruction *set_type = ir_append_instruction(
                builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
                IR_INVALID_ID, &marker, 1U, span);
            if (set_type != NULL) {
                set_type->index = local;
                set_type->symbol = type_name;
                set_type->symbol_length = strlen(type_name);
            }
        }
        IrInstruction *load = ir_append_instruction(
            builder, IR_OP_LOCAL_FIELD_GET,
            constructor->parameters[parameter].type,
            NULL, 0U, span);
        if (load == NULL) continue;
        load->index = 0U;
        load->auxiliary = (uint32_t)field;
        load->symbol = instance->field_names[field];
        load->symbol_length = strlen(load->symbol);
        IrValueId value = load->result;
        IrInstruction *set_value = ir_append_instruction(
            builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
            IR_INVALID_ID, &value, 1U, span);
        if (set_value != NULL) {
            set_value->index = local;
            set_value->symbol = value_name;
            set_value->symbol_length = strlen(value_name);
        }
    }
    for (size_t field = 0U; field < instance->field_count; ++field) {
        const IrType *list = &builder->module->types[
            instance->field_types[field]];
        if (list->shape != IR_TYPE_BUILTIN_OBJECT || list->name == NULL ||
            strncmp(list->name, "List<", 5U) != 0 ||
            list->element_type == IR_INVALID_ID)
            continue;
        const IrType *item = &builder->module->types[list->element_type];
        if (item->shape != IR_TYPE_STRUCT || item->field_count == 0U)
            continue;
        char *schema = lang_arena_alloc(
            &builder->module->lowering_module->arena,
            item->field_count * 20U + 1U);
        size_t offset = 0U;
        bool supported = true;
        for (size_t item_field = 0U;
             item_field < item->field_count; ++item_field) {
            const IrType *field_type = &builder->module->types[
                item->field_types[item_field]];
            char code = strcmp(item->field_names[item_field], "key") == 0
                ? 'k' : field_type->shape == IR_TYPE_BOOL ? 'b'
                : field_type->shape == IR_TYPE_SIGNED_INT ||
                  field_type->shape == IR_TYPE_UNSIGNED_INT ? 'l'
                : field_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                  field_type->name != NULL &&
                  strcmp(field_type->name, "string") == 0 ? 's' : '?';
            if (code == '?') {
                supported = false;
                break;
            }
            uint64_t id = lang_projection_part_id(
                item->module_name, item->name, item_field);
            offset += (size_t)snprintf(
                schema + offset, item->field_count * 20U + 1U - offset,
                "%s%c:%016" PRIx64,
                item_field == 0U ? "" : ",", code, id);
        }
        if (!supported) continue;
        IrInstruction *constant = ir_append_instruction(
            builder, IR_OP_CONST_STRING,
            ir_intern_type(builder->module, &ir_str_type),
            NULL, 0U, span);
        if (constant == NULL) return;
        constant->symbol = schema;
        constant->symbol_length = offset;
        IrValueId schema_value = constant->result;
        IrInstruction *set_schema = ir_append_instruction(
            builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
            IR_INVALID_ID, &schema_value, 1U, span);
        if (set_schema != NULL) {
            set_schema->index = local;
            set_schema->symbol = "data-aster-component-list-state";
            set_schema->symbol_length =
                strlen(set_schema->symbol);
        }
        break;
    }
}

static const Expr *keyed_text_part_expression(const Expr *element) {
    const Expr *first_dynamic = NULL;
    for (size_t item_index = 0U;
         item_index < element->as.element.body_count; ++item_index) {
        const ElementBodyItem *item = &element->as.element.body[item_index];
        if (item->is_statement) return NULL;
        if (item->is_static_text) continue;
        const Expr *expression = item->as.expression;
        if (expression->kind == EXPR_ELEMENT) return NULL;
        if (expression->kind != EXPR_INTERPOLATION &&
            expression->type != NULL &&
            !(expression->type->kind == TYPE_STRING ||
              expression->type->kind == TYPE_STR ||
              expression->type->kind == TYPE_BOOL ||
              expression->type->kind == TYPE_CHAR ||
              (expression->type->kind >= TYPE_I8 &&
               expression->type->kind <= TYPE_USIZE)))
            return NULL;
        if (first_dynamic == NULL) first_dynamic = expression;
    }
    return first_dynamic;
}

IrValueId ir_lower_element_with_parent(
    IrBuilder *builder, const Expr *expr, uint32_t parent_local) {
    record_static_css(builder, expr);
    const Decl *resolved = expr->resolved_decl;
    if (resolved != NULL && resolved->kind == DECL_FUNCTION)
        return lower_component_element(
            builder, expr, resolved, parent_local);

    IrTypeId result_type = ir_intern_type(builder->module, expr->type);
    bool keyed_item = builder->function->is_component_render ||
        (builder->element_count != 0U &&
         builder->elements[builder->element_count - 1U].keyed_item);
    for (size_t property = 0U;
         property < expr->as.element.property_count; ++property)
        if (expr->as.element.properties[property].keyed_identity)
            keyed_item = true;
    bool class_render_root = builder->element_count == 0U &&
        builder->function->is_component_render;
    const char *name =
        resolved != NULL && resolved->kind == DECL_ELEMENT
        ? resolved->as.element.name : expr->as.element.name;
    uint32_t local = begin_element_builder(
        builder, result_type, name, parent_local,
        expr->as.element.open_span);
    if (local == UINT32_MAX) return IR_INVALID_ID;
    if (class_render_root) {
        IrInstruction *owner = ir_append_instruction(
            builder, IR_OP_CONST_STRING,
            ir_intern_type(builder->module, &ir_str_type),
            NULL, 0U, expr->span);
        if (owner != NULL) {
            owner->symbol = builder->function->owner_type;
            owner->symbol_length = strlen(owner->symbol);
            IrValueId value = owner->result;
            IrInstruction *set = ir_append_instruction(
                builder, IR_OP_LOCAL_ELEMENT_PROPERTY,
                IR_INVALID_ID, &value, 1U, expr->span);
            if (set != NULL) {
                set->index = local;
                set->symbol = "data-aster-component";
                set->symbol_length = strlen(set->symbol);
            }
        }
        emit_component_constructor_markers(
            builder, local, expr->span);
    }
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
        bool dynamic_property =
            property->value->kind != EXPR_INT &&
            property->value->kind != EXPR_FLOAT &&
            property->value->kind != EXPR_STRING &&
            property->value->kind != EXPR_BOOL;
        if (keyed_item && dynamic_property &&
            strcmp(property_name, "class") == 0)
            emit_keyed_part_marker(
                builder, local, property->value, 'c', property->span);
        else if (keyed_item && dynamic_property &&
                 strcmp(property_name, "disabled") == 0)
            emit_keyed_part_marker(
                builder, local, property->value, 'd', property->span);
        else if (keyed_item && dynamic_property &&
                 strcmp(property_name, "hidden") == 0)
            emit_keyed_part_marker(
                builder, local, property->value, 'h', property->span);
        else if (keyed_item && dynamic_property &&
                 strcmp(property_name, "title") == 0)
            emit_keyed_part_marker(
                builder, local, property->value, 'a', property->span);
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
    const Expr *text_part = keyed_item
        ? keyed_text_part_expression(expr) : NULL;
    if (text_part != NULL)
        emit_keyed_part_marker(
            builder, local, text_part, 't', text_part->span);
    if (!push_element_context(builder, local, keyed_item, expr->span))
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
