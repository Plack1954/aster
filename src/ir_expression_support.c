#include "internal.h"
#include "ir_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool load_requires_clone(const Type *type) {
    return type != NULL &&
        (type->kind == TYPE_ARRAY || type->kind == TYPE_OPTION ||
         type->kind == TYPE_RESULT || type->kind == TYPE_NAMED);
}

bool is_float_type(const Type *type) {
    return type != NULL &&
           (type->kind == TYPE_F32 || type->kind == TYPE_F64);
}

uint32_t ir_field_index(const Type *object_type, const char *name) {
    if (object_type == NULL || object_type->declaration == NULL ||
        (object_type->declaration->kind != DECL_STRUCT &&
         object_type->declaration->kind != DECL_CLASS))
        return UINT32_MAX;
    const Decl *decl = object_type->declaration;
    for (size_t i = 0U; i < decl->as.structure.field_count; ++i)
        if (strcmp(decl->as.structure.fields[i].name, name) == 0)
            return (uint32_t)i;
    return UINT32_MAX;
}

uint32_t ir_static_field_index(
    const IrModule *ir, const Decl *owner, const char *name
) {
    for (size_t i = 0U; i < ir->static_field_count; ++i)
        if (ir->static_fields[i].owner_declaration == owner &&
            strcmp(ir->static_fields[i].name, name) == 0)
            return (uint32_t)i;
    return UINT32_MAX;
}

static uint32_t variant_index(const Decl *decl, const char *name) {
    if (decl == NULL || decl->kind != DECL_ENUM)
        return UINT32_MAX;
    for (size_t i = 0U; i < decl->as.enumeration.variant_count; ++i)
        if (strcmp(decl->as.enumeration.variants[i].name, name) == 0)
            return (uint32_t)i;
    return UINT32_MAX;
}

static const char *unqualified_variant(const char *name) {
    const char *result = name;
    for (const char *cursor = strstr(name, "::");
         cursor != NULL; cursor = strstr(cursor + 2U, "::"))
        result = cursor + 2U;
    return result;
}

static uint32_t type_variant_index(const Type *type, const char *name) {
    const char *variant = unqualified_variant(name);
    if (type != NULL && type->kind == TYPE_OPTION)
        return strcmp(variant, "Some") == 0 ? 1U : 0U;
    if (type != NULL && type->kind == TYPE_RESULT)
        return strcmp(variant, "Err") == 0 ? 1U : 0U;
    return type != NULL
        ? variant_index(type->declaration, variant)
        : UINT32_MAX;
}

IrValueId ir_lower_expr(IrBuilder *builder, const Expr *expr);
void ir_lower_stmt(IrBuilder *builder, const Stmt *stmt);
IrValueId ir_emit_synthetic_native_call(
    IrBuilder *builder, const char *name,
    const Type *result_type, const IrValueId *operands,
    size_t operand_count, bool borrow_first,
    LangSpan span);
IrValueId emit_plain_clone(
    IrBuilder *builder, const Type *type,
    IrValueId source, LangSpan span);

static void ir_set_native_call_descriptor(
    IrBuilder *builder, IrInstruction *call, bool compiler_generated,
    bool registry_dispatch
) {
    call->native_call = ir_resize(NULL, 1U, sizeof(*call->native_call));
    memset(call->native_call, 0, sizeof(*call->native_call));
    call->native_call->name = call->symbol;
    call->native_call->return_type = call->result_type;
    call->native_call->parameter_count = call->operand_count;
    call->native_call->calling_convention = IR_CALLING_CONVENTION_NATIVE;
    call->native_call->may_propagate_exception = true;
    call->native_call->compiler_generated = compiler_generated;
    call->native_call->registry_dispatch = registry_dispatch;
    if (call->operand_count == 0U) return;
    call->native_call->parameter_types = ir_resize(
        NULL, call->operand_count,
        sizeof(*call->native_call->parameter_types));
    call->native_call->parameter_modes = ir_resize(
        NULL, call->operand_count,
        sizeof(*call->native_call->parameter_modes));
    for (size_t i = 0U; i < call->operand_count; ++i) {
        IrValueId operand = call->operands[i];
        call->native_call->parameter_types[i] =
            operand < builder->function->value_count
                ? builder->function->value_types[operand]
                : IR_INVALID_ID;
        call->native_call->parameter_modes[i] =
            call->argument_modes[i];
    }
}

static bool builtin_borrows_first_place(const char *name) {
    return name != NULL &&
           (strcmp(name, "Html::ToHtmlString") == 0 ||
            strcmp(name, "ArenaAlloc") == 0 ||
            strcmp(name, "ArenaReset") == 0 ||
            strcmp(name, "StringBuilder::Append") == 0 ||
            strcmp(name, "StringBuilder::AppendByte") == 0 ||
            strcmp(name, "StringBuilder::ToString") == 0 ||
            strcmp(name, "StringBuilder::Length") == 0 ||
            strcmp(name, "StringBuilder::Clear") == 0 ||
            strcmp(name, "List::Add") == 0 ||
            strcmp(name, "List::Count") == 0 ||
            strcmp(name, "List::Get") == 0 ||
            strcmp(name, "List::Capacity") == 0 ||
            strcmp(name, "List::Clear") == 0 ||
            strcmp(name, "List::Insert") == 0 ||
            strcmp(name, "List::RemoveAt") == 0 ||
            strcmp(name, "List::Set") == 0 ||
            strcmp(name, "List::Contains") == 0 ||
            strcmp(name, "List::IndexOf") == 0 ||
            strcmp(name, "List::LastIndexOf") == 0 ||
            strcmp(name, "List::Remove") == 0 ||
            strcmp(name, "List::AddRange") == 0 ||
            strcmp(name, "List::InsertRange") == 0 ||
            strcmp(name, "List::RemoveRange") == 0 ||
            strcmp(name, "List::GetRange") == 0 ||
            strcmp(name, "List::Reverse") == 0 ||
            strcmp(name, "List::EnsureCapacity") == 0 ||
            strcmp(name, "List::TrimExcess") == 0 ||
            strcmp(name, "List::SetCapacity") == 0 ||
            strcmp(name, "List::Exists") == 0 ||
            strcmp(name, "List::FindAll") == 0 ||
            strcmp(name, "List::FindIndex") == 0 ||
            strcmp(name, "List::FindLastIndex") == 0 ||
            strcmp(name, "List::RemoveAll") == 0 ||
            strcmp(name, "List::ForEach") == 0 ||
            strcmp(name, "List::TrueForAll") == 0 ||
            strcmp(name, "Dictionary::Add") == 0 ||
            strcmp(name, "Dictionary::Count") == 0 ||
            strcmp(name, "Dictionary::ContainsKey") == 0 ||
            strcmp(name, "Dictionary::Remove") == 0 ||
            strcmp(name, "Dictionary::Clear") == 0 ||
            strcmp(name, "Dictionary::Get") == 0 ||
            strcmp(name, "Dictionary::Set") == 0 ||
            strcmp(name, "Dictionary::TryAdd") == 0 ||
            strcmp(name, "Dictionary::TryGetValue") == 0 ||
            strcmp(name, "Dictionary::ContainsValue") == 0 ||
            strcmp(name, "Dictionary::EnsureCapacity") == 0 ||
            strcmp(name, "Dictionary::TrimExcess") == 0 ||
            strcmp(name, "Dictionary::Capacity") == 0 ||
            strcmp(name, "Dictionary::KeyAt") == 0 ||
            strcmp(name, "Dictionary::ValueAt") == 0 ||
            strcmp(name, "Queue::Enqueue") == 0 ||
            strcmp(name, "Queue::Dequeue") == 0 ||
            strcmp(name, "Queue::Peek") == 0 ||
            strcmp(name, "Queue::TryDequeue") == 0 ||
            strcmp(name, "Queue::TryPeek") == 0 ||
            strcmp(name, "Queue::Count") == 0 ||
            strcmp(name, "Queue::Clear") == 0 ||
            strcmp(name, "Queue::EnsureCapacity") == 0 ||
            strcmp(name, "Queue::TrimExcess") == 0 ||
            strcmp(name, "Queue::Capacity") == 0 ||
            strcmp(name, "Stack::Push") == 0 ||
            strcmp(name, "Stack::Pop") == 0 ||
            strcmp(name, "Stack::Peek") == 0 ||
            strcmp(name, "Stack::TryPop") == 0 ||
            strcmp(name, "Stack::TryPeek") == 0 ||
            strcmp(name, "Stack::Count") == 0 ||
            strcmp(name, "Stack::Clear") == 0 ||
            strcmp(name, "Stack::EnsureCapacity") == 0 ||
            strcmp(name, "Stack::TrimExcess") == 0 ||
            strcmp(name, "Stack::Capacity") == 0 ||
            strcmp(name, "CancellationTokenSource::Token") == 0 ||
            strcmp(name, "CancellationTokenSource::Cancel") == 0 ||
            strcmp(name, "CancellationToken::IsCancellationRequested") == 0 ||
            strcmp(name,
                   "CancellationToken::ThrowIfCancellationRequested") == 0 ||
            strcmp(name, "BufferAsMutSlice") == 0 ||
            strcmp(name, "BufferAsSlice") == 0);
}

