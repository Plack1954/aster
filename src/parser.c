#include "parser_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Stmt *new_stmt(Parser *parser, StmtKind kind, LangSpan span) {
    Stmt *stmt = lang_arena_alloc(&parser->module->arena, sizeof(*stmt));
    stmt->kind = kind;
    stmt->span = span;
    return stmt;
}

Expr *parser_parse_expression(Parser *parser);
Stmt *parser_parse_statement(Parser *parser);
Stmt *parse_block(Parser *parser);
char *parser_parse_path(Parser *parser);

static Token expect_qualified_path_part(Parser *parser) {
    if (parser->current.kind == TOK_IDENT ||
        parser->current.kind == TOK_NEW) {
        Token result = parser->current;
        parser_next(parser);
        return result;
    }
    return parser_expect(parser, TOK_IDENT, "expected qualified name");
}

static char *numeric_token_text(Parser *parser, Token token) {
    char *text = lang_arena_alloc(&parser->module->arena, token.length + 1U);
    size_t length = 0U;
    for (size_t i = 0U; i < token.length; ++i)
        if (token.start[i] != '_') text[length++] = token.start[i];
    text[length] = '\0';
    return text;
}

static Expr *parse_match_expression(Parser *parser, Expr *value,
                                    Token switch_token) {
    Expr *expr = parser_new_expr(
        parser, EXPR_MATCH,
        (LangSpan){value->span.file, value->span.start,
                   switch_token.span.end});
    ParserArrayBuilder arms = parser_array_builder(sizeof(MatchArm));
    expr->as.match_.value = value;
    parser_expect(parser, TOK_LBRACE, "expected `{` after switch value");
    while (parser->current.kind != TOK_RBRACE &&
           parser->current.kind != TOK_EOF) {
        Token arm_start = parser->current;
        const char *variant = parser_parse_path(parser);
        const char *binding = NULL;
        const char *binding_type_name = NULL;
        TypeSyntax *binding_type_syntax = NULL;
        if (parser_accept(parser, TOK_LPAREN)) {
            parse_switch_binding(
                    parser, &binding, &binding_type_name,
                    &binding_type_syntax);
            parser_expect(parser, TOK_RPAREN,
                   "expected `)` after payload binding");
        }
        parser_expect(parser, TOK_FAT_ARROW,
               "expected `=>` after switch pattern");
        Expr *arm_value = parser_parse_expression(parser);
        Stmt *arm_statement = new_stmt(
            parser, STMT_EXPR, arm_value->span);
        arm_statement->as.expression = arm_value;
        Stmt *body = new_stmt(parser, STMT_BLOCK, arm_value->span);
        body->as.block.count = 1U;
        body->as.block.items = lang_arena_alloc(
            &parser->module->arena, sizeof(*body->as.block.items));
        body->as.block.items[0] = arm_statement;
        MatchArm arm = {
                .variant=variant,
                .binding=binding,
                .binding_type_name=binding_type_name,
                .binding_type_syntax=binding_type_syntax,
                .binding_type=NULL,
                .body=body,
                .span=(LangSpan){
                    arm_start.span.file, arm_start.span.start,
                    body->span.end
                }
            };
        parser_array_push(&arms, &arm);
        if (parser->current.kind != TOK_RBRACE)
            parser_expect(parser, TOK_COMMA,
                          "expected `,` after switch expression arm");
    }
    Token close = parser_expect(parser, TOK_RBRACE,
                         "expected `}` after switch cases");
    expr->as.match_.arm_count = arms.count;
    expr->as.match_.arms = parser_array_freeze(parser, &arms);
    expr->span.end = close.span.end;
    return expr;
}

static char *decode_string_bytes(
    Parser *parser, const char *source, size_t source_length,
    size_t *decoded_length) {
    char *decoded = lang_arena_alloc(
        &parser->module->arena, source_length + 1U);
    size_t output_length = 0U;
    for (size_t i = 0U; i < source_length; ++i) {
        char value = source[i];
        if (value == '\\' && i + 1U < source_length) {
            char escape = source[++i];
            if (escape == 'n') value = '\n';
            else if (escape == 'r') value = '\r';
            else if (escape == 't') value = '\t';
            else if (escape == '0') value = '\0';
            else if (escape == '\\') value = '\\';
            else if (escape == '"') value = '"';
            else value = escape;
        }
        decoded[output_length++] = value;
    }
    decoded[output_length] = '\0';
    *decoded_length = output_length;
    return decoded;
}

static void append_interpolation_text(
    Parser *parser, ParserArrayBuilder *parts, Token string,
    size_t start, size_t end) {
    if (end <= start) return;
    size_t decoded_length = 0U;
    char *decoded = decode_string_bytes(
        parser, string.start + start, end - start,
        &decoded_length);
    InterpolationPart part = {
            .text=decoded,
            .text_length=decoded_length,
            .expression=NULL,
            .span={
                string.span.file,
                string.span.start + start,
                string.span.start + end
            }
        };
    parser_array_push(parts, &part);
}

