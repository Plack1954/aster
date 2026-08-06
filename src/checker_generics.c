#include "checker_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static Expr *clone_generic_expr(Module *module, const Expr *source);

static void *json_node(Checker *checker, size_t size) {
    if (size == 0U) size = 1U;
    void *node = lang_arena_alloc(&checker->module->arena, size);
    memset(node, 0, size);
    return node;
}

static Expr *json_name(Checker *checker, LangSpan span, const char *name) {
    Expr *expr = json_node(checker, sizeof(*expr));
    expr->kind = EXPR_NAME;
    expr->span = span;
    expr->as.name = name;
    return expr;
}

static Expr *json_string(
    Checker *checker, LangSpan span, const char *value
) {
    Expr *expr = json_node(checker, sizeof(*expr));
    expr->kind = EXPR_STRING;
    expr->span = span;
    expr->as.string.data = value;
    expr->as.string.length = strlen(value);
    return expr;
}

static Expr *json_field(
    Checker *checker, LangSpan span, Expr *object, const char *field
) {
    Expr *expr = json_node(checker, sizeof(*expr));
    expr->kind = EXPR_FIELD;
    expr->span = span;
    expr->as.field.object = object;
    expr->as.field.field = field;
    return expr;
}

static Expr *json_binary(
    Checker *checker, LangSpan span, TokenKind op,
    Expr *left, Expr *right
) {
    Expr *expr = json_node(checker, sizeof(*expr));
    expr->kind = EXPR_BINARY;
    expr->span = span;
    expr->as.binary.op = op;
    expr->as.binary.left = left;
    expr->as.binary.right = right;
    return expr;
}

static Expr *json_call(
    Checker *checker, LangSpan span, const char *name,
    Expr **arguments, ParameterMode *modes, size_t count
) {
    Expr *expr = json_node(checker, sizeof(*expr));
    expr->kind = EXPR_CALL;
    expr->span = span;
    expr->as.call.callee = json_name(checker, span, name);
    expr->as.call.arguments.items = arguments;
    expr->as.call.arguments.count = count;
    expr->as.call.argument_modes = modes;
    return expr;
}

static Expr *json_call_values(
    Checker *checker, LangSpan span, const char *name,
    Expr **arguments, size_t count
) {
    ParameterMode *modes = json_node(
        checker, count * sizeof(*modes));
    return json_call(checker, span, name, arguments, modes, count);
}

static Expr *json_writer_call(
    Checker *checker, LangSpan span, const char *name,
    Expr *value
) {
    size_t count = value == NULL ? 1U : 2U;
    Expr **arguments = json_node(
        checker, count * sizeof(*arguments));
    ParameterMode *modes = json_node(
        checker, count * sizeof(*modes));
    arguments[0] = json_name(checker, span, "writer");
    modes[0] = PARAMETER_MODE_MUTABLE_REFERENCE;
    if (value != NULL) arguments[1] = value;
    return json_call(checker, span, name, arguments, modes, count);
}

static Stmt *json_expression_statement(
    Checker *checker, LangSpan span, Expr *expression
) {
    Stmt *stmt = json_node(checker, sizeof(*stmt));
    stmt->kind = STMT_EXPR;
    stmt->span = span;
    stmt->expression_terminated = true;
    stmt->as.expression = expression;
    return stmt;
}

static Stmt *json_return_statement(
    Checker *checker, LangSpan span, Expr *value
) {
    Stmt *stmt = json_node(checker, sizeof(*stmt));
    stmt->kind = STMT_RETURN;
    stmt->span = span;
    stmt->as.return_value = value;
    return stmt;
}

static Stmt *json_let_statement(
    Checker *checker, LangSpan span, const char *name,
    const char *type_name, Expr *value
) {
    Stmt *stmt = json_node(checker, sizeof(*stmt));
    stmt->kind = STMT_LET;
    stmt->span = span;
    stmt->expression_terminated = true;
    stmt->as.let.name = name;
    stmt->as.let.type_name = type_name;
    stmt->as.let.mutable_ = false;
    stmt->as.let.value = value;
    return stmt;
}

static Stmt *json_if_statement(
    Checker *checker, LangSpan span, Expr *condition, Stmt *then_branch
) {
    Stmt *stmt = json_node(checker, sizeof(*stmt));
    stmt->kind = STMT_IF;
    stmt->span = span;
    stmt->as.if_.condition = condition;
    stmt->as.if_.then_branch = then_branch;
    return stmt;
}

