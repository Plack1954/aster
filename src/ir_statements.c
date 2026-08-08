#include "ir_internal.h"

#include <stdint.h>
#include <string.h>

static void lower_finalizers_to(
    IrBuilder *builder, size_t target_count, LangSpan span) {
    size_t original_count = builder->finalizer_count;
    while (builder->finalizer_count > target_count &&
           !ir_current_terminated(builder)) {
        size_t index = builder->finalizer_count - 1U;
        const Stmt *body = builder->finalizers[index].body;
        builder->finalizer_count = index;
        ir_lower_stmt(builder, body);
    }
    if (!ir_current_terminated(builder))
        builder->finalizer_count = original_count;
    (void)span;
}

static void lower_match(IrBuilder *builder, const Stmt *stmt) {
    const Expr *matched_expr = stmt->as.match_.value;
    const Type *matched_type = matched_expr->type;
    IrValueId borrowed_values[64];
    size_t borrowed_count = 0U;
    IrValueId matched = stmt->as.match_.borrowed
        ? lower_borrowed_expr(
              builder, matched_expr,
              borrowed_values, &borrowed_count)
        : ir_lower_expr(builder, matched_expr);
    uint32_t matched_local = ir_add_local(
        builder, "<match>", 0U, matched_type, false);
    IrInstruction *store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &matched, 1U, matched_expr->span);
    if (store != NULL) store->index = matched_local;
    if (stmt->as.match_.borrowed)
        builder->function->locals[matched_local].borrowed = true;

    IrBlockId merge = ir_add_block(builder->function);
    bool has_fallthrough_arm = false;
    for (size_t a = 0U; a < stmt->as.match_.arm_count; ++a) {
        const MatchArm *arm = &stmt->as.match_.arms[a];
        uint32_t binding = IR_INVALID_ID;
        IrInstruction *matches = ir_emit_local_enum_operation(
            builder, IR_OP_LOCAL_ENUM_IS,
            ir_intern_type(builder->module, &ir_bool_type),
            matched_local, matched_type, arm->variant, arm->span);
        if (matches == NULL) return;
        IrBlockId arm_block = ir_add_block(builder->function);
        IrBlockId next_arm = ir_add_block(builder->function);
        ir_set_terminator(builder, IR_TERM_BRANCH, matches->result,
                       arm_block, next_arm, arm->span);

        builder->current = arm_block;
        if (arm->binding != NULL && arm->binding_type != NULL) {
            IrInstruction *payload;
            if (stmt->as.match_.borrowed) {
                IrInstruction *load = ir_append_instruction(
                    builder, IR_OP_LOCAL_LOAD,
                    ir_intern_type(builder->module, matched_type),
                    NULL, 0U, arm->span);
                if (load != NULL) load->index = matched_local;
                payload = load != NULL
                    ? ir_emit_enum_payload_borrow(
                          builder,
                          ir_intern_type(
                              builder->module, arm->binding_type),
                          load->result, matched_type,
                          arm->variant, arm->span)
                    : NULL;
            } else {
                payload = ir_emit_local_enum_operation(
                    builder, IR_OP_LOCAL_ENUM_PAYLOAD_MOVE,
                    ir_intern_type(builder->module, arm->binding_type),
                    matched_local, matched_type,
                    arm->variant, arm->span);
            }
            if (payload == NULL) return;
            binding = ir_add_local(
                builder, arm->binding, arm->binding_id,
                arm->binding_type, false);
            if (stmt->as.match_.borrowed)
                builder->function->locals[binding].borrowed = true;
            IrValueId value = payload->result;
            IrInstruction *binding_store = ir_append_instruction(
                builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                &value, 1U, arm->span);
            if (binding_store != NULL)
                binding_store->index = binding;
        } else if (!stmt->as.match_.borrowed) {
            IrInstruction *drop = ir_append_instruction(
                builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                NULL, 0U, arm->span);
            if (drop != NULL) drop->index = matched_local;
        }
        ir_lower_stmt(builder, arm->body);
        if (!ir_current_terminated(builder)) {
            /* The payload binding is introduced outside the parsed arm body,
             * so that body's lexical cleanup plan cannot contain it. */
            if (!stmt->as.match_.borrowed &&
                binding != IR_INVALID_ID &&
                ir_type_needs_cleanup(
                    builder->module,
                    builder->function->locals[binding].type)) {
                IrInstruction *drop = ir_append_instruction(
                    builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                    NULL, 0U, arm->span);
                if (drop != NULL) drop->index = binding;
            }
            has_fallthrough_arm = true;
            ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                           merge, IR_INVALID_ID, arm->span);
        }
        builder->current = next_arm;
    }
    if (!stmt->as.match_.borrowed) {
        IrInstruction *drop = ir_append_instruction(
            builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
            NULL, 0U, stmt->span);
        if (drop != NULL) drop->index = matched_local;
    }
    ir_set_terminator(builder, IR_TERM_TRAP, IR_INVALID_ID,
                   IR_INVALID_ID, IR_INVALID_ID, stmt->span);
    builder->current = merge;
    if (!has_fallthrough_arm)
        ir_set_terminator(builder, IR_TERM_TRAP, IR_INVALID_ID,
                       IR_INVALID_ID, IR_INVALID_ID, stmt->span);
}

