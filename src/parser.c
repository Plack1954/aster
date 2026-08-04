#include "parser_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void parser_next(Parser *parser) {
    parser->previous = parser->current;
    do { parser->current = lang_lexer_next(&parser->lexer); }
    while (parser->current.kind == TOK_ERROR);
}

bool parser_accept(Parser *parser, TokenKind kind) {
    if (parser->current.kind != kind) return false;
    parser_next(parser);
    return true;
}

Token parser_expect(Parser *parser, TokenKind kind, const char *message) {
    if (parser->current.kind == kind) { Token result = parser->current; parser_next(parser); return result; }
    lang_diag(parser->diagnostics, parser->current.span, "%s; found `%s`",
              message, lang_token_name(parser->current.kind));
    parser->panic = true;
    return parser->current;
}

Token parser_take_without_lookahead(
    Parser *parser, TokenKind kind, const char *message) {
    if (parser->current.kind == kind) {
        parser->previous = parser->current;
        return parser->current;
    }
    lang_diag(parser->diagnostics, parser->current.span,
              "%s; found `%s`", message,
              lang_token_name(parser->current.kind));
    parser->panic = true;
    return parser->current;
}

void *parser_grow_array(LangArena *arena, const void *old, size_t count,
                        size_t item_size) {
    void *result = lang_arena_alloc(arena, (count + 1U) * item_size);
    if (count != 0U) memcpy(result, old, count * item_size);
    return result;
}

const char *parser_copy_token(Parser *parser, Token token) {
    return lang_arena_strndup(&parser->module->arena, token.start, token.length);
}

static const char *join_text(Parser *parser, const char *left,
                             const char *middle, const char *right) {
    size_t a = strlen(left), b = strlen(middle), c = strlen(right);
    char *result = lang_arena_alloc(&parser->module->arena, a + b + c + 1U);
    memcpy(result, left, a);
    memcpy(result + a, middle, b);
    memcpy(result + a + b, right, c);
    result[a + b + c] = '\0';
    return result;
}

Expr *parser_new_expr(Parser *parser, ExprKind kind, LangSpan span) {
    Expr *expr = lang_arena_alloc(&parser->module->arena, sizeof(*expr));
    expr->kind = kind;
    expr->span = span;
    return expr;
}

static Stmt *new_stmt(Parser *parser, StmtKind kind, LangSpan span) {
    Stmt *stmt = lang_arena_alloc(&parser->module->arena, sizeof(*stmt));
    stmt->kind = kind;
    stmt->span = span;
    return stmt;
}

Expr *parser_parse_expression(Parser *parser);
Stmt *parser_parse_statement(Parser *parser);
static Stmt *parse_block(Parser *parser);
char *parser_parse_path(Parser *parser);

typedef struct TypeProbe {
    Lexer lexer;
    Token current;
} TypeProbe;

static void probe_next(TypeProbe *probe) {
    probe->current = lang_lexer_next(&probe->lexer);
}

static bool probe_accept(TypeProbe *probe, TokenKind kind) {
    if (probe->current.kind != kind) return false;
    probe_next(probe);
    return true;
}

static bool probe_type(TypeProbe *probe) {
    bool constant = probe_accept(probe, TOK_CONST);
    if (probe_accept(probe, TOK_STAR)) {
        if (probe->current.kind != TOK_CONST &&
            probe->current.kind != TOK_IDENT)
            return false;
        probe_next(probe);
        return !constant && probe_type(probe);
    }
    if (probe_accept(probe, TOK_LBRACKET)) {
        if (!probe_type(probe) ||
            !probe_accept(probe, TOK_SEMICOLON) ||
            !probe_accept(probe, TOK_INT) ||
            !probe_accept(probe, TOK_RBRACKET))
            return false;
        return true;
    }
    if (probe->current.kind != TOK_IDENT) return false;
    probe_next(probe);
    while (probe->current.kind == TOK_DOT) {
        probe_next(probe);
        if (probe->current.kind != TOK_IDENT) return false;
        probe_next(probe);
    }
    if (probe_accept(probe, TOK_LESS)) {
        if (!probe_type(probe)) return false;
        while (probe_accept(probe, TOK_COMMA))
            if (!probe_type(probe)) return false;
        if (probe->current.kind == TOK_SHIFT_RIGHT) {
            probe->current.kind = TOK_GREATER;
            ++probe->current.start;
            ++probe->current.span.start;
            probe->current.length = 1U;
        } else if (!probe_accept(probe, TOK_GREATER)) {
            return false;
        }
    }
    if (probe_accept(probe, TOK_LPAREN)) {
        if (probe->current.kind != TOK_RPAREN) {
            if (!probe_accept(probe, TOK_REF))
                (void)probe_accept(probe, TOK_OUT);
            if (!probe_type(probe)) return false;
            while (probe_accept(probe, TOK_COMMA)) {
                if (!probe_accept(probe, TOK_REF))
                    (void)probe_accept(probe, TOK_OUT);
                if (!probe_type(probe)) return false;
            }
        }
        if (!probe_accept(probe, TOK_RPAREN)) return false;
    }
    if (probe_accept(probe, TOK_QUESTION)) constant = false;
    if (probe_accept(probe, TOK_STAR)) constant = false;
    if (constant) return false;
    while (probe_accept(probe, TOK_LBRACKET)) {
        if (!probe_accept(probe, TOK_INT) ||
            !probe_accept(probe, TOK_RBRACKET))
            return false;
    }
    return true;
}

bool parser_looks_like_c_local(const Parser *parser) {
    TypeProbe probe = {parser->lexer, parser->current};
    if (!probe_type(&probe) || probe.current.kind != TOK_IDENT)
        return false;
    probe_next(&probe);
    while (probe_accept(&probe, TOK_LBRACKET)) {
        if (!probe_accept(&probe, TOK_INT) ||
            !probe_accept(&probe, TOK_RBRACKET))
            return false;
    }
    return probe.current.kind == TOK_EQUAL;
}

static bool looks_like_deconstruction(const Parser *parser) {
    if (parser->current.kind != TOK_LPAREN) return false;
    TypeProbe probe = {parser->lexer, parser->current};
    probe_next(&probe);
    size_t count = 0U;
    for (;;) {
        if (!probe_type(&probe) || probe.current.kind != TOK_IDENT)
            return false;
        probe_next(&probe);
        ++count;
        if (!probe_accept(&probe, TOK_COMMA)) break;
    }
    return count >= 2U && probe_accept(&probe, TOK_RPAREN) &&
           probe_accept(&probe, TOK_EQUAL);
}

static void expect_type_greater(Parser *parser) {
    if (parser_accept(parser, TOK_GREATER)) return;
    if (parser->current.kind == TOK_SHIFT_RIGHT) {
        parser->previous = parser->current;
        parser->previous.kind = TOK_GREATER;
        parser->previous.length = 1U;
        parser->previous.span.end =
            parser->previous.span.start + 1U;
        parser->current.kind = TOK_GREATER;
        ++parser->current.start;
        parser->current.length = 1U;
        ++parser->current.span.start;
        return;
    }
    (void)parser_expect(parser, TOK_GREATER,
                 "expected `>` after generic type arguments");
}

static const char *parse_array_declarator_suffix(
    Parser *parser, const char *base_type);

