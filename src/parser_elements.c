#include "parser_internal.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool element_word(TokenKind kind) {
    return kind == TOK_IDENT ||
           (kind >= TOK_VAR && kind <= TOK_ELEMENT);
}

Token parser_expect_element_word(Parser *parser, const char *message) {
    if (element_word(parser->current.kind)) {
        Token result = parser->current;
        parser_next(parser);
        return result;
    }
    return parser_expect(parser, TOK_IDENT, message);
}

char *parser_parse_path(Parser *parser) {
    Token first = parser_expect_element_word(parser, "expected element name");
    const char *name = parser_copy_token(parser, first);
    while (parser->current.kind == TOK_DOT) {
        parser_next(parser);
        Token part = parser_expect_element_word(
            parser, "expected qualified element name");
        size_t a = strlen(name), b = part.length;
        char *next_name = lang_arena_alloc(&parser->module->arena, a + b + 3U);
        (void)snprintf(next_name, a + b + 3U, "%s::%.*s", name, (int)b, part.start);
        name = next_name;
    }
    return (char *)name;
}

bool parser_element_property_word(TokenKind kind) {
    return element_word(kind);
}

char *parser_parse_element_property_name(Parser *parser, Token first) {
    const char *name = parser_copy_token(parser, first);
    while (parser_accept(parser, TOK_MINUS)) {
        Token part = parser->current;
        if (!parser_element_property_word(part.kind)) {
            lang_diag(
                parser->diagnostics, part.span,
                "expected an identifier after `-` in element property");
        } else {
            parser_next(parser);
        }
        size_t prefix_length = strlen(name);
        char *joined = lang_arena_alloc(
            &parser->module->arena,
            prefix_length + part.length + 2U);
        (void)snprintf(
            joined, prefix_length + part.length + 2U,
            "%s-%.*s", name, (int)part.length, part.start);
        name = joined;
    }
    return (char *)name;
}

static bool body_statement_start(TokenKind kind) {
    return kind == TOK_VAR || kind == TOK_IF || kind == TOK_WHILE ||
           kind == TOK_FOR || kind == TOK_FOREACH || kind == TOK_MATCH ||
           kind == TOK_RETURN || kind == TOK_BREAK ||
           kind == TOK_CONTINUE || kind == TOK_UNSAFE;
}

static void element_body_sync(Parser *parser, size_t offset) {
    parser->lexer.offset = offset;
    parser->lexer.interpolation_pending = false;
    parser->current = lang_lexer_next(&parser->lexer);
}

static void finish_element_parse(
    Parser *parser, size_t offset, bool suppress_lookahead) {
    if (!suppress_lookahead)
        element_body_sync(parser, offset);
}

static Token element_body_probe(const Parser *parser, size_t offset) {
    Lexer lexer = parser->lexer;
    lexer.offset = offset;
    lexer.interpolation_pending = false;
    return lang_lexer_next(&lexer);
}

static bool element_body_code_start(Parser *parser, Token first) {
    if (body_statement_start(first.kind) ||
        first.kind == TOK_STRING || first.kind == TOK_DOLLAR)
        return true;
    Parser probe = *parser;
    probe.lexer.offset = first.span.end;
    probe.lexer.interpolation_pending = false;
    probe.current = first;
    return parser_looks_like_c_local(&probe);
}

static Expr *element_body_text(
    Parser *parser, size_t start, size_t end) {
    const LangSource *source = parser->lexer.source;
    char *text = lang_arena_alloc(
        &parser->module->arena, end - start + 1U);
    size_t length = 0U;
    size_t offset = start;
    while (offset < end) {
        unsigned char byte = (unsigned char)source->text[offset];
        if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
            size_t whitespace_start = offset;
            bool newline = false;
            while (offset < end) {
                byte = (unsigned char)source->text[offset];
                if (byte != ' ' && byte != '\t' &&
                    byte != '\r' && byte != '\n')
                    break;
                newline |= byte == '\r' || byte == '\n';
                ++offset;
            }
            bool leading = whitespace_start == start;
            bool trailing = offset == end;
            if ((!leading && !trailing) ||
                ((leading || trailing) && !newline))
                text[length++] = ' ';
            continue;
        }
        text[length++] = (char)byte;
        ++offset;
    }
    if (length == 0U) return NULL;
    text[length] = '\0';
    Expr *expr = parser_new_expr(
        parser, EXPR_STRING,
        (LangSpan){
            lang_source_path_at(source, start), start, end
        });
    expr->as.string.data = text;
    expr->as.string.length = length;
    return expr;
}

static void append_element_expression(
    Parser *parser, Expr *element, Expr *child, bool static_text) {
    element->as.element.body = parser_grow_array(
        &parser->module->arena, element->as.element.body,
        element->as.element.body_count, sizeof(ElementBodyItem));
    element->as.element.body[element->as.element.body_count++] =
        (ElementBodyItem){
            .is_statement=false,
            .is_static_text=static_text,
            .as.expression=child
        };
}