static void lower_for(IrBuilder *builder, const Stmt *stmt) {
    if (stmt->as.for_.range_end != NULL) {
        const Type *element_type =
            stmt->as.for_.iterable->type;
        IrTypeId element_ir =
            ir_intern_type(builder->module, element_type);
        IrValueId start = ir_lower_expr(
            builder, stmt->as.for_.iterable);
        IrValueId end = ir_lower_expr(
            builder, stmt->as.for_.range_end);
        uint32_t counter_local = ir_add_synthetic_local(
            builder, "<range-counter>", element_ir);
        uint32_t end_local = ir_add_synthetic_local(
            builder, "<range-end>", element_ir);
        uint32_t item_local = ir_add_local(
            builder, stmt->as.for_.name,
            stmt->as.for_.binding_id, element_type, false);
        IrInstruction *store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &start, 1U, stmt->span);
        if (store != NULL) store->index = counter_local;
        store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &end, 1U, stmt->span);
        if (store != NULL) store->index = end_local;

        IrBlockId condition = ir_add_block(builder->function);
        IrBlockId body = ir_add_block(builder->function);
        IrBlockId exit_block = ir_add_block(builder->function);
        ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                       condition, IR_INVALID_ID, stmt->span);

        builder->current = condition;
        IrInstruction *counter = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, element_ir,
            NULL, 0U, stmt->span);
        if (counter != NULL) counter->index = counter_local;
        IrInstruction *limit = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, element_ir,
            NULL, 0U, stmt->span);
        if (limit != NULL) limit->index = end_local;
        IrValueId comparison_operands[2] = {
            counter != NULL ? counter->result : IR_INVALID_ID,
            limit != NULL ? limit->result : IR_INVALID_ID
        };
        IrInstruction *less = ir_append_instruction(
            builder, IR_OP_LESS,
            ir_intern_type(builder->module, &ir_bool_type),
            comparison_operands, 2U, stmt->span);
        ir_set_terminator(
            builder, IR_TERM_BRANCH,
            less != NULL ? less->result : IR_INVALID_ID,
            body, exit_block, stmt->span);

        if (builder->loop_count >=
            sizeof(builder->loops) / sizeof(builder->loops[0])) {
            lang_diag(builder->diagnostics, stmt->span,
                      "IR loop nesting limit exceeded");
            builder->failed = true;
            builder->current = exit_block;
            return;
        }
        builder->loops[builder->loop_count++] =
            (IrLoop){exit_block, condition, builder->finalizer_count};
        builder->current = body;
        counter = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, element_ir,
            NULL, 0U, stmt->span);
        if (counter != NULL) counter->index = counter_local;
        IrValueId item =
            counter != NULL ? counter->result : IR_INVALID_ID;
        store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &item, 1U, stmt->span);
        if (store != NULL) store->index = item_local;

        IrInstruction *increment_source = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, element_ir,
            NULL, 0U, stmt->span);
        if (increment_source != NULL)
            increment_source->index = counter_local;
        IrInstruction *one = ir_append_instruction(
            builder, IR_OP_CONST_INT, element_ir,
            NULL, 0U, stmt->span);
        if (one != NULL) one->integer = 1U;
        IrValueId add_operands[2] = {
            increment_source != NULL
                ? increment_source->result : IR_INVALID_ID,
            one != NULL ? one->result : IR_INVALID_ID
        };
        IrInstruction *next = ir_append_instruction(
            builder, IR_OP_ADD_CHECKED, element_ir,
            add_operands, 2U, stmt->span);
        IrValueId next_value =
            next != NULL ? next->result : IR_INVALID_ID;
        store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &next_value, 1U, stmt->span);
        if (store != NULL) store->index = counter_local;

        ir_lower_stmt(builder, stmt->as.for_.body);
        --builder->loop_count;
        if (!ir_current_terminated(builder))
            ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                           condition, IR_INVALID_ID, stmt->span);
        builder->current = exit_block;
        return;
    }
    const Expr *iterable_expr = stmt->as.for_.iterable;
    const Type *iterable_type = iterable_expr->type;
    const Type *element_type = stmt->as.for_.element_type;
    IrTypeId iterable_ir =
        ir_intern_type(builder->module, iterable_type);
    IrTypeId element_ir =
        ir_intern_type(builder->module, element_type);
    IrTypeId iterator_ir = ir_intern_iterator_type(
        builder->module, iterable_ir, element_ir);

    IrInstruction *begin;
    if (stmt->as.for_.borrowed) {
        IrValueId field_value = IR_INVALID_ID;
        const Expr *field_object = NULL;
        if (iterable_expr->kind == EXPR_FIELD)
            field_object = iterable_expr->as.field.object;
        if (field_object != NULL) {
            uint32_t field = ir_field_index(
                field_object->type, iterable_expr->as.field.field);
            IrInstruction *borrow = ir_append_instruction(
                builder, IR_OP_LOCAL_FIELD_BORROW, iterable_ir,
                NULL, 0U, iterable_expr->span);
            if (borrow != NULL) {
                borrow->index = ir_find_local(
                    builder, field_object->resolved_local_id,
                    field_object->span);
                borrow->auxiliary = field;
                borrow->symbol = iterable_expr->as.field.field;
                borrow->symbol_length = strlen(borrow->symbol);
                field_value = borrow->result;
            }
        }
        begin = ir_append_instruction(
            builder, IR_OP_BORROWED_ITERATOR_BEGIN,
            iterator_ir,
            field_value != IR_INVALID_ID ? &field_value : NULL,
            field_value != IR_INVALID_ID ? 1U : 0U, stmt->span);
        if (begin != NULL && field_object == NULL)
            begin->index = ir_find_local(
                builder, iterable_expr->resolved_local_id,
                iterable_expr->span);
    } else {
        IrValueId iterable = ir_lower_expr(builder, iterable_expr);
        begin = ir_append_instruction(
            builder, IR_OP_ITERATOR_BEGIN, iterator_ir,
            &iterable, 1U, stmt->span);
    }
    if (begin == NULL) return;
    uint32_t iterator_local = ir_add_synthetic_local(
        builder, "<iterator>", iterator_ir);
    IrValueId iterator = begin->result;
    IrInstruction *iterator_store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &iterator, 1U, stmt->span);
    if (iterator_store != NULL)
        iterator_store->index = iterator_local;

    uint32_t item_local = ir_add_local(
        builder, stmt->as.for_.name,
        stmt->as.for_.binding_id, element_type, true);
    builder->function->locals[item_local].borrowed =
        stmt->as.for_.borrowed;
    IrBlockId condition = ir_add_block(builder->function);
    IrBlockId body = ir_add_block(builder->function);
    IrBlockId cleanup = ir_add_block(builder->function);
    IrBlockId exit_block = ir_add_block(builder->function);
    ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                   condition, IR_INVALID_ID, stmt->span);

    builder->current = condition;
    IrInstruction *has_next = ir_append_instruction(
        builder, IR_OP_LOCAL_ITERATOR_HAS_NEXT,
        ir_intern_type(builder->module, &ir_bool_type),
        NULL, 0U, stmt->span);
    if (has_next == NULL) return;
    has_next->index = iterator_local;
    ir_set_terminator(builder, IR_TERM_BRANCH, has_next->result,
                   body, cleanup, stmt->span);

    if (builder->loop_count >=
        sizeof(builder->loops) / sizeof(builder->loops[0])) {
        lang_diag(builder->diagnostics, stmt->span,
                  "IR loop nesting limit exceeded");
        builder->failed = true;
        builder->current = exit_block;
        return;
    }
    builder->loops[builder->loop_count++] =
        (IrLoop){cleanup, condition, builder->finalizer_count};
    builder->current = body;
    IrInstruction *next = ir_append_instruction(
        builder, IR_OP_LOCAL_ITERATOR_NEXT,
        element_ir, NULL, 0U, stmt->span);
    if (next != NULL) next->index = iterator_local;
    if (next != NULL) {
        IrValueId item = next->result;
        IrInstruction *item_store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &item, 1U, stmt->span);
        if (item_store != NULL) item_store->index = item_local;
    }
    ir_lower_stmt(builder, stmt->as.for_.body);
    --builder->loop_count;
    if (!ir_current_terminated(builder)) {
        if (!stmt->as.for_.borrowed &&
            ir_type_needs_cleanup(builder->module, element_ir)) {
            IrInstruction *drop = ir_append_instruction(
                builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                NULL, 0U, stmt->span);
            if (drop != NULL) drop->index = item_local;
        }
        ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                       condition, IR_INVALID_ID, stmt->span);
    }

    builder->current = cleanup;
    IrInstruction *drop_iterator = ir_append_instruction(
        builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
        NULL, 0U, stmt->span);
    if (drop_iterator != NULL)
        drop_iterator->index = iterator_local;
    ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                   exit_block, IR_INVALID_ID, stmt->span);
    builder->current = exit_block;
}