static const char *parse_type_name(Parser *parser) {
    bool constant = parser_accept(parser, TOK_CONST);
    if (parser_accept(parser, TOK_STAR)) {
        const char *qualifier = NULL;
        if (parser_accept(parser, TOK_CONST)) {
            qualifier = "const";
        } else {
            Token qualifier_token = parser_expect(
                parser, TOK_IDENT,
                "expected `mut` or `const` after `*`");
            const char *text = parser_copy_token(parser, qualifier_token);
            if (strcmp(text, "const") != 0)
                lang_diag(parser->diagnostics, qualifier_token.span,
                          "raw pointer qualifier must be `mut` or `const`");
            qualifier = text;
        }
        const char *pointee = parse_type_name(parser);
        const char *prefix = join_text(parser, "*", qualifier, " ");
        return join_text(parser, prefix, pointee, "");
    }
    if (constant && parser->current.kind == TOK_LBRACKET)
        lang_diag(parser->diagnostics, parser->current.span,
                  "`const` currently applies only to raw-pointer pointees");
    if (parser_accept(parser, TOK_LBRACKET)) {
        const char *element = parse_type_name(parser);
        parser_expect(parser, TOK_SEMICOLON, "expected `;` in array type");
        Token count = parser_expect(parser, TOK_INT, "expected array length");
        parser_expect(parser, TOK_RBRACKET, "expected `]` after array type");
        char *normalized_count =
            lang_arena_alloc(&parser->module->arena, count.length + 1U);
        size_t normalized_length = 0U;
        for (size_t i = 0U; i < count.length; ++i)
            if (count.start[i] != '_')
                normalized_count[normalized_length++] = count.start[i];
        normalized_count[normalized_length] = '\0';
        size_t length = strlen(element) + normalized_length + 4U;
        char *name = lang_arena_alloc(&parser->module->arena, length);
        (void)snprintf(name, length, "[%s;%s]", element,
                       normalized_count);
        return name;
    }
    Token name_token = parser_expect(parser, TOK_IDENT, "expected type name");
    const char *name = parser_copy_token(parser, name_token);
    if (strcmp(name, "List") == 0)
        name = "List";
    while (parser->current.kind == TOK_DOT) {
        parser_next(parser);
        Token part = parser_expect(parser, TOK_IDENT, "expected qualified type name");
        name = join_text(parser, name, "::", parser_copy_token(parser, part));
    }
    if (parser_accept(parser, TOK_LESS)) {
        bool csharp_func = strcmp(name, "Func") == 0;
        const char *arguments[17];
        size_t argument_count = 0U;
        const char *generic = join_text(parser, name, "<", "");
        bool first = true;
        while (parser->current.kind != TOK_GREATER &&
               parser->current.kind != TOK_EOF) {
            const char *argument = parse_type_name(parser);
            if (argument_count <
                sizeof(arguments) / sizeof(arguments[0]))
                arguments[argument_count++] = argument;
            generic = join_text(parser, generic, first ? "" : ",", argument);
            first = false;
            if (!parser_accept(parser, TOK_COMMA)) break;
        }
        expect_type_greater(parser);
        if (csharp_func) {
            if (argument_count == 0U) {
                lang_diag(parser->diagnostics, name_token.span,
                          "`Func` requires at least a return type");
                name = "fn()->error";
            } else {
                const char *function = "fn(";
                for (size_t i = 0U; i + 1U < argument_count; ++i)
                    function = join_text(
                        parser, function, i == 0U ? "" : ",",
                        arguments[i]);
                name = join_text(
                    parser, function, ")->",
                    arguments[argument_count - 1U]);
            }
        } else {
            name = join_text(parser, generic, ">", "");
        }
    }
    if (parser_accept(parser, TOK_LPAREN)) {
        const char *function = "fn(";
        bool first = true;
        while (parser->current.kind != TOK_RPAREN &&
               parser->current.kind != TOK_EOF) {
            bool by_ref = parser_accept(parser, TOK_REF);
            bool by_out = !by_ref && parser_accept(parser, TOK_OUT);
            const char *parameter = parse_type_name(parser);
            function = join_text(
                parser, function, first ? "" : ",",
                by_ref ? join_text(parser, "ref ", parameter, "") :
                by_out ? join_text(parser, "out ", parameter, "") :
                parameter);
            first = false;
            if (!parser_accept(parser, TOK_COMMA)) break;
        }
        parser_expect(parser, TOK_RPAREN,
               "expected `)` after function parameter types");
        name = join_text(parser, function, ")->", name);
    }
    if (parser_accept(parser, TOK_QUESTION))
        name = join_text(parser, "Option<", name, ">");
    if (parser_accept(parser, TOK_STAR)) {
        const char *prefix = constant ? "*const " : "*mut ";
        name = join_text(parser, prefix, name, "");
        constant = false;
    }
    if (constant)
        lang_diag(parser->diagnostics, name_token.span,
                  "`const` currently requires a pointer type such as `const long*`");
    return parse_array_declarator_suffix(parser, name);
}

static const char *parse_array_declarator_suffix(
    Parser *parser, const char *base_type
) {
    const char *lengths[16];
    size_t count = 0U;
    while (parser_accept(parser, TOK_LBRACKET)) {
        Token length = parser_expect(
            parser, TOK_INT, "expected fixed array length");
        parser_expect(parser, TOK_RBRACKET,
               "expected `]` after fixed array length");
        if (count >= sizeof(lengths) / sizeof(lengths[0])) {
            lang_diag(parser->diagnostics, length.span,
                      "fixed arrays are limited to 16 dimensions");
            continue;
        }
        char *normalized = lang_arena_alloc(
            &parser->module->arena, length.length + 1U);
        size_t normalized_length = 0U;
        for (size_t i = 0U; i < length.length; ++i)
            if (length.start[i] != '_')
                normalized[normalized_length++] = length.start[i];
        normalized[normalized_length] = '\0';
        lengths[count++] = normalized;
    }
    const char *type = base_type;
    while (count != 0U) {
        --count;
        size_t size = strlen(type) + strlen(lengths[count]) + 4U;
        char *array = lang_arena_alloc(&parser->module->arena, size);
        (void)snprintf(array, size, "[%s;%s]", type, lengths[count]);
        type = array;
    }
    return type;
}

static void parse_switch_binding(
    Parser *parser, const char **binding,
    const char **binding_type_name) {
    Parser probe = *parser;
    const char *candidate_type = parse_type_name(&probe);
    if (probe.current.kind == TOK_IDENT) {
        *parser = probe;
        Token name = parser->current;
        parser_next(parser);
        *binding = parser_copy_token(parser, name);
        *binding_type_name = candidate_type;
        return;
    }
    Token name = parser_expect(parser, TOK_IDENT,
                        "expected payload binding name");
    *binding = parser_copy_token(parser, name);
}

Expr *parser_parse_element(Parser *parser, LangSpan open_span);

static void parse_type_parameters(Parser *parser, Decl *decl) {
    if (!parser_accept(parser, TOK_LESS)) return;
    while (parser->current.kind != TOK_GREATER &&
           parser->current.kind != TOK_EOF) {
        Token parameter = parser_expect(
            parser, TOK_IDENT, "expected generic type parameter");
        const char *name = parser_copy_token(parser, parameter);
        for (size_t i = 0U; i < decl->type_param_count; ++i)
            if (strcmp(decl->type_params[i], name) == 0)
                lang_diag(parser->diagnostics, parameter.span,
                          "duplicate generic type parameter `%s`",
                          name);
        decl->type_params = parser_grow_array(
            &parser->module->arena, decl->type_params,
            decl->type_param_count, sizeof(*decl->type_params));
        decl->type_params[decl->type_param_count++] = name;
        if (!parser_accept(parser, TOK_COMMA)) break;
    }
    expect_type_greater(parser);
}

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

