#include "internal.h"

#include <stddef.h>
#include <string.h>

int main(void) {
    static const char source_text[] =
        "// leading comment\n"
        "fn let mut var new delete owned take struct class enum if else while for foreach in match switch case return break "
        "continue move clone borrow true false null unsafe try async await namespace using "
        "as pub public private extern drop type delegate element ident 123 1.5 \"utf8: \xc3\xa9\" "
        "( ) { } [ ] , : ; . .. ..= :: + - * / % ++ -- += -= *= /= %= "
        "& | ^ ~ &= |= ^= <<= >>= && || "
        "= == ! != < <= > >= << >> -> => "
        "/* outer /* nested */ done */ tail";
    static const TokenKind expected[] = {
        TOK_IDENT, TOK_IDENT, TOK_IDENT, TOK_VAR, TOK_NEW, TOK_DELETE,
        TOK_IDENT, TOK_IDENT, TOK_STRUCT,
        TOK_CLASS, TOK_ENUM, TOK_IF, TOK_ELSE,
        TOK_WHILE, TOK_FOR, TOK_FOREACH, TOK_IN, TOK_IDENT, TOK_MATCH,
        TOK_CASE, TOK_RETURN, TOK_BREAK,
        TOK_CONTINUE, TOK_IDENT, TOK_IDENT, TOK_IDENT, TOK_TRUE, TOK_FALSE,
        TOK_NULL, TOK_UNSAFE, TOK_TRY, TOK_ASYNC, TOK_AWAIT,
        TOK_NAMESPACE, TOK_USING, TOK_AS,
        TOK_IDENT, TOK_PUB, TOK_PRIVATE, TOK_EXTERN, TOK_IDENT, TOK_TYPE, TOK_DELEGATE,
        TOK_ELEMENT, TOK_IDENT,
        TOK_INT, TOK_FLOAT, TOK_STRING, TOK_LPAREN, TOK_RPAREN, TOK_LBRACE,
        TOK_RBRACE, TOK_LBRACKET, TOK_RBRACKET, TOK_COMMA, TOK_COLON,
        TOK_SEMICOLON, TOK_DOT, TOK_DOT_DOT, TOK_DOT_DOT_EQUAL,
        TOK_COLON, TOK_COLON, TOK_PLUS, TOK_MINUS,
        TOK_STAR, TOK_SLASH, TOK_PERCENT,
        TOK_PLUS_PLUS, TOK_MINUS_MINUS,
        TOK_PLUS_EQUAL, TOK_MINUS_EQUAL, TOK_STAR_EQUAL,
        TOK_SLASH_EQUAL, TOK_PERCENT_EQUAL,
        TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE,
        TOK_AMP_EQUAL, TOK_PIPE_EQUAL, TOK_CARET_EQUAL,
        TOK_SHIFT_LEFT_EQUAL, TOK_SHIFT_RIGHT_EQUAL,
        TOK_AND_AND, TOK_OR_OR,
        TOK_EQUAL, TOK_EQUAL_EQUAL,
        TOK_BANG, TOK_BANG_EQUAL, TOK_LESS, TOK_LESS_EQUAL, TOK_GREATER,
        TOK_GREATER_EQUAL, TOK_SHIFT_LEFT, TOK_SHIFT_RIGHT, TOK_ARROW,
        TOK_FAT_ARROW, TOK_IDENT, TOK_EOF
    };
    LangSource source = {
        .text=(char *)source_text,
        .length=sizeof(source_text) - 1U,
        .path="<lexer-test>"
    };
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Lexer lexer;
    lang_lexer_init(&lexer, &source, &diagnostics);
    size_t previous_end = 0U;
    for (size_t i = 0U;
         i < sizeof(expected) / sizeof(expected[0]); ++i) {
        Token token = lang_lexer_next(&lexer);
        if (token.kind != expected[i] ||
            token.span.start < previous_end ||
            token.span.end < token.span.start ||
            token.span.file == NULL ||
            strcmp(token.span.file, "<lexer-test>") != 0) {
            lang_diagnostics_free(&diagnostics);
            return 1;
        }
        previous_end = token.span.end;
    }
    int status = diagnostics.count == 0U ? 0 : 2;
    lang_diagnostics_free(&diagnostics);
    return status;
}