static void lower_c_for(IrBuilder *builder, const Stmt *stmt) {
    if (stmt->as.c_for.initializer != NULL)
        ir_lower_stmt(builder, stmt->as.c_for.initializer);

    IrBlockId condition_block = ir_add_block(builder->function);
    IrBlockId body_block = ir_add_block(builder->function);
    IrBlockId increment_block = ir_add_block(builder->function);
    IrBlockId cleanup_block = ir_add_block(builder->function);
    IrBlockId exit_block = ir_add_block(builder->function);
    ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                   condition_block, IR_INVALID_ID, stmt->span);

    builder->current = condition_block;
    IrValueId condition;
    if (stmt->as.c_for.condition != NULL) {
        condition = ir_lower_expr(builder, stmt->as.c_for.condition);
    } else {
        IrInstruction *always = ir_append_instruction(
            builder, IR_OP_CONST_BOOL,
            ir_intern_type(builder->module, &ir_bool_type),
            NULL, 0U, stmt->span);
        if (always != NULL) always->integer = 1U;
        condition = always != NULL
                  ? always->result : IR_INVALID_ID;
    }
    ir_set_terminator(builder, IR_TERM_BRANCH, condition,
                   body_block, cleanup_block, stmt->span);

    if (builder->loop_count >=
        sizeof(builder->loops) / sizeof(builder->loops[0])) {
        lang_diag(builder->diagnostics, stmt->span,
                  "IR loop nesting limit exceeded");
        builder->failed = true;
        builder->current = exit_block;
        return;
    }
    builder->loops[builder->loop_count++] =
        (IrLoop){cleanup_block, increment_block,
                 builder->finalizer_count};
    builder->current = body_block;
    ir_lower_stmt(builder, stmt->as.c_for.body);
    --builder->loop_count;
    if (!ir_current_terminated(builder))
        ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                       increment_block, IR_INVALID_ID, stmt->span);

    builder->current = increment_block;
    if (stmt->as.c_for.increment != NULL) {
        IrValueId value = ir_lower_expr(
            builder, stmt->as.c_for.increment);
        if (!ir_current_terminated(builder))
            (void)ir_append_instruction(
                builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
                &value, 1U, stmt->span);
    }
    if (!ir_current_terminated(builder))
        ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                       condition_block, IR_INVALID_ID, stmt->span);

    builder->current = cleanup_block;
    ir_emit_cleanup(builder, &stmt->exit_cleanup, stmt->span);
    if (!ir_current_terminated(builder))
        ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                       exit_block, IR_INVALID_ID, stmt->span);
    builder->current = exit_block;
}