static bool builtin_borrows_named_first(const char *name) {
    return name != NULL &&
           (strcmp(name, "Console::WriteLine") == 0 ||
            strcmp(name, "Console::Write") == 0 ||
            strcmp(name, "Console::Error::WriteLine") == 0 ||
            strcmp(name, "Console::Error::Write") == 0 ||
            strcmp(name, "TextLen") == 0);
}

static bool builtin_borrows_argument(
    const char *name, size_t index
) {
    if (name == NULL) return false;
    if (index == 1U &&
        (strcmp(name, "StringBuilder::Append") == 0 ||
         strcmp(name, "List::Contains") == 0 ||
         strcmp(name, "List::IndexOf") == 0 ||
         strcmp(name, "List::LastIndexOf") == 0 ||
         strcmp(name, "List::Remove") == 0 ||
         strcmp(name, "Dictionary::ContainsKey") == 0 ||
         strcmp(name, "Dictionary::Remove") == 0 ||
         strcmp(name, "Dictionary::Get") == 0 ||
         strcmp(name, "Dictionary::ContainsValue") == 0 ||
         strcmp(name, "Dictionary::TryGetValue") == 0))
        return true;
    return false;
}

IrOpcode binary_opcode(TokenKind token, bool floating) {
    switch (token) {
        case TOK_PLUS: return floating ? IR_OP_ADD_FLOAT : IR_OP_ADD_CHECKED;
        case TOK_MINUS: return floating ? IR_OP_SUB_FLOAT : IR_OP_SUB_CHECKED;
        case TOK_STAR: return floating ? IR_OP_MUL_FLOAT : IR_OP_MUL_CHECKED;
        case TOK_SLASH: return floating ? IR_OP_DIV_FLOAT : IR_OP_DIV_CHECKED;
        case TOK_PERCENT: return IR_OP_REM_CHECKED;
        case TOK_SHIFT_LEFT: return IR_OP_SHIFT_LEFT_CHECKED;
        case TOK_SHIFT_RIGHT: return IR_OP_SHIFT_RIGHT_CHECKED;
        case TOK_AMP: return IR_OP_BIT_AND;
        case TOK_PIPE: return IR_OP_BIT_OR;
        case TOK_CARET: return IR_OP_BIT_XOR;
        case TOK_EQUAL_EQUAL: return IR_OP_EQUAL;
        case TOK_BANG_EQUAL: return IR_OP_NOT_EQUAL;
        case TOK_LESS: return IR_OP_LESS;
        case TOK_LESS_EQUAL: return IR_OP_LESS_EQUAL;
        case TOK_GREATER: return IR_OP_GREATER;
        case TOK_GREATER_EQUAL: return IR_OP_GREATER_EQUAL;
        default: return IR_OP_VALUE_DISCARD;
    }
}

static IrValueId lower_enum_constructor(IrBuilder *builder,
                                        const Expr *expr,
                                        const char *name) {
    size_t argument_count = expr->as.call.arguments.count;
    IrValueId *operands = ir_resize(
        NULL, argument_count, sizeof(*operands));
    for (size_t i = 0U; i < argument_count; ++i)
        operands[i] = ir_lower_expr(
            builder, expr->as.call.arguments.items[i]);
    const char *separator = strrchr(name, ':');
    const char *variant =
        separator != NULL ? separator + 1U : name;
    IrInstruction *make = ir_append_instruction(
        builder, IR_OP_AGGREGATE_MAKE,
        ir_intern_type(builder->module, expr->type),
        operands, argument_count, expr->span);
    free(operands);
    if (make == NULL) return IR_INVALID_ID;
    make->symbol = variant;
    make->symbol_length = strlen(variant);
    if (expr->resolved_decl != NULL &&
        expr->resolved_decl->kind == DECL_ENUM)
        make->index = variant_index(expr->resolved_decl, variant);
    else if (strcmp(variant, "Some") == 0 ||
             strcmp(variant, "Err") == 0)
        make->index = 1U;
    else
        make->index = 0U;
    return make->result;
}

IrInstruction *ir_emit_local_enum_operation(
    IrBuilder *builder, IrOpcode opcode, IrTypeId result_type,
    uint32_t local, const Type *enum_type, const char *variant,
    LangSpan span) {
    IrInstruction *instruction = ir_append_instruction(
        builder, opcode, result_type, NULL, 0U, span);
    if (instruction != NULL) {
        instruction->index = local;
        instruction->auxiliary =
            type_variant_index(enum_type, variant);
        instruction->symbol = unqualified_variant(variant);
        instruction->symbol_length = strlen(instruction->symbol);
    }
    return instruction;
}

IrInstruction *ir_emit_enum_payload_borrow(
    IrBuilder *builder, IrTypeId result_type, IrValueId value,
    const Type *enum_type, const char *variant, LangSpan span
) {
    IrInstruction *instruction = ir_append_instruction(
        builder, IR_OP_ENUM_PAYLOAD_BORROW, result_type,
        &value, 1U, span);
    if (instruction != NULL) {
        instruction->auxiliary =
            type_variant_index(enum_type, variant);
        instruction->symbol = unqualified_variant(variant);
        instruction->symbol_length = strlen(instruction->symbol);
    }
    return instruction;
}

IrValueId lower_try(IrBuilder *builder, const Expr *expr) {
    const Expr *operand_expr = expr->as.try_.value;
    const Type *result_type = operand_expr->type;
    IrValueId result = ir_lower_expr(builder, operand_expr);
    uint32_t local = ir_add_local(
        builder, "<try-result>", 0U, result_type, false);
    IrInstruction *store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &result, 1U, expr->span);
    if (store != NULL) store->index = local;

    IrInstruction *is_ok = ir_emit_local_enum_operation(
        builder, IR_OP_LOCAL_ENUM_IS,
        ir_intern_type(builder->module, &ir_bool_type),
        local, result_type, "Result::Ok", expr->span);
    if (is_ok == NULL) return IR_INVALID_ID;
    IrBlockId success = ir_add_block(builder->function);
    IrBlockId error = ir_add_block(builder->function);
    ir_set_terminator(builder, IR_TERM_BRANCH, is_ok->result,
                   success, error, expr->span);

    builder->current = error;
    IrInstruction *error_payload = ir_emit_local_enum_operation(
        builder, IR_OP_LOCAL_ENUM_PAYLOAD_MOVE,
        ir_intern_type(builder->module, result_type->error_type),
        local, result_type, "Result::Err", expr->span);
    if (error_payload == NULL) return IR_INVALID_ID;
    IrValueId payload = error_payload->result;
    const Type *function_result =
        builder->function->declaration->as.function.checked_return_type;
    IrInstruction *propagated = ir_append_instruction(
        builder, IR_OP_AGGREGATE_MAKE,
        ir_intern_type(builder->module, function_result),
        &payload, 1U, expr->span);
    if (propagated == NULL) return IR_INVALID_ID;
    propagated->symbol = "Err";
    propagated->symbol_length = 3U;
    propagated->index = 1U;
    IrValueId propagated_result = propagated->result;
    ir_emit_function_cleanup(builder, expr->span);
    ir_set_terminator(builder, IR_TERM_RETURN, propagated_result,
                   IR_INVALID_ID, IR_INVALID_ID, expr->span);

    builder->current = success;
    IrInstruction *success_payload = ir_emit_local_enum_operation(
        builder, IR_OP_LOCAL_ENUM_PAYLOAD_MOVE,
        ir_intern_type(builder->module, expr->type),
        local, result_type, "Result::Ok", expr->span);
    return success_payload != NULL
         ? success_payload->result : IR_INVALID_ID;
}

static IrValueId lower_borrowed_collection_place(
    IrBuilder *builder, const Expr *expr, bool *borrowed
) {
    *borrowed = false;
    if (expr->kind == EXPR_NAME) {
        IrInstruction *load = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD,
            ir_intern_type(builder->module, expr->type),
            NULL, 0U, expr->span);
        if (load == NULL) return IR_INVALID_ID;
        load->index = ir_find_local(
            builder, expr->resolved_local_id, expr->span);
        *borrowed = true;
        return load->result;
    }
    if (expr->kind == EXPR_FIELD &&
        expr->as.field.object->kind == EXPR_NAME) {
        const Expr *owner = expr->as.field.object;
        IrInstruction *field = ir_append_instruction(
            builder, IR_OP_LOCAL_FIELD_BORROW,
            ir_intern_type(builder->module, expr->type),
            NULL, 0U, expr->span);
        if (field == NULL) return IR_INVALID_ID;
        field->index = ir_find_local(
            builder, owner->resolved_local_id, owner->span);
        field->auxiliary = ir_field_index(
            owner->type, expr->as.field.field);
        field->symbol = expr->as.field.field;
        field->symbol_length = strlen(field->symbol);
        *borrowed = true;
        return field->result;
    }
    return ir_lower_expr(builder, expr);
}

bool expression_is_local_place(const Expr *expr) {
    if (expr == NULL) return false;
    if (expr->kind == EXPR_NAME)
        return expr->resolved_local_id != 0U;
    if (expr->kind == EXPR_FIELD)
        return !expr->as.field.static_field &&
               expression_is_local_place(expr->as.field.object);
    if (expr->kind == EXPR_INDEX)
        return expression_is_local_place(expr->as.index.object);
    return false;
}