static Expr *parse_if_expression(Parser *parser, Token start) {
    Expr *expr = parser_new_expr(parser, EXPR_IF, start.span);
    parser_expect(parser, TOK_LPAREN, "expected `(` after `if`");
    expr->as.if_.condition = parser_parse_expression(parser);
    parser_expect(parser, TOK_RPAREN, "expected `)` after `if` condition");
    expr->as.if_.then_branch = parse_block(parser);
    if (!parser_accept(parser, TOK_ELSE)) {
        lang_diag(parser->diagnostics, parser->current.span,
                  "`if` expression requires an `else` branch");
        expr->as.if_.else_branch =
            new_stmt(parser, STMT_BLOCK, parser->current.span);
    } else if (parser_accept(parser, TOK_IF)) {
        Expr *nested = parse_if_expression(parser, parser->previous);
        Stmt *body = new_stmt(parser, STMT_EXPR, nested->span);
        body->as.expression = nested;
        Stmt *block = new_stmt(parser, STMT_BLOCK, nested->span);
        block->as.block.items = parser_grow_array(
            &parser->module->arena, NULL, 0U, sizeof(Stmt *));
        block->as.block.items[0] = body;
        block->as.block.count = 1U;
        expr->as.if_.else_branch = block;
    } else {
        expr->as.if_.else_branch = parse_block(parser);
    }
    expr->span.end = expr->as.if_.else_branch->span.end;
    return expr;
}

static Expr *parse_match_expression(Parser *parser, Token start) {
    Expr *expr = parser_new_expr(parser, EXPR_MATCH, start.span);
    parser_expect(parser, TOK_LPAREN, "expected `(` after `switch`");
    expr->as.match_.value = parser_parse_expression(parser);
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
        if (parser_accept(parser, TOK_LPAREN)) {
            parse_switch_binding(
                parser, &binding, &binding_type_name);
            parser_expect(parser, TOK_RPAREN,
                   "expected `)` after payload binding");
        }
        parser_expect(parser, TOK_COLON,
               "expected `:` after switch case");
        Stmt *body = parse_block(parser);
        expr->as.match_.arms = parser_grow_array(
            &parser->module->arena, expr->as.match_.arms,
            expr->as.match_.arm_count, sizeof(MatchArm));
        expr->as.match_.arms[expr->as.match_.arm_count++] =
            (MatchArm){
                .variant=variant,
                .binding=binding,
                .binding_type_name=binding_type_name,
                .binding_type=NULL,
                .body=body,
                .span=(LangSpan){
                    arm_start.span.file, arm_start.span.start,
                    body->span.end
                }
            };
        (void)parser_accept(parser, TOK_COMMA);
    }
    Token close = parser_expect(parser, TOK_RBRACE,
                         "expected `}` after switch cases");
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
    Parser *parser, Expr *expr, Token string,
    size_t start, size_t end) {
    if (end <= start) return;
    size_t decoded_length = 0U;
    char *decoded = decode_string_bytes(
        parser, string.start + start, end - start,
        &decoded_length);
    expr->as.interpolation.parts = parser_grow_array(
        &parser->module->arena,
        expr->as.interpolation.parts,
        expr->as.interpolation.part_count,
        sizeof(*expr->as.interpolation.parts));
    expr->as.interpolation.parts[
        expr->as.interpolation.part_count++] =
        (InterpolationPart){
            .text=decoded,
            .text_length=decoded_length,
            .expression=NULL,
            .span={
                string.span.file,
                string.span.start + start,
                string.span.start + end
            }
        };
}

static Expr *parse_interpolation(
    Parser *parser, Token dollar, Token string) {
    Expr *expr = parser_new_expr(
        parser, EXPR_INTERPOLATION,
        (LangSpan){
            dollar.span.file, dollar.span.start,
            string.span.end
        });
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
            parser, expr, string, literal_start, cursor);
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
        expr->as.interpolation.parts = parser_grow_array(
            &parser->module->arena,
            expr->as.interpolation.parts,
            expr->as.interpolation.part_count,
            sizeof(*expr->as.interpolation.parts));
        expr->as.interpolation.parts[
            expr->as.interpolation.part_count++] =
            (InterpolationPart){
                .text=NULL,
                .text_length=0U,
                .expression=value,
                .span={
                    string.span.file,
                    string.span.start + cursor,
                    nested.current.span.end
                }
            };
        parser->panic = parser->panic || nested.panic;
        cursor = nested.current.span.end -
                 string.span.start;
        literal_start = cursor;
    }
    append_interpolation_text(
        parser, expr, string, literal_start, content_end);
    return expr;
}

Expr *parser_parse_primary(Parser *parser) {
    Token token = parser->current;
    if (parser_accept(parser, TOK_IF))
        return parse_if_expression(parser, token);
    if (parser_accept(parser, TOK_MATCH))
        return parse_match_expression(parser, token);
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
            } else {
                expr->as.structure.name = parse_type_name(parser);
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
                    lang_diag(parser->diagnostics, expr->span,
                              "constructor-call syntax is currently supported for exception types");
                }
                Expr *message = parser_parse_expression(parser);
                Token close = parser_expect(
                    parser, TOK_RPAREN,
                    "expected `)` after constructor arguments");
                expr->as.structure.fields = parser_grow_array(
                    &parser->module->arena, NULL, 0U,
                    sizeof(ElementProperty));
                expr->as.structure.fields[0] = (ElementProperty){
                    .name="Message", .value=message, .span=message->span
                };
                expr->as.structure.field_count = 1U;
                expr->span.end = close.span.end;
                return expr;
            }
        }
        parser_expect(parser, TOK_LBRACE,
               "expected `{` after constructed type");
        while (parser->current.kind != TOK_RBRACE &&
               parser->current.kind != TOK_EOF) {
            Token field = parser_expect(parser, TOK_IDENT,
                                 "expected initialized field name");
            const char *field_name = parser_copy_token(parser, field);
            parser_expect(parser, TOK_EQUAL,
                   "expected `=` after initialized field name");
            Expr *value = parser_parse_expression(parser);
            expr->as.structure.fields = parser_grow_array(
                &parser->module->arena,
                expr->as.structure.fields,
                expr->as.structure.field_count,
                sizeof(ElementProperty));
            expr->as.structure.fields[
                expr->as.structure.field_count++] =
                (ElementProperty){
                    .name=field_name, .value=value, .span=field.span
                };
            if (!parser_accept(parser, TOK_COMMA)) break;
        }
        Token close = parser_expect(parser, TOK_RBRACE,
                             "expected `}` after field initializers");
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
        while (parser->current.kind != TOK_RBRACKET &&
               parser->current.kind != TOK_EOF) {
            expr->as.array.items = parser_grow_array(&parser->module->arena,
                expr->as.array.items, expr->as.array.count, sizeof(Expr *));
            expr->as.array.items[expr->as.array.count++] = parser_parse_expression(parser);
            if (!parser_accept(parser, TOK_COMMA)) break;
        }
        Token close = parser_expect(parser, TOK_RBRACKET, "expected `]` after array");
        expr->span.end = close.span.end;
        return expr;
    }
    if (parser_accept(parser, TOK_LESS)) return parser_parse_element(parser, token.span);
    if (parser_accept(parser, TOK_IDENT)) {
        const char *name = parser_copy_token(parser, token);
        if (!parser->stop_at_lbrace && parser_accept(parser, TOK_LBRACE)) {
            Expr *expr = parser_new_expr(parser, EXPR_STRUCT, token.span);
            expr->as.structure.name = name;
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
                expr->as.structure.fields = parser_grow_array(&parser->module->arena,
                    expr->as.structure.fields, expr->as.structure.field_count,
                    sizeof(ElementProperty));
                expr->as.structure.fields[expr->as.structure.field_count++] = item;
                if (!parser_accept(parser, TOK_COMMA)) break;
            }
            Token close = parser_expect(parser, TOK_RBRACE, "expected `}` after fields");
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
            while (parser->current.kind != TOK_RPAREN && parser->current.kind != TOK_EOF) {
                bool by_ref = parser_accept(parser, TOK_REF);
                bool by_out = !by_ref && parser_accept(parser, TOK_OUT);
                call->as.call.arguments.items = parser_grow_array(&parser->module->arena,
                    call->as.call.arguments.items, call->as.call.arguments.count,
                    sizeof(Expr *));
                if (by_ref) {
                    if (call->as.call.arguments.count >= 32U)
                        lang_diag(
                            parser->diagnostics, parser->previous.span,
                            "`ref` call arguments are limited to 32 parameters");
                    else
                        call->as.call.ref_argument_mask |=
                            UINT32_C(1) <<
                            (unsigned)call->as.call.arguments.count;
                }
                if (by_out) {
                    if (call->as.call.arguments.count >= 32U)
                        lang_diag(
                            parser->diagnostics, parser->previous.span,
                            "`out` call arguments are limited to 32 parameters");
                    else
                        call->as.call.out_argument_mask |=
                            UINT32_C(1) <<
                            (unsigned)call->as.call.arguments.count;
                }
                call->as.call.arguments.items[call->as.call.arguments.count++] =
                    parser_parse_expression(parser);
                if (!parser_accept(parser, TOK_COMMA)) break;
            }
            Token close = parser_expect(parser, TOK_RPAREN, "expected `)` after arguments");
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
        } else if (parser->current.kind == TOK_AS) {
            Parser lookahead = *parser;
            parser_next(&lookahead);
            if (parser->stop_at_element_slash &&
                lookahead.current.kind == TOK_EQUAL)
                break;
            parser_next(parser);
            Expr *cast = parser_new_expr(parser, EXPR_CAST, expr->span);
            cast->as.cast.value = expr;
            cast->as.cast.type_name = parse_type_name(parser);
            cast->span.end = parser->previous.span.end;
            expr = cast;
        } else {
            break;
        }
    }
    return expr;
}