static bool native_style_name(const char *name) {
    const char *part = name;
    for (const char *cursor = strstr(name, "::"); cursor != NULL;
         cursor = strstr(cursor + 2U, "::"))
        part = cursor + 2U;
    return strcmp(part, "style") == 0;
}

static uint64_t css_identity_hash(uint64_t hash, const char *text,
                                  size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        hash ^= (unsigned char)text[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static const char *ensure_css_scope(Parser *parser, LangSpan span) {
    Function *function = parser->current_function;
    if (function == NULL) {
        lang_diag(parser->diagnostics, span,
                  "`scoped` styles are only valid inside an Html function");
        return NULL;
    }
    if (strcmp(function->return_type, "Html") != 0) {
        lang_diag(parser->diagnostics, span,
                  "`scoped` styles require an Html-returning component");
        return NULL;
    }
    if (function->css_scope_attribute != NULL)
        return function->css_scope_attribute;
    uint64_t hash = UINT64_C(1469598103934665603);
    const char *parts[] = {
        parser->current_module != NULL ? parser->current_module : "",
        "::", function->name
    };
    for (size_t part = 0U; part < 3U; ++part)
        hash = css_identity_hash(hash, parts[part], strlen(parts[part]));
    size_t capacity = sizeof("data-aster-scope-") + 16U;
    char *attribute = lang_arena_alloc(&parser->module->arena, capacity);
    (void)snprintf(attribute, capacity, "data-aster-scope-%016" PRIx64, hash);
    function->css_scope_attribute = attribute;
    return attribute;
}

static size_t find_native_style_close(const LangSource *source,
                                      size_t start, const char *name,
                                      size_t *close_length) {
    for (size_t offset = start; offset + 3U <= source->length; ++offset) {
        if (source->text[offset] != '<' ||
            source->text[offset + 1U] != '/')
            continue;
        size_t source_offset = offset + 2U;
        size_t name_offset = 0U;
        while (name[name_offset] != '\0' &&
               source_offset < source->length) {
            if (name[name_offset] == ':' && name[name_offset + 1U] == ':') {
                if (source->text[source_offset] != '.') break;
                name_offset += 2U;
            } else {
                if (source->text[source_offset] != name[name_offset]) break;
                ++name_offset;
            }
            ++source_offset;
        }
        if (name[name_offset] == '\0' &&
            source_offset < source->length &&
            source->text[source_offset] == '>') {
            *close_length = source_offset + 1U - offset;
            return offset;
        }
    }
    return SIZE_MAX;
}

static void parse_native_style_body(Parser *parser, Expr *expr,
                                    Token open_end) {
    const LangSource *source = parser->lexer.source;
    size_t body_start = open_end.span.end;
    size_t close_length = 0U;
    size_t close_start = find_native_style_close(
        source, body_start, expr->as.element.name, &close_length);
    size_t body_end = close_start == SIZE_MAX
        ? source->length : close_start;
    (void)lang_css_parse(
        source, body_start, body_end, &parser->module->arena,
        parser->diagnostics, &expr->as.element.css);

    if (body_start != body_end) {
        Expr *text = parser_new_expr(
            parser, EXPR_STRING,
            (LangSpan){
                lang_source_path_at(source, body_start),
                body_start, body_end
            });
        text->as.string.data = lang_arena_strndup(
            &parser->module->arena,
            source->text + body_start, body_end - body_start);
        text->as.string.length = body_end - body_start;
        expr->as.element.body = parser_grow_array(
            &parser->module->arena, expr->as.element.body,
            expr->as.element.body_count, sizeof(ElementBodyItem));
        expr->as.element.body[expr->as.element.body_count++] =
            (ElementBodyItem){
                .is_statement=false,
                .as.expression=text
            };
    }

    if (close_start == SIZE_MAX) {
        lang_diag(parser->diagnostics, expr->span,
                  "unterminated native CSS `<style>` element");
        expr->span.end = source->length;
        parser->lexer.offset = source->length;
    } else {
        size_t close_end = close_start + close_length;
        expr->as.element.close_span = (LangSpan){
            lang_source_path_at(source, close_start), close_start, close_end
        };
        expr->span.end = close_end;
        parser->lexer.offset = close_end;
    }
    if (expr->as.element.css_scoped) {
        const char *attribute = ensure_css_scope(parser, expr->span);
        if (attribute != NULL && expr->as.element.body_count != 0U) {
            Expr *text = expr->as.element.body[0].as.expression;
            const char *rewritten = NULL;
            size_t rewritten_length = 0U;
            if (lang_css_scope(source, expr->as.element.css, attribute,
                               &parser->module->arena, &rewritten,
                               &rewritten_length)) {
                text->as.string.data = rewritten;
                text->as.string.length = rewritten_length;
                uint64_t style_hash = css_identity_hash(
                    UINT64_C(1469598103934665603), rewritten,
                    rewritten_length);
                size_t capacity =
                    sizeof("data-aster-static-style-") + 16U;
                char *style_attribute = lang_arena_alloc(
                    &parser->module->arena, capacity);
                (void)snprintf(
                    style_attribute, capacity,
                    "data-aster-static-style-%016" PRIx64,
                    style_hash);
                expr->as.element.css_style_attribute = style_attribute;
            }
        }
    }
}

Expr *parser_parse_element(Parser *parser, LangSpan open_span) {
    bool suppress_finish_lookahead =
        parser->suppress_element_lookahead;
    parser->suppress_element_lookahead = false;
    Expr *expr = parser_new_expr(parser, EXPR_ELEMENT, open_span);
    expr->as.element.name =
        parser->current.kind == TOK_GREATER
        ? "#fragment" : parser_parse_path(parser);
    expr->as.element.open_span = open_span;
    while (parser_element_property_word(parser->current.kind) ||
           parser->current.kind == TOK_MINUS ||
           parser->current.kind == TOK_MINUS_MINUS) {
        bool css_custom_property =
            parser->current.kind == TOK_MINUS ||
            parser->current.kind == TOK_MINUS_MINUS;
        Token property = parser->current;
        char *property_name = NULL;
        if (css_custom_property) {
            bool combined_minus =
                parser->current.kind == TOK_MINUS_MINUS;
            parser_next(parser);
            if (!combined_minus)
                (void)parser_expect(
                    parser, TOK_MINUS,
                    "expected a second `-` in CSS custom property");
            Token first = parser_expect_element_word(
                parser, "expected a name after `--`");
            char *base = parser_parse_element_property_name(parser, first);
            size_t length = strlen(base);
            property_name = lang_arena_alloc(
                &parser->module->arena, length + 3U);
            (void)snprintf(
                property_name, length + 3U, "--%s", base);
        } else {
            parser_next(parser);
            property_name =
                parser_parse_element_property_name(parser, property);
        }
        if (native_style_name(expr->as.element.name) &&
            strcmp(property_name, "scoped") == 0 &&
            parser->current.kind != TOK_EQUAL) {
            if (expr->as.element.css_scoped)
                lang_diag(parser->diagnostics, property.span,
                          "duplicate `scoped` style marker");
            expr->as.element.css_scoped = true;
            continue;
        }
        if (!parser_accept(parser, TOK_EQUAL)) {
            lang_diag(parser->diagnostics, property.span,
                      "expected `=` after element property `%s`",
                      property_name);
            break;
        }
        parser->stop_at_element_slash = true;
        ElementProperty item = {
            .name=property_name,
            .value=parser_parse_expression(parser),
            .span=property.span,
            .css_custom_property=css_custom_property
        };
        parser->stop_at_element_slash = false;
        expr->as.element.properties = parser_grow_array(&parser->module->arena,
            expr->as.element.properties, expr->as.element.property_count,
            sizeof(ElementProperty));
        expr->as.element.properties[expr->as.element.property_count++] = item;
        (void)parser_accept(parser, TOK_COMMA);
    }
    if (parser_accept(parser, TOK_SLASH)) {
        Token close = parser_take_without_lookahead(
            parser, TOK_GREATER, "expected `>` after `/`");
        expr->as.element.self_closing = true;
        expr->as.element.open_span.end = close.span.end;
        expr->span.end = close.span.end;
        finish_element_parse(
            parser, close.span.end, suppress_finish_lookahead);
        return expr;
    }
    if (native_style_name(expr->as.element.name) &&
        parser->current.kind == TOK_GREATER) {
        Token open_end = parser->current;
        parser->previous = open_end;
        expr->as.element.open_span.end = open_end.span.end;
        parse_native_style_body(parser, expr, open_end);
        finish_element_parse(
            parser, expr->span.end, suppress_finish_lookahead);
        return expr;
    }
    Token open_end = parser_take_without_lookahead(
        parser, TOK_GREATER, "expected `>` after element opening");
    expr->as.element.open_span.end = open_end.span.end;
    size_t body_offset = open_end.span.end;
    const LangSource *source = parser->lexer.source;
    for (;;) {
        size_t significant = body_offset;
        while (significant < source->length &&
               (source->text[significant] == ' ' ||
                source->text[significant] == '\t' ||
                source->text[significant] == '\r' ||
                source->text[significant] == '\n'))
            ++significant;
        if (significant >= source->length) {
            lang_diag(parser->diagnostics, expr->span,
                      "unterminated element `%s`", expr->as.element.name);
            break;
        }
        if (significant + 1U < source->length &&
            source->text[significant] == '/' &&
            (source->text[significant + 1U] == '/' ||
             source->text[significant + 1U] == '*')) {
            Token after_comment =
                element_body_probe(parser, body_offset);
            if (after_comment.kind == TOK_EOF) {
                lang_diag(parser->diagnostics, expr->span,
                          "unterminated element `%s`",
                          expr->as.element.name);
                break;
            }
            body_offset = after_comment.span.start;
            continue;
        }
        Token probe = {0};
        unsigned char first_byte =
            (unsigned char)source->text[significant];
        bool can_start_code =
            (first_byte >= 'a' && first_byte <= 'z') ||
            (first_byte >= 'A' && first_byte <= 'Z') ||
            first_byte == '_' || first_byte == '"' ||
            first_byte == '$';
        if (can_start_code)
            probe = element_body_probe(parser, significant);
        if (significant < source->length &&
            source->text[significant] == '<') {
            element_body_sync(parser, significant);
            (void)parser_accept(parser, TOK_LESS);
            LangSpan less = parser->previous.span;
            if (parser_accept(parser, TOK_SLASH)) {
                const char *close_name =
                    parser->current.kind == TOK_GREATER
                    ? "#fragment" : parser_parse_path(parser);
                Token close = parser_take_without_lookahead(
                    parser, TOK_GREATER,
                    "expected `>` after closing element");
                expr->as.element.close_span =
                    (LangSpan){less.file, less.start, close.span.end};
                expr->span.end = close.span.end;
                if (strcmp(close_name, expr->as.element.name) != 0) {
                    lang_diag(parser->diagnostics, expr->as.element.close_span,
                        "closing %s `%s` does not match opening %s `%s`",
                        strcmp(close_name, "#fragment") == 0
                            ? "fragment" : "element",
                        strcmp(close_name, "#fragment") == 0
                            ? "<>" : close_name,
                        strcmp(expr->as.element.name, "#fragment") == 0
                            ? "fragment" : "element",
                        strcmp(expr->as.element.name, "#fragment") == 0
                            ? "<>" : expr->as.element.name);
                }
                body_offset = close.span.end;
                break;
            }
            parser->suppress_element_lookahead = true;
            Expr *child = parser_parse_element(parser, less);
            append_element_expression(parser, expr, child, false);
            body_offset = child->span.end;
            continue;
        }
        if (significant < source->length &&
            source->text[significant] == '{') {
            element_body_sync(parser, significant);
            (void)parser_accept(parser, TOK_LBRACE);
            Token brace = parser->previous;
            ElementBodyItem item;
            if (body_statement_start(parser->current.kind) ||
                parser_looks_like_c_local(parser) ||
                parser->current.kind == TOK_LBRACE ||
                parser->current.kind == TOK_RBRACE) {
                item.is_statement = true;
                item.is_static_text = false;
                item.as.statement =
                    parser_parse_opened_block(parser, brace);
                body_offset = item.as.statement->span.end;
            } else {
                item.is_statement = false;
                item.is_static_text = false;
                item.as.expression = parser_parse_expression(parser);
                Token close = parser_take_without_lookahead(
                    parser, TOK_RBRACE,
                    "expected `}` after element child expression");
                body_offset = close.span.end;
            }
            expr->as.element.body = parser_grow_array(
                &parser->module->arena, expr->as.element.body,
                expr->as.element.body_count, sizeof(ElementBodyItem));
            expr->as.element.body[expr->as.element.body_count++] = item;
            continue;
        }
        if (can_start_code && element_body_code_start(parser, probe)) {
            element_body_sync(parser, significant);
            ElementBodyItem item = {.is_static_text=false};
            if (body_statement_start(parser->current.kind) ||
                parser_looks_like_c_local(parser)) {
                item.is_statement = true;
                item.as.statement = parser_parse_statement(parser);
                body_offset = item.as.statement->span.end;
            } else {
                item.is_statement = false;
                item.as.expression = parser_parse_primary(parser);
                body_offset = item.as.expression->span.end;
            }
            expr->as.element.body = parser_grow_array(
                &parser->module->arena, expr->as.element.body,
                expr->as.element.body_count, sizeof(ElementBodyItem));
            expr->as.element.body[expr->as.element.body_count++] = item;
            continue;
        }
        size_t text_end = significant;
        while (text_end < source->length &&
               source->text[text_end] != '<' &&
               source->text[text_end] != '{')
            ++text_end;
        Expr *text = element_body_text(
            parser, body_offset, text_end);
        if (text != NULL)
            append_element_expression(parser, expr, text, true);
        body_offset = text_end;
        continue;
    }
    finish_element_parse(
        parser, expr->span.end, suppress_finish_lookahead);
    return expr;
}
