#include "internal.h"
#include "ir_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool expression_contains_return(const Expr *expr);

static bool statement_contains_return(const Stmt *stmt) {
    if (stmt == NULL) return false;
    switch (stmt->kind) {
        case STMT_RETURN:
            return true;
        case STMT_LET:
            return expression_contains_return(stmt->as.let.value);
        case STMT_DESTRUCTURE:
            return expression_contains_return(
                stmt->as.destructure.value);
        case STMT_DELETE:
            return expression_contains_return(stmt->as.delete_value);
        case STMT_EXPR:
            return expression_contains_return(stmt->as.expression);
        case STMT_IF:
            return expression_contains_return(stmt->as.if_.condition) ||
                   statement_contains_return(stmt->as.if_.then_branch) ||
                   statement_contains_return(stmt->as.if_.else_branch);
        case STMT_WHILE:
            return expression_contains_return(stmt->as.while_.condition) ||
                   statement_contains_return(stmt->as.while_.body);
        case STMT_FOR:
            return expression_contains_return(stmt->as.for_.iterable) ||
                   expression_contains_return(stmt->as.for_.range_end) ||
                   statement_contains_return(stmt->as.for_.body);
        case STMT_C_FOR:
            return statement_contains_return(
                       stmt->as.c_for.initializer) ||
                   expression_contains_return(
                       stmt->as.c_for.condition) ||
                   expression_contains_return(
                       stmt->as.c_for.increment) ||
                   statement_contains_return(stmt->as.c_for.body);
        case STMT_MATCH:
            if (expression_contains_return(stmt->as.match_.value))
                return true;
            for (size_t i = 0U;
                 i < stmt->as.match_.arm_count; ++i)
                if (statement_contains_return(
                        stmt->as.match_.arms[i].body))
                    return true;
            return false;
        case STMT_THROW:
            return expression_contains_return(stmt->as.throw_value);
        case STMT_TRY:
            return statement_contains_return(stmt->as.try_.body) ||
                   statement_contains_return(stmt->as.try_.catch_body) ||
                   statement_contains_return(stmt->as.try_.finally_body);
        case STMT_BLOCK:
            for (size_t i = 0U; i < stmt->as.block.count; ++i)
                if (statement_contains_return(
                        stmt->as.block.items[i]))
                    return true;
            return false;
        case STMT_UNSAFE:
            return statement_contains_return(
                stmt->as.unsafe_body);
        case STMT_BREAK:
        case STMT_CONTINUE:
            return false;
    }
    return false;
}

static bool expression_contains_return(const Expr *expr) {
    if (expr == NULL) return false;
    switch (expr->kind) {
        case EXPR_BINARY:
            return expression_contains_return(expr->as.binary.left) ||
                   expression_contains_return(expr->as.binary.right);
        case EXPR_UNARY:
            return expression_contains_return(expr->as.unary.operand);
        case EXPR_CALL:
            if (expression_contains_return(expr->as.call.callee))
                return true;
            for (size_t i = 0U;
                 i < expr->as.call.arguments.count; ++i)
                if (expression_contains_return(
                        expr->as.call.arguments.items[i]))
                    return true;
            return false;
        case EXPR_ASSIGN:
            return expression_contains_return(expr->as.assign.target) ||
                   expression_contains_return(expr->as.assign.value);
        case EXPR_COPY:
            return expression_contains_return(expr->as.copy.value);
        case EXPR_TRY:
            return expression_contains_return(expr->as.try_.value);
        case EXPR_AWAIT:
            return expression_contains_return(expr->as.try_.value);
        case EXPR_CAST:
            return expression_contains_return(expr->as.cast.value);
        case EXPR_ARRAY:
            for (size_t i = 0U; i < expr->as.array.count; ++i)
                if (expression_contains_return(
                        expr->as.array.items[i]))
                    return true;
            return false;
        case EXPR_INTERPOLATION:
            for (size_t i = 0U;
                 i < expr->as.interpolation.part_count; ++i)
                if (expr->as.interpolation.parts[i].expression !=
                        NULL &&
                    expression_contains_return(
                        expr->as.interpolation.parts[i].expression))
                    return true;
            return false;
        case EXPR_INDEX:
            return expression_contains_return(expr->as.index.object) ||
                   expression_contains_return(expr->as.index.index);
        case EXPR_FIELD:
            return expression_contains_return(expr->as.field.object);
        case EXPR_STRUCT:
            for (size_t i = 0U;
                 i < expr->as.structure.field_count; ++i)
                if (expression_contains_return(
                        expr->as.structure.fields[i].value))
                    return true;
            return false;
        case EXPR_ELEMENT:
            for (size_t i = 0U;
                 i < expr->as.element.property_count; ++i)
                if (expression_contains_return(
                        expr->as.element.properties[i].value))
                    return true;
            for (size_t i = 0U;
                 i < expr->as.element.body_count; ++i) {
                const ElementBodyItem *item =
                    &expr->as.element.body[i];
                if ((item->is_statement &&
                     statement_contains_return(
                         item->as.statement)) ||
                    (!item->is_statement &&
                     expression_contains_return(
                         item->as.expression)))
                    return true;
            }
            return false;
        case EXPR_IF:
            return expression_contains_return(expr->as.if_.condition) ||
                   statement_contains_return(expr->as.if_.then_branch) ||
                   statement_contains_return(expr->as.if_.else_branch);
        case EXPR_MATCH:
            if (expression_contains_return(expr->as.match_.value))
                return true;
            for (size_t i = 0U;
                 i < expr->as.match_.arm_count; ++i)
                if (statement_contains_return(
                        expr->as.match_.arms[i].body))
                    return true;
            return false;
        case EXPR_INT:
        case EXPR_FLOAT:
        case EXPR_STRING:
        case EXPR_BOOL:
        case EXPR_NULL:
        case EXPR_NAME:
            return false;
    }
    return false;
}

