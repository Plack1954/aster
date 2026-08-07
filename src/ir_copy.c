#include "internal.h"
#include "ir_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool ir_type_requires_custom_copy_inner(
    IrBuilder *builder, const Type *value_type,
    const Type **seen, size_t seen_count
) {
    if (value_type == NULL) return false;
    if (type_copy_constructor(value_type) != NULL) return true;
    if (value_type->kind == TYPE_ARRAY)
        return ir_type_requires_custom_copy_inner(
            builder, value_type->element, seen, seen_count);
    if (value_type->kind == TYPE_VEC ||
        value_type->kind == TYPE_STACK ||
        value_type->kind == TYPE_QUEUE)
        return ir_type_requires_custom_copy_inner(
            builder, value_type->element, seen, seen_count);
    if (value_type->kind == TYPE_OPTION)
        return ir_type_requires_custom_copy_inner(
            builder, value_type->element, seen, seen_count);
    if (value_type->kind == TYPE_RESULT)
        return ir_type_requires_custom_copy_inner(
                   builder, value_type->element, seen, seen_count) ||
               ir_type_requires_custom_copy_inner(
                   builder, value_type->error_type, seen, seen_count);
    if (value_type->kind == TYPE_DICTIONARY)
        return ir_type_requires_custom_copy_inner(
                   builder, value_type->element, seen, seen_count) ||
               ir_type_requires_custom_copy_inner(
                   builder, value_type->error_type, seen, seen_count);
    if (value_type->kind == TYPE_HASH_SET)
        return ir_type_requires_custom_copy_inner(
            builder, value_type->element, seen, seen_count);
    if (value_type->kind != TYPE_NAMED ||
        value_type->declaration == NULL ||
        (value_type->declaration->kind != DECL_STRUCT &&
         (value_type->declaration->kind != DECL_ENUM ||
          !value_type->declaration->as.enumeration.is_union)))
        return false;
    for (size_t i = 0U; i < seen_count; ++i)
        if (same_ir_type_identity(seen[i], value_type)) return false;
    if (seen_count >= 64U) return false;
    const Type *next_seen[64];
    for (size_t i = 0U; i < seen_count; ++i)
        next_seen[i] = seen[i];
    next_seen[seen_count++] = value_type;
    size_t field_count = value_type->declaration->kind == DECL_STRUCT
        ? value_type->declaration->as.structure.field_count
        : value_type->declaration->as.enumeration.variant_count;
    for (size_t field = 0U; field < field_count; ++field) {
        Type *field_type = lang_checker_resolve_aggregate_member(
            builder->source, builder->diagnostics,
            value_type, field);
        if (field_type != NULL &&
            ir_type_requires_custom_copy_inner(
                builder, field_type, next_seen, seen_count))
            return true;
    }
    return false;
}
bool ir_type_requires_custom_copy(
    IrBuilder *builder, const Type *value_type
) {
    return ir_type_requires_custom_copy_inner(
        builder, value_type, NULL, 0U);
}

IrValueId emit_plain_clone(
    IrBuilder *builder, const Type *value_type,
    IrValueId source, LangSpan span
) {
    IrInstruction *clone = ir_append_instruction(
        builder, IR_OP_VALUE_CLONE,
        ir_intern_type(builder->module, value_type),
        &source, 1U, span);
    if (clone == NULL) return IR_INVALID_ID;
    clone->auxiliary = 0U;
    return clone->result;
}