static Expr *parse_interpolation(
    Parser *parser, Token dollar, Token string) {
    Expr *expr = parser_new_expr(
        parser, EXPR_INTERPOLATION,
        (LangSpan){
            dollar.span.file, dollar.span.start,
            string.span.end
        });
    ParserArrayBuilder parts = parser_array_builder(
        sizeof(InterpolationPart));
    if (dollar.span.end != string.span.start)
        lang_diag(
            parser->diagnostics,
            (LangSpan){
                dollar.span.file, dollar.span.end,
                string.span.start
            },
            "interpolation requires `$\"...\"` without whitespace");

    size_t content_end = string.length - 1U;
    size_t literal_start = 1U;
    size_t cursor = 1U;
    while (cursor < content_end) {
        if (string.start[cursor] == '\\') {
            cursor += cursor + 1U < content_end ? 2U : 1U;
            continue;
        }
        if (string.start[cursor] == '}') {
            lang_diag(
                parser->diagnostics,
                (LangSpan){
                    string.span.file,
                    string.span.start + cursor,
                    string.span.start + cursor + 1U
                },
                "unmatched `}` in interpolated string");
            ++cursor;
            continue;
        }
        if (string.start[cursor] != '{') {
            ++cursor;
            continue;
        }

        append_interpolation_text(
            parser, &parts, string, literal_start, cursor);
        size_t expression_start =
            string.span.start + cursor + 1U;
        Parser nested = *parser;
        nested.lexer.offset = expression_start;
        nested.current = lang_lexer_next(&nested.lexer);
        nested.previous = (Token){0};
        nested.panic = false;
        nested.stop_at_lbrace = false;
        nested.stop_at_element_slash = false;
        if (nested.current.kind == TOK_RBRACE) {
            lang_diag(
                parser->diagnostics, nested.current.span,
                "empty interpolation expression");
            cursor = nested.current.span.end -
                     string.span.start;
            literal_start = cursor;
            continue;
        }
        Expr *value = parser_parse_expression(&nested);
        if (nested.current.kind != TOK_RBRACE ||
            nested.current.span.start >= string.span.end - 1U) {
            lang_diag(
                parser->diagnostics,
                (LangSpan){
                    string.span.file,
                    expression_start,
                    string.span.end - 1U
                },
                "unterminated interpolation expression");
            parser->panic = true;
            break;
        }
        InterpolationPart part = {
                .text=NULL,
                .text_length=0U,
                .expression=value,
                .span={
                    string.span.file,
                    string.span.start + cursor,
                    nested.current.span.end
                }
            };
        parser_array_push(&parts, &part);
        parser->panic = parser->panic || nested.panic;
        cursor = nested.current.span.end -
                 string.span.start;
        literal_start = cursor;
    }
    append_interpolation_text(
        parser, &parts, string, literal_start, content_end);
    expr->as.interpolation.part_count = parts.count;
    expr->as.interpolation.parts = parser_array_freeze(parser, &parts);
    return expr;
}