bool expression_is_borrowable(const Expr *expr) {
    if (expression_is_local_place(expr)) return true;
    if (expr == NULL || expr->kind != EXPR_CALL ||
        expr->as.call.callee->kind != EXPR_NAME ||
        expr->as.call.arguments.count == 0U)
        return false;
    const char *name = expr->as.call.callee->as.name;
    bool projection =
        strcmp(name, "List::Get") == 0 ||
        strcmp(name, "Queue::Peek") == 0 ||
        strcmp(name, "Stack::Peek") == 0 ||
        strcmp(name, "Dictionary::Get") == 0 ||
        strcmp(name, "Dictionary::KeyAt") == 0 ||
        strcmp(name, "Dictionary::ValueAt") == 0;
    return projection && expression_is_local_place(
        expr->as.call.arguments.items[0]);
}

IrValueId lower_local_place_borrow(
    IrBuilder *builder, const Expr *expr,
    IrValueId *borrowed_values, size_t *borrowed_count
) {
    if (*borrowed_count >= 64U) {
        lang_diag(builder->diagnostics, expr->span,
                  "IR place nesting limit exceeded");
        builder->failed = true;
        return IR_INVALID_ID;
    }
    if (expr->kind == EXPR_NAME) {
        IrInstruction *load = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD,
            ir_intern_type(builder->module, expr->type),
            NULL, 0U, expr->span);
        if (load == NULL) return IR_INVALID_ID;
        load->index = ir_find_local(
            builder, expr->resolved_local_id, expr->span);
        borrowed_values[(*borrowed_count)++] = load->result;
        return load->result;
    }
    if (expr->kind == EXPR_FIELD) {
        if (expr->as.field.object->type != NULL &&
            expr->as.field.object->type->kind == TYPE_OPTION &&
            strcmp(expr->as.field.field, "Value") == 0) {
            IrValueId option = lower_local_place_borrow(
                builder, expr->as.field.object,
                borrowed_values, borrowed_count);
            if (option == IR_INVALID_ID) return IR_INVALID_ID;
            IrInstruction *some = ir_append_instruction(
                builder, IR_OP_ENUM_IS,
                ir_intern_type(builder->module, &ir_bool_type),
                &option, 1U, expr->span);
            if (some == NULL) return IR_INVALID_ID;
            some->auxiliary = 1U;
            some->symbol = "Option::Some";
            some->symbol_length = strlen(some->symbol);
            IrBlockId present = ir_add_block(builder->function);
            IrBlockId absent = ir_add_block(builder->function);
            ir_set_terminator(
                builder, IR_TERM_BRANCH, some->result,
                present, absent, expr->span);
            builder->current = absent;
            ir_set_terminator(
                builder, IR_TERM_TRAP, IR_INVALID_ID,
                IR_INVALID_ID, IR_INVALID_ID, expr->span);
            builder->current = present;
            IrInstruction *payload = ir_append_instruction(
                builder, IR_OP_ENUM_PAYLOAD_BORROW,
                ir_intern_type(builder->module, expr->type),
                &option, 1U, expr->span);
            if (payload == NULL) return IR_INVALID_ID;
            payload->auxiliary = 1U;
            payload->symbol = "Option::Some";
            payload->symbol_length = strlen(payload->symbol);
            borrowed_values[(*borrowed_count)++] = payload->result;
            return payload->result;
        }
        if (expr->as.field.object->kind == EXPR_NAME) {
            const Expr *owner = expr->as.field.object;
            IrInstruction *field = ir_append_instruction(
                builder, IR_OP_LOCAL_FIELD_BORROW,
                ir_intern_type(builder->module, expr->type),
                NULL, 0U, expr->span);
            if (field == NULL) return IR_INVALID_ID;
            field->index = ir_find_local(
                builder, owner->resolved_local_id, owner->span);
            field->auxiliary = ir_field_index(
                owner->type, expr->as.field.field);
            field->symbol = expr->as.field.field;
            field->symbol_length = strlen(field->symbol);
            borrowed_values[(*borrowed_count)++] = field->result;
            return field->result;
        }
        IrValueId owner = lower_local_place_borrow(
            builder, expr->as.field.object,
            borrowed_values, borrowed_count);
        if (owner == IR_INVALID_ID) return IR_INVALID_ID;
        IrInstruction *field = ir_append_instruction(
            builder, IR_OP_FIELD_GET,
            ir_intern_type(builder->module, expr->type),
            &owner, 1U, expr->span);
        if (field == NULL) return IR_INVALID_ID;
        field->index = ir_field_index(
            expr->as.field.object->type, expr->as.field.field);
        field->auxiliary = 1U;
        field->symbol = expr->as.field.field;
        field->symbol_length = strlen(field->symbol);
        borrowed_values[(*borrowed_count)++] = field->result;
        return field->result;
    }
    if (expr->kind == EXPR_INDEX) {
        IrValueId aggregate = lower_local_place_borrow(
            builder, expr->as.index.object,
            borrowed_values, borrowed_count);
        if (aggregate == IR_INVALID_ID) return IR_INVALID_ID;
        IrValueId index = ir_lower_expr(
            builder, expr->as.index.index);
        IrValueId operands[2] = {aggregate, index};
        IrInstruction *item = ir_append_instruction(
            builder, IR_OP_INDEX_GET,
            ir_intern_type(builder->module, expr->type),
            operands, 2U, expr->span);
        if (item == NULL) return IR_INVALID_ID;
        item->auxiliary = expr->as.index.unchecked ? 1U : 0U;
        item->integer = 1U;
        borrowed_values[(*borrowed_count)++] = item->result;
        return item->result;
    }
    return IR_INVALID_ID;
}

void discard_local_place_borrows(
    IrBuilder *builder, const IrValueId *values,
    size_t count, LangSpan span
) {
    while (count != 0U) {
        IrValueId value = values[--count];
        IrInstruction *discard = ir_append_instruction(
            builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
            &value, 1U, span);
        if (discard != NULL) discard->auxiliary = 1U;
    }
}

IrValueId lower_borrowed_expr(
    IrBuilder *builder, const Expr *expr,
    IrValueId *borrowed_values, size_t *borrowed_count
) {
    if (expression_is_local_place(expr))
        return lower_local_place_borrow(
            builder, expr, borrowed_values, borrowed_count);
    if (!expression_is_borrowable(expr)) return IR_INVALID_ID;

    const char *name = expr->as.call.callee->as.name;
    IrValueId collection = lower_local_place_borrow(
        builder, expr->as.call.arguments.items[0],
        borrowed_values, borrowed_count);
    if (collection == IR_INVALID_ID) return IR_INVALID_ID;
    IrOpcode opcode;
    IrValueId operands[2] = {collection, IR_INVALID_ID};
    size_t operand_count = 1U;
    if (strcmp(name, "Queue::Peek") == 0)
        opcode = IR_OP_QUEUE_FRONT_BORROW;
    else if (strcmp(name, "Stack::Peek") == 0)
        opcode = IR_OP_STACK_TOP_BORROW;
    else {
        operands[1] = ir_lower_expr(
            builder, expr->as.call.arguments.items[1]);
        operand_count = 2U;
        if (strcmp(name, "List::Get") == 0)
            opcode = IR_OP_LIST_ELEMENT_BORROW;
        else if (strcmp(name, "Dictionary::Get") == 0)
            opcode = IR_OP_DICTIONARY_GET_BORROW;
        else if (strcmp(name, "Dictionary::KeyAt") == 0)
            opcode = IR_OP_DICTIONARY_KEY_BORROW;
        else
            opcode = IR_OP_DICTIONARY_VALUE_BORROW;
    }
    IrInstruction *borrow = ir_append_instruction(
        builder, opcode,
        ir_intern_type(builder->module, expr->type),
        operands, operand_count, expr->span);
    if (borrow == NULL) return IR_INVALID_ID;
    if (*borrowed_count >= 64U) {
        lang_diag(builder->diagnostics, expr->span,
                  "IR place nesting limit exceeded");
        builder->failed = true;
        return IR_INVALID_ID;
    }
    borrowed_values[(*borrowed_count)++] = borrow->result;
    return borrow->result;
}

static void emit_store_to_out_place(
    IrBuilder *builder, const Expr *place, IrValueId value,
    LangSpan span
) {
    IrInstruction *store = NULL;
    if (place->kind == EXPR_NAME) {
        store = ir_append_instruction(
            builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
            &value, 1U, span);
        if (store != NULL)
            store->index = ir_find_local(
                builder, place->resolved_local_id, place->span);
        return;
    }
    if (place->kind == EXPR_FIELD &&
        place->as.field.object->kind == EXPR_NAME) {
        const Expr *owner = place->as.field.object;
        store = ir_append_instruction(
            builder, IR_OP_LOCAL_FIELD_SET, IR_INVALID_ID,
            &value, 1U, span);
        if (store != NULL) {
            store->index = ir_find_local(
                builder, owner->resolved_local_id, owner->span);
            store->auxiliary = ir_field_index(
                owner->type, place->as.field.field);
            store->symbol = place->as.field.field;
            store->symbol_length = strlen(store->symbol);
        }
        return;
    }
    lang_diag(builder->diagnostics, place->span,
              "IR lowering does not support this out destination");
    builder->failed = true;
}

