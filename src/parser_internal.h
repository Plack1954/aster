#ifndef ASTER_PARSER_INTERNAL_H
#define ASTER_PARSER_INTERNAL_H

#include "internal.h"

typedef struct Parser {
    Lexer lexer;
    Token current;
    Token previous;
    LangDiagnostics *diagnostics;
    Module *module;
    const char *current_module;
    Function *current_function;
    bool panic;
    bool stop_at_lbrace;
    bool stop_at_element_slash;
    bool suppress_element_lookahead;
} Parser;

void parser_next(Parser *parser);
bool parser_accept(Parser *parser, TokenKind kind);
Token parser_expect(Parser *parser, TokenKind kind,
                    const char *message);
Token parser_take_without_lookahead(
    Parser *parser, TokenKind kind, const char *message);
void *parser_grow_array(LangArena *arena, const void *old,
                        size_t count, size_t item_size);
const char *parser_copy_token(Parser *parser, Token token);
Expr *parser_new_expr(Parser *parser, ExprKind kind, LangSpan span);
bool parser_looks_like_c_local(const Parser *parser);
Expr *parser_parse_expression(Parser *parser);
Stmt *parser_parse_statement(Parser *parser);
Stmt *parser_parse_opened_block(Parser *parser, Token open);
Expr *parser_parse_primary(Parser *parser);
char *parser_parse_path(Parser *parser);
Expr *parser_parse_element(Parser *parser, LangSpan open_span);
Token parser_expect_element_word(Parser *parser, const char *message);
bool parser_element_property_word(TokenKind kind);
char *parser_parse_element_property_name(Parser *parser, Token first);

#endif
