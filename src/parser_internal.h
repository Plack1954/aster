#ifndef ASTER_PARSER_INTERNAL_H
#define ASTER_PARSER_INTERNAL_H

#include "internal.h"

typedef struct ParserArrayBuilder {
    /* Temporary heap storage; freeze exactly once into the module arena. */
    void *items;
    size_t count;
    size_t capacity;
    size_t item_size;
} ParserArrayBuilder;

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
ParserArrayBuilder parser_array_builder(size_t item_size);
void parser_array_push(ParserArrayBuilder *builder, const void *item);
void *parser_array_freeze(Parser *parser, ParserArrayBuilder *builder);
const char *parser_copy_token(Parser *parser, Token token);
const char *join_text(Parser *parser, const char *left,
                      const char *middle, const char *right);
Expr *parser_new_expr(Parser *parser, ExprKind kind, LangSpan span);
bool parser_looks_like_c_local(const Parser *parser);
bool looks_like_deconstruction(const Parser *parser);
bool parser_looks_like_cast(const Parser *parser);
TypeSyntax *new_type_syntax(
    Parser *parser, TypeSyntaxKind kind, LangSpan span);
const char *format_type_syntax(
    Parser *parser, const TypeSyntax *syntax);
const char *parse_type(Parser *parser, TypeSyntax **out_syntax);
void parse_declarator_suffix(
    Parser *parser, TypeSyntax **syntax, const char **type_name);
void parse_switch_binding(
    Parser *parser, const char **binding,
    const char **binding_type_name, TypeSyntax **binding_type_syntax);
void parse_type_parameters(Parser *parser, Decl *decl);
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