static const Expr *direct_render_root(const Function *function) {
    if (function == NULL || function->checked_return_type == NULL ||
        function->checked_return_type->kind != TYPE_HTML ||
        function->body == NULL ||
        function->body->kind != STMT_BLOCK ||
        function->body->as.block.count == 0U)
        return NULL;
    const Stmt *tail = function->body->as.block.items[
        function->body->as.block.count - 1U];
    if (tail == NULL) return NULL;
    const Expr *root = NULL;
    if (tail->kind == STMT_RETURN)
        root = tail->as.return_value;
    else if (tail->kind == STMT_EXPR &&
             !tail->expression_terminated)
        root = tail->as.expression;
    bool intrinsic_root =
        root != NULL && root->kind == EXPR_ELEMENT &&
        ((root->resolved_decl != NULL &&
          root->resolved_decl->kind == DECL_ELEMENT) ||
         strcmp(root->as.element.name, "#fragment") == 0);
    if (!intrinsic_root || expression_contains_return(root))
        return NULL;
    for (size_t i = 0U;
         i + 1U < function->body->as.block.count; ++i)
        if (statement_contains_return(
                function->body->as.block.items[i]))
            return NULL;
    return root;
}

static void initialize_functions(const Module *module, IrModule *ir) {
    for (size_t i = 0U; i < module->count; ++i) {
        const Decl *decl = module->decls[i];
        if (decl->kind == DECL_FUNCTION &&
            decl->type_param_count == 0U &&
            !decl->as.function.is_extern &&
            !decl->as.function.is_deleted)
            ++ir->function_count;
    }
    ir->functions = ir_resize(
        NULL, ir->function_count, sizeof(*ir->functions));
    memset(ir->functions, 0,
           ir->function_count * sizeof(*ir->functions));
    size_t output = 0U;
    for (size_t i = 0U; i < module->count; ++i) {
        const Decl *decl = module->decls[i];
        if (decl->kind != DECL_FUNCTION ||
            decl->type_param_count != 0U ||
            decl->as.function.is_extern ||
            decl->as.function.is_deleted)
            continue;
        IrFunction *function = &ir->functions[output++];
        function->name = decl->as.function.name;
        function->module_name = decl->module_name;
        function->owner_type = decl->as.function.owner_type;
        function->declaration = decl;
        function->span = decl->span;
        function->is_public = decl->is_public;
        function->is_abstract =
            decl->as.function.is_abstract_member;
        function->is_virtual =
            decl->as.function.is_virtual_member ||
            decl->as.function.is_override_member;
        function->virtual_root = IR_INVALID_ID;
        function->abi.calling_convention = IR_CALLING_CONVENTION_ASTER;
        function->abi.may_propagate_exception = true;
        function->is_destructor =
            decl->as.function.is_drop;
        function->is_constructor =
            decl->as.function.is_constructor;
        function->is_component_render =
            decl->as.function.is_interactive_component_render;
        function->is_web_export =
            decl->as.function.is_web_handler;
        const Expr *render_root =
            direct_render_root(&decl->as.function);
        if (render_root != NULL) {
            function->has_render_root = true;
            function->render_root_span =
                render_root->as.element.open_span;
        }
        function->return_type =
            ir_intern_type(ir, decl->as.function.checked_return_type);
        function->is_async = decl->as.function.is_async;
        function->abi.returns_async_task = function->is_async;
        function->async_result_type = function->is_async
            ? ir_intern_type(
                  ir, decl->as.function.checked_return_type->element)
            : function->return_type;
        function->parameter_count = decl->as.function.param_count;
        function->parameters = ir_resize(
            NULL, function->parameter_count,
            sizeof(*function->parameters));
        for (size_t p = 0U; p < function->parameter_count; ++p) {
            function->parameters[p].name =
                decl->as.function.params[p].name;
            function->parameters[p].type = ir_intern_type(
                ir, decl->as.function.params[p].checked_type);
            function->parameters[p].mode = parameter_mode_from_param(
                &decl->as.function.params[p]);
            function->parameters[p].span =
                decl->as.function.params[p].span;
        }
        function->css_scope_attribute =
            decl->as.function.css_scope_attribute;
        function->is_entry =
            strcmp(function->name, "main") == 0 &&
            module->entry_module != NULL &&
            function->module_name != NULL &&
            strcmp(module->entry_module, function->module_name) == 0;
    }
    for (size_t i = 0U; i < ir->function_count; ++i) {
        const Function *source =
            &ir->functions[i].declaration->as.function;
        const Decl *root = source->virtual_root_decl;
        if (root != NULL)
            ir->functions[i].virtual_root =
                ir_find_function(ir, root);
    }
}