Expr *parser_parse_primary(Parser *parser) {
    Token token = parser->current;
    if (parser_accept(parser, TOK_INT)) {
        Expr *expr = parser_new_expr(parser, EXPR_INT, token.span);
        char *text = numeric_token_text(parser, token);
        int base = 10;
        const char *digits = text;
        if (text[0] == '0' && text[1] != '\0') {
            if (text[1] == 'x' || text[1] == 'X') base = 16;
            else if (text[1] == 'b' || text[1] == 'B') base = 2;
            else if (text[1] == 'o' || text[1] == 'O') base = 8;
            if (base != 10) digits += 2;
        }
        errno = 0;
        expr->as.integer = strtoull(digits, NULL, base);
        if (errno == ERANGE)
            lang_diag(parser->diagnostics, token.span,
                      "integer literal exceeds `u64` range");
        return expr;
    }
    if (parser_accept(parser, TOK_FLOAT)) {
        Expr *expr = parser_new_expr(parser, EXPR_FLOAT, token.span);
        char *text = numeric_token_text(parser, token);
        expr->as.floating = strtod(text, NULL);
        return expr;
    }
    if (parser_accept(parser, TOK_STRING)) {
        Expr *expr = parser_new_expr(parser, EXPR_STRING, token.span);
        size_t source_length = token.length - 2U;
        expr->as.string.data = decode_string_bytes(
            parser, token.start + 1U, source_length,
            &expr->as.string.length);
        return expr;
    }
    if (parser_accept(parser, TOK_DOLLAR)) {
        Token string = parser_expect(
            parser, TOK_STRING,
            "expected string literal after `$`");
        return parse_interpolation(parser, token, string);
    }
    if (parser_accept(parser, TOK_TRUE) || parser_accept(parser, TOK_FALSE)) {
        Expr *expr = parser_new_expr(parser, EXPR_BOOL, token.span);
        expr->as.boolean = token.kind == TOK_TRUE;
        return expr;
    }
    if (parser_accept(parser, TOK_NULL))
        return parser_new_expr(parser, EXPR_NULL, token.span);
    if (parser_accept(parser, TOK_NEW)) {
        Expr *expr = parser_new_expr(parser, EXPR_STRUCT, token.span);
        if (parser_accept(parser, TOK_LPAREN)) {
            if (parser->current.kind != TOK_RPAREN) {
                ParserArrayBuilder arguments = parser_array_builder(
                    sizeof(Expr *));
                while (parser->current.kind != TOK_RPAREN &&
                       parser->current.kind != TOK_EOF) {
                    Expr *argument = parser_parse_expression(parser);
                    parser_array_push(&arguments, &argument);
                    if (!parser_accept(parser, TOK_COMMA)) break;
                }
                Token close = parser_expect(
                    parser, TOK_RPAREN,
                    "expected `)` after constructor arguments");
                Expr *callee = parser_new_expr(
                    parser, EXPR_NAME, token.span);
                callee->as.name = "$target::new";
                Expr *call = parser_new_expr(
                    parser, EXPR_CALL, token.span);
                call->as.call.callee = callee;
                call->as.call.arguments.count = arguments.count;
                call->as.call.arguments.items =
                    parser_array_freeze(parser, &arguments);
                call->span.end = close.span.end;
                return call;
            }
            parser_expect(parser, TOK_RPAREN,
                   "target-typed `new()` does not take constructor arguments");
            if (parser->current.kind != TOK_LBRACE) {
                Expr *callee = parser_new_expr(parser, EXPR_NAME, token.span);
                callee->as.name = "List::New";
                Expr *call = parser_new_expr(parser, EXPR_CALL, token.span);
                call->as.call.callee = callee;
                call->span.end = parser->previous.span.end;
                return call;
            }
            expr->as.structure.name = NULL;
        } else {
            Parser constructor_probe = *parser;
            Token constructor_name = constructor_probe.current;
            if (constructor_probe.current.kind == TOK_IDENT)
                parser_next(&constructor_probe);
            bool constructor_call =
                constructor_name.kind == TOK_IDENT &&
                constructor_probe.current.kind == TOK_LPAREN;
            if (constructor_call) {
                parser_next(parser);
                expr->as.structure.name =
                    parser_copy_token(parser, constructor_name);
                expr->as.structure.type_syntax = new_type_syntax(
                    parser, TYPE_SYNTAX_NAMED, constructor_name.span);
                expr->as.structure.type_syntax->as.name =
                    expr->as.structure.name;
            } else {
                expr->as.structure.name = parse_type(
                    parser, &expr->as.structure.type_syntax);
            }
            if (parser_accept(parser, TOK_LPAREN)) {
                const char *name = expr->as.structure.name;
                bool exception_constructor =
                    strcmp(name, "Exception") == 0 ||
                    strcmp(name, "FormatException") == 0 ||
                    strcmp(name, "OverflowException") == 0 ||
                    strcmp(name, "ArgumentException") == 0 ||
                    strcmp(name, "InvalidOperationException") == 0 ||
                    strcmp(name, "IOException") == 0 ||
                    strcmp(name, "JsonException") == 0 ||
                    strcmp(name, "SqliteException") == 0 ||
                    strcmp(name, "OperationCanceledException") == 0 ||
                    strcmp(name, "TaskCanceledException") == 0;
                if (!exception_constructor) {
                    ParserArrayBuilder arguments = parser_array_builder(
                        sizeof(Expr *));
                    while (parser->current.kind != TOK_RPAREN &&
                           parser->current.kind != TOK_EOF) {
                        Expr *argument = parser_parse_expression(parser);
                        parser_array_push(&arguments, &argument);
                        if (!parser_accept(parser, TOK_COMMA)) break;
                    }
                    Token close = parser_expect(
                        parser, TOK_RPAREN,
                        "expected `)` after constructor arguments");
                    Expr *callee = parser_new_expr(
                        parser, EXPR_NAME, token.span);
                    callee->as.name = join_text(
                        parser, name, "::", "new");
                    Expr *call = parser_new_expr(
                        parser, EXPR_CALL, token.span);
                    call->as.call.callee = callee;
                    call->as.call.arguments.count = arguments.count;
                    call->as.call.arguments.items =
                        parser_array_freeze(parser, &arguments);
                    call->span.end = close.span.end;
                    return call;
                }
                Expr *message = parser_parse_expression(parser);
                Token close = parser_expect(
                    parser, TOK_RPAREN,
                    "expected `)` after constructor arguments");
                ElementProperty message_field = {
                    .name="Message", .value=message, .span=message->span
                };
                ParserArrayBuilder fields = parser_array_builder(
                    sizeof(ElementProperty));
                parser_array_push(&fields, &message_field);
                expr->as.structure.field_count = 1U;
                expr->as.structure.fields =
                    parser_array_freeze(parser, &fields);
                expr->span.end = close.span.end;
                return expr;
            }
        }
        parser_expect(parser, TOK_LBRACE,
               "expected `{` after constructed type");
        ParserArrayBuilder fields = parser_array_builder(
            sizeof(ElementProperty));
        while (parser->current.kind != TOK_RBRACE &&
               parser->current.kind != TOK_EOF) {
            Token field = parser_expect(parser, TOK_IDENT,
                                 "expected initialized field name");
            const char *field_name = parser_copy_token(parser, field);
            parser_expect(parser, TOK_EQUAL,
                   "expected `=` after initialized field name");
            Expr *value = parser_parse_expression(parser);
            ElementProperty item = {
                    .name=field_name, .value=value, .span=field.span
                };
            parser_array_push(&fields, &item);
            if (!parser_accept(parser, TOK_COMMA)) break;
        }
        Token close = parser_expect(parser, TOK_RBRACE,
                             "expected `}` after field initializers");
        expr->as.structure.field_count = fields.count;
        expr->as.structure.fields = parser_array_freeze(parser, &fields);
        expr->span.end = close.span.end;
        return expr;
    }
    if (parser_accept(parser, TOK_LPAREN)) {
        Expr *expr = parser_parse_expression(parser);
        parser_expect(parser, TOK_RPAREN, "expected `)` after expression");
        return expr;
    }
    if (parser_accept(parser, TOK_LBRACKET)) {
        Expr *expr = parser_new_expr(parser, EXPR_ARRAY, token.span);
        ParserArrayBuilder items = parser_array_builder(sizeof(Expr *));
        while (parser->current.kind != TOK_RBRACKET &&
               parser->current.kind != TOK_EOF) {
            Expr *item = parser_parse_expression(parser);
            parser_array_push(&items, &item);
            if (!parser_accept(parser, TOK_COMMA)) break;
        }
        Token close = parser_expect(parser, TOK_RBRACKET, "expected `]` after array");
        expr->as.array.count = items.count;
        expr->as.array.items = parser_array_freeze(parser, &items);
        expr->span.end = close.span.end;
        return expr;
    }
    if (parser_accept(parser, TOK_LESS)) return parser_parse_element(parser, token.span);
    if (parser_accept(parser, TOK_IDENT)) {
        const char *name = parser_copy_token(parser, token);
        if (!parser->stop_at_lbrace && parser_accept(parser, TOK_LBRACE)) {
            Expr *expr = parser_new_expr(parser, EXPR_STRUCT, token.span);
            expr->as.structure.name = name;
            expr->as.structure.type_syntax = new_type_syntax(
                parser, TYPE_SYNTAX_NAMED, token.span);
            expr->as.structure.type_syntax->as.name = name;
            ParserArrayBuilder fields = parser_array_builder(
                sizeof(ElementProperty));
            while (parser->current.kind != TOK_RBRACE && parser->current.kind != TOK_EOF) {
                Token field = parser_expect(parser, TOK_IDENT, "expected field name");
                const char *field_name = parser_copy_token(parser, field);
                Expr *value;
                if (parser_accept(parser, TOK_COLON)) {
                    value = parser_parse_expression(parser);
                } else {
                    value = parser_new_expr(parser, EXPR_NAME, field.span);
                    value->as.name = field_name;
                }
                ElementProperty item = {
                    .name=field_name, .value=value, .span=field.span
                };
                parser_array_push(&fields, &item);
                if (!parser_accept(parser, TOK_COMMA)) break;
            }
            Token close = parser_expect(parser, TOK_RBRACE, "expected `}` after fields");
            expr->as.structure.field_count = fields.count;
            expr->as.structure.fields = parser_array_freeze(parser, &fields);
            expr->span.end = close.span.end;
            return expr;
        }
        Expr *expr = parser_new_expr(parser, EXPR_NAME, token.span);
        expr->as.name = name;
        return expr;
    }
    lang_diag(parser->diagnostics, token.span, "expected an expression");
    parser_next(parser);
    return parser_new_expr(parser, EXPR_INT, token.span);
}