static IrValueId emit_structural_default(
    IrBuilder *builder, const Type *type, LangSpan span
) {
    IrTypeId ir_type = ir_intern_type(builder->module, type);
    IrInstruction *value = NULL;
    if (type == NULL) return IR_INVALID_ID;
    switch (type->kind) {
        case TYPE_BOOL:
            value = ir_append_instruction(
                builder, IR_OP_CONST_BOOL, ir_type, NULL, 0U, span);
            if (value != NULL) value->integer = 0U;
            return value != NULL ? value->result : IR_INVALID_ID;
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
        case TYPE_ISIZE: case TYPE_U8: case TYPE_U16: case TYPE_U32:
        case TYPE_U64: case TYPE_USIZE: case TYPE_CHAR:
            value = ir_append_instruction(
                builder, IR_OP_CONST_INT, ir_type, NULL, 0U, span);
            if (value != NULL) value->integer = 0U;
            return value != NULL ? value->result : IR_INVALID_ID;
        case TYPE_F32: case TYPE_F64:
            value = ir_append_instruction(
                builder, IR_OP_CONST_FLOAT, ir_type, NULL, 0U, span);
            if (value != NULL) value->floating = 0.0;
            return value != NULL ? value->result : IR_INVALID_ID;
        default:
            break;
    }
    if (type->kind != TYPE_NAMED || type->declaration == NULL ||
        type->declaration->kind != DECL_STRUCT)
        return IR_INVALID_ID;
    const Decl *declaration = type->declaration;
    size_t count = declaration->as.structure.field_count;
    IrValueId *fields = ir_resize(NULL, count, sizeof(*fields));
    uint32_t *labels = ir_resize(NULL, count, sizeof(*labels));
    for (size_t field = 0U; field < count; ++field) {
        Type *field_type = lang_checker_resolve_aggregate_member(
            builder->source, builder->diagnostics, type, field);
        fields[field] = emit_structural_default(
            builder, field_type, span);
        labels[field] = (uint32_t)field;
        if (fields[field] == IR_INVALID_ID) {
            free(fields);
            free(labels);
            return IR_INVALID_ID;
        }
    }
    value = ir_append_instruction(
        builder, IR_OP_AGGREGATE_MAKE, ir_type,
        fields, count, span);
    free(fields);
    if (value == NULL) {
        free(labels);
        return IR_INVALID_ID;
    }
    value->labels = labels;
    value->label_count = count;
    value->symbol = declaration->as.structure.name;
    value->symbol_length = strlen(value->symbol);
    value->index = (uint32_t)count;
    return value->result;
}

static void emit_default_out_place(
    IrBuilder *builder, const Expr *place, LangSpan span
) {
    IrInstruction *clear = NULL;
    if (place->kind == EXPR_NAME) {
        clear = ir_append_instruction(
            builder, IR_OP_LOCAL_DEFAULT, IR_INVALID_ID,
            NULL, 0U, span);
        if (clear != NULL)
            clear->index = ir_find_local(
                builder, place->resolved_local_id, place->span);
        return;
    }
    if (place->kind == EXPR_FIELD &&
        place->as.field.object->kind == EXPR_NAME) {
        const Expr *owner = place->as.field.object;
        clear = ir_append_instruction(
            builder, IR_OP_LOCAL_FIELD_DEFAULT, IR_INVALID_ID,
            NULL, 0U, span);
        if (clear != NULL) {
            clear->index = ir_find_local(
                builder, owner->resolved_local_id, owner->span);
            clear->auxiliary = ir_field_index(
                owner->type, place->as.field.field);
            clear->symbol = place->as.field.field;
            clear->symbol_length = strlen(clear->symbol);
        }
        return;
    }
    lang_diag(builder->diagnostics, place->span,
              "IR lowering does not support this out destination");
    builder->failed = true;
}