static Expr *parse_unary(Parser *parser) {
    Token token = parser->current;
    if (parser->current.kind == TOK_LPAREN) {
        TypeProbe probe = {parser->lexer, parser->current};
        probe_next(&probe);
        bool type_name = probe_type(&probe);
        bool closed = type_name && probe_accept(&probe, TOK_RPAREN);
        bool value_follows =
            probe.current.kind == TOK_IDENT ||
            probe.current.kind == TOK_INT ||
            probe.current.kind == TOK_FLOAT ||
            probe.current.kind == TOK_STRING ||
            probe.current.kind == TOK_DOLLAR ||
            probe.current.kind == TOK_TRUE ||
            probe.current.kind == TOK_FALSE ||
            probe.current.kind == TOK_NULL ||
            probe.current.kind == TOK_NEW ||
            probe.current.kind == TOK_LPAREN ||
            probe.current.kind == TOK_LBRACKET ||
            probe.current.kind == TOK_LESS ||
            probe.current.kind == TOK_MINUS ||
            probe.current.kind == TOK_BANG ||
            probe.current.kind == TOK_TILDE ||
            probe.current.kind == TOK_STAR ||
            probe.current.kind == TOK_TRY ||
            probe.current.kind == TOK_AWAIT;
        if (closed && value_follows) {
            parser_next(parser);
            const char *cast_type = parse_type_name(parser);
            parser_expect(parser, TOK_RPAREN,
                   "expected `)` after cast type");
            Expr *expr = parser_new_expr(parser, EXPR_CAST, token.span);
            expr->as.cast.type_name = cast_type;
            expr->as.cast.value = parse_unary(parser);
            expr->span.end = expr->as.cast.value->span.end;
            return expr;
        }
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

Expr *parser_parse_expression(Parser *parser) {
    Expr *expr = parse_binary(parser, 1);
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
    while (parser->current.kind != TOK_RBRACE && parser->current.kind != TOK_EOF) {
        block->as.block.items = parser_grow_array(&parser->module->arena,
            block->as.block.items, block->as.block.count, sizeof(Stmt *));
        block->as.block.items[block->as.block.count++] = parser_parse_statement(parser);
        if (parser->panic) {
            while (parser->current.kind != TOK_SEMICOLON &&
                   parser->current.kind != TOK_RBRACE &&
                   parser->current.kind != TOK_EOF) parser_next(parser);
            (void)parser_accept(parser, TOK_SEMICOLON);
            parser->panic = false;
        }
    }
    Token close = parser_expect(parser, TOK_RBRACE, "expected `}` after block");
    block->span.end = close.span.end;
    return block;
}

static Stmt *parse_block(Parser *parser) {
    Token open = parser_expect(parser, TOK_LBRACE, "expected `{`");
    return parser_parse_opened_block(parser, open);
}

Stmt *parser_parse_statement(Parser *parser) {
    Token start = parser->current;
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
            stmt->as.try_.catch_type_name = parse_type_name(parser);
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
        parser_expect(parser, TOK_LPAREN,
               "expected `(` before deconstruction bindings");
        while (parser->current.kind != TOK_RPAREN &&
               parser->current.kind != TOK_EOF) {
            const char *type_name = parse_type_name(parser);
            Token name = parser_expect(
                parser, TOK_IDENT,
                "expected binding name after deconstruction type");
            size_t next_count = stmt->as.destructure.count + 1U;
            const char **names = lang_arena_alloc(
                &parser->module->arena,
                next_count * sizeof(*names));
            const char **types = lang_arena_alloc(
                &parser->module->arena,
                next_count * sizeof(*types));
            if (stmt->as.destructure.count != 0U) {
                memcpy(names, stmt->as.destructure.names,
                       stmt->as.destructure.count * sizeof(*names));
                memcpy(types, stmt->as.destructure.type_names,
                       stmt->as.destructure.count * sizeof(*types));
            }
            names[next_count - 1U] = parser_copy_token(parser, name);
            types[next_count - 1U] = type_name;
            stmt->as.destructure.names = names;
            stmt->as.destructure.type_names = types;
            stmt->as.destructure.count = next_count;
            if (!parser_accept(parser, TOK_COMMA)) break;
        }
        parser_expect(parser, TOK_RPAREN,
               "expected `)` after deconstruction bindings");
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
        const char *type_name = parse_type_name(parser);
        Token name = parser_expect(parser, TOK_IDENT,
                            "expected local name after type");
        type_name = parse_array_declarator_suffix(parser, type_name);
        parser_expect(parser, TOK_EQUAL, "locals require an initializer");
        Expr *value = parser_parse_expression(parser);
        Token end = parser_expect(parser, TOK_SEMICOLON,
                           "expected `;` after local declaration");
        Stmt *stmt = new_stmt(
            parser, STMT_LET,
            (LangSpan){start.span.file, start.span.start, end.span.end});
        stmt->as.let.name = parser_copy_token(parser, name);
        stmt->as.let.type_name = type_name;
        stmt->as.let.mutable_ = true;
        stmt->as.let.value = value;
        return stmt;
    }
    if (parser_accept(parser, TOK_VAR)) {
        Token name = parser_expect(parser, TOK_IDENT, "expected local name");
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
        Parser probe = *parser;
        if (probe.current.kind == TOK_IDENT)
            parser_next(&probe);
        bool legacy_iteration =
            parser->current.kind == TOK_IDENT &&
            probe.current.kind == TOK_IN;
        if (!legacy_iteration) {
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
        Token name = parser_expect(parser, TOK_IDENT, "expected loop variable");
        parser_expect(parser, TOK_IN, "expected `in` after loop variable");
        Stmt *stmt = new_stmt(parser, STMT_FOR, start.span);
        stmt->as.for_.name = parser_copy_token(parser, name);
        stmt->as.for_.borrowed = false;
        stmt->as.for_.iterable = parser_parse_expression(parser);
        if (parser_accept(parser, TOK_DOT_DOT) ||
            parser_accept(parser, TOK_DOT_DOT_EQUAL)) {
            if (parser->previous.kind == TOK_DOT_DOT_EQUAL)
                lang_diag(
                    parser->diagnostics, parser->previous.span,
                    "inclusive ranges are not supported; use a half-open `..` range");
            stmt->as.for_.range_end = parser_parse_expression(parser);
        }
        parser_expect(parser, TOK_RPAREN, "expected `)` after `for` clause");
        stmt->as.for_.body = parse_block(parser);
        stmt->span.end = stmt->as.for_.body->span.end;
        return stmt;
    }
    if (parser_accept(parser, TOK_FOREACH)) {
        parser_expect(parser, TOK_LPAREN, "expected `(` after `foreach`");
        const char *type_name = parse_type_name(parser);
        Token name = parser_expect(
            parser, TOK_IDENT, "expected loop variable after type");
        parser_expect(parser, TOK_IN, "expected `in` after loop variable");
        Stmt *stmt = new_stmt(parser, STMT_FOR, start.span);
        stmt->as.for_.name = parser_copy_token(parser, name);
        stmt->as.for_.type_name = type_name;
        stmt->as.for_.foreach = true;
        stmt->as.for_.iterable = parser_parse_expression(parser);
        stmt->as.for_.borrowed = true;
        parser_expect(parser, TOK_RPAREN, "expected `)` after `foreach` clause");
        stmt->as.for_.body = parse_block(parser);
        stmt->span.end = stmt->as.for_.body->span.end;
        return stmt;
    }
    if (parser_accept(parser, TOK_MATCH)) {
        Stmt *stmt = new_stmt(parser, STMT_MATCH, start.span);
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
            if (parser_accept(parser, TOK_LPAREN)) {
                parse_switch_binding(
                    parser, &binding, &binding_type_name);
                parser_expect(parser, TOK_RPAREN,
                       "expected `)` after payload binding");
            }
            parser_expect(parser, TOK_COLON,
                   "expected `:` after switch case");
            Stmt *body = parse_block(parser);
            stmt->as.match_.arms = parser_grow_array(&parser->module->arena,
                stmt->as.match_.arms, stmt->as.match_.arm_count,
                sizeof(MatchArm));
            stmt->as.match_.arms[stmt->as.match_.arm_count++] =
                (MatchArm){
                    .variant=variant,
                    .binding=binding,
                    .binding_type_name=binding_type_name,
                    .binding_type=NULL,
                    .body=body,
                    .span=(LangSpan){
                        arm_start.span.file, arm_start.span.start,
                        body->span.end
                    }
                };
            (void)parser_accept(parser, TOK_COMMA);
        }
        Token close = parser_expect(parser, TOK_RBRACE,
                             "expected `}` after switch cases");
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

static Decl *parse_struct_decl(
    Parser *parser, Token start, bool enumeration, bool is_union) {
    Token name = parser_expect(
        parser, TOK_IDENT,
        enumeration
            ? (is_union ? "expected union name" : "expected enum name")
            : "expected struct name");
    Decl *decl = lang_arena_alloc(&parser->module->arena, sizeof(*decl));
    decl->kind = enumeration ? DECL_ENUM : DECL_STRUCT;
    decl->span = start.span;
    const char **decl_name = enumeration ? &decl->as.enumeration.name : &decl->as.structure.name;
    *decl_name = parser_copy_token(parser, name);
    if (enumeration) decl->as.enumeration.is_union = is_union;
    parse_type_parameters(parser, decl);
    if (decl->type_param_count > 16U)
        lang_diag(parser->diagnostics, name.span,
                  "generic declarations are limited to 16 type parameters");
    parser_expect(parser, TOK_LBRACE, "expected `{` after type name");
    FieldDecl **items = enumeration ? &decl->as.enumeration.variants : &decl->as.structure.fields;
    size_t *count = enumeration ? &decl->as.enumeration.variant_count : &decl->as.structure.field_count;
    while (parser->current.kind != TOK_RBRACE && parser->current.kind != TOK_EOF) {
        Token field;
        const char *type_name = "unit";
        if (enumeration) {
            field = parser_expect(parser, TOK_IDENT, "expected variant name");
        } else {
            Parser lookahead = *parser;
            Token first = lookahead.current;
            parser_next(&lookahead);
            bool legacy = first.kind == TOK_IDENT &&
                          lookahead.current.kind == TOK_COLON;
            if (legacy) {
                field = parser_expect(parser, TOK_IDENT, "expected field name");
                parser_expect(parser, TOK_COLON, "expected `:` after field name");
                type_name = parse_type_name(parser);
            } else {
                type_name = parse_type_name(parser);
                field = parser_expect(parser, TOK_IDENT,
                               "expected field name after type");
                type_name = parse_array_declarator_suffix(
                    parser, type_name);
            }
        }
        if (enumeration && parser_accept(parser, TOK_LPAREN)) {
            if (!is_union)
                lang_diag(
                    parser->diagnostics, field.span,
                    "enum member `%s` cannot carry a payload; declare `%s` as a union",
                    parser_copy_token(parser, field), *decl_name);
            type_name = parse_type_name(parser);
            if (parser->current.kind != TOK_RPAREN) {
                lang_diag(
                    parser->diagnostics,
                    parser->current.span,
                    parser->current.kind == TOK_COMMA
                        ? "enum variants parser_accept one payload type"
                        : "expected `)` after variant payload");
                while (parser->current.kind != TOK_RPAREN &&
                       parser->current.kind != TOK_RBRACE &&
                       parser->current.kind != TOK_EOF)
                    parser_next(parser);
            }
            if (parser->current.kind == TOK_RPAREN)
                parser_next(parser);
        }
        *items = parser_grow_array(&parser->module->arena, *items, *count, sizeof(FieldDecl));
        (*items)[(*count)++] = (FieldDecl){parser_copy_token(parser, field), type_name, field.span};
        if (!parser_accept(parser, TOK_COMMA))
            (void)parser_accept(parser, TOK_SEMICOLON);
    }
    decl->span.end = parser_expect(parser, TOK_RBRACE, "expected `}` after declaration").span.end;
    return decl;
}

static Decl *parse_function(
    Parser *parser, Token start, bool is_extern,
    const char *return_type, Token name
) {
    Decl *decl = lang_arena_alloc(&parser->module->arena, sizeof(*decl));
    decl->kind = DECL_FUNCTION;
    Function *fn = &decl->as.function;
    fn->name = parser_copy_token(parser, name);
    while (parser->current.kind == TOK_DOT) {
        parser_next(parser);
        Token part = parser_expect(
            parser, TOK_IDENT,
            "expected function name after `::`");
        fn->name = join_text(
            parser, fn->name, "::", parser_copy_token(parser, part));
    }
    fn->is_extern = is_extern;
    parse_type_parameters(parser, decl);
    if (decl->type_param_count > 16U)
        lang_diag(parser->diagnostics, name.span,
                  "generic functions are limited to 16 type parameters");
    if (is_extern && decl->type_param_count != 0U)
        lang_diag(parser->diagnostics, name.span,
                  "extern functions cannot be generic");
    parser_expect(parser, TOK_LPAREN, "expected `(` after function name");
    while (parser->current.kind != TOK_RPAREN && parser->current.kind != TOK_EOF) {
        bool by_ref = parser_accept(parser, TOK_REF);
        bool by_out = !by_ref && parser_accept(parser, TOK_OUT);
        const char *type_name = parse_type_name(parser);
        Token param_name = parser_expect(parser, TOK_IDENT,
                                  "expected parameter name after type");
        type_name = parse_array_declarator_suffix(parser, type_name);
        const char *parameter_name = parser_copy_token(parser, param_name);
        Param param = {
            .name=parameter_name,
            .type_name=type_name,
            .borrowed=by_ref || by_out,
            .mutable_=true,
            .by_ref=by_ref || by_out,
            .by_out=by_out,
            .span=param_name.span,
            .checked_type=NULL
        };
        fn->params = parser_grow_array(&parser->module->arena, fn->params,
                                fn->param_count, sizeof(Param));
        fn->params[fn->param_count++] = param;
        if (!parser_accept(parser, TOK_COMMA)) break;
    }
    parser_expect(parser, TOK_RPAREN, "expected `)` after parameters");
    fn->return_type = return_type;
    if (is_extern) {
        Token end = parser_expect(parser, TOK_SEMICOLON,
                           "expected `;` after extern function declaration");
        fn->span = (LangSpan){start.span.file, start.span.start, end.span.end};
    } else {
        Function *previous_function = parser->current_function;
        parser->current_function = fn;
        fn->body = parse_block(parser);
        parser->current_function = previous_function;
        fn->span = (LangSpan){start.span.file, start.span.start, fn->body->span.end};
    }
    decl->span = fn->span;
    return decl;
}

static Decl *parse_destructor_decl(Parser *parser, Token start) {
    Token type_name = parser_expect(
        parser, TOK_IDENT, "expected type name after `~`");
    parser_expect(parser, TOK_LPAREN,
           "expected `(` after destructor type");
    parser_expect(parser, TOK_RPAREN,
           "destructors do not declare parameters; `self` is implicit");

    Decl *decl = lang_arena_alloc(
        &parser->module->arena, sizeof(*decl));
    decl->kind = DECL_FUNCTION;
    Function *function = &decl->as.function;
    const char *type = parser_copy_token(parser, type_name);
    size_t function_name_length = strlen(type) + sizeof("::drop");
    char *function_name = lang_arena_alloc(
        &parser->module->arena, function_name_length);
    (void)snprintf(
        function_name, function_name_length, "%s::drop", type);
    function->name = function_name;
    function->return_type = "unit";
    function->is_drop = true;
    function->params = lang_arena_alloc(
        &parser->module->arena, sizeof(*function->params));
    function->params[0] = (Param){
        .name="self",
        .type_name=type,
        .borrowed=false,
        .mutable_=true,
        .by_ref=false,
        .by_out=false,
        .span=type_name.span,
        .checked_type=NULL
    };
    function->param_count = 1U;
    function->body = parse_block(parser);
    function->span = (LangSpan){
        start.span.file, start.span.start, function->body->span.end
    };
    decl->span = function->span;
    return decl;
}

static Decl *parse_alias_decl(Parser *parser, Token start) {
    Token name = parser_expect(parser, TOK_IDENT, "expected alias name");
    parser_expect(parser, TOK_EQUAL, "expected `=` after alias name");
    const char *target = parse_type_name(parser);
    Token end = parser_expect(parser, TOK_SEMICOLON,
                       "expected `;` after type alias");
    Decl *decl = lang_arena_alloc(&parser->module->arena, sizeof(*decl));
    decl->kind = DECL_ALIAS;
    decl->as.alias.name = parser_copy_token(parser, name);
    decl->as.alias.target = target;
    decl->span = (LangSpan){
        start.span.file, start.span.start, end.span.end
    };
    return decl;
}

static Decl *parse_delegate_decl(Parser *parser, Token start) {
    const char *return_type = parse_type_name(parser);
    Token name = parser_expect(parser, TOK_IDENT,
                        "expected delegate name after return type");
    parser_expect(parser, TOK_LPAREN,
           "expected `(` after delegate name");
    const char *function_type = "fn(";
    bool first = true;
    while (parser->current.kind != TOK_RPAREN &&
           parser->current.kind != TOK_EOF) {
        bool by_ref = parser_accept(parser, TOK_REF);
        bool by_out = !by_ref && parser_accept(parser, TOK_OUT);
        const char *parameter_type = parse_type_name(parser);
        Token parameter = parser_expect(
            parser, TOK_IDENT,
            "expected parameter name after delegate parameter type");
        parameter_type = parse_array_declarator_suffix(
            parser, parameter_type);
        function_type = join_text(
            parser, function_type, first ? "" : ",",
            by_ref ? join_text(parser, "ref ", parameter_type, "") :
            by_out ? join_text(parser, "out ", parameter_type, "") :
            parameter_type);
        first = false;
        (void)parameter;
        if (!parser_accept(parser, TOK_COMMA)) break;
    }
    parser_expect(parser, TOK_RPAREN,
           "expected `)` after delegate parameters");
    Token end = parser_expect(parser, TOK_SEMICOLON,
                       "expected `;` after delegate declaration");
    function_type = join_text(
        parser, function_type, ")->", return_type);

    Decl *decl = lang_arena_alloc(
        &parser->module->arena, sizeof(*decl));
    decl->kind = DECL_ALIAS;
    decl->as.alias.name = parser_copy_token(parser, name);
    decl->as.alias.target = function_type;
    decl->span = (LangSpan){
        start.span.file, start.span.start, end.span.end
    };
    return decl;
}

static Decl *parse_element_decl(Parser *parser, Token start) {
    Parser lookahead = *parser;
    parser_next(&lookahead);
    Token name;
    const char *result_type;
    if (lookahead.current.kind == TOK_ARROW) {
        name = parser_expect_element_word(parser, "expected element name");
        parser_expect(parser, TOK_ARROW,
               "expected `->` after element name");
        result_type = parse_type_name(parser);
    } else {
        result_type = parse_type_name(parser);
        name = parser_expect_element_word(
            parser, "expected element name after result type");
    }
    parser_expect(parser, TOK_LBRACE,
           "expected `{` before element properties");
    Decl *decl =
        lang_arena_alloc(&parser->module->arena, sizeof(*decl));
    decl->kind = DECL_ELEMENT;
    decl->as.element.name = parser_copy_token(parser, name);
    decl->as.element.result_type = result_type;
    while (parser->current.kind != TOK_RBRACE &&
           parser->current.kind != TOK_EOF) {
        Parser property_lookahead = *parser;
        Token first = property_lookahead.current;
        LangSpan property_span = first.span;
        parser_next(&property_lookahead);
        bool legacy = parser_element_property_word(first.kind) &&
                      (property_lookahead.current.kind == TOK_COLON ||
                       property_lookahead.current.kind == TOK_MINUS);
        char *property_name;
        const char *type_name;
        if (legacy) {
            parser_next(parser);
            property_name = parser_parse_element_property_name(parser, first);
            parser_expect(parser, TOK_COLON,
                   "expected `:` after element property name");
            type_name = parse_type_name(parser);
        } else {
            type_name = parse_type_name(parser);
            Token property = parser->current;
            property_span = property.span;
            if (!parser_element_property_word(property.kind)) {
                lang_diag(parser->diagnostics, property.span,
                          "expected element property name after type");
            } else {
                parser_next(parser);
            }
            property_name = parser_parse_element_property_name(parser, property);
            type_name = parse_array_declarator_suffix(
                parser, type_name);
        }
        decl->as.element.properties = parser_grow_array(
            &parser->module->arena, decl->as.element.properties,
            decl->as.element.property_count, sizeof(FieldDecl));
        decl->as.element.properties[
            decl->as.element.property_count++] = (FieldDecl){
                property_name, type_name, property_span
            };
        if (!parser_accept(parser, TOK_COMMA))
            (void)parser_accept(parser, TOK_SEMICOLON);
    }
    decl->span = (LangSpan){
        start.span.file, start.span.start,
        parser_expect(parser, TOK_RBRACE,
               "expected `}` after element declaration").span.end
    };
    return decl;
}

static Decl *parse_using_decl(Parser *parser, Token start) {
    ImportDecl import_decl;
    memset(&import_decl, 0, sizeof(import_decl));
    import_decl.owner_module = parser->current_module;
    import_decl.span = start.span;
    Token first = parser_expect(parser, TOK_IDENT, "expected namespace name");
    if (parser_accept(parser, TOK_EQUAL)) {
        const char *target = parse_type_name(parser);
        Token end = parser_expect(parser, TOK_SEMICOLON,
                           "expected `;` after using alias");
        /* A qualified target is a namespace alias. Unqualified aliases are
         * type aliases; this keeps declaration-side aliases deterministic
         * without requiring filesystem knowledge in the parser. */
        if (strstr(target, "::") == NULL) {
            Decl *decl = lang_arena_alloc(
                &parser->module->arena, sizeof(*decl));
            decl->kind = DECL_ALIAS;
            decl->as.alias.name = parser_copy_token(parser, first);
            decl->as.alias.target = target;
            decl->span = (LangSpan){
                start.span.file, start.span.start, end.span.end
            };
            return decl;
        }
        import_decl.alias = parser_copy_token(parser, first);
        import_decl.module_path = target;
        import_decl.span.end = end.span.end;
        parser->module->imports = parser_grow_array(
            &parser->module->arena, parser->module->imports,
            parser->module->import_count, sizeof(*parser->module->imports));
        parser->module->imports[parser->module->import_count++] = import_decl;
        return NULL;
    }
    const char *path = parser_copy_token(parser, first);
    while (parser->current.kind == TOK_DOT) {
        parser_next(parser);
        Token part = parser_expect(
            parser, TOK_IDENT, "expected namespace name after `.`");
        path = join_text(
            parser, path, "::", parser_copy_token(parser, part));
    }
    parser_expect(parser, TOK_SEMICOLON, "expected `;` after using declaration");
    import_decl.module_path = path;
    import_decl.span.end = parser->previous.span.end;
    parser->module->imports = parser_grow_array(
        &parser->module->arena, parser->module->imports,
        parser->module->import_count, sizeof(*parser->module->imports));
    parser->module->imports[parser->module->import_count++] = import_decl;
    return NULL;
}

bool lang_parse_module(const LangSource *source, LangDiagnostics *diagnostics,
                       Module *module) {
    memset(module, 0, sizeof(*module));
    module->source = source;
    module->require_entrypoint = true;
    lang_arena_init(&module->arena);
    Parser parser;
    memset(&parser, 0, sizeof(parser));
    parser.diagnostics = diagnostics;
    parser.module = module;
    parser.current_module = source->path;
    lang_lexer_init(&parser.lexer, source, diagnostics);
    parser_next(&parser);
    while (parser.current.kind != TOK_EOF) {
        Token start = parser.current;
        bool is_public = parser_accept(&parser, TOK_PUB);
        bool is_async = parser_accept(&parser, TOK_ASYNC);
        if (!is_public && !is_async)
            start = parser.current;
        Decl *decl = NULL;
        bool is_extern = false;
        if (parser_accept(&parser, TOK_TILDE)) {
            decl = parse_destructor_decl(&parser, start);
        } else if (parser_accept(&parser, TOK_TYPE)) {
            decl = parse_alias_decl(&parser, start);
        } else if (parser_accept(&parser, TOK_DELEGATE)) {
            decl = parse_delegate_decl(&parser, start);
        } else if (parser_accept(&parser, TOK_ELEMENT)) {
            decl = parse_element_decl(&parser, start);
        } else {
            is_extern = parser_accept(&parser, TOK_EXTERN);
        }
        if (decl != NULL) {
            /* Already parsed above. */
        } else if (parser_accept(&parser, TOK_STRUCT)) {
            decl = parse_struct_decl(&parser, start, false, false);
            decl->as.structure.is_extern = is_extern;
        } else if (parser_accept(&parser, TOK_ENUM)) {
            decl = parse_struct_decl(&parser, start, true, false);
            if (is_extern)
                lang_diag(diagnostics, start.span,
                          "`extern enum` is not supported; use an integer-backed C ABI wrapper");
        } else if (parser_accept(&parser, TOK_UNION)) {
            decl = parse_struct_decl(&parser, start, true, true);
            if (is_extern)
                lang_diag(diagnostics, start.span,
                          "`extern union` is not supported");
        }
        else if (parser_accept(&parser, TOK_NAMESPACE)) {
            if (is_public)
                lang_diag(diagnostics, start.span,
                          "`pub` cannot be applied to a namespace declaration");
            Token first = parser_expect(&parser, TOK_IDENT,
                                 "expected namespace name");
            const char *name = parser_copy_token(&parser, first);
            while (parser.current.kind == TOK_DOT) {
                parser_next(&parser);
                Token part = parser_expect(&parser, TOK_IDENT,
                                    "expected namespace name after `.`");
                name = join_text(&parser, name, "::",
                                 parser_copy_token(&parser, part));
            }
            parser_expect(&parser, TOK_SEMICOLON,
                   "expected `;` after namespace declaration");
            parser.current_module = name;
            continue;
        } else if (parser_accept(&parser, TOK_USING)) {
            decl = parse_using_decl(&parser, start);
            if (decl == NULL) {
                if (is_public)
                    lang_diag(diagnostics, start.span,
                              "`pub` cannot be applied to a namespace using declaration");
                continue;
            }
        } else if (parser.current.kind == TOK_IDENT ||
                   parser.current.kind == TOK_STAR ||
                   parser.current.kind == TOK_LBRACKET) {
            const char *return_type = parse_type_name(&parser);
            Token name = parser_expect(
                &parser, TOK_IDENT,
                "expected function name after return type");
            decl = parse_function(
                &parser, start, is_extern, return_type, name);
            decl->as.function.is_async = is_async;
        } else {
            lang_diag(diagnostics, parser.current.span,
                      "expected a function, struct, enum, union, element, type, or drop declaration");
            parser_next(&parser);
            continue;
        }
        if (is_async && (decl == NULL || decl->kind != DECL_FUNCTION))
            lang_diag(diagnostics, start.span,
                      "`async` can only be applied to a function");
        decl->module_name = parser.current_module;
        decl->is_public = is_public;
        module->decls = parser_grow_array(&module->arena, module->decls,
                                   module->count, sizeof(Decl *));
        module->decls[module->count++] = decl;
    }
    module->entry_module = parser.current_module;
    return diagnostics->count == 0U;
}

void lang_module_free(Module *module) {
    lang_arena_free(&module->arena);
    memset(module, 0, sizeof(*module));
}

static void indent(int depth) { for (int i = 0; i < depth; ++i) fputs("  ", stdout); }
static void print_expr(const Expr *expr, int depth);
static void print_stmt(const Stmt *stmt, int depth) {
    indent(depth);
    switch (stmt->kind) {
        case STMT_LET: printf("let %s\n", stmt->as.let.name); print_expr(stmt->as.let.value, depth + 1); break;
        case STMT_DESTRUCTURE:
            printf("destructure %zu\n", stmt->as.destructure.count);
            print_expr(stmt->as.destructure.value, depth + 1);
            break;
        case STMT_EXPR: puts("expr"); print_expr(stmt->as.expression, depth + 1); break;
        case STMT_RETURN: puts("return"); if (stmt->as.return_value) print_expr(stmt->as.return_value, depth + 1); break;
        case STMT_IF: puts("if"); print_expr(stmt->as.if_.condition, depth + 1); print_stmt(stmt->as.if_.then_branch, depth + 1); break;
        case STMT_WHILE: puts("while"); print_expr(stmt->as.while_.condition, depth + 1); print_stmt(stmt->as.while_.body, depth + 1); break;
        case STMT_FOR:
            printf("%s %s%s\n",
                   stmt->as.for_.foreach ? "foreach" : "for",
                   stmt->as.for_.name,
                   stmt->as.for_.range_end != NULL ? " range" : "");
            print_expr(stmt->as.for_.iterable, depth + 1);
            if (stmt->as.for_.range_end != NULL)
                print_expr(stmt->as.for_.range_end, depth + 1);
            print_stmt(stmt->as.for_.body, depth + 1);
            break;
        case STMT_C_FOR:
            puts("for");
            if (stmt->as.c_for.initializer != NULL)
                print_stmt(stmt->as.c_for.initializer, depth + 1);
            if (stmt->as.c_for.condition != NULL)
                print_expr(stmt->as.c_for.condition, depth + 1);
            if (stmt->as.c_for.increment != NULL)
                print_expr(stmt->as.c_for.increment, depth + 1);
            print_stmt(stmt->as.c_for.body, depth + 1);
            break;
        case STMT_MATCH:
            puts("switch");
            print_expr(stmt->as.match_.value, depth + 1);
            for (size_t i = 0U; i < stmt->as.match_.arm_count; ++i) {
                indent(depth + 1);
                printf("arm %s%s%s\n", stmt->as.match_.arms[i].variant,
                       stmt->as.match_.arms[i].binding != NULL ? " as " : "",
                       stmt->as.match_.arms[i].binding != NULL
                           ? stmt->as.match_.arms[i].binding : "");
                print_stmt(stmt->as.match_.arms[i].body, depth + 2);
            }
            break;
        case STMT_THROW:
            puts("throw");
            if (stmt->as.throw_value != NULL)
                print_expr(stmt->as.throw_value, depth + 1);
            break;
        case STMT_TRY:
            puts("try");
            print_stmt(stmt->as.try_.body, depth + 1);
            if (stmt->as.try_.catch_body != NULL) {
                indent(depth);
                printf("catch %s %s\n", stmt->as.try_.catch_type_name,
                       stmt->as.try_.catch_name);
                print_stmt(stmt->as.try_.catch_body, depth + 1);
            }
            if (stmt->as.try_.finally_body != NULL) {
                indent(depth);
                puts("finally");
                print_stmt(stmt->as.try_.finally_body, depth + 1);
            }
            break;
        case STMT_BREAK: puts("break"); break;
        case STMT_CONTINUE: puts("continue"); break;
        case STMT_BLOCK:
            puts("block");
            for (size_t i = 0U; i < stmt->as.block.count; ++i) print_stmt(stmt->as.block.items[i], depth + 1);
            break;
        case STMT_UNSAFE:
            puts("unsafe");
            print_stmt(stmt->as.unsafe_body, depth + 1);
            break;
    }
}
static void print_expr(const Expr *expr, int depth) {
    indent(depth);
    switch (expr->kind) {
        case EXPR_INT: printf("int %" PRIu64 "\n", expr->as.integer); break;
        case EXPR_FLOAT: printf("float %g\n", expr->as.floating); break;
        case EXPR_STRING:
            printf("string length=%zu\n", expr->as.string.length);
            break;
        case EXPR_INTERPOLATION:
            printf(
                "interpolation parts=%zu\n",
                expr->as.interpolation.part_count);
            for (size_t i = 0U;
                 i < expr->as.interpolation.part_count; ++i) {
                const InterpolationPart *part =
                    &expr->as.interpolation.parts[i];
                if (part->expression != NULL)
                    print_expr(part->expression, depth + 1);
                else {
                    indent(depth + 1);
                    printf(
                        "text length=%zu\n",
                        part->text_length);
                }
            }
            break;
        case EXPR_BOOL: printf("bool %s\n", expr->as.boolean ? "true" : "false"); break;
        case EXPR_NULL: puts("null"); break;
        case EXPR_NAME: printf("name %s\n", expr->as.name); break;
        case EXPR_BINARY: printf("binary %s\n", lang_token_name(expr->as.binary.op)); print_expr(expr->as.binary.left, depth + 1); print_expr(expr->as.binary.right, depth + 1); break;
        case EXPR_UNARY: printf("unary %s\n", lang_token_name(expr->as.unary.op)); print_expr(expr->as.unary.operand, depth + 1); break;
        case EXPR_CALL: puts("call"); print_expr(expr->as.call.callee, depth + 1); break;
        case EXPR_ASSIGN: puts("assign"); print_expr(expr->as.assign.target, depth + 1); break;
        case EXPR_CLONE: puts("clone"); print_expr(expr->as.clone.value, depth + 1); break;
        case EXPR_TRY: puts("try"); print_expr(expr->as.try_.value, depth + 1); break;
        case EXPR_AWAIT: puts("await"); print_expr(expr->as.try_.value, depth + 1); break;
        case EXPR_CAST:
            printf("cast %s\n", expr->as.cast.type_name);
            print_expr(expr->as.cast.value, depth + 1);
            break;
        case EXPR_ARRAY: printf("array [%zu]\n", expr->as.array.count); break;
        case EXPR_INDEX: puts("index"); break;
        case EXPR_FIELD: printf("field .%s\n", expr->as.field.field); break;
        case EXPR_STRUCT: printf("construct %s\n", expr->as.structure.name); break;
        case EXPR_ELEMENT:
            printf("element <%s> body=%zu\n", expr->as.element.name, expr->as.element.body_count);
            for (size_t i = 0U; i < expr->as.element.body_count; ++i) {
                if (expr->as.element.body[i].is_statement)
                    print_stmt(expr->as.element.body[i].as.statement, depth + 1);
                else print_expr(expr->as.element.body[i].as.expression, depth + 1);
            }
            break;
        case EXPR_IF:
            puts("if expression");
            print_expr(expr->as.if_.condition, depth + 1);
            print_stmt(expr->as.if_.then_branch, depth + 1);
            print_stmt(expr->as.if_.else_branch, depth + 1);
            break;
        case EXPR_MATCH:
            puts("switch expression");
            print_expr(expr->as.match_.value, depth + 1);
            for (size_t i = 0U; i < expr->as.match_.arm_count; ++i)
                print_stmt(expr->as.match_.arms[i].body, depth + 1);
            break;
    }
}

void lang_dump_ast(const Module *module) {
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *decl = module->decls[i];
        if (decl->kind == DECL_FUNCTION) {
            printf("%s%s %s",
                   decl->as.function.is_extern ? "extern " : "",
                   decl->as.function.return_type,
                   decl->as.function.name);
            if (decl->type_param_count != 0U) {
                putchar('<');
                for (size_t parameter = 0U;
                     parameter < decl->type_param_count; ++parameter)
                    printf("%s%s", parameter == 0U ? "" : ",",
                           decl->type_params[parameter]);
                putchar('>');
            }
            putchar('\n');
            if (decl->as.function.body != NULL)
                print_stmt(decl->as.function.body, 1);
        } else if (decl->kind == DECL_STRUCT) {
            printf("struct %s", decl->as.structure.name);
            for (size_t parameter = 0U;
                 parameter < decl->type_param_count; ++parameter)
                printf("%s%s", parameter == 0U ? "<" : ",",
                       decl->type_params[parameter]);
            puts(decl->type_param_count == 0U ? "" : ">");
        } else if (decl->kind == DECL_ENUM) {
            printf("%s %s",
                   decl->as.enumeration.is_union ? "union" : "enum",
                   decl->as.enumeration.name);
            for (size_t parameter = 0U;
                 parameter < decl->type_param_count; ++parameter)
                printf("%s%s", parameter == 0U ? "<" : ",",
                       decl->type_params[parameter]);
            puts(decl->type_param_count == 0U ? "" : ">");
        } else if (decl->kind == DECL_ALIAS) {
            printf("type %s = %s\n", decl->as.alias.name,
                   decl->as.alias.target);
        } else {
            printf("element %s -> %s\n", decl->as.element.name,
                   decl->as.element.result_type);
        }
    }
}