static Expr *parse_postfix(Parser *parser) {
    Expr *expr = parser_parse_primary(parser);
    for (;;) {
        if (parser_accept(parser, TOK_LPAREN)) {
            Expr *call = parser_new_expr(parser, EXPR_CALL, expr->span);
            call->as.call.callee = expr;
            ParserArrayBuilder arguments = parser_array_builder(
                sizeof(Expr *));
            ParserArrayBuilder modes = parser_array_builder(
                sizeof(ParameterMode));
            while (parser->current.kind != TOK_RPAREN && parser->current.kind != TOK_EOF) {
                bool by_ref = parser_accept(parser, TOK_REF);
                bool by_out = !by_ref && parser_accept(parser, TOK_OUT);
                ParameterMode mode = by_out
                        ? PARAMETER_MODE_OUT
                        : by_ref
                        ? PARAMETER_MODE_MUTABLE_REFERENCE
                        : PARAMETER_MODE_VALUE;
                Expr *argument = parser_parse_expression(parser);
                parser_array_push(&modes, &mode);
                parser_array_push(&arguments, &argument);
                if (!parser_accept(parser, TOK_COMMA)) break;
            }
            Token close = parser_expect(parser, TOK_RPAREN, "expected `)` after arguments");
            call->as.call.arguments.count = arguments.count;
            call->as.call.arguments.items =
                parser_array_freeze(parser, &arguments);
            call->as.call.argument_modes =
                parser_array_freeze(parser, &modes);
            call->span.end = close.span.end;
            expr = call;
        } else if (parser_accept(parser, TOK_DOT)) {
            Token field = expect_qualified_path_part(parser);
            Expr *access = parser_new_expr(parser, EXPR_FIELD,
                                    (LangSpan){expr->span.file, expr->span.start, field.span.end});
            access->as.field.object = expr;
            access->as.field.field = parser_copy_token(parser, field);
            expr = access;
        } else if (parser_accept(parser, TOK_LBRACKET)) {
            Expr *index = parser_new_expr(parser, EXPR_INDEX, expr->span);
            index->as.index.object = expr;
            index->as.index.index = parser_parse_expression(parser);
            Token close = parser_expect(parser, TOK_RBRACKET, "expected `]` after index");
            index->span.end = close.span.end;
            expr = index;
        } else {
            break;
        }
    }
    return expr;
}

static Expr *parse_unary(Parser *parser) {
    Token token = parser->current;
    if (parser_looks_like_cast(parser)) {
            parser_next(parser);
            TypeSyntax *cast_syntax = NULL;
            const char *cast_type = parse_type(parser, &cast_syntax);
            parser_expect(parser, TOK_RPAREN,
                   "expected `)` after cast type");
            Expr *expr = parser_new_expr(parser, EXPR_CAST, token.span);
            expr->as.cast.type_name = cast_type;
            expr->as.cast.type_syntax = cast_syntax;
            expr->as.cast.value = parse_unary(parser);
            expr->span.end = expr->as.cast.value->span.end;
            return expr;
    }
    if (parser_accept(parser, TOK_MINUS) || parser_accept(parser, TOK_BANG) ||
        parser_accept(parser, TOK_TILDE) || parser_accept(parser, TOK_STAR)) {
        Expr *expr = parser_new_expr(parser, EXPR_UNARY, token.span);
        expr->as.unary.op = token.kind;
        expr->as.unary.operand = parse_unary(parser);
        expr->span.end = expr->as.unary.operand->span.end;
        return expr;
    }
    if (parser_accept(parser, TOK_TRY)) {
        Expr *expr = parser_new_expr(parser, EXPR_TRY, token.span);
        expr->as.try_.value = parse_unary(parser);
        expr->span.end = expr->as.try_.value->span.end;
        return expr;
    }
    if (parser_accept(parser, TOK_AWAIT)) {
        Expr *expr = parser_new_expr(parser, EXPR_AWAIT, token.span);
        expr->as.try_.value = parse_unary(parser);
        expr->span.end = expr->as.try_.value->span.end;
        return expr;
    }
    return parse_postfix(parser);
}