IrValueId lower_call(IrBuilder *builder, const Expr *expr) {
    const char *callee_name =
        expr->as.call.callee->kind == EXPR_NAME
        ? expr->as.call.callee->as.name : NULL;
    if (callee_name != NULL &&
        strcmp(callee_name, "ArenaAlloc") == 0) {
        IrValueId borrowed_values[64];
        size_t borrowed_count = 0U;
        IrValueId operands[2] = {
            lower_borrowed_expr(
                builder, expr->as.call.arguments.items[0],
                borrowed_values, &borrowed_count),
            ir_lower_expr(builder, expr->as.call.arguments.items[1])
        };
        IrInstruction *allocation = ir_append_instruction(
            builder, IR_OP_RAW_ALLOC,
            ir_intern_type(builder->module, expr->type),
            operands, 2U, expr->span);
        discard_local_place_borrows(
            builder, borrowed_values, borrowed_count, expr->span);
        return allocation != NULL
             ? allocation->result : IR_INVALID_ID;
    }
    if (callee_name != NULL &&
        strcmp(callee_name, "raw_load_i64") == 0) {
        IrValueId pointer = ir_lower_expr(
            builder, expr->as.call.arguments.items[0]);
        IrInstruction *load = ir_append_instruction(
            builder, IR_OP_RAW_LOAD,
            ir_intern_type(builder->module, expr->type),
            &pointer, 1U, expr->span);
        return load != NULL ? load->result : IR_INVALID_ID;
    }
    if (callee_name != NULL &&
        strcmp(callee_name, "raw_store_i64") == 0) {
        IrValueId operands[2] = {
            ir_lower_expr(builder, expr->as.call.arguments.items[0]),
            ir_lower_expr(builder, expr->as.call.arguments.items[1])
        };
        IrInstruction *store = ir_append_instruction(
            builder, IR_OP_RAW_STORE,
            ir_intern_type(builder->module, expr->type),
            operands, 2U, expr->span);
        return store != NULL ? store->result : IR_INVALID_ID;
    }
    if (callee_name != NULL &&
        ((expr->resolved_decl != NULL &&
          expr->resolved_decl->kind == DECL_ENUM) ||
         strcmp(callee_name, "Option::Some") == 0 ||
         strcmp(callee_name, "Option::None") == 0 ||
         strcmp(callee_name, "Result::Ok") == 0 ||
         strcmp(callee_name, "Result::Err") == 0))
        return lower_enum_constructor(builder, expr, callee_name);
    if (callee_name != NULL &&
        strcmp(callee_name, "List::Get") == 0 &&
        expr->as.call.arguments.count == 2U &&
        ir_type_requires_custom_copy(builder, expr->type)) {
        const Expr *list_expr = expr->as.call.arguments.items[0];
        bool borrowed_list = false;
        IrValueId list = lower_borrowed_collection_place(
            builder, list_expr, &borrowed_list);
        if (list == IR_INVALID_ID) return IR_INVALID_ID;
        IrValueId index = ir_lower_expr(
            builder, expr->as.call.arguments.items[1]);
        IrValueId operands[2] = {list, index};
        IrInstruction *element = ir_append_instruction(
            builder, IR_OP_LIST_ELEMENT_BORROW,
            ir_intern_type(builder->module, expr->type),
            operands, 2U, expr->span);
        if (element == NULL) return IR_INVALID_ID;
        IrValueId copied = ir_emit_recursive_copy(
            builder, expr->type, element->result, expr->span, false);
        IrInstruction *discard = ir_append_instruction(
            builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
            &list, 1U, expr->span);
        if (discard != NULL)
            discard->auxiliary = borrowed_list ? 1U : 0U;
        return copied;
    }
    if (callee_name != NULL &&
        (strcmp(callee_name, "Queue::Peek") == 0 ||
         strcmp(callee_name, "Stack::Peek") == 0) &&
        expr->as.call.arguments.count == 1U &&
        ir_type_requires_custom_copy(builder, expr->type)) {
        const Expr *collection_expr =
            expr->as.call.arguments.items[0];
        bool borrowed_collection = false;
        IrValueId collection = lower_borrowed_collection_place(
            builder, collection_expr, &borrowed_collection);
        if (collection == IR_INVALID_ID) return IR_INVALID_ID;
        IrInstruction *element = ir_append_instruction(
            builder,
            strcmp(callee_name, "Queue::Peek") == 0
                ? IR_OP_QUEUE_FRONT_BORROW
                : IR_OP_STACK_TOP_BORROW,
            ir_intern_type(builder->module, expr->type),
            &collection, 1U, expr->span);
        if (element == NULL) return IR_INVALID_ID;
        IrValueId copied = ir_emit_recursive_copy(
            builder, expr->type, element->result,
            expr->span, false);
        IrInstruction *discard = ir_append_instruction(
            builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
            &collection, 1U, expr->span);
        if (discard != NULL)
            discard->auxiliary = borrowed_collection ? 1U : 0U;
        return copied;
    }
    if (callee_name != NULL &&
        strcmp(callee_name, "Dictionary::Get") == 0 &&
        expr->as.call.arguments.count == 2U &&
        ir_type_requires_custom_copy(builder, expr->type)) {
        const Expr *dictionary_expr =
            expr->as.call.arguments.items[0];
        bool borrowed_dictionary = false;
        IrValueId dictionary = lower_borrowed_collection_place(
            builder, dictionary_expr, &borrowed_dictionary);
        if (dictionary == IR_INVALID_ID) return IR_INVALID_ID;
        IrValueId key = ir_lower_expr(
            builder, expr->as.call.arguments.items[1]);
        IrValueId operands[2] = {dictionary, key};
        IrInstruction *mapped = ir_append_instruction(
            builder, IR_OP_DICTIONARY_GET_BORROW,
            ir_intern_type(builder->module, expr->type),
            operands, 2U, expr->span);
        if (mapped == NULL) return IR_INVALID_ID;
        IrValueId copied = ir_emit_recursive_copy(
            builder, expr->type, mapped->result,
            expr->span, false);
        IrInstruction *discard = ir_append_instruction(
            builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
            &dictionary, 1U, expr->span);
        if (discard != NULL)
            discard->auxiliary = borrowed_dictionary ? 1U : 0U;
        return copied;
    }
    if (callee_name != NULL &&
        (strcmp(callee_name, "Dictionary::KeyAt") == 0 ||
         strcmp(callee_name, "Dictionary::ValueAt") == 0) &&
        expr->as.call.arguments.count == 2U) {
        const Expr *dictionary_expr = expr->as.call.arguments.items[0];
        bool borrowed_dictionary = false;
        IrValueId dictionary = lower_borrowed_collection_place(
            builder, dictionary_expr, &borrowed_dictionary);
        if (dictionary == IR_INVALID_ID) return IR_INVALID_ID;
        IrValueId index = ir_lower_expr(
            builder, expr->as.call.arguments.items[1]);
        IrValueId operands[2] = {dictionary, index};
        IrTypeId result_type = ir_intern_type(builder->module, expr->type);
        IrInstruction *element = ir_append_instruction(
            builder,
            strcmp(callee_name, "Dictionary::KeyAt") == 0
                ? IR_OP_DICTIONARY_KEY_BORROW
                : IR_OP_DICTIONARY_VALUE_BORROW,
            result_type, operands, 2U, expr->span);
        if (element == NULL) return IR_INVALID_ID;
        IrValueId copied = ir_type_requires_custom_copy(builder, expr->type)
            ? ir_emit_recursive_copy(
                builder, expr->type, element->result, expr->span, false)
            : builder->module->types[result_type].copy_policy != IR_COPY_TRIVIAL
                ? emit_plain_clone(
                    builder, expr->type, element->result, expr->span)
                : element->result;
        IrInstruction *discard = ir_append_instruction(
            builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
            &dictionary, 1U, expr->span);
        if (discard != NULL)
            discard->auxiliary = borrowed_dictionary ? 1U : 0U;
        return copied;
    }
    bool queue_try_peek = callee_name != NULL &&
        strcmp(callee_name, "Queue::TryPeek") == 0;
    bool stack_try_peek = callee_name != NULL &&
        strcmp(callee_name, "Stack::TryPeek") == 0;
    bool dictionary_try_get = callee_name != NULL &&
        strcmp(callee_name, "Dictionary::TryGetValue") == 0;
    size_t expected_arguments = dictionary_try_get ? 3U : 2U;
    if ((queue_try_peek || stack_try_peek || dictionary_try_get) &&
        expr->as.call.arguments.count == expected_arguments) {
        const Expr *out_place = expr->as.call.arguments.items[
            dictionary_try_get ? 2U : 1U];
        const Type *out_type = out_place->type;
        if (ir_type_requires_custom_copy(builder, out_type)) {
            const Expr *collection_expr =
                expr->as.call.arguments.items[0];
            bool borrowed_collection = false;
            IrValueId collection = lower_borrowed_collection_place(
                builder, collection_expr, &borrowed_collection);
            if (collection == IR_INVALID_ID) return IR_INVALID_ID;
            IrTypeId usize_ir = ir_intern_type(
                builder->module, &ir_usize_type);
            IrTypeId bool_ir = ir_intern_type(
                builder->module, &ir_bool_type);
            IrInstruction *count = ir_append_instruction(
                builder, IR_OP_COLLECTION_COUNT, usize_ir,
                &collection, 1U, expr->span);
            if (count == NULL) return IR_INVALID_ID;
            IrValueId count_value = count->result;
            uint32_t count_local = IR_INVALID_ID;
            if (dictionary_try_get) {
                count_local = ir_add_synthetic_local(
                    builder, "<dictionary-count>", usize_ir);
                IrInstruction *count_store = ir_append_instruction(
                    builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                    &count_value, 1U, expr->span);
                if (count_store == NULL) return IR_INVALID_ID;
                count_store->index = count_local;
            }
            IrInstruction *present = NULL;
            IrValueId position = IR_INVALID_ID;
            uint32_t position_local = IR_INVALID_ID;
            if (dictionary_try_get) {
                IrValueId key = ir_lower_expr(
                    builder, expr->as.call.arguments.items[1]);
                IrValueId operands[2] = {collection, key};
                IrInstruction *find = ir_append_instruction(
                    builder, IR_OP_DICTIONARY_FIND, usize_ir,
                    operands, 2U, expr->span);
                if (find == NULL) return IR_INVALID_ID;
                position_local = ir_add_synthetic_local(
                    builder, "<dictionary-position>", usize_ir);
                IrValueId found_position = find->result;
                IrInstruction *position_store = ir_append_instruction(
                    builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                    &found_position, 1U, expr->span);
                if (position_store == NULL) return IR_INVALID_ID;
                position_store->index = position_local;
                IrInstruction *position_load = ir_append_instruction(
                    builder, IR_OP_LOCAL_LOAD, usize_ir,
                    NULL, 0U, expr->span);
                if (position_load == NULL) return IR_INVALID_ID;
                position_load->index = position_local;
                position = position_load->result;
                IrInstruction *count_load = ir_append_instruction(
                    builder, IR_OP_LOCAL_LOAD, usize_ir,
                    NULL, 0U, expr->span);
                if (count_load == NULL) return IR_INVALID_ID;
                count_load->index = count_local;
                IrValueId comparison[2] = {
                    position, count_load->result
                };
                present = ir_append_instruction(
                    builder, IR_OP_LESS, bool_ir,
                    comparison, 2U, expr->span);
            } else {
                IrInstruction *zero = ir_append_instruction(
                    builder, IR_OP_CONST_INT, usize_ir,
                    NULL, 0U, expr->span);
                if (zero == NULL) return IR_INVALID_ID;
                zero->integer = 0U;
                IrValueId comparison[2] = {
                    count_value, zero->result
                };
                present = ir_append_instruction(
                    builder, IR_OP_GREATER, bool_ir,
                    comparison, 2U, expr->span);
            }
            if (present == NULL) return IR_INVALID_ID;

            IrBlockId success = ir_add_block(builder->function);
            IrBlockId failure = ir_add_block(builder->function);
            IrBlockId finish = ir_add_block(builder->function);
            uint32_t result_local = ir_add_synthetic_local(
                builder, "<conditional-out-result>", bool_ir);
            ir_set_terminator(
                builder, IR_TERM_BRANCH, present->result,
                success, failure, expr->span);

            builder->current = success;
            IrInstruction *borrow = NULL;
            if (dictionary_try_get) {
                IrInstruction *position_load = ir_append_instruction(
                    builder, IR_OP_LOCAL_LOAD, usize_ir,
                    NULL, 0U, expr->span);
                if (position_load == NULL) return IR_INVALID_ID;
                position_load->index = position_local;
                position = position_load->result;
                IrValueId operands[2] = {collection, position};
                borrow = ir_append_instruction(
                    builder, IR_OP_DICTIONARY_VALUE_BORROW,
                    ir_intern_type(builder->module, out_type),
                    operands, 2U, expr->span);
            } else {
                borrow = ir_append_instruction(
                    builder,
                    queue_try_peek ? IR_OP_QUEUE_FRONT_BORROW
                                   : IR_OP_STACK_TOP_BORROW,
                    ir_intern_type(builder->module, out_type),
                    &collection, 1U, expr->span);
            }
            if (borrow == NULL) return IR_INVALID_ID;
            IrValueId copied = ir_emit_recursive_copy(
                builder, out_type, borrow->result,
                expr->span, false);
            emit_store_to_out_place(
                builder, out_place, copied, expr->span);
            IrInstruction *success_value = ir_append_instruction(
                builder, IR_OP_CONST_BOOL, bool_ir,
                NULL, 0U, expr->span);
            if (success_value == NULL) return IR_INVALID_ID;
            success_value->integer = 1U;
            IrValueId success_result = success_value->result;
            IrInstruction *success_store = ir_append_instruction(
                builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                &success_result, 1U, expr->span);
            if (success_store != NULL)
                success_store->index = result_local;
            ir_set_terminator(
                builder, IR_TERM_JUMP, IR_INVALID_ID,
                finish, IR_INVALID_ID, expr->span);

            builder->current = failure;
            IrValueId default_value = emit_structural_default(
                builder, out_type, expr->span);
            if (default_value != IR_INVALID_ID)
                emit_store_to_out_place(
                    builder, out_place, default_value, expr->span);
            else
                emit_default_out_place(builder, out_place, expr->span);
            IrInstruction *failure_value = ir_append_instruction(
                builder, IR_OP_CONST_BOOL, bool_ir,
                NULL, 0U, expr->span);
            if (failure_value == NULL) return IR_INVALID_ID;
            failure_value->integer = 0U;
            IrValueId failure_result = failure_value->result;
            IrInstruction *failure_store = ir_append_instruction(
                builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                &failure_result, 1U, expr->span);
            if (failure_store != NULL)
                failure_store->index = result_local;
            ir_set_terminator(
                builder, IR_TERM_JUMP, IR_INVALID_ID,
                finish, IR_INVALID_ID, expr->span);

            builder->current = finish;
            IrInstruction *discard = ir_append_instruction(
                builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
                &collection, 1U, expr->span);
            if (discard != NULL)
                discard->auxiliary = borrowed_collection ? 1U : 0U;
            IrInstruction *result = ir_append_instruction(
                builder, IR_OP_LOCAL_MOVE, bool_ir,
                NULL, 0U, expr->span);
            if (result != NULL) result->index = result_local;
            return result != NULL ? result->result : IR_INVALID_ID;
        }
    }
    size_t argument_count = expr->as.call.arguments.count;
    size_t temporary_cleanup_base =
        builder->temporary_cleanup_count;
    bool indirect = expr->as.call.callee->resolved_local_id != 0U;
    size_t operand_count = argument_count + (indirect ? 1U : 0U);
    IrValueId *operands = ir_resize(
        NULL, operand_count, sizeof(*operands));
    uint32_t *borrowed_temporary_locals = ir_resize(
        NULL, argument_count, sizeof(*borrowed_temporary_locals));
    size_t borrowed_temporary_count = 0U;
    uint32_t *prepared_argument_locals = ir_resize(
        NULL, argument_count, sizeof(*prepared_argument_locals));
    IrValueId *borrowed_place_values = ir_resize(
        NULL, argument_count * 64U,
        sizeof(*borrowed_place_values));
    size_t *borrowed_place_counts = ir_resize(
        NULL, argument_count, sizeof(*borrowed_place_counts));
    if (argument_count != 0U)
        memset(
            borrowed_place_counts, 0,
            argument_count * sizeof(*borrowed_place_counts));
    for (size_t i = 0U; i < argument_count; ++i)
        prepared_argument_locals[i] = IR_INVALID_ID;
    size_t offset = 0U;
    const Type *indirect_function_type = indirect
        ? expr->as.call.callee->type : NULL;
    if (indirect)
        operands[offset++] = ir_lower_expr(builder, expr->as.call.callee);
    const Decl *target = expr->resolved_decl;
    for (size_t i = 0U; i < argument_count; ++i) {
        Expr *argument = expr->as.call.arguments.items[i];
        bool builtin_borrow =
            i == 0U &&
             (builtin_borrows_first_place(callee_name) ||
              builtin_borrows_named_first(callee_name));
        builtin_borrow = builtin_borrow ||
            builtin_borrows_argument(callee_name, i);
        bool indirect_borrow = indirect_function_type != NULL &&
            parameter_mode_is_reference(
                indirect_function_type->parameter_modes[i]);
        bool declared_borrow =
            target != NULL && target->kind == DECL_FUNCTION &&
            i < target->as.function.param_count &&
            target->as.function.params[i].borrowed;
        ParameterMode explicit_mode =
            expr->as.call.argument_modes != NULL
                ? expr->as.call.argument_modes[i]
                : PARAMETER_MODE_VALUE;
        bool explicit_borrow =
            parameter_mode_is_reference(explicit_mode);
        bool borrowed =
            indirect_borrow || declared_borrow || builtin_borrow ||
            explicit_borrow;
        bool borrowed_place = borrowed &&
            expression_is_borrowable(argument);
        if (borrowed_place) {
            operands[offset + i] = lower_borrowed_expr(
                builder, argument,
                &borrowed_place_values[i * 64U],
                &borrowed_place_counts[i]);
        } else if (borrowed) {
            IrValueId owner = ir_lower_expr(builder, argument);
            IrTypeId owner_type = ir_intern_type(
                builder->module, argument->type);
            uint32_t owner_local = ir_add_synthetic_local(
                builder, "<borrowed-temporary>", owner_type);
            IrInstruction *store = ir_append_instruction(
                builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                &owner, 1U, argument->span);
            if (store != NULL) store->index = owner_local;
            IrInstruction *load = ir_append_instruction(
                builder, IR_OP_LOCAL_LOAD, owner_type,
                NULL, 0U, argument->span);
            if (load != NULL) load->index = owner_local;
            operands[offset + i] = load != NULL
                ? load->result : IR_INVALID_ID;
            if (!ir_push_temporary_cleanup(
                    builder, owner_local, argument->span)) {
                free(operands);
                free(borrowed_temporary_locals);
                free(prepared_argument_locals);
                free(borrowed_place_values);
                free(borrowed_place_counts);
                builder->temporary_cleanup_count =
                    temporary_cleanup_base;
                return IR_INVALID_ID;
            }
            borrowed_temporary_locals[borrowed_temporary_count++] =
                owner_local;
        } else {
            IrValueId value = ir_lower_expr(builder, argument);
            IrTypeId argument_type = ir_intern_type(
                builder->module, argument->type);
            if (argument->type->requires_cleanup ||
                argument->type->managed) {
                uint32_t owner_local = ir_add_synthetic_local(
                    builder, "<prepared-argument>", argument_type);
                IrInstruction *store = ir_append_instruction(
                    builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                    &value, 1U, argument->span);
                if (store != NULL) store->index = owner_local;
                if (!ir_push_temporary_cleanup(
                        builder, owner_local, argument->span)) {
                    free(operands);
                    free(borrowed_temporary_locals);
                    free(prepared_argument_locals);
                    free(borrowed_place_values);
                    free(borrowed_place_counts);
                    builder->temporary_cleanup_count =
                        temporary_cleanup_base;
                    return IR_INVALID_ID;
                }
                prepared_argument_locals[i] = owner_local;
                operands[offset + i] = IR_INVALID_ID;
            } else {
                operands[offset + i] = value;
            }
        }
    }
    /* Argument evaluation is the prepare phase.  Keep each owned value in a
     * cleanup-tracked local until every argument has succeeded, then commit
     * ownership to the callee with adjacent, non-throwing moves. */
    for (size_t i = 0U; i < argument_count; ++i) {
        uint32_t local = prepared_argument_locals[i];
        if (local == IR_INVALID_ID) continue;
        IrInstruction *move = ir_append_instruction(
            builder, IR_OP_LOCAL_MOVE,
            builder->function->locals[local].type,
            NULL, 0U, expr->as.call.arguments.items[i]->span);
        if (move != NULL) move->index = local;
        operands[offset + i] = move != NULL
            ? move->result : IR_INVALID_ID;
    }
    bool native = target != NULL && target->kind == DECL_FUNCTION &&
                  target->as.function.is_extern;
    if (!indirect && target == NULL)
        native = true;
    IrOpcode opcode = indirect ? IR_OP_CALL_INDIRECT :
                        expr->as.call.virtual_dispatch
                            ? IR_OP_CALL_VIRTUAL :
                        native ? IR_OP_CALL_NATIVE : IR_OP_CALL_DIRECT;
    IrInstruction *call = ir_append_instruction(
        builder, opcode, ir_intern_type(builder->module, expr->type),
        operands, operand_count, expr->span);
    free(operands);
    if (call == NULL) {
        free(borrowed_temporary_locals);
        free(prepared_argument_locals);
        free(borrowed_place_values);
        free(borrowed_place_counts);
        builder->temporary_cleanup_count = temporary_cleanup_base;
        return IR_INVALID_ID;
    }
    call->argument_mode_count = argument_count;
    if (argument_count != 0U)
        call->argument_modes = ir_resize(
            NULL, argument_count, sizeof(*call->argument_modes));
    for (size_t i = 0U; i < argument_count; ++i) {
        ParameterMode mode = expr->as.call.argument_modes != NULL
            ? expr->as.call.argument_modes[i]
            : PARAMETER_MODE_VALUE;
        if (indirect && indirect_function_type != NULL)
            mode = indirect_function_type->parameter_modes[i];
        else if (target != NULL && target->kind == DECL_FUNCTION &&
                 i < target->as.function.param_count)
            mode = parameter_mode_from_param(
                &target->as.function.params[i]);
        if (mode == PARAMETER_MODE_VALUE && i == 0U &&
                 (builtin_borrows_first_place(callee_name) ||
                  builtin_borrows_named_first(callee_name)))
            mode = PARAMETER_MODE_IMMUTABLE_REFERENCE;
        else if (mode == PARAMETER_MODE_VALUE &&
                 builtin_borrows_argument(callee_name, i))
            mode = PARAMETER_MODE_IMMUTABLE_REFERENCE;
        call->argument_modes[i] = mode;
    }
    if (target != NULL && target->kind == DECL_FUNCTION) {
        call->symbol = target->as.function.name;
        call->symbol_length = strlen(call->symbol);
        if (!native) call->index = ir_find_function(builder->module, target);
    } else if (!indirect && expr->as.call.callee->kind == EXPR_NAME) {
        call->symbol = expr->as.call.callee->as.name;
        call->symbol_length = strlen(call->symbol);
    }
    if (native) {
        bool registry_dispatch = false;
        if (target != NULL && target->kind == DECL_FUNCTION &&
            target->as.function.is_extern) {
            registry_dispatch = builder->function->name != NULL &&
                strncmp(builder->function->name,
                        "<extern-value:", 14U) == 0;
            for (size_t i = 0U;
                 !registry_dispatch && i < operand_count; ++i)
                registry_dispatch = builder->module->types[
                    builder->function->value_types[call->operands[i]]].shape ==
                    IR_TYPE_FUNCTION;
        }
        ir_set_native_call_descriptor(
            builder, call, false, registry_dispatch);
    }
    IrValueId result = call->result;
    for (size_t i = borrowed_temporary_count; i > 0U; --i) {
        IrInstruction *drop = ir_append_instruction(
            builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
            NULL, 0U, expr->span);
        if (drop != NULL)
            drop->index = borrowed_temporary_locals[i - 1U];
    }
    builder->temporary_cleanup_count = temporary_cleanup_base;
    free(borrowed_temporary_locals);
    free(prepared_argument_locals);
    for (size_t i = 0U; i < argument_count; ++i)
        discard_local_place_borrows(
            builder, &borrowed_place_values[i * 64U],
            borrowed_place_counts[i], expr->span);
    free(borrowed_place_values);
    free(borrowed_place_counts);
    bool registered_native = target != NULL &&
        target->kind == DECL_FUNCTION && target->as.function.is_extern;
    bool builtin_may_throw = callee_name != NULL &&
        strcmp(callee_name,
               "CancellationToken::ThrowIfCancellationRequested") == 0;
    if (!native || registered_native || builtin_may_throw) {
        IrInstruction *pending = ir_append_instruction(
            builder, IR_OP_EXCEPTION_PENDING,
            ir_intern_type(builder->module, &ir_bool_type),
            NULL, 0U, expr->span);
        if (pending == NULL) return IR_INVALID_ID;
        IrBlockId exceptional = ir_add_block(builder->function);
        IrBlockId continuation = ir_add_block(builder->function);
        ir_set_terminator(builder, IR_TERM_BRANCH, pending->result,
                          exceptional, continuation, expr->span);
        builder->current = exceptional;
        if (builder->exception_count != 0U) {
            ir_emit_temporary_cleanups(builder, expr->span);
            ir_emit_cleanup(builder, &expr->error_cleanup, expr->span);
            ir_set_terminator(
                builder, IR_TERM_JUMP, IR_INVALID_ID,
                builder->exceptions[builder->exception_count - 1U].handler,
                IR_INVALID_ID, expr->span);
        } else {
            /* A propagation edge leaves the function, so clean every local
             * the lowerer has created so far.  Checker cleanup plans are
             * intentionally lexical and cannot name lowering-only locals. */
            ir_emit_function_cleanup(builder, expr->span);
            ir_set_terminator(
                builder, IR_TERM_PROPAGATE_EXCEPTION, IR_INVALID_ID,
                IR_INVALID_ID, IR_INVALID_ID, expr->span);
        }
        builder->current = continuation;
    }
    return result;
}