void ir_lower_stmt(IrBuilder *builder, const Stmt *stmt) {
    if (ir_current_terminated(builder)) return;
    switch (stmt->kind) {
        case STMT_DELETE: {
            IrValueId value = ir_lower_expr(builder, stmt->as.delete_value);
            IrInstruction *drop = ir_append_instruction(
                builder, IR_OP_CLASS_DELETE, IR_INVALID_ID,
                &value, 1U, stmt->span);
            (void)drop;
            break;
        }
        case STMT_LET: {
            IrValueId value = ir_lower_expr(builder, stmt->as.let.value);
            if (strcmp(stmt->as.let.name, "_") == 0) {
                (void)ir_append_instruction(
                    builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
                    &value, 1U, stmt->span);
                break;
            }
            uint32_t local = ir_add_local(
                builder, stmt->as.let.name, stmt->as.let.binding_id,
                stmt->as.let.checked_type, stmt->as.let.mutable_);
            IrInstruction *store = ir_append_instruction(
                builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                &value, 1U, stmt->span);
            if (store != NULL) store->index = local;
            break;
        }
        case STMT_DESTRUCTURE: {
            const Expr *source = stmt->as.destructure.value;
            uint32_t source_local = ir_find_local(
                builder, source->resolved_local_id, source->span);
            const Decl *structure = source->type->declaration;
            IrTypeId source_type = ir_intern_type(
                builder->module, source->type);
            bool nontrivial = source->type->kind != TYPE_CLASS &&
                source->type->kind != TYPE_RAW_POINTER &&
                (source->type->requires_cleanup || source->type->managed ||
                 ir_type_requires_custom_copy(builder, source->type));
            if (nontrivial) {
                IrInstruction *transfer = ir_append_instruction(
                    builder, IR_OP_LOCAL_TRANSFER, source_type,
                    NULL, 0U, source->span);
                if (transfer == NULL) break;
                transfer->index = source_local;
                ir_set_transfer_exception_context(
                    builder, transfer, &source->error_cleanup);
                source_local = ir_add_synthetic_local(
                    builder, "<deconstruction>", source_type);
                IrValueId transferred = transfer->result;
                IrInstruction *store = ir_append_instruction(
                    builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                    &transferred, 1U, source->span);
                if (store != NULL) store->index = source_local;
            }
            for (size_t field = 0U;
                 field < stmt->as.destructure.count; ++field) {
                Type *field_type =
                    stmt->as.destructure.checked_types[field];
                const char *field_name =
                    structure->as.structure.fields[field].name;
                bool move = field_type != NULL &&
                    field_type->kind != TYPE_CLASS &&
                    field_type->kind != TYPE_RAW_POINTER &&
                    (field_type->requires_cleanup || field_type->managed ||
                     ir_type_requires_custom_copy(builder, field_type));
                IrInstruction *field_value = ir_append_instruction(
                    builder, move ? IR_OP_LOCAL_FIELD_MOVE
                                  : IR_OP_LOCAL_FIELD_GET,
                    ir_intern_type(builder->module, field_type),
                    NULL, 0U, stmt->span);
                if (field_value == NULL) break;
                field_value->index = source_local;
                field_value->auxiliary = (uint32_t)field;
                field_value->symbol = field_name;
                field_value->symbol_length = strlen(field_name);
                uint32_t destination = ir_add_local(
                    builder, stmt->as.destructure.names[field],
                    stmt->as.destructure.binding_ids[field],
                    field_type, true);
                IrValueId value = field_value->result;
                IrInstruction *store = ir_append_instruction(
                    builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                    &value, 1U, stmt->span);
                if (store != NULL) store->index = destination;
            }
            if (nontrivial) {
                IrInstruction *invalidate = ir_append_instruction(
                    builder, IR_OP_LOCAL_INVALIDATE, IR_INVALID_ID,
                    NULL, 0U, stmt->span);
                if (invalidate != NULL) {
                    invalidate->index = source_local;
                    /* The VM represents value aggregates with a heap shell.
                     * All owned fields have moved, so reclaim that shell
                     * without invoking the consumed value's destructor. */
                    invalidate->auxiliary = 1U;
                }
            }
            break;
        }
        case STMT_EXPR: {
            IrValueId value =
                builder->element_count != 0U &&
                stmt->as.expression->kind == EXPR_ELEMENT
                ? ir_lower_element_with_parent(
                      builder, stmt->as.expression,
                      builder->elements[
                          builder->element_count - 1U].local)
                : ir_lower_expr(builder, stmt->as.expression);
            if (!ir_current_terminated(builder) &&
                builder->element_count != 0U)
                ir_append_element_child(
                    builder, value, stmt->as.expression->type,
                    stmt->span);
            else if (!ir_current_terminated(builder))
                (void)ir_append_instruction(
                    builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
                    &value, 1U, stmt->span);
            break;
        }
        case STMT_RETURN: {
            const Expr *return_expr = stmt->as.return_value;
            const Expr *return_local_expr = return_expr;
            uint32_t returned_local = IR_INVALID_ID;
            IrValueId value;
            if (builder->finalizer_count != 0U) {
                IrValueId pending_return = return_expr != NULL
                    ? ir_lower_expr(builder, return_expr)
                    : ir_emit_unit(
                        builder, stmt->span,
                        builder->function->declaration
                            ->as.function.checked_return_type);
                IrTypeId return_type = ir_intern_type(
                    builder->module,
                    builder->function->declaration
                        ->as.function.checked_return_type);
                returned_local = ir_add_synthetic_local(
                    builder, "<pending-return>", return_type);
                IrInstruction *store = ir_append_instruction(
                    builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                    &pending_return, 1U, stmt->span);
                if (store != NULL) store->index = returned_local;
                lower_finalizers_to(builder, 0U, stmt->span);
                if (ir_current_terminated(builder)) break;
                IrInstruction *move = ir_append_instruction(
                    builder, IR_OP_LOCAL_MOVE, return_type,
                    NULL, 0U, stmt->span);
                if (move != NULL) move->index = returned_local;
                value = move != NULL ? move->result : IR_INVALID_ID;
            } else
            if (return_expr != NULL &&
                return_expr->kind == EXPR_CALL &&
                return_expr->as.call.callee->kind == EXPR_NAME &&
                strcmp(return_expr->as.call.callee->as.name,
                       "StringBuilder::ToString") == 0 &&
                return_expr->as.call.arguments.count == 1U &&
                return_expr->as.call.arguments.items[0]->kind == EXPR_NAME) {
                const Expr *builder_expr =
                    return_expr->as.call.arguments.items[0];
                returned_local = ir_find_local(
                    builder, builder_expr->resolved_local_id,
                    builder_expr->span);
                IrTypeId builder_type = ir_intern_type(
                    builder->module, builder_expr->type);
                IrInstruction *move = ir_append_instruction(
                    builder, IR_OP_LOCAL_MOVE, builder_type,
                    NULL, 0U, builder_expr->span);
                if (move != NULL) move->index = returned_local;
                IrValueId moved = move != NULL
                    ? move->result : IR_INVALID_ID;
                value = ir_emit_synthetic_native_call(
                    builder, "StringBuilder::Finish",
                    return_expr->type,
                    move != NULL ? &moved : NULL,
                    move != NULL ? 1U : 0U,
                    0U, return_expr->span);
            } else
            if (return_local_expr != NULL &&
                return_local_expr->kind == EXPR_NAME &&
                return_local_expr->resolved_local_id != 0U &&
                ir_type_needs_cleanup(
                    builder->module,
                    ir_intern_type(builder->module,
                                   return_local_expr->type))) {
                returned_local = ir_find_local(
                    builder, return_local_expr->resolved_local_id,
                    return_local_expr->span);
                if (builder->function->locals[returned_local].borrowed) {
                    returned_local = IR_INVALID_ID;
                    value = ir_lower_expr(builder, return_expr);
                } else {
                    IrInstruction *move = ir_append_instruction(
                        builder, IR_OP_LOCAL_MOVE,
                        ir_intern_type(builder->module,
                                       return_local_expr->type),
                        NULL, 0U, return_local_expr->span);
                    if (move != NULL) move->index = returned_local;
                    value = move != NULL
                        ? move->result : IR_INVALID_ID;
                }
            } else {
                value = return_expr != NULL
                      ? ir_lower_expr(builder, return_expr)
                      : ir_emit_unit(
                            builder, stmt->span,
                            builder->function->is_async
                                ? builder->function->declaration
                                      ->as.function.checked_return_type
                                      ->element
                                : builder->function->declaration
                                      ->as.function.checked_return_type);
            }
            ir_emit_function_cleanup_except(
                builder, stmt->span, returned_local);
            ir_set_terminator(builder, IR_TERM_RETURN, value,
                           IR_INVALID_ID, IR_INVALID_ID, stmt->span);
            break;
        }
        case STMT_THROW: {
            IrValueId value = ir_lower_expr(
                builder, stmt->as.throw_value);
            IrInstruction *set = ir_append_instruction(
                builder, IR_OP_EXCEPTION_SET, IR_INVALID_ID,
                &value, 1U, stmt->span);
            (void)set;
            if (builder->exception_count != 0U) {
                ir_emit_temporary_cleanups(builder, stmt->span);
                ir_emit_cleanup(
                    builder, &stmt->exit_cleanup, stmt->span);
                ir_set_terminator(
                    builder, IR_TERM_JUMP, IR_INVALID_ID,
                    builder->exceptions[
                        builder->exception_count - 1U].handler,
                    IR_INVALID_ID, stmt->span);
            } else {
                ir_emit_function_cleanup(builder, stmt->span);
                ir_set_terminator(
                    builder, IR_TERM_PROPAGATE_EXCEPTION,
                    IR_INVALID_ID, IR_INVALID_ID, IR_INVALID_ID,
                    stmt->span);
            }
            break;
        }
        case STMT_TRY: {
            bool has_catch = stmt->as.try_.catch_body != NULL;
            bool has_finally = stmt->as.try_.finally_body != NULL;
            IrBlockId catch_block = has_catch
                ? ir_add_block(builder->function) : IR_INVALID_ID;
            IrBlockId exceptional_finally = has_finally
                ? ir_add_block(builder->function) : IR_INVALID_ID;
            IrBlockId normal_finally = has_finally
                ? ir_add_block(builder->function) : IR_INVALID_ID;
            IrBlockId merge = ir_add_block(builder->function);
            if (builder->exception_count >= 32U) {
                lang_diag(builder->diagnostics, stmt->span,
                          "IR exception nesting limit exceeded");
                builder->failed = true;
                break;
            }
            builder->exceptions[builder->exception_count++] =
                (IrExceptionContext){
                    has_catch ? catch_block : exceptional_finally
                };
            if (has_finally) {
                if (builder->finalizer_count >= 32U) {
                    lang_diag(builder->diagnostics, stmt->span,
                              "IR finalizer nesting limit exceeded");
                    builder->failed = true;
                    break;
                }
                builder->finalizers[builder->finalizer_count++] =
                    (IrFinalizerContext){stmt->as.try_.finally_body};
            }
            ir_lower_stmt(builder, stmt->as.try_.body);
            if (has_finally) --builder->finalizer_count;
            --builder->exception_count;
            if (!ir_current_terminated(builder))
                ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                                  has_finally ? normal_finally : merge,
                                  IR_INVALID_ID, stmt->span);

            if (has_catch) {
                builder->current = catch_block;
                IrInstruction *match = ir_append_instruction(
                    builder, IR_OP_EXCEPTION_MATCH,
                    ir_intern_type(builder->module, &ir_bool_type),
                    NULL, 0U, stmt->span);
                if (match != NULL) {
                    match->symbol = stmt->as.try_.catch_type->name;
                    match->symbol_length = strlen(match->symbol);
                }
                IrBlockId matched_catch = ir_add_block(builder->function);
                IrBlockId unmatched_catch = ir_add_block(builder->function);
                ir_set_terminator(
                    builder, IR_TERM_BRANCH,
                    match != NULL ? match->result : IR_INVALID_ID,
                    matched_catch, unmatched_catch, stmt->span);

                builder->current = unmatched_catch;
                if (has_finally)
                    ir_set_terminator(
                        builder, IR_TERM_JUMP, IR_INVALID_ID,
                        exceptional_finally, IR_INVALID_ID, stmt->span);
                else if (builder->exception_count != 0U) {
                    ir_emit_cleanup(
                        builder, &stmt->exit_cleanup, stmt->span);
                    ir_set_terminator(
                        builder, IR_TERM_JUMP, IR_INVALID_ID,
                        builder->exceptions[
                            builder->exception_count - 1U].handler,
                        IR_INVALID_ID, stmt->span);
                } else {
                    ir_emit_function_cleanup(builder, stmt->span);
                    ir_set_terminator(
                        builder, IR_TERM_PROPAGATE_EXCEPTION,
                        IR_INVALID_ID, IR_INVALID_ID, IR_INVALID_ID,
                        stmt->span);
                }

                builder->current = matched_catch;
                IrInstruction *take = ir_append_instruction(
                    builder, IR_OP_EXCEPTION_TAKE,
                    ir_intern_type(builder->module,
                                   stmt->as.try_.catch_type),
                    NULL, 0U, stmt->span);
                uint32_t catch_local = IR_INVALID_ID;
                if (take != NULL) {
                    catch_local = ir_add_local(
                        builder, stmt->as.try_.catch_name,
                        stmt->as.try_.catch_binding_id,
                        stmt->as.try_.catch_type, false);
                    IrValueId caught = take->result;
                    IrInstruction *store = ir_append_instruction(
                        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                        &caught, 1U, stmt->span);
                    if (store != NULL) store->index = catch_local;
                }
                if (has_finally) {
                    builder->exceptions[builder->exception_count++] =
                        (IrExceptionContext){exceptional_finally};
                    builder->finalizers[builder->finalizer_count++] =
                        (IrFinalizerContext){stmt->as.try_.finally_body};
                }
                ir_lower_stmt(builder, stmt->as.try_.catch_body);
                if (has_finally) {
                    --builder->finalizer_count;
                    --builder->exception_count;
                }
                if (!ir_current_terminated(builder))
                {
                    /* Like switch payloads, the catch binding is outside the
                     * parsed catch-body block and needs an explicit normal
                     * scope-exit drop. */
                    if (catch_local != IR_INVALID_ID &&
                        ir_type_needs_cleanup(
                            builder->module,
                            builder->function->locals[catch_local].type)) {
                        IrInstruction *drop = ir_append_instruction(
                            builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                            NULL, 0U, stmt->span);
                        if (drop != NULL) drop->index = catch_local;
                    }
                    ir_set_terminator(
                        builder, IR_TERM_JUMP, IR_INVALID_ID,
                        has_finally ? normal_finally : merge,
                        IR_INVALID_ID, stmt->span);
                }
            }

            if (has_finally) {
                builder->current = normal_finally;
                ir_lower_stmt(builder, stmt->as.try_.finally_body);
                if (!ir_current_terminated(builder))
                    ir_set_terminator(builder, IR_TERM_JUMP,
                                      IR_INVALID_ID, merge,
                                      IR_INVALID_ID, stmt->span);

                builder->current = exceptional_finally;
                ir_lower_stmt(builder, stmt->as.try_.finally_body);
                if (!ir_current_terminated(builder)) {
                    if (builder->exception_count != 0U) {
                        ir_emit_temporary_cleanups(builder, stmt->span);
                        ir_emit_cleanup(
                            builder, &stmt->exit_cleanup, stmt->span);
                        ir_set_terminator(
                            builder, IR_TERM_JUMP, IR_INVALID_ID,
                            builder->exceptions[
                                builder->exception_count - 1U].handler,
                            IR_INVALID_ID, stmt->span);
                    } else {
                        ir_emit_function_cleanup(builder, stmt->span);
                        ir_set_terminator(
                            builder, IR_TERM_PROPAGATE_EXCEPTION,
                            IR_INVALID_ID, IR_INVALID_ID,
                            IR_INVALID_ID, stmt->span);
                    }
                }
            }
            builder->current = merge;
            break;
        }
        case STMT_IF: {
            IrValueId condition = ir_lower_expr(
                builder, stmt->as.if_.condition);
            IrBlockId then_block = ir_add_block(builder->function);
            IrBlockId else_block = ir_add_block(builder->function);
            IrBlockId merge_block = ir_add_block(builder->function);
            ir_set_terminator(builder, IR_TERM_BRANCH, condition,
                           then_block, else_block, stmt->span);
            builder->current = then_block;
            ir_lower_stmt(builder, stmt->as.if_.then_branch);
            if (!ir_current_terminated(builder))
                ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                               merge_block, IR_INVALID_ID, stmt->span);
            builder->current = else_block;
            if (stmt->as.if_.else_branch != NULL)
                ir_lower_stmt(builder, stmt->as.if_.else_branch);
            if (!ir_current_terminated(builder))
                ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                               merge_block, IR_INVALID_ID, stmt->span);
            builder->current = merge_block;
            break;
        }
        case STMT_WHILE: {
            IrBlockId condition_block = ir_add_block(builder->function);
            IrBlockId body_block = ir_add_block(builder->function);
            IrBlockId exit_block = ir_add_block(builder->function);
            ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                           condition_block, IR_INVALID_ID, stmt->span);
            builder->current = condition_block;
            IrValueId condition = ir_lower_expr(
                builder, stmt->as.while_.condition);
            ir_set_terminator(builder, IR_TERM_BRANCH, condition,
                           body_block, exit_block, stmt->span);
            if (builder->loop_count >=
                sizeof(builder->loops) / sizeof(builder->loops[0])) {
                lang_diag(builder->diagnostics, stmt->span,
                          "IR loop nesting limit exceeded");
                builder->failed = true;
                builder->current = exit_block;
                break;
            }
            builder->loops[builder->loop_count++] =
                (IrLoop){exit_block, condition_block,
                         builder->finalizer_count};
            builder->current = body_block;
            ir_lower_stmt(builder, stmt->as.while_.body);
            --builder->loop_count;
            if (!ir_current_terminated(builder))
                ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                               condition_block, IR_INVALID_ID, stmt->span);
            builder->current = exit_block;
            break;
        }
        case STMT_FOR:
            lower_for(builder, stmt);
            break;
        case STMT_C_FOR:
            lower_c_for(builder, stmt);
            break;
        case STMT_MATCH:
            lower_match(builder, stmt);
            break;
        case STMT_BREAK:
        case STMT_CONTINUE: {
            if (builder->loop_count == 0U) {
                lang_diag(builder->diagnostics, stmt->span,
                          "internal IR error: loop exit outside loop");
                builder->failed = true;
                break;
            }
            ir_emit_cleanup(builder, &stmt->exit_cleanup, stmt->span);
            ir_emit_element_exit_cleanup(
                builder, builder->loop_count, stmt->span);
            IrLoop *loop = &builder->loops[builder->loop_count - 1U];
            lower_finalizers_to(
                builder, loop->finalizer_count, stmt->span);
            if (ir_current_terminated(builder)) break;
            ir_set_terminator(
                builder, IR_TERM_JUMP, IR_INVALID_ID,
                stmt->kind == STMT_BREAK
                    ? loop->break_target : loop->continue_target,
                IR_INVALID_ID, stmt->span);
            break;
        }
        case STMT_BLOCK:
            for (size_t i = 0U; i < stmt->as.block.count; ++i) {
                ir_lower_stmt(builder, stmt->as.block.items[i]);
                if (ir_current_terminated(builder)) break;
            }
            if (!ir_current_terminated(builder))
                ir_emit_cleanup(builder, &stmt->exit_cleanup, stmt->span);
            break;
        case STMT_UNSAFE:
            ir_lower_stmt(builder, stmt->as.unsafe_body);
            break;
        default:
            lang_diag(builder->diagnostics, stmt->span,
                      "IR foundation does not yet lower this statement");
            builder->failed = true;
            break;
    }
}