static int precedence(TokenKind kind) {
    switch (kind) {
        case TOK_OR_OR: return 1;
        case TOK_AND_AND: return 2;
        case TOK_PIPE: return 3;
        case TOK_CARET: return 4;
        case TOK_AMP: return 5;
        case TOK_EQUAL_EQUAL: case TOK_BANG_EQUAL: return 6;
        case TOK_LESS: case TOK_LESS_EQUAL: case TOK_GREATER:
        case TOK_GREATER_EQUAL: return 7;
        case TOK_SHIFT_LEFT: case TOK_SHIFT_RIGHT: return 8;
        case TOK_PLUS: case TOK_MINUS: return 9;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return 10;
        default: return 0;
    }
}

static Expr *parse_binary(Parser *parser, int minimum) {
    Expr *left = parse_unary(parser);
    while (precedence(parser->current.kind) >= minimum) {
        if (parser->stop_at_element_slash &&
            (parser->current.kind == TOK_SLASH ||
             parser->current.kind == TOK_GREATER))
            break;
        Token op = parser->current;
        int level = precedence(op.kind);
        parser_next(parser);
        Expr *right = parse_binary(parser, level + 1);
        Expr *binary = parser_new_expr(parser, EXPR_BINARY,
            (LangSpan){left->span.file, left->span.start, right->span.end});
        binary->as.binary.op = op.kind;
        binary->as.binary.left = left;
        binary->as.binary.right = right;
        left = binary;
    }
    return left;
}

static Stmt *expression_branch(Parser *parser, Expr *value) {
    Stmt *statement = new_stmt(parser, STMT_EXPR, value->span);
    statement->as.expression = value;
    Stmt *block = new_stmt(parser, STMT_BLOCK, value->span);
    block->as.block.count = 1U;
    block->as.block.items = lang_arena_alloc(
        &parser->module->arena, sizeof(*block->as.block.items));
    block->as.block.items[0] = statement;
    return block;
}

static Expr *parse_conditional(Parser *parser) {
    Expr *condition = parse_binary(parser, 1);
    if (parser->current.kind == TOK_MATCH) {
        Token switch_token = parser->current;
        parser_next(parser);
        condition = parse_match_expression(
            parser, condition, switch_token);
    }
    if (!parser_accept(parser, TOK_QUESTION)) return condition;
    Expr *when_true = parser_parse_expression(parser);
    parser_expect(parser, TOK_COLON,
                  "expected `:` in conditional expression");
    Expr *when_false = parse_conditional(parser);
    Expr *conditional = parser_new_expr(
        parser, EXPR_IF,
        (LangSpan){condition->span.file, condition->span.start,
                   when_false->span.end});
    conditional->as.if_.condition = condition;
    conditional->as.if_.then_branch = expression_branch(parser, when_true);
    conditional->as.if_.else_branch = expression_branch(parser, when_false);
    return conditional;
}

Expr *parser_parse_expression(Parser *parser) {
    Expr *expr = parse_conditional(parser);
    TokenKind assignment = parser->current.kind;
    if (assignment == TOK_EQUAL ||
        assignment == TOK_PLUS_EQUAL ||
        assignment == TOK_MINUS_EQUAL ||
        assignment == TOK_STAR_EQUAL ||
        assignment == TOK_SLASH_EQUAL ||
        assignment == TOK_PERCENT_EQUAL ||
        assignment == TOK_AMP_EQUAL ||
        assignment == TOK_PIPE_EQUAL ||
        assignment == TOK_CARET_EQUAL ||
        assignment == TOK_SHIFT_LEFT_EQUAL ||
        assignment == TOK_SHIFT_RIGHT_EQUAL) {
        parser_next(parser);
        Expr *assign = parser_new_expr(parser, EXPR_ASSIGN, expr->span);
        assign->as.assign.target = expr;
        Expr *right = parser_parse_expression(parser);
        assign->as.assign.value = right;
        assign->as.assign.compound_op =
            assignment == TOK_EQUAL ? TOK_ERROR :
            assignment == TOK_PLUS_EQUAL ? TOK_PLUS :
            assignment == TOK_MINUS_EQUAL ? TOK_MINUS :
            assignment == TOK_STAR_EQUAL ? TOK_STAR :
            assignment == TOK_SLASH_EQUAL ? TOK_SLASH :
            assignment == TOK_PERCENT_EQUAL ? TOK_PERCENT :
            assignment == TOK_AMP_EQUAL ? TOK_AMP :
            assignment == TOK_PIPE_EQUAL ? TOK_PIPE :
            assignment == TOK_CARET_EQUAL ? TOK_CARET :
            assignment == TOK_SHIFT_LEFT_EQUAL ? TOK_SHIFT_LEFT :
            TOK_SHIFT_RIGHT;
        assign->span.end = assign->as.assign.value->span.end;
        return assign;
    }
    return expr;
}