IrValueId lower_logical_expr(IrBuilder *builder,
                                    const Expr *expr,
                                    IrTypeId type) {
    bool conjunction = expr->as.binary.op == TOK_AND_AND;
    IrValueId left = ir_lower_expr(builder, expr->as.binary.left);
    uint32_t result_local = ir_add_synthetic_local(
        builder, "<logical-result>", type);
    IrBlockId right_block = ir_add_block(builder->function);
    IrBlockId constant_block = ir_add_block(builder->function);
    IrBlockId merge_block = ir_add_block(builder->function);
    ir_set_terminator(
        builder, IR_TERM_BRANCH, left,
        conjunction ? right_block : constant_block,
        conjunction ? constant_block : right_block,
        expr->span);

    builder->current = right_block;
    IrValueId right = ir_lower_expr(builder, expr->as.binary.right);
    IrInstruction *right_store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &right, 1U, expr->as.binary.right->span);
    if (right_store != NULL) right_store->index = result_local;
    ir_set_terminator(
        builder, IR_TERM_JUMP, IR_INVALID_ID,
        merge_block, IR_INVALID_ID, expr->span);

    builder->current = constant_block;
    IrInstruction *constant = ir_append_instruction(
        builder, IR_OP_CONST_BOOL, type, NULL, 0U, expr->span);
    if (constant != NULL)
        constant->integer = conjunction ? 0U : 1U;
    IrValueId constant_value =
        constant != NULL ? constant->result : IR_INVALID_ID;
    IrInstruction *constant_store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &constant_value, 1U, expr->span);
    if (constant_store != NULL) constant_store->index = result_local;
    ir_set_terminator(
        builder, IR_TERM_JUMP, IR_INVALID_ID,
        merge_block, IR_INVALID_ID, expr->span);

    builder->current = merge_block;
    IrInstruction *load = ir_append_instruction(
        builder, IR_OP_LOCAL_LOAD, type, NULL, 0U, expr->span);
    if (load != NULL) load->index = result_local;
    return load != NULL ? load->result : IR_INVALID_ID;
}