static Stmt *json_invalid_enum_return(
    Checker *checker, LangSpan span
) {
    return json_return_statement(
        checker, span,
        json_call(checker, span, "JsonInvalidEnum", NULL, NULL, 0U));
}

static Stmt *json_block(
    Checker *checker, LangSpan span, Stmt **items, size_t count
) {
    Stmt *stmt = json_node(checker, sizeof(*stmt));
    stmt->kind = STMT_BLOCK;
    stmt->span = span;
    stmt->as.block.items = items;
    stmt->as.block.count = count;
    return stmt;
}

static bool json_element_type(const Type *type) {
    return type != NULL && type->kind == TYPE_NAMED &&
           type->declaration != NULL &&
           type->declaration->kind == DECL_STRUCT &&
           strcmp(type->declaration->as.structure.name,
                  "JsonElement") == 0 &&
           type->declaration->module_name != NULL &&
           strcmp(type->declaration->module_name,
                  "System::Text::Json") == 0;
}

static const char *json_writer_method(const Type *type) {
    switch (type->kind) {
        case TYPE_STRING: return "JsonWriter::WriteStringValue";
        case TYPE_BOOL: return "JsonWriter::WriteBooleanValue";
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
        case TYPE_ISIZE: case TYPE_USIZE:
        case TYPE_F32: case TYPE_F64:
            return "JsonWriter::WriteNumberValue";
        default: return NULL;
    }
}

static const char *json_reader_method(const Type *type) {
    switch (type->kind) {
        case TYPE_STRING: return "JsonReadRequiredString";
        case TYPE_BOOL: return "JsonElement::GetBoolean";
        case TYPE_I8: return "JsonElement::GetSByte";
        case TYPE_I16: return "JsonElement::GetInt16";
        case TYPE_I32: return "JsonElement::GetInt32";
        case TYPE_I64: return "JsonElement::GetInt64";
        case TYPE_U8: return "JsonElement::GetByte";
        case TYPE_U16: return "JsonElement::GetUInt16";
        case TYPE_U32: return "JsonElement::GetUInt32";
        case TYPE_U64: return "JsonElement::GetUInt64";
        case TYPE_ISIZE: return "JsonElement::GetNInt";
        case TYPE_USIZE: return "JsonElement::GetNUInt";
        case TYPE_F32: return "JsonElement::GetSingle";
        case TYPE_F64: return "JsonElement::GetDouble";
        default: return NULL;
    }
}