static void resolve_type_copy_functions(IrModule *ir) {
    for (size_t type_index = 0U;
         type_index < ir->type_count; ++type_index) {
        IrType *type = &ir->types[type_index];
        if (type->copy_policy != IR_COPY_CUSTOM ||
            type->checked_type == NULL)
            continue;
        const Decl *copy = type_copy_constructor(type->checked_type);
        if (copy == NULL || copy->as.function.is_deleted)
            continue;
        type->copy_function = ir_find_function(ir, copy);
    }
}

static void initialize_static_fields(const Module *module, IrModule *ir) {
    for (size_t i = 0U; i < module->count; ++i) {
        const Decl *owner = module->decls[i];
        if (owner->kind == DECL_STRUCT || owner->kind == DECL_CLASS)
            ir->static_field_count +=
                owner->as.structure.static_field_count;
    }
    ir->static_fields = ir_resize(
        NULL, ir->static_field_count, sizeof(*ir->static_fields));
    size_t output = 0U;
    for (size_t i = 0U; i < module->count; ++i) {
        const Decl *owner = module->decls[i];
        if (owner->kind != DECL_STRUCT && owner->kind != DECL_CLASS)
            continue;
        for (size_t field = 0U;
             field < owner->as.structure.static_field_count; ++field) {
            const FieldDecl *source =
                &owner->as.structure.static_fields[field];
            ir->static_fields[output++] = (IrStaticField){
                .name=source->name,
                .owner_name=owner->as.structure.name,
                .module_name=owner->module_name,
                .type=ir_intern_type(ir, source->checked_type),
                .span=source->span,
                .owner_declaration=owner
            };
            IrStaticField *target = &ir->static_fields[output - 1U];
            const Expr *initializer = source->initializer;
            if (initializer == NULL) continue;
            if (initializer->kind == EXPR_INT)
                target->initial_integer = initializer->as.integer;
            else if (initializer->kind == EXPR_BOOL)
                target->initial_integer =
                    initializer->as.boolean ? 1U : 0U;
            else if (initializer->kind == EXPR_FLOAT)
                target->initial_floating = initializer->as.floating;
            else if (initializer->kind == EXPR_UNARY &&
                     initializer->as.unary.op == TOK_MINUS &&
                     initializer->as.unary.operand->kind == EXPR_INT)
                target->initial_integer =
                    0U - initializer->as.unary.operand->as.integer;
            else if (initializer->kind == EXPR_UNARY &&
                     initializer->as.unary.op == TOK_MINUS &&
                     initializer->as.unary.operand->kind == EXPR_FLOAT)
                target->initial_floating =
                    -initializer->as.unary.operand->as.floating;
        }
    }
}