static bool lower_value_block_to_local(IrBuilder *builder,
                                       const Stmt *block,
                                       uint32_t result_local) {
    size_t count = block->as.block.count;
    for (size_t i = 0U; i + 1U < count; ++i) {
        ir_lower_stmt(builder, block->as.block.items[i]);
        if (ir_current_terminated(builder)) return false;
    }
    if (count == 0U) return false;
    const Stmt *tail = block->as.block.items[count - 1U];
    if (tail->kind != STMT_EXPR) return false;
    IrValueId value = ir_lower_expr(builder, tail->as.expression);
    if (ir_current_terminated(builder)) return false;
    IrInstruction *store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &value, 1U, tail->span);
    if (store != NULL) store->index = result_local;
    ir_emit_cleanup(builder, &block->exit_cleanup, block->span);
    return true;
}

IrValueId load_conditional_result(IrBuilder *builder,
                                         uint32_t result_local,
                                         IrTypeId type,
                                         LangSpan span) {
    IrOpcode opcode = ir_type_needs_cleanup(builder->module, type)
                    ? IR_OP_LOCAL_MOVE : IR_OP_LOCAL_LOAD;
    IrInstruction *load = ir_append_instruction(
        builder, opcode, type, NULL, 0U, span);
    if (load != NULL) load->index = result_local;
    return load != NULL ? load->result : IR_INVALID_ID;
}

IrValueId lower_if_expression(IrBuilder *builder, const Expr *expr,
                                     IrTypeId type) {
    IrValueId condition = ir_lower_expr(builder, expr->as.if_.condition);
    uint32_t result_local = ir_add_synthetic_local(
        builder, "<if-result>", type);
    IrBlockId then_block = ir_add_block(builder->function);
    IrBlockId else_block = ir_add_block(builder->function);
    IrBlockId merge_block = ir_add_block(builder->function);
    ir_set_terminator(builder, IR_TERM_BRANCH, condition,
                   then_block, else_block, expr->span);

    bool reaches_merge = false;
    builder->current = then_block;
    if (lower_value_block_to_local(
            builder, expr->as.if_.then_branch, result_local) &&
        !ir_current_terminated(builder)) {
        ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                       merge_block, IR_INVALID_ID, expr->span);
        reaches_merge = true;
    }

    builder->current = else_block;
    if (lower_value_block_to_local(
            builder, expr->as.if_.else_branch, result_local) &&
        !ir_current_terminated(builder)) {
        ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                       merge_block, IR_INVALID_ID, expr->span);
        reaches_merge = true;
    }

    builder->current = merge_block;
    if (!reaches_merge) {
        ir_set_terminator(builder, IR_TERM_TRAP, IR_INVALID_ID,
                       IR_INVALID_ID, IR_INVALID_ID, expr->span);
        return IR_INVALID_ID;
    }
    return load_conditional_result(
        builder, result_local, type, expr->span);
}