static Stmt *synthesize_json_write_body(
    Checker *checker, Type *type, LangSpan span
) {
    const char *method = json_writer_method(type);
    if (method != NULL) {
        Stmt **items = json_node(checker, sizeof(*items));
        items[0] = json_expression_statement(
            checker, span, json_writer_call(
                checker, span, method,
                json_name(checker, span, "value")));
        return json_block(checker, span, items, 1U);
    }
    if (json_element_type(type)) {
        Stmt **items = json_node(checker, sizeof(*items));
        items[0] = json_expression_statement(
            checker, span, json_writer_call(
                checker, span, "JsonWriter::WriteValue",
                json_name(checker, span, "value")));
        return json_block(checker, span, items, 1U);
    }
    if (type->kind == TYPE_VEC || type->kind == TYPE_OPTION ||
        (type->kind == TYPE_DICTIONARY &&
         type->element->kind == TYPE_STRING)) {
        Expr **arguments = json_node(
            checker, 2U * sizeof(*arguments));
        ParameterMode *modes = json_node(
            checker, 2U * sizeof(*modes));
        arguments[0] = json_name(checker, span, "writer");
        arguments[1] = json_name(checker, span, "value");
        modes[0] = PARAMETER_MODE_MUTABLE_REFERENCE;
        Stmt **items = json_node(checker, sizeof(*items));
        items[0] = json_expression_statement(
            checker, span, json_call(
                checker, span,
                type->kind == TYPE_VEC ? "JsonWriteList" :
                type->kind == TYPE_OPTION ? "JsonWriteOption" :
                "JsonWriteDictionary",
                arguments, modes, 2U));
        return json_block(checker, span, items, 1U);
    }
    if (type->kind == TYPE_NAMED && type->declaration != NULL &&
        type->declaration->kind == DECL_ENUM &&
        !type->declaration->as.enumeration.is_union) {
        const Decl *decl = type->declaration;
        size_t count = decl->as.enumeration.variant_count;
        Stmt *match = json_node(checker, sizeof(*match));
        match->kind = STMT_MATCH;
        match->span = span;
        match->as.match_.value = json_name(checker, span, "value");
        match->as.match_.arms = json_node(
            checker, count * sizeof(*match->as.match_.arms));
        match->as.match_.arm_count = count;
        for (size_t i = 0U; i < count; ++i) {
            const char *variant =
                decl->as.enumeration.variants[i].name;
            size_t length = strlen(decl->as.enumeration.name) +
                            strlen(variant) + 3U;
            char *path = lang_arena_alloc(
                &checker->module->arena, length);
            (void)snprintf(
                path, length, "%s::%s",
                decl->as.enumeration.name, variant);
            MatchArm *arm = &match->as.match_.arms[i];
            arm->variant = path;
            arm->span = span;
            Stmt **arm_items = json_node(
                checker, sizeof(*arm_items));
            arm_items[0] = json_expression_statement(
                checker, span, json_writer_call(
                    checker, span, "JsonWriter::WriteStringValue",
                    json_string(checker, span, variant)));
            arm->body = json_block(
                checker, span, arm_items, 1U);
        }
        Stmt **items = json_node(checker, sizeof(*items));
        items[0] = match;
        return json_block(checker, span, items, 1U);
    }
    if (type->kind != TYPE_NAMED || type->declaration == NULL ||
        type->declaration->kind != DECL_STRUCT)
        return NULL;

    const Decl *decl = type->declaration;
    size_t field_count = decl->as.structure.field_count;
    size_t statement_count = 2U + field_count * 2U;
    Stmt **items = json_node(
        checker, statement_count * sizeof(*items));
    size_t next = 0U;
    items[next++] = json_expression_statement(
        checker, span, json_writer_call(
            checker, span, "JsonWriter::WriteStartObject", NULL));
    for (size_t i = 0U; i < field_count; ++i) {
        const FieldDecl *field = &decl->as.structure.fields[i];
        items[next++] = json_expression_statement(
            checker, span, json_writer_call(
                checker, span, "JsonWriter::WritePropertyName",
                json_string(checker, span, field->name)));
        Expr **arguments = json_node(
            checker, 2U * sizeof(*arguments));
        ParameterMode *modes = json_node(
            checker, 2U * sizeof(*modes));
        arguments[0] = json_name(checker, span, "writer");
        modes[0] = PARAMETER_MODE_MUTABLE_REFERENCE;
        arguments[1] = json_field(
            checker, span,
            json_name(checker, span, "value"), field->name);
        items[next++] = json_expression_statement(
            checker, span, json_call(
                checker, span, "JsonWriteTyped",
                arguments, modes, 2U));
    }
    items[next++] = json_expression_statement(
        checker, span, json_writer_call(
            checker, span, "JsonWriter::WriteEndObject", NULL));
    return json_block(checker, span, items, next);
}