static void initialize_interface_dispatches(
    const Module *module, IrModule *ir
) {
    for (size_t i = 0U; i < module->count; ++i)
        if (module->decls[i]->kind == DECL_CLASS &&
            !module->decls[i]->as.structure.is_interface)
            ir->interface_dispatch_count += module->decls[i]
                ->as.structure.interface_implementation_count;
    ir->interface_dispatches = ir_resize(
        NULL, ir->interface_dispatch_count,
        sizeof(*ir->interface_dispatches));
    size_t output = 0U;
    for (size_t i = 0U; i < module->count; ++i) {
        const Decl *owner = module->decls[i];
        if (owner->kind != DECL_CLASS ||
            owner->as.structure.is_interface)
            continue;
        Type *runtime = lang_arena_alloc(
            &ir->lowering_module->arena, sizeof(*runtime));
        memset(runtime, 0, sizeof(*runtime));
        runtime->kind = TYPE_CLASS;
        runtime->name = owner->as.structure.name;
        runtime->declaration = owner;
        IrTypeId runtime_type = ir_intern_type(ir, runtime);
        for (size_t entry = 0U;
             entry < owner->as.structure.interface_implementation_count;
             ++entry) {
            const Decl *contract =
                owner->as.structure.interface_members[entry];
            const Decl *implementation =
                owner->as.structure.interface_implementations[entry];
            const Decl *interface_owner = NULL;
            const char *owner_name = contract->as.function.owner_type;
            for (size_t candidate = 0U;
                 candidate < module->count; ++candidate)
                if (module->decls[candidate]->kind == DECL_CLASS &&
                    module->decls[candidate]->as.structure.is_interface &&
                    module->decls[candidate]->module_name != NULL &&
                    contract->module_name != NULL &&
                    strcmp(module->decls[candidate]->module_name,
                           contract->module_name) == 0 &&
                    strcmp(module->decls[candidate]->as.structure.name,
                           owner_name) == 0) {
                    interface_owner = module->decls[candidate];
                    break;
                }
            if (interface_owner == NULL) continue;
            Type *interface_type = lang_arena_alloc(
                &ir->lowering_module->arena, sizeof(*interface_type));
            memset(interface_type, 0, sizeof(*interface_type));
            interface_type->kind = TYPE_CLASS;
            interface_type->name = interface_owner->as.structure.name;
            interface_type->declaration = interface_owner;
            ir->interface_dispatches[output++] = (IrInterfaceDispatch){
                .interface_type=ir_intern_type(ir, interface_type),
                .runtime_type=runtime_type,
                .interface_function=ir_find_function(ir, contract),
                .target_function=ir_find_function(ir, implementation)
            };
        }
    }
    ir->interface_dispatch_count = output;
}

static void resolve_type_destructors(IrModule *ir) {
    for (size_t type_index = 0U;
         type_index < ir->type_count; ++type_index) {
        IrType *type = &ir->types[type_index];
        if (type->shape != IR_TYPE_STRUCT &&
            type->shape != IR_TYPE_CLASS_REFERENCE &&
            type->shape != IR_TYPE_ENUM &&
            type->shape != IR_TYPE_UNION)
            continue;
        for (size_t function_index = 0U;
             function_index < ir->function_count;
             ++function_index) {
            const IrFunction *function =
                &ir->functions[function_index];
            if (!function->is_destructor ||
                function->parameter_count != 1U ||
                function->parameters[0].type !=
                    (IrTypeId)type_index)
                continue;
            type->destructor_function =
                (IrFunctionId)function_index;
            if (type->shape != IR_TYPE_CLASS_REFERENCE)
                type->drop_policy = IR_DROP_CUSTOM;
            break;
        }
    }
    for (size_t type_index = 0U;
         type_index < ir->type_count; ++type_index) {
        IrType *type = &ir->types[type_index];
        if (type->shape != IR_TYPE_CLASS_REFERENCE ||
            type->destructor_function != IR_INVALID_ID)
            continue;
        for (IrTypeId base = type->base_type;
             base != IR_INVALID_ID && base < ir->type_count;
             base = ir->types[base].base_type)
            if (ir->types[base].destructor_function != IR_INVALID_ID) {
                type->destructor_function =
                    ir->types[base].destructor_function;
                break;
            }
    }
}