IrValueId lower_match_expression(IrBuilder *builder,
                                        const Expr *expr,
                                        IrTypeId type) {
    const Expr *matched_expr = expr->as.match_.value;
    const Type *matched_type = matched_expr->type;
    IrValueId matched = ir_lower_expr(builder, matched_expr);
    uint32_t matched_local = ir_add_local(
        builder, "<match>", 0U, matched_type, false);
    IrInstruction *store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &matched, 1U, matched_expr->span);
    if (store != NULL) store->index = matched_local;
    uint32_t result_local = ir_add_synthetic_local(
        builder, "<match-result>", type);

    IrBlockId merge = ir_add_block(builder->function);
    bool reaches_merge = false;
    for (size_t a = 0U; a < expr->as.match_.arm_count; ++a) {
        const MatchArm *arm = &expr->as.match_.arms[a];
        IrInstruction *matches = ir_emit_local_enum_operation(
            builder, IR_OP_LOCAL_ENUM_IS,
            ir_intern_type(builder->module, &ir_bool_type),
            matched_local, matched_type, arm->variant, arm->span);
        if (matches == NULL) return IR_INVALID_ID;
        IrBlockId arm_block = ir_add_block(builder->function);
        IrBlockId next_arm = ir_add_block(builder->function);
        ir_set_terminator(builder, IR_TERM_BRANCH, matches->result,
                       arm_block, next_arm, arm->span);

        builder->current = arm_block;
        if (arm->binding != NULL && arm->binding_type != NULL) {
            IrInstruction *payload = ir_emit_local_enum_operation(
                builder, IR_OP_LOCAL_ENUM_PAYLOAD_MOVE,
                ir_intern_type(builder->module, arm->binding_type),
                matched_local, matched_type, arm->variant, arm->span);
            if (payload == NULL) return IR_INVALID_ID;
            uint32_t binding = ir_add_local(
                builder, arm->binding, arm->binding_id,
                arm->binding_type, false);
            IrValueId value = payload->result;
            IrInstruction *binding_store = ir_append_instruction(
                builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                &value, 1U, arm->span);
            if (binding_store != NULL) binding_store->index = binding;
        } else {
            IrInstruction *drop = ir_append_instruction(
                builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                NULL, 0U, arm->span);
            if (drop != NULL) drop->index = matched_local;
        }
        if (lower_value_block_to_local(
                builder, arm->body, result_local) &&
            !ir_current_terminated(builder)) {
            ir_set_terminator(builder, IR_TERM_JUMP, IR_INVALID_ID,
                           merge, IR_INVALID_ID, arm->span);
            reaches_merge = true;
        }
        builder->current = next_arm;
    }
    IrInstruction *drop = ir_append_instruction(
        builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
        NULL, 0U, expr->span);
    if (drop != NULL) drop->index = matched_local;
    ir_set_terminator(builder, IR_TERM_TRAP, IR_INVALID_ID,
                   IR_INVALID_ID, IR_INVALID_ID, expr->span);

    builder->current = merge;
    if (!reaches_merge) {
        ir_set_terminator(builder, IR_TERM_TRAP, IR_INVALID_ID,
                       IR_INVALID_ID, IR_INVALID_ID, expr->span);
        return IR_INVALID_ID;
    }
    return load_conditional_result(
        builder, result_local, type, expr->span);
}

IrValueId ir_emit_synthetic_native_call(
    IrBuilder *builder, const char *name, const Type *result_type,
    const IrValueId *operands, size_t operand_count,
    bool borrow_first, LangSpan span) {
    IrInstruction *call = ir_append_instruction(
        builder, IR_OP_CALL_NATIVE,
        ir_intern_type(builder->module, result_type),
        operands, operand_count, span);
    if (call == NULL) return IR_INVALID_ID;
    call->symbol = name;
    call->symbol_length = strlen(name);
    call->argument_mode_count = operand_count;
    if (operand_count != 0U) {
        call->argument_modes = ir_resize(
            NULL, operand_count, sizeof(*call->argument_modes));
        for (size_t i = 0U; i < operand_count; ++i)
            call->argument_modes[i] = i == 0U && borrow_first
                ? PARAMETER_MODE_IMMUTABLE_REFERENCE
                : PARAMETER_MODE_VALUE;
    }
    ir_set_native_call_descriptor(builder, call, true, false);
    return call->result;
}

static IrValueId emit_interpolation_literal(
    IrBuilder *builder, const InterpolationPart *part) {
    IrInstruction *literal = ir_append_instruction(
        builder, IR_OP_CONST_STRING,
        ir_intern_type(builder->module, &ir_str_type),
        NULL, 0U, part->span);
    if (literal == NULL) return IR_INVALID_ID;
    literal->symbol = part->text;
    literal->symbol_length = part->text_length;
    return literal->result;
}

IrValueId lower_owned_interpolation(
    IrBuilder *builder, const Expr *expr) {
    IrValueId builder_value = ir_emit_synthetic_native_call(
        builder, "StringBuilder::New",
        &ir_string_builder_type, NULL, 0U, 0U, expr->span);
    IrTypeId builder_type = ir_intern_type(
        builder->module, &ir_string_builder_type);
    uint32_t local = ir_add_synthetic_local(
        builder, "<interpolation-builder>", builder_type);
    IrInstruction *store = ir_append_instruction(
        builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
        &builder_value, 1U, expr->span);
    if (store != NULL) store->index = local;

    for (size_t i = 0U;
         i < expr->as.interpolation.part_count; ++i) {
        const InterpolationPart *part =
            &expr->as.interpolation.parts[i];
        IrValueId borrowed_values[64];
        size_t borrowed_count = 0U;
        bool borrowed_part =
            part->expression != NULL &&
            part->borrow_owned_string;
        IrValueId value = part->expression == NULL
            ? emit_interpolation_literal(builder, part)
            : borrowed_part
                ? lower_borrowed_expr(
                      builder, part->expression,
                      borrowed_values, &borrowed_count)
                : ir_lower_expr(builder, part->expression);
        if (value == IR_INVALID_ID ||
            ir_current_terminated(builder))
            return IR_INVALID_ID;

        const Type *value_type = part->expression != NULL
            ? part->expression->type : &ir_str_type;
        if (value_type->kind != TYPE_STR &&
            value_type->kind != TYPE_STRING) {
            IrInstruction *load = ir_append_instruction(
                builder, IR_OP_LOCAL_LOAD, builder_type,
                NULL, 0U, part->span);
            if (load != NULL) load->index = local;
            IrValueId arguments[2] = {
                load != NULL
                    ? load->result : IR_INVALID_ID,
                value
            };
            IrValueId appended = ir_emit_synthetic_native_call(
                builder,
                "__interpolation_builder_append_formatted",
                &ir_unit_type, arguments, 2U, 1U,
                part->span);
            IrInstruction *discard_result =
                ir_append_instruction(
                    builder, IR_OP_VALUE_DISCARD,
                    IR_INVALID_ID, &appended, 1U,
                    part->span);
            (void)discard_result;
            continue;
        }
        IrValueId owned_text = IR_INVALID_ID;
        uint32_t owned_text_local = IR_INVALID_ID;
        IrValueId text = value;
        if (value_type->kind == TYPE_STRING && borrowed_part) {
            text = ir_emit_synthetic_native_call(
                builder, "StringView", &ir_str_type,
                &value, 1U, 1U, part->span);
        } else if (value_type->kind == TYPE_STRING) {
            owned_text = value;
        }
        if (owned_text != IR_INVALID_ID) {
            IrTypeId owned_text_type = ir_intern_type(
                builder->module, &ir_string_type);
            owned_text_local = ir_add_synthetic_local(
                builder, "<interpolation-text>",
                owned_text_type);
            IrInstruction *store_text = ir_append_instruction(
                builder, IR_OP_LOCAL_STORE, IR_INVALID_ID,
                &owned_text, 1U, part->span);
            if (store_text != NULL)
                store_text->index = owned_text_local;
            IrInstruction *load_text = ir_append_instruction(
                builder, IR_OP_LOCAL_LOAD, owned_text_type,
                NULL, 0U, part->span);
            if (load_text != NULL)
                load_text->index = owned_text_local;
            IrValueId borrowed_text =
                load_text != NULL
                    ? load_text->result : IR_INVALID_ID;
            text = ir_emit_synthetic_native_call(
                builder, "StringView", &ir_str_type,
                &borrowed_text, 1U, 1U, part->span);
        }

        IrInstruction *load = ir_append_instruction(
            builder, IR_OP_LOCAL_LOAD, builder_type,
            NULL, 0U, part->span);
        if (load != NULL) load->index = local;
        IrValueId arguments[2] = {
            load != NULL ? load->result : IR_INVALID_ID,
            text
        };
        IrValueId appended = ir_emit_synthetic_native_call(
            builder, "StringBuilder::Append", &ir_unit_type,
            arguments, 2U, 1U, part->span);
        IrInstruction *discard_result = ir_append_instruction(
            builder, IR_OP_VALUE_DISCARD, IR_INVALID_ID,
            &appended, 1U, part->span);
        (void)discard_result;
        if (owned_text_local != IR_INVALID_ID) {
            IrInstruction *drop_text = ir_append_instruction(
                builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
                NULL, 0U, part->span);
            if (drop_text != NULL)
                drop_text->index = owned_text_local;
        }
        discard_local_place_borrows(
            builder, borrowed_values,
            borrowed_count, part->span);
    }

    IrInstruction *move = ir_append_instruction(
        builder, IR_OP_LOCAL_MOVE, builder_type,
        NULL, 0U, expr->span);
    if (move != NULL) move->index = local;
    IrValueId moved_builder = move != NULL
        ? move->result : IR_INVALID_ID;
    IrValueId result = ir_emit_synthetic_native_call(
        builder, "StringBuilder::Finish", expr->type,
        move != NULL ? &moved_builder : NULL,
        move != NULL ? 1U : 0U, 0U, expr->span);
    IrInstruction *drop = ir_append_instruction(
        builder, IR_OP_LOCAL_DROP, IR_INVALID_ID,
        NULL, 0U, expr->span);
    if (drop != NULL) drop->index = local;
    return result;
}