static Stmt *synthesize_json_read_body(
    Checker *checker, Type *type, LangSpan span
) {
    Expr *value = NULL;
    const char *method = json_reader_method(type);
    if (method != NULL) {
        Expr **arguments = json_node(
            checker, sizeof(*arguments));
        arguments[0] = json_name(checker, span, "jsonValue");
        value = json_call_values(
            checker, span, method, arguments, 1U);
    } else if (json_element_type(type)) {
        value = json_name(checker, span, "jsonValue");
    } else if (type->kind == TYPE_VEC || type->kind == TYPE_OPTION ||
               (type->kind == TYPE_DICTIONARY &&
                type->element->kind == TYPE_STRING)) {
        Expr **arguments = json_node(
            checker, sizeof(*arguments));
        arguments[0] = json_name(checker, span, "jsonValue");
        value = json_call_values(
            checker, span,
            type->kind == TYPE_VEC ? "JsonReadList" :
            type->kind == TYPE_OPTION ? "JsonReadOption" :
            "JsonReadDictionary",
            arguments, 1U);
    } else if (type->kind == TYPE_NAMED && type->declaration != NULL &&
               type->declaration->kind == DECL_ENUM &&
               !type->declaration->as.enumeration.is_union) {
        const Decl *decl = type->declaration;
        size_t count = decl->as.enumeration.variant_count;
        Stmt **items = json_node(
            checker, (count + 2U) * sizeof(*items));
        Expr **name_arguments = json_node(
            checker, sizeof(*name_arguments));
        name_arguments[0] = json_name(checker, span, "jsonValue");
        items[0] = json_let_statement(
            checker, span, "enumName", "string",
            json_call_values(
                checker, span, "JsonReadRequiredString",
                name_arguments, 1U));
        for (size_t i = 0U; i < count; ++i) {
            const char *variant =
                decl->as.enumeration.variants[i].name;
            Expr *enum_value = json_field(
                checker, span,
                json_name(
                    checker, span,
                    decl->as.enumeration.name),
                variant);
            Stmt **return_items = json_node(
                checker, sizeof(*return_items));
            return_items[0] = json_return_statement(
                checker, span, enum_value);
            items[i + 1U] = json_if_statement(
                checker, span,
                json_binary(
                    checker, span, TOK_EQUAL_EQUAL,
                    json_name(checker, span, "enumName"),
                    json_string(checker, span, variant)),
                json_block(checker, span, return_items, 1U));
        }
        items[count + 1U] = json_invalid_enum_return(checker, span);
        return json_block(checker, span, items, count + 2U);
    } else if (type->kind == TYPE_NAMED && type->declaration != NULL &&
               type->declaration->kind == DECL_STRUCT) {
        const Decl *decl = type->declaration;
        size_t count = decl->as.structure.field_count;
        Expr *aggregate = json_node(checker, sizeof(*aggregate));
        aggregate->kind = EXPR_STRUCT;
        aggregate->span = span;
        aggregate->as.structure.fields = json_node(
            checker, count * sizeof(*aggregate->as.structure.fields));
        aggregate->as.structure.field_count = count;
        for (size_t i = 0U; i < count; ++i) {
            const FieldDecl *field = &decl->as.structure.fields[i];
            ElementProperty *property =
                &aggregate->as.structure.fields[i];
            property->name = field->name;
            property->span = span;
            Expr **property_arguments = json_node(
                checker, 2U * sizeof(*property_arguments));
            property_arguments[0] = json_name(
                checker, span, "jsonValue");
            property_arguments[1] = json_string(
                checker, span, field->name);
            Expr *property_element = json_call_values(
                checker, span, "JsonElement::GetProperty",
                property_arguments, 2U);
            Expr **read_arguments = json_node(
                checker, sizeof(*read_arguments));
            read_arguments[0] = property_element;
            property->value = json_call_values(
                checker, span, "JsonReadTyped", read_arguments, 1U);
        }
        value = aggregate;
    }
    if (value == NULL) return NULL;
    Stmt **items = json_node(checker, sizeof(*items));
    items[0] = json_return_statement(checker, span, value);
    return json_block(checker, span, items, 1U);
}

static void synthesize_typed_json_instance(
    Checker *checker, const Decl *template_decl, Decl *instance,
    Type *type, LangSpan use_span
) {
    if (template_decl->module_name == NULL ||
        strcmp(template_decl->module_name, "System::Text::Json") != 0)
        return;
    const char *name = template_decl->as.function.name;
    Stmt *body = NULL;
    if (strcmp(name, "JsonWriteTyped") == 0)
        body = synthesize_json_write_body(checker, type, use_span);
    else if (strcmp(name, "JsonReadTyped") == 0)
        body = synthesize_json_read_body(checker, type, use_span);
    else
        return;
    if (body != NULL) {
        instance->as.function.body = body;
        return;
    }
    lang_diag(
        checker->diagnostics, use_span,
        "typed JSON does not support `%s`; supported shapes are scalars, payloadless enums, JsonElement, Option<T>, List<T>, Dictionary<string, T>, and structs with supported public fields",
        type_display_name(checker, type));
}

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
        case STMT_DELETE:
            result->as.delete_value = clone_generic_expr(
                module, source->as.delete_value);
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
    if (argument_count == 1U)
        synthesize_typed_json_instance(
            checker, template_decl, instance, arguments[0], span);

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