bool lang_ir_lower_module(Module *module,
                          const LangTargetInfo *target,
                          LangDiagnostics *diagnostics,
                          IrModule *ir) {
    memset(ir, 0, sizeof(*ir));
    ir->target = *target;
    ir->lowering_module = module;
    ir->lowering_diagnostics = diagnostics;
    initialize_static_fields(module, ir);
    initialize_functions(module, ir);
    resolve_type_copy_functions(ir);
    initialize_interface_dispatches(module, ir);
    for (size_t i = 0U; i < ir->function_count; ++i) {
        IrFunction *output = &ir->functions[i];
        const Function *source = &output->declaration->as.function;
        IrBuilder builder;
        memset(&builder, 0, sizeof(builder));
        builder.source = module;
        builder.diagnostics = diagnostics;
        builder.module = ir;
        builder.function = output;
        output->entry_block = ir_add_block(output);
        builder.current = output->entry_block;
        for (size_t p = 0U; p < source->param_count; ++p) {
            uint32_t local = ir_add_local(
                &builder, source->params[p].name,
                source->params[p].binding_id,
                source->params[p].checked_type,
                source->params[p].mutable_);
            output->locals[local].borrowed =
                parameter_mode_is_reference(output->parameters[p].mode);
            IrInstruction *parameter = ir_append_instruction(
                &builder, IR_OP_PARAMETER,
                output->parameters[p].type, NULL, 0U,
                source->params[p].span);
            if (parameter == NULL) continue;
            parameter->index = (uint32_t)p;
            if (!parameter_mode_is_reference(
                    output->parameters[p].mode)) {
                IrValueId value = parameter->result;
                IrInstruction *store = ir_append_instruction(
                    &builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                    &value, 1U, source->params[p].span);
                if (store != NULL) store->index = local;
            }
        }
        if (source->is_constructor) {
            const Decl *structure =
                source->checked_return_type->declaration;
            for (size_t field = 0U;
                 field < source->constructor_field_count; ++field)
                (void)ir_add_local(
                    &builder,
                    structure->as.structure.fields[field].name,
                    source->constructor_field_binding_ids[field],
                    source->constructor_field_types[field], true);
        }
        if (source->is_abstract_member)
            ir_set_terminator(
                &builder, IR_TERM_TRAP, IR_INVALID_ID,
                IR_INVALID_ID, IR_INVALID_ID, source->span);
        else
            ir_lower_stmt(&builder, source->body);
        if (!ir_current_terminated(&builder)) {
            if (source->is_drop && source->param_count == 1U &&
                source->params[0].checked_type != NULL &&
                source->params[0].checked_type->kind == TYPE_CLASS &&
                source->params[0].checked_type->declaration != NULL) {
                const Decl *owner =
                    source->params[0].checked_type->declaration;
                const Decl *base = owner->as.structure.base_class;
                const Decl *base_destructor = NULL;
                if (base != NULL)
                    for (size_t member = 0U;
                         member < base->as.structure.member_count; ++member)
                        if (base->as.structure.members[member]
                                ->as.function.is_drop) {
                            base_destructor =
                                base->as.structure.members[member];
                            break;
                        }
                if (base_destructor != NULL) {
                    uint32_t local = ir_find_local(
                        &builder, source->params[0].binding_id,
                        source->params[0].span);
                    IrInstruction *receiver = ir_append_instruction(
                        &builder, IR_OP_LOCAL_LOAD,
                        ir_intern_type(
                            ir, source->params[0].checked_type),
                        NULL, 0U, source->span);
                    if (receiver != NULL) receiver->index = local;
                    IrValueId operand = receiver != NULL
                        ? receiver->result : IR_INVALID_ID;
                    IrInstruction *call = ir_append_instruction(
                        &builder, IR_OP_CALL_DIRECT,
                        ir_intern_type(
                            ir, base_destructor->as.function
                                    .checked_return_type),
                        &operand, 1U, source->span);
                    if (call != NULL) {
                        call->index = ir_find_function(
                            ir, base_destructor);
                        call->symbol = base_destructor->as.function.name;
                        call->symbol_length = strlen(call->symbol);
                        call->argument_mode_count = 1U;
                        call->argument_modes = ir_resize(
                            NULL, 1U, sizeof(*call->argument_modes));
                        call->argument_modes[0] = PARAMETER_MODE_VALUE;
                    }
                }
            }
            /*
             * Parameters live outside the function body's lexical block.
             * Body cleanup plans therefore do not contain them. A local drop
             * is defined to destroy the slot only when it still contains an
             * owning value, so a parameter moved on the fallthrough path is
             * already empty.
             */
            for (size_t p = source->param_count; p > 0U; --p) {
                const Type *parameter_type =
                    source->params[p - 1U].checked_type;
                if ((source->is_drop && p == 1U) ||
                    source->params[p - 1U].borrowed ||
                    parameter_type == NULL ||
                    !parameter_type->requires_cleanup)
                    continue;
                uint32_t local = ir_find_local(
                    &builder, source->params[p - 1U].binding_id,
                    source->params[p - 1U].span);
                IrInstruction *drop = ir_append_instruction(
                    &builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                    NULL, 0U, source->params[p - 1U].span);
                if (drop != NULL) drop->index = local;
            }
            const Type *logical_return =
                source->is_async &&
                source->checked_return_type != NULL &&
                source->checked_return_type->kind == TYPE_TASK
                    ? source->checked_return_type->element
                    : source->checked_return_type;
            if (source->is_constructor) {
                const Decl *structure =
                    source->checked_return_type->declaration;
                size_t count = source->constructor_field_count;
                IrValueId *fields = ir_resize(
                    NULL, count, sizeof(*fields));
                uint32_t *labels = ir_resize(
                    NULL, count, sizeof(*labels));
                for (size_t field = 0U; field < count; ++field) {
                    uint32_t local = ir_find_local(
                        &builder,
                        source->constructor_field_binding_ids[field],
                        source->span);
                    IrInstruction *load = ir_append_instruction(
                        &builder, IR_OP_LOCAL_MOVE,
                        ir_intern_type(
                            ir, source->constructor_field_types[field]),
                        NULL, 0U, source->span);
                    if (load != NULL) load->index = local;
                    fields[field] = load != NULL
                        ? load->result : IR_INVALID_ID;
                    labels[field] = (uint32_t)field;
                }
                IrInstruction *value = ir_append_instruction(
                    &builder, IR_OP_AGGREGATE_MAKE,
                    ir_intern_type(ir, source->checked_return_type),
                    fields, count, source->span);
                free(fields);
                if (value != NULL) {
                    value->labels = labels;
                    value->label_count = count;
                    value->symbol = structure->as.structure.name;
                    value->symbol_length = strlen(value->symbol);
                    ir_set_terminator(
                        &builder, IR_TERM_RETURN, value->result,
                        IR_INVALID_ID, IR_INVALID_ID, source->span);
                } else free(labels);
            } else if (logical_return != NULL &&
                logical_return->kind == TYPE_UNIT) {
                IrValueId unit = ir_emit_unit(
                    &builder, source->span,
                    logical_return);
                ir_set_terminator(
                    &builder, IR_TERM_RETURN, unit,
                    IR_INVALID_ID, IR_INVALID_ID, source->span);
            } else {
                /*
                 * The checker has already required a value-returning function
                 * to return on every reachable path. A merge block whose
                 * predecessors all returned is structurally present but
                 * unreachable; terminate it without inventing a typed value.
                 */
                ir_set_terminator(
                    &builder, IR_TERM_TRAP, IR_INVALID_ID,
                    IR_INVALID_ID, IR_INVALID_ID, source->span);
            }
        }
        if (builder.failed) {
            ir->lowering_module = NULL;
            ir->lowering_diagnostics = NULL;
            return false;
        }
        for (size_t b = 0U; b < output->block_count; ++b)
            for (size_t instruction = 0U;
                 instruction < output->blocks[b].instruction_count;
                 ++instruction)
                if (output->blocks[b].instructions[instruction].opcode ==
                    IR_OP_AWAIT)
                    ++output->async_suspension_count;
    }
    resolve_type_destructors(ir);
    /* Frontend links are lowering-only. A completed IR module is closed
     * backend input; keeping these null makes accidental semantic fallback
     * fail immediately during development. */
    for (size_t i = 0U; i < ir->function_count; ++i)
        ir->functions[i].declaration = NULL;
    for (size_t i = 0U; i < ir->static_field_count; ++i)
        ir->static_fields[i].owner_declaration = NULL;
    for (size_t i = 0U; i < ir->type_count; ++i)
        ir->types[i].checked_type = NULL;
    ir->lowering_module = NULL;
    ir->lowering_diagnostics = NULL;
    return diagnostics->count == 0U;
}