Stmt *parser_parse_opened_block(Parser *parser, Token open) {
    Stmt *block = new_stmt(parser, STMT_BLOCK, open.span);
    ParserArrayBuilder items = parser_array_builder(sizeof(Stmt *));
    while (parser->current.kind != TOK_RBRACE && parser->current.kind != TOK_EOF) {
        Stmt *item = parser_parse_statement(parser);
        parser_array_push(&items, &item);
        if (parser->panic) {
            while (parser->current.kind != TOK_SEMICOLON &&
                   parser->current.kind != TOK_RBRACE &&
                   parser->current.kind != TOK_EOF) parser_next(parser);
            (void)parser_accept(parser, TOK_SEMICOLON);
            parser->panic = false;
        }
    }
    Token close = parser_expect(parser, TOK_RBRACE, "expected `}` after block");
    block->as.block.count = items.count;
    block->as.block.items = parser_array_freeze(parser, &items);
    block->span.end = close.span.end;
    return block;
}

Stmt *parse_block(Parser *parser) {
    Token open = parser_expect(parser, TOK_LBRACE, "expected `{`");
    return parser_parse_opened_block(parser, open);
}

Stmt *parser_parse_statement(Parser *parser) {
    Token start = parser->current;
    if (parser_accept(parser, TOK_DELETE)) {
        Stmt *stmt = new_stmt(parser, STMT_DELETE, start.span);
        stmt->as.delete_value = parser_parse_expression(parser);
        Token end = parser_expect(
            parser, TOK_SEMICOLON, "expected `;` after deleted object");
        stmt->span.end = end.span.end;
        return stmt;
    }
    if (parser_accept(parser, TOK_THROW)) {
        Stmt *stmt = new_stmt(parser, STMT_THROW, start.span);
        if (parser->current.kind != TOK_SEMICOLON)
            stmt->as.throw_value = parser_parse_expression(parser);
        Token end = parser_expect(
            parser, TOK_SEMICOLON, "expected `;` after thrown value");
        stmt->span.end = end.span.end;
        return stmt;
    }
    Parser try_probe = *parser;
    if (try_probe.current.kind == TOK_TRY)
        parser_next(&try_probe);
    if (parser->current.kind == TOK_TRY &&
        try_probe.current.kind == TOK_LBRACE) {
        parser_next(parser);
        Stmt *stmt = new_stmt(parser, STMT_TRY, start.span);
        stmt->as.try_.body = parse_block(parser);
        if (parser_accept(parser, TOK_CATCH)) {
            parser_expect(parser, TOK_LPAREN, "expected `(` after `catch`");
            stmt->as.try_.catch_type_name = parse_type(
                parser, &stmt->as.try_.catch_type_syntax);
            Token name = parser_expect(
                parser, TOK_IDENT, "expected exception name after catch type");
            stmt->as.try_.catch_name = parser_copy_token(parser, name);
            parser_expect(parser, TOK_RPAREN,
                          "expected `)` after catch declaration");
            stmt->as.try_.catch_body = parse_block(parser);
        }
        if (parser_accept(parser, TOK_FINALLY))
            stmt->as.try_.finally_body = parse_block(parser);
        if (stmt->as.try_.catch_body == NULL &&
            stmt->as.try_.finally_body == NULL)
            lang_diag(parser->diagnostics, stmt->span,
                      "`try` requires a `catch` or `finally` block");
        stmt->span.end = stmt->as.try_.finally_body != NULL
            ? stmt->as.try_.finally_body->span.end
            : stmt->as.try_.catch_body != NULL
                ? stmt->as.try_.catch_body->span.end
                : stmt->as.try_.body->span.end;
        return stmt;
    }
    if (looks_like_deconstruction(parser)) {
        Stmt *stmt = new_stmt(parser, STMT_DESTRUCTURE, start.span);
        ParserArrayBuilder names = parser_array_builder(
            sizeof(const char *));
        ParserArrayBuilder types = parser_array_builder(
            sizeof(const char *));
        ParserArrayBuilder syntaxes = parser_array_builder(
            sizeof(TypeSyntax *));
        parser_expect(parser, TOK_LPAREN,
               "expected `(` before deconstruction bindings");
        while (parser->current.kind != TOK_RPAREN &&
               parser->current.kind != TOK_EOF) {
            TypeSyntax *type_syntax = NULL;
            const char *type_name = parse_type(parser, &type_syntax);
            Token name = parser_expect(
                parser, TOK_IDENT,
                "expected binding name after deconstruction type");
            const char *binding_name = parser_copy_token(parser, name);
            parser_array_push(&names, &binding_name);
            parser_array_push(&types, &type_name);
            parser_array_push(&syntaxes, &type_syntax);
            if (!parser_accept(parser, TOK_COMMA)) break;
        }
        parser_expect(parser, TOK_RPAREN,
               "expected `)` after deconstruction bindings");
        stmt->as.destructure.count = names.count;
        stmt->as.destructure.names = parser_array_freeze(parser, &names);
        stmt->as.destructure.type_names =
            parser_array_freeze(parser, &types);
        stmt->as.destructure.type_syntaxes =
            parser_array_freeze(parser, &syntaxes);
        parser_expect(parser, TOK_EQUAL,
               "expected `=` after deconstruction bindings");
        stmt->as.destructure.value = parser_parse_expression(parser);
        Token end = parser_expect(
            parser, TOK_SEMICOLON,
            "expected `;` after deconstruction");
        stmt->span.end = end.span.end;
        return stmt;
    }
    if (parser_looks_like_c_local(parser)) {
        TypeSyntax *type_syntax = NULL;
        const char *type_name = parse_type(parser, &type_syntax);
        Token name = parser_expect(parser, TOK_IDENT,
                            "expected local name after type");
        parse_declarator_suffix(parser, &type_syntax, &type_name);
        parser_expect(parser, TOK_EQUAL, "locals require an initializer");
        Expr *value = parser_parse_expression(parser);
        Token end = parser_expect(parser, TOK_SEMICOLON,
                           "expected `;` after local declaration");
        Stmt *stmt = new_stmt(
            parser, STMT_LET,
            (LangSpan){start.span.file, start.span.start, end.span.end});
        stmt->as.let.name = parser_copy_token(parser, name);
        stmt->as.let.type_name = type_name;
        stmt->as.let.type_syntax = type_syntax;
        stmt->as.let.mutable_ = true;
        stmt->as.let.value = value;
        return stmt;
    }
    if (parser_accept(parser, TOK_VAR)) {
        Token name = parser_expect(parser, TOK_IDENT, "expected local name");
        if (name.length == 1U && name.start[0] == '_')
            lang_diag(parser->diagnostics, name.span,
                      "discard assignments use `_ = expression;`, not `var _ = expression;`");
        parser_expect(parser, TOK_EQUAL, "locals require an initializer");
        Expr *value = parser_parse_expression(parser);
        Token end = parser_expect(parser, TOK_SEMICOLON, "expected `;` after local declaration");
        Stmt *stmt = new_stmt(parser, STMT_LET,
                              (LangSpan){start.span.file, start.span.start, end.span.end});
        stmt->as.let.name = parser_copy_token(parser, name);
        stmt->as.let.type_name = NULL;
        stmt->as.let.mutable_ = true;
        stmt->as.let.value = value;
        return stmt;
    }
    if (parser_accept(parser, TOK_RETURN)) {
        Stmt *stmt = new_stmt(parser, STMT_RETURN, start.span);
        if (parser->current.kind != TOK_SEMICOLON)
            stmt->as.return_value = parser_parse_expression(parser);
        Token end = parser_expect(parser, TOK_SEMICOLON, "expected `;` after return");
        stmt->span.end = end.span.end;
        return stmt;
    }
    if (parser_accept(parser, TOK_IF)) {
        Stmt *stmt = new_stmt(parser, STMT_IF, start.span);
        parser_expect(parser, TOK_LPAREN, "expected `(` after `if`");
        stmt->as.if_.condition = parser_parse_expression(parser);
        parser_expect(parser, TOK_RPAREN, "expected `)` after `if` condition");
        stmt->as.if_.then_branch = parse_block(parser);
        if (parser_accept(parser, TOK_ELSE)) {
            stmt->as.if_.else_branch = parser->current.kind == TOK_IF
                ? parser_parse_statement(parser) : parse_block(parser);
        }
        stmt->span.end = stmt->as.if_.else_branch != NULL
                       ? stmt->as.if_.else_branch->span.end
                       : stmt->as.if_.then_branch->span.end;
        return stmt;
    }
    if (parser_accept(parser, TOK_WHILE)) {
        Stmt *stmt = new_stmt(parser, STMT_WHILE, start.span);
        parser_expect(parser, TOK_LPAREN, "expected `(` after `while`");
        stmt->as.while_.condition = parser_parse_expression(parser);
        parser_expect(parser, TOK_RPAREN, "expected `)` after `while` condition");
        stmt->as.while_.body = parse_block(parser);
        stmt->span.end = stmt->as.while_.body->span.end;
        return stmt;
    }
    if (parser_accept(parser, TOK_FOR)) {
        parser_expect(parser, TOK_LPAREN, "expected `(` after `for`");
        {
            Stmt *stmt = new_stmt(parser, STMT_C_FOR, start.span);
            if (parser->current.kind != TOK_SEMICOLON)
                stmt->as.c_for.initializer = parser_parse_statement(parser);
            else
                parser_next(parser);
            if (parser->current.kind != TOK_SEMICOLON)
                stmt->as.c_for.condition = parser_parse_expression(parser);
            parser_expect(parser, TOK_SEMICOLON,
                   "expected `;` after `for` condition");
            if (parser->current.kind != TOK_RPAREN) {
                stmt->as.c_for.increment = parser_parse_expression(parser);
                if (parser->current.kind == TOK_PLUS_PLUS ||
                    parser->current.kind == TOK_MINUS_MINUS) {
                    Token op = parser->current;
                    parser_next(parser);
                    Expr *target = stmt->as.c_for.increment;
                    Expr *assign = parser_new_expr(parser, EXPR_ASSIGN,
                        (LangSpan){target->span.file,
                                   target->span.start, op.span.end});
                    assign->as.assign.target = target;
                    Expr *read = parser_new_expr(parser, EXPR_NAME,
                                          target->span);
                    if (target->kind != EXPR_NAME) {
                        lang_diag(parser->diagnostics, target->span,
                                  "`++` and `--` currently require a direct local");
                        read->as.name = "<error>";
                    } else {
                        read->as.name = target->as.name;
                    }
                    Expr *one = parser_new_expr(parser, EXPR_INT, op.span);
                    one->as.integer = 1U;
                    Expr *binary = parser_new_expr(parser, EXPR_BINARY,
                        (LangSpan){target->span.file,
                                   target->span.start, op.span.end});
                    binary->as.binary.op =
                        op.kind == TOK_PLUS_PLUS ? TOK_PLUS : TOK_MINUS;
                    binary->as.binary.left = read;
                    binary->as.binary.right = one;
                    assign->as.assign.value = binary;
                    assign->as.assign.compound_op = TOK_ERROR;
                    stmt->as.c_for.increment = assign;
                }
            }
            parser_expect(parser, TOK_RPAREN,
                   "expected `)` after `for` clauses");
            stmt->as.c_for.body = parse_block(parser);
            stmt->span.end = stmt->as.c_for.body->span.end;
            return stmt;
        }
    }
    if (parser_accept(parser, TOK_FOREACH)) {
        parser_expect(parser, TOK_LPAREN, "expected `(` after `foreach`");
        TypeSyntax *type_syntax = NULL;
        const char *type_name = parse_type(parser, &type_syntax);
        Token name = parser_expect(
            parser, TOK_IDENT, "expected loop variable after type");
        parser_expect(parser, TOK_IN, "expected `in` after loop variable");
        Stmt *stmt = new_stmt(parser, STMT_FOR, start.span);
        stmt->as.for_.name = parser_copy_token(parser, name);
        stmt->as.for_.type_name = type_name;
        stmt->as.for_.type_syntax = type_syntax;
        stmt->as.for_.foreach = true;
        stmt->as.for_.iterable = parser_parse_expression(parser);
        stmt->as.for_.borrowed = true;
        if (parser_accept(parser, TOK_DOT_DOT) ||
            parser_accept(parser, TOK_DOT_DOT_EQUAL)) {
            if (parser->previous.kind == TOK_DOT_DOT_EQUAL)
                lang_diag(
                    parser->diagnostics, parser->previous.span,
                    "inclusive ranges are not supported; use a half-open `..` range");
            stmt->as.for_.range_end = parser_parse_expression(parser);
            stmt->as.for_.borrowed = false;
        }
        parser_expect(parser, TOK_RPAREN, "expected `)` after `foreach` clause");
        stmt->as.for_.body = parse_block(parser);
        stmt->span.end = stmt->as.for_.body->span.end;
        return stmt;
    }
    if (parser_accept(parser, TOK_MATCH)) {
        Stmt *stmt = new_stmt(parser, STMT_MATCH, start.span);
        ParserArrayBuilder arms = parser_array_builder(sizeof(MatchArm));
        parser_expect(parser, TOK_LPAREN, "expected `(` after `switch`");
        stmt->as.match_.value = parser_parse_expression(parser);
        parser_expect(parser, TOK_RPAREN, "expected `)` after `switch` value");
        parser_expect(parser, TOK_LBRACE, "expected `{` after switch value");
        while (parser->current.kind != TOK_RBRACE &&
               parser->current.kind != TOK_EOF) {
            parser_expect(parser, TOK_CASE,
                   "expected `case` before switch pattern");
            Token arm_start = parser->current;
            const char *variant = parser_parse_path(parser);
            const char *binding = NULL;
            const char *binding_type_name = NULL;
            TypeSyntax *binding_type_syntax = NULL;
            if (parser_accept(parser, TOK_LPAREN)) {
                parse_switch_binding(
                    parser, &binding, &binding_type_name,
                    &binding_type_syntax);
                parser_expect(parser, TOK_RPAREN,
                       "expected `)` after payload binding");
            }
            parser_expect(parser, TOK_COLON,
                   "expected `:` after switch case");
            Stmt *body = parse_block(parser);
            MatchArm arm = {
                    .variant=variant,
                    .binding=binding,
                    .binding_type_name=binding_type_name,
                    .binding_type_syntax=binding_type_syntax,
                    .binding_type=NULL,
                    .body=body,
                    .span=(LangSpan){
                        arm_start.span.file, arm_start.span.start,
                        body->span.end
                    }
                };
            parser_array_push(&arms, &arm);
            (void)parser_accept(parser, TOK_COMMA);
        }
        Token close = parser_expect(parser, TOK_RBRACE,
                             "expected `}` after switch cases");
        stmt->as.match_.arm_count = arms.count;
        stmt->as.match_.arms = parser_array_freeze(parser, &arms);
        stmt->span.end = close.span.end;
        return stmt;
    }
    if (parser_accept(parser, TOK_BREAK)) {
        Stmt *stmt = new_stmt(parser, STMT_BREAK, start.span);
        stmt->span.end = parser_expect(parser, TOK_SEMICOLON, "expected `;` after break").span.end;
        return stmt;
    }
    if (parser_accept(parser, TOK_CONTINUE)) {
        Stmt *stmt = new_stmt(parser, STMT_CONTINUE, start.span);
        stmt->span.end = parser_expect(parser, TOK_SEMICOLON, "expected `;` after continue").span.end;
        return stmt;
    }
    if (parser_accept(parser, TOK_UNSAFE)) {
        Stmt *stmt = new_stmt(parser, STMT_UNSAFE, start.span);
        stmt->as.unsafe_body = parse_block(parser);
        stmt->span.end = stmt->as.unsafe_body->span.end;
        return stmt;
    }
    if (parser->current.kind == TOK_LBRACE) return parse_block(parser);
    Expr *expr = parser_parse_expression(parser);
    Stmt *stmt = new_stmt(parser, STMT_EXPR, expr->span);
    stmt->as.expression = expr;
    if (parser_accept(parser, TOK_SEMICOLON)) {
        stmt->expression_terminated = true;
        stmt->span.end = parser->previous.span.end;
    }
    else if (parser->current.kind == TOK_RBRACE)
        stmt->span.end = expr->span.end;
    else {
        Token end = parser_expect(parser, TOK_SEMICOLON, "expected `;` after expression");
        stmt->expression_terminated = true;
        stmt->span.end = end.span.end;
    }
    return stmt;
}