IrValueId ir_emit_recursive_copy(
    IrBuilder *builder, const Type *value_type,
    IrValueId source, LangSpan span, bool preserve_source
) {
    IrTypeId result_type = ir_intern_type(builder->module, value_type);
    const Decl *copy_constructor = type_copy_constructor(value_type);
    if (copy_constructor != NULL &&
        !copy_constructor->as.function.is_deleted) {
        IrInstruction *call = ir_append_instruction(
            builder, IR_OP_CALL_DIRECT, result_type,
            &source, 1U, span);
        if (call == NULL) return IR_INVALID_ID;
        call->index = ir_find_function(builder->module, copy_constructor);
        call->symbol = copy_constructor->as.function.name;
        call->symbol_length = strlen(call->symbol);
        call->argument_mode_count = 1U;
        call->argument_modes = ir_resize(
            NULL, 1U, sizeof(*call->argument_modes));
        call->argument_modes[0] = PARAMETER_MODE_IMMUTABLE_REFERENCE;
        return call->result;
    }
    if (value_type != NULL && value_type->kind == TYPE_ARRAY) {
        size_t count = value_type->array_length;
        IrValueId *items = ir_resize(
            NULL, count, sizeof(*items));
        IrTypeId index_type = ir_intern_type(
            builder->module, &ir_usize_type);
        for (size_t item = 0U; item < count; ++item) {
            IrInstruction *index = ir_append_instruction(
                builder, IR_OP_CONST_INT, index_type,
                NULL, 0U, span);
            if (index == NULL) {
                free(items);
                return IR_INVALID_ID;
            }
            index->integer = item;
            IrValueId operands[2] = {source, index->result};
            IrTypeId element_type = ir_intern_type(
                builder->module, value_type->element);
            IrInstruction *borrow = ir_append_instruction(
                builder, IR_OP_INDEX_GET,
                element_type,
                operands, 2U, span);
            if (borrow == NULL) {
                free(items);
                return IR_INVALID_ID;
            }
            borrow->integer = 1U;
            if (ir_type_requires_custom_copy(
                    builder, value_type->element))
                items[item] = ir_emit_recursive_copy(
                    builder, value_type->element,
                    borrow->result, span, false);
            else if (builder->module->types[element_type].copy_policy !=
                     IR_COPY_TRIVIAL)
                items[item] = emit_plain_clone(
                    builder, value_type->element,
                    borrow->result, span);
            else
                items[item] = borrow->result;
        }
        IrInstruction *make = ir_append_instruction(
            builder, IR_OP_AGGREGATE_MAKE, result_type,
            items, count, span);
        free(items);
        if (make == NULL) return IR_INVALID_ID;
        make->symbol = "array";
        make->symbol_length = 5U;
        make->index = (uint32_t)count;
        IrValueId result = make->result;
        if (!preserve_source) {
            IrInstruction *discard = ir_append_instruction(
                builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
                &source, 1U, span);
            if (discard != NULL) discard->auxiliary = 1U;
        }
        return result;
    }
    if (value_type != NULL &&
        (value_type->kind == TYPE_VEC ||
         value_type->kind == TYPE_STACK ||
         value_type->kind == TYPE_QUEUE)) {
        const char *new_name = value_type->kind == TYPE_VEC
            ? "List::New"
            : value_type->kind == TYPE_STACK
            ? "Stack::New" : "Queue::New";
        const char *add_name = value_type->kind == TYPE_VEC
            ? "List::Add"
            : value_type->kind == TYPE_STACK
            ? "Stack::Push" : "Queue::Enqueue";
        const Type *element_type = value_type->element;
        IrTypeId element_ir = ir_intern_type(
            builder->module, element_type);
        IrTypeId iterator_ir = ir_intern_iterator_type(
            builder->module, result_type, element_ir);

        IrValueId destination = ir_emit_synthetic_native_call(
            builder, new_name, value_type,
            NULL, 0U, false, span);
        uint32_t destination_local = ir_add_synthetic_local(
            builder, "<copy-list>", result_type);
        IrInstruction *destination_store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &destination, 1U, span);
        if (destination_store == NULL) return IR_INVALID_ID;
        destination_store->index = destination_local;

        IrInstruction *begin = ir_append_instruction(
            builder, IR_OP_BORROWED_ITERATOR_BEGIN,
            iterator_ir, &source, 1U, span);
        if (begin == NULL) return IR_INVALID_ID;
        uint32_t iterator_local = ir_add_synthetic_local(
            builder, "<copy-iterator>", iterator_ir);
        IrValueId iterator = begin->result;
        IrInstruction *iterator_store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &iterator, 1U, span);
        if (iterator_store == NULL) return IR_INVALID_ID;
        iterator_store->index = iterator_local;

        IrBlockId condition = ir_add_block(builder->function);
        IrBlockId body = ir_add_block(builder->function);
        IrBlockId finish = ir_add_block(builder->function);
        ir_set_terminator(
            builder, IR_TERM_JUMP, IR_INVALID_ID,
            condition, IR_INVALID_ID, span);

        builder->current = condition;
        IrInstruction *has_next = ir_append_instruction(
            builder, IR_OP_LOCAL_ITERATOR_HAS_NEXT,
            ir_intern_type(builder->module, &ir_bool_type),
            NULL, 0U, span);
        if (has_next == NULL) return IR_INVALID_ID;
        has_next->index = iterator_local;
        ir_set_terminator(
            builder, IR_TERM_BRANCH, has_next->result,
            body, finish, span);

        builder->current = body;
        IrInstruction *next = ir_append_instruction(
            builder, IR_OP_LOCAL_ITERATOR_NEXT,
            element_ir, NULL, 0U, span);
        if (next == NULL) return IR_INVALID_ID;
        next->index = iterator_local;
        IrValueId copied = ir_emit_recursive_copy(
            builder, element_type, next->result, span, false);
        IrInstruction *destination_load = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, result_type,
            NULL, 0U, span);
        if (destination_load == NULL) return IR_INVALID_ID;
        destination_load->index = destination_local;
        IrValueId add_operands[2] = {
            destination_load->result, copied
        };
        IrValueId added = ir_emit_synthetic_native_call(
            builder, add_name, &ir_unit_type,
            add_operands, 2U, true, span);
        IrInstruction *discard_add = ir_append_instruction(
            builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
            &added, 1U, span);
        (void)discard_add;
        ir_set_terminator(
            builder, IR_TERM_JUMP, IR_INVALID_ID,
            condition, IR_INVALID_ID, span);

        builder->current = finish;
        IrInstruction *drop_iterator = ir_append_instruction(
            builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
            NULL, 0U, span);
        if (drop_iterator != NULL) drop_iterator->index = iterator_local;
        IrInstruction *result = ir_append_instruction(
            builder, IR_OP_LOCAL_MOVE, result_type,
            NULL, 0U, span);
        if (result == NULL) return IR_INVALID_ID;
        result->index = destination_local;
        if (!preserve_source) {
            IrInstruction *discard = ir_append_instruction(
                builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
                &source, 1U, span);
            if (discard != NULL) discard->auxiliary = 1U;
        }
        return result->result;
    }
    if (value_type != NULL &&
        (value_type->kind == TYPE_DICTIONARY ||
         value_type->kind == TYPE_HASH_SET)) {
        bool is_set = value_type->kind == TYPE_HASH_SET;
        const Type *key_type = value_type->element;
        const Type *mapped_type = value_type->error_type;
        IrTypeId key_ir = ir_intern_type(builder->module, key_type);
        IrTypeId mapped_ir = ir_intern_type(builder->module, mapped_type);
        IrTypeId usize_ir = ir_intern_type(
            builder->module, &ir_usize_type);

        IrValueId destination = ir_emit_synthetic_native_call(
            builder, "Dictionary::New", value_type,
            NULL, 0U, false, span);
        uint32_t destination_local = ir_add_synthetic_local(
            builder, "<copy-dictionary>", result_type);
        IrInstruction *destination_store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &destination, 1U, span);
        if (destination_store == NULL) return IR_INVALID_ID;
        destination_store->index = destination_local;

        IrInstruction *zero = ir_append_instruction(
            builder, IR_OP_CONST_INT, usize_ir, NULL, 0U, span);
        if (zero == NULL) return IR_INVALID_ID;
        zero->integer = 0U;
        uint32_t index_local = ir_add_synthetic_local(
            builder, "<copy-index>", usize_ir);
        IrValueId zero_value = zero->result;
        IrInstruction *index_store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &zero_value, 1U, span);
        if (index_store == NULL) return IR_INVALID_ID;
        index_store->index = index_local;

        IrBlockId condition = ir_add_block(builder->function);
        IrBlockId body = ir_add_block(builder->function);
        IrBlockId finish = ir_add_block(builder->function);
        ir_set_terminator(
            builder, IR_TERM_JUMP, IR_INVALID_ID,
            condition, IR_INVALID_ID, span);

        builder->current = condition;
        IrInstruction *condition_index = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, usize_ir,
            NULL, 0U, span);
        if (condition_index == NULL) return IR_INVALID_ID;
        condition_index->index = index_local;
        IrInstruction *count = ir_append_instruction(
            builder, IR_OP_COLLECTION_COUNT, usize_ir,
            &source, 1U, span);
        if (count == NULL) return IR_INVALID_ID;
        IrValueId less_operands[2] = {
            condition_index->result, count->result
        };
        IrInstruction *less = ir_append_instruction(
            builder, IR_OP_LESS,
            ir_intern_type(builder->module, &ir_bool_type),
            less_operands, 2U, span);
        if (less == NULL) return IR_INVALID_ID;
        ir_set_terminator(
            builder, IR_TERM_BRANCH, less->result,
            body, finish, span);

        builder->current = body;
        IrInstruction *key_index = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, usize_ir,
            NULL, 0U, span);
        if (key_index == NULL) return IR_INVALID_ID;
        key_index->index = index_local;
        IrValueId key_operands[2] = {
            source, key_index->result
        };
        IrInstruction *key_borrow = ir_append_instruction(
            builder, IR_OP_DICTIONARY_KEY_BORROW,
            key_ir, key_operands, 2U, span);
        if (key_borrow == NULL) return IR_INVALID_ID;
        IrValueId key = ir_type_requires_custom_copy(builder, key_type)
            ? ir_emit_recursive_copy(
                builder, key_type, key_borrow->result, span, false)
            : builder->module->types[key_ir].copy_policy != IR_COPY_TRIVIAL
            ? emit_plain_clone(
                builder, key_type, key_borrow->result, span)
            : key_borrow->result;

        IrValueId mapped;
        if (is_set) {
            IrInstruction *present = ir_append_instruction(
                builder, IR_OP_CONST_BOOL,
                ir_intern_type(builder->module, &ir_bool_type),
                NULL, 0U, span);
            if (present == NULL) return IR_INVALID_ID;
            present->integer = 1U;
            mapped = present->result;
        } else {
            IrInstruction *mapped_index = ir_append_instruction(
                builder, IR_OP_LOCAL_LOAD, usize_ir,
                NULL, 0U, span);
            if (mapped_index == NULL) return IR_INVALID_ID;
            mapped_index->index = index_local;
            IrValueId mapped_operands[2] = {
                source, mapped_index->result
            };
            IrInstruction *mapped_borrow = ir_append_instruction(
                builder, IR_OP_DICTIONARY_VALUE_BORROW,
                mapped_ir, mapped_operands, 2U, span);
            if (mapped_borrow == NULL) return IR_INVALID_ID;
            mapped = ir_type_requires_custom_copy(builder, mapped_type)
                ? ir_emit_recursive_copy(
                    builder, mapped_type,
                    mapped_borrow->result, span, false)
                : builder->module->types[mapped_ir].copy_policy !=
                      IR_COPY_TRIVIAL
                ? emit_plain_clone(
                    builder, mapped_type,
                    mapped_borrow->result, span)
                : mapped_borrow->result;
        }

        IrInstruction *destination_load = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, result_type,
            NULL, 0U, span);
        if (destination_load == NULL) return IR_INVALID_ID;
        destination_load->index = destination_local;
        IrValueId add_operands[3] = {
            destination_load->result, key, mapped
        };
        IrValueId added = ir_emit_synthetic_native_call(
            builder,
            is_set ? "Dictionary::TryAdd" : "Dictionary::Add",
            is_set ? &ir_bool_type : &ir_unit_type,
            add_operands, 3U, true, span);
        IrInstruction *discard_add = ir_append_instruction(
            builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
            &added, 1U, span);
        (void)discard_add;

        IrInstruction *increment_index = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, usize_ir,
            NULL, 0U, span);
        if (increment_index == NULL) return IR_INVALID_ID;
        increment_index->index = index_local;
        IrInstruction *one = ir_append_instruction(
            builder, IR_OP_CONST_INT, usize_ir, NULL, 0U, span);
        if (one == NULL) return IR_INVALID_ID;
        one->integer = 1U;
        IrValueId increment_operands[2] = {
            increment_index->result, one->result
        };
        IrInstruction *increment = ir_append_instruction(
            builder, IR_OP_ADD_CHECKED, usize_ir,
            increment_operands, 2U, span);
        if (increment == NULL) return IR_INVALID_ID;
        IrValueId incremented = increment->result;
        IrInstruction *increment_store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &incremented, 1U, span);
        if (increment_store == NULL) return IR_INVALID_ID;
        increment_store->index = index_local;
        ir_set_terminator(
            builder, IR_TERM_JUMP, IR_INVALID_ID,
            condition, IR_INVALID_ID, span);

        builder->current = finish;
        IrInstruction *result = ir_append_instruction(
            builder, IR_OP_LOCAL_MOVE, result_type,
            NULL, 0U, span);
        if (result == NULL) return IR_INVALID_ID;
        result->index = destination_local;
        if (!preserve_source) {
            IrInstruction *discard = ir_append_instruction(
                builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
                &source, 1U, span);
            if (discard != NULL) discard->auxiliary = 1U;
        }
        return result->result;
    }
    bool tagged_union = value_type != NULL &&
        (value_type->kind == TYPE_OPTION ||
         value_type->kind == TYPE_RESULT ||
         (value_type->kind == TYPE_NAMED &&
          value_type->declaration != NULL &&
          value_type->declaration->kind == DECL_ENUM &&
          value_type->declaration->as.enumeration.is_union));
    if (tagged_union) {
        size_t variant_count =
            builder->module->types[result_type].variant_count;
        uint32_t result_local = ir_add_synthetic_local(
            builder, "<copy-result>", result_type);
        IrBlockId merge = ir_add_block(builder->function);
        for (size_t variant = 0U;
             variant < variant_count; ++variant) {
            const char *variant_name =
                builder->module->types[result_type].variant_names[variant];
            IrInstruction *is_variant = ir_append_instruction(
                builder, IR_OP_ENUM_IS,
                ir_intern_type(builder->module, &ir_bool_type),
                &source, 1U, span);
            if (is_variant == NULL) return IR_INVALID_ID;
            is_variant->auxiliary = (uint32_t)variant;
            is_variant->symbol = variant_name;
            is_variant->symbol_length = strlen(is_variant->symbol);
            IrBlockId copy_block = ir_add_block(builder->function);
            IrBlockId next_block = ir_add_block(builder->function);
            ir_set_terminator(
                builder, IR_TERM_BRANCH, is_variant->result,
                copy_block, next_block, span);

            builder->current = copy_block;
            IrValueId payload = IR_INVALID_ID;
            IrTypeId payload_ir_type = builder->module->types[result_type]
                .variant_payload_types[variant];
            if (payload_ir_type != IR_INVALID_ID) {
                Type *payload_type = value_type->kind == TYPE_OPTION
                    ? value_type->element
                    : value_type->kind == TYPE_RESULT
                    ? (variant == 0U
                        ? value_type->element : value_type->error_type)
                    : lang_checker_resolve_aggregate_member(
                        builder->source, builder->diagnostics,
                        value_type, variant);
                if (payload_type == NULL) {
                    lang_diag(builder->diagnostics, span,
                              "could not resolve `%s` payload while lowering a copy",
                              variant_name);
                    builder->failed = true;
                    return IR_INVALID_ID;
                }
                IrInstruction *borrow = ir_append_instruction(
                    builder, IR_OP_ENUM_PAYLOAD_BORROW,
                    payload_ir_type, &source, 1U, span);
                if (borrow == NULL) return IR_INVALID_ID;
                borrow->auxiliary = (uint32_t)variant;
                borrow->symbol = variant_name;
                borrow->symbol_length = strlen(borrow->symbol);
                if (ir_type_requires_custom_copy(builder, payload_type))
                    payload = ir_emit_recursive_copy(
                        builder, payload_type, borrow->result, span, false);
                else if (builder->module->types[payload_ir_type].copy_policy !=
                         IR_COPY_TRIVIAL)
                    payload = emit_plain_clone(
                        builder, payload_type, borrow->result, span);
                else
                    payload = borrow->result;
            }
            IrInstruction *make = ir_append_instruction(
                builder, IR_OP_AGGREGATE_MAKE, result_type,
                payload_ir_type == IR_INVALID_ID ? NULL : &payload,
                payload_ir_type == IR_INVALID_ID ? 0U : 1U, span);
            if (make == NULL) return IR_INVALID_ID;
            make->index = (uint32_t)variant;
            make->symbol = variant_name;
            make->symbol_length = strlen(make->symbol);
            IrValueId made = make->result;
            IrInstruction *store = ir_append_instruction(
                builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                &made, 1U, span);
            if (store == NULL) return IR_INVALID_ID;
            store->index = result_local;
            ir_set_terminator(
                builder, IR_TERM_JUMP, IR_INVALID_ID,
                merge, IR_INVALID_ID, span);
            builder->current = next_block;
        }
        ir_set_terminator(
            builder, IR_TERM_TRAP, IR_INVALID_ID,
            IR_INVALID_ID, IR_INVALID_ID, span);
        builder->current = merge;
        IrValueId result = load_conditional_result(
            builder, result_local, result_type, span);
        if (!preserve_source) {
            IrInstruction *discard = ir_append_instruction(
                builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
                &source, 1U, span);
            if (discard != NULL) discard->auxiliary = 1U;
        }
        return result;
    }
    if (value_type == NULL || value_type->kind != TYPE_NAMED ||
        value_type->declaration == NULL ||
        value_type->declaration->kind != DECL_STRUCT) {
        IrInstruction *clone = ir_append_instruction(
            builder, IR_OP_VALUE_CLONE, result_type,
            &source, 1U, span);
        if (clone == NULL) return IR_INVALID_ID;
        clone->auxiliary = 0U;
        return clone->result;
    }

    const Decl *declaration = value_type->declaration;
    size_t field_count = declaration->as.structure.field_count;
    IrValueId *fields = ir_resize(
        NULL, field_count, sizeof(*fields));
    uint32_t *labels = ir_resize(
        NULL, field_count, sizeof(*labels));
    for (size_t field = 0U; field < field_count; ++field) {
        const FieldDecl *member =
            &declaration->as.structure.fields[field];
        const Type *field_type = lang_checker_resolve_aggregate_member(
            builder->source, builder->diagnostics,
            value_type, field);
        if (field_type == NULL) {
            lang_diag(builder->diagnostics, member->span,
                      "could not resolve field `%s` while lowering a copy",
                      member->name);
            builder->failed = true;
            free(fields);
            free(labels);
            return IR_INVALID_ID;
        }
        IrTypeId field_ir_type = ir_intern_type(
            builder->module, field_type);
        IrInstruction *borrow = ir_append_instruction(
            builder, IR_OP_FIELD_GET,
            field_ir_type,
            &source, 1U, member->span);
        if (borrow == NULL) {
            free(fields);
            free(labels);
            return IR_INVALID_ID;
        }
        borrow->index = (uint32_t)field;
        /* A borrowed projection never destroys its aggregate. The final
         * projection may invalidate the temporary VM slot after all sibling
         * fields have been read. */
        borrow->auxiliary = 1U;
        borrow->symbol = member->name;
        borrow->symbol_length = strlen(member->name);
        if (ir_type_requires_custom_copy(builder, field_type))
            fields[field] = ir_emit_recursive_copy(
                builder, field_type, borrow->result, member->span, false);
        else if (builder->module->types[field_ir_type].copy_policy !=
                 IR_COPY_TRIVIAL)
            fields[field] = emit_plain_clone(
                builder, field_type, borrow->result, member->span);
        else
            fields[field] = borrow->result;
        labels[field] = (uint32_t)field;
    }
    IrInstruction *make = ir_append_instruction(
        builder, IR_OP_AGGREGATE_MAKE, result_type,
        fields, field_count, span);
    free(fields);
    if (make == NULL) {
        free(labels);
        return IR_INVALID_ID;
    }
    make->labels = labels;
    make->label_count = field_count;
    make->symbol = declaration->as.structure.name;
    make->symbol_length = strlen(make->symbol);
    make->index = (uint32_t)field_count;
    IrValueId result = make->result;
    if (!preserve_source) {
        IrInstruction *discard = ir_append_instruction(
            builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
            &source, 1U, span);
        if (discard != NULL) discard->auxiliary = 1U;
    }
    return result;
}
