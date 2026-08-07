#include "parser_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void parser_next(Parser *parser) {
    parser->previous = parser->current;
    do {
        parser->current = lang_lexer_next(&parser->lexer);
    } while (parser->current.kind == TOK_ERROR);
}

bool parser_accept(Parser *parser, TokenKind kind) {
    if (parser->current.kind != kind) return false;
    parser_next(parser);
    return true;
}

Token parser_expect(Parser *parser, TokenKind kind, const char *message) {
    if (parser->current.kind == kind) {
        Token result = parser->current;
        parser_next(parser);
        return result;
    }
    lang_diag(parser->diagnostics, parser->current.span,
              "%s; found `%s`", message,
              lang_token_name(parser->current.kind));
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

ParserArrayBuilder parser_array_builder(size_t item_size) {
    return (ParserArrayBuilder){.item_size=item_size};
}

void parser_array_push(ParserArrayBuilder *builder, const void *item) {
    if (builder->count == builder->capacity) {
        size_t capacity = builder->capacity == 0U
            ? 8U : builder->capacity * 2U;
        if (capacity < builder->capacity ||
            (builder->item_size != 0U &&
             capacity > SIZE_MAX / builder->item_size)) {
            fputs("fatal: parser collection is too large\n", stderr);
            exit(2);
        }
        void *items = realloc(
            builder->items, capacity * builder->item_size);
        if (items == NULL) {
            fputs("fatal: out of memory growing parser collection\n", stderr);
            exit(2);
        }
        builder->items = items;
        builder->capacity = capacity;
    }
    unsigned char *destination = builder->items;
    memcpy(destination + builder->count * builder->item_size,
           item, builder->item_size);
    ++builder->count;
}

void *parser_array_freeze(Parser *parser, ParserArrayBuilder *builder) {
    void *result = NULL;
    if (builder->count != 0U) {
        result = lang_arena_alloc(
            &parser->module->arena,
            builder->count * builder->item_size);
        memcpy(result, builder->items,
               builder->count * builder->item_size);
    }
    free(builder->items);
    *builder = (ParserArrayBuilder){0};
    return result;
}

const char *parser_copy_token(Parser *parser, Token token) {
    return lang_arena_strndup(
        &parser->module->arena, token.start, token.length);
}

const char *join_text(Parser *parser, const char *left,
                      const char *middle, const char *right) {
    size_t left_length = strlen(left);
    size_t middle_length = strlen(middle);
    size_t right_length = strlen(right);
    size_t length = left_length + middle_length + right_length;
    char *result = lang_arena_alloc(&parser->module->arena, length + 1U);
    memcpy(result, left, left_length);
    memcpy(result + left_length, middle, middle_length);
    memcpy(result + left_length + middle_length, right, right_length);
    result[length] = '\0';
    return result;
}

Expr *parser_new_expr(Parser *parser, ExprKind kind, LangSpan span) {
    Expr *expr = lang_arena_alloc(
        &parser->module->arena, sizeof(*expr));
    expr->kind = kind;
    expr->span = span;
    return expr;
}
