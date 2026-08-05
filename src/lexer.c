#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct Keyword { const char *text; TokenKind kind; } Keyword;

static const Keyword keywords[] = {
    {"var", TOK_VAR},
    {"new", TOK_NEW}, {"const", TOK_CONST},
    {"ref", TOK_REF},
    {"out", TOK_OUT},
    {"struct", TOK_STRUCT}, {"enum", TOK_ENUM}, {"union", TOK_UNION},
    {"if", TOK_IF},
    {"else", TOK_ELSE}, {"while", TOK_WHILE}, {"for", TOK_FOR},
    {"foreach", TOK_FOREACH},
    {"in", TOK_IN}, {"switch", TOK_MATCH},
    {"case", TOK_CASE}, {"return", TOK_RETURN}, {"break", TOK_BREAK},
    {"continue", TOK_CONTINUE},
    {"true", TOK_TRUE}, {"false", TOK_FALSE}, {"null", TOK_NULL},
    {"unsafe", TOK_UNSAFE},
    {"try", TOK_TRY}, {"catch", TOK_CATCH}, {"finally", TOK_FINALLY},
    {"throw", TOK_THROW},
    {"async", TOK_ASYNC}, {"await", TOK_AWAIT},
    {"namespace", TOK_NAMESPACE}, {"using", TOK_USING},
    {"as", TOK_AS}, {"public", TOK_PUB},
    {"private", TOK_PRIVATE},
    {"static", TOK_STATIC},
    {"readonly", TOK_READONLY},
    {"extern", TOK_EXTERN},
    {"type", TOK_TYPE},
    {"delegate", TOK_DELEGATE},
    {"element", TOK_ELEMENT}
};

void lang_lexer_init(Lexer *lexer, const LangSource *source,
                     LangDiagnostics *diagnostics) {
    lexer->source = source;
    lexer->diagnostics = diagnostics;
    lexer->offset = 0U;
    lexer->interpolation_pending = false;
}

static Token token(Lexer *lexer, TokenKind kind, size_t start) {
    Token result;
    result.kind = kind;
    result.span.file = lang_source_path_at(lexer->source, start);
    result.span.start = start;
    result.span.end = lexer->offset;
    result.start = lexer->source->text + start;
    result.length = lexer->offset - start;
    return result;
}

static bool at_end(const Lexer *lexer) { return lexer->offset >= lexer->source->length; }
static char peek(const Lexer *lexer) {
    return at_end(lexer) ? '\0' : lexer->source->text[lexer->offset];
}
static char peek_next(const Lexer *lexer) {
    return lexer->offset + 1U >= lexer->source->length
         ? '\0' : lexer->source->text[lexer->offset + 1U];
}
static char advance(Lexer *lexer) { return lexer->source->text[lexer->offset++]; }

static void skip_trivia(Lexer *lexer) {
    for (;;) {
        while (isspace((unsigned char)peek(lexer)) != 0) (void)advance(lexer);
        if (peek(lexer) == '/' && peek_next(lexer) == '/') {
            while (!at_end(lexer) && peek(lexer) != '\n') (void)advance(lexer);
        } else if (peek(lexer) == '/' && peek_next(lexer) == '*') {
            size_t start = lexer->offset;
            lexer->offset += 2U;
            unsigned depth = 1U;
            while (!at_end(lexer) && depth != 0U) {
                if (peek(lexer) == '/' && peek_next(lexer) == '*') {
                    lexer->offset += 2U; ++depth;
                } else if (peek(lexer) == '*' && peek_next(lexer) == '/') {
                    lexer->offset += 2U; --depth;
                } else {
                    (void)advance(lexer);
                }
            }
            if (depth != 0U) {
                LangSpan span = {
                    lang_source_path_at(lexer->source, start),
                    start, lexer->offset
                };
                lang_diag(lexer->diagnostics, span, "unterminated block comment");
            }
        } else {
            return;
        }
    }
}

static Token identifier(Lexer *lexer, size_t start) {
    while (isalnum((unsigned char)peek(lexer)) != 0 || peek(lexer) == '_')
        (void)advance(lexer);
    size_t length = lexer->offset - start;
    for (size_t i = 0U; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
        if (strlen(keywords[i].text) == length &&
            memcmp(lexer->source->text + start, keywords[i].text, length) == 0)
            return token(lexer, keywords[i].kind, start);
    }
    return token(lexer, TOK_IDENT, start);
}

static Token number(Lexer *lexer, size_t start) {
    if (lexer->source->text[start] == '0' &&
        (peek(lexer) == 'x' || peek(lexer) == 'X' ||
         peek(lexer) == 'b' || peek(lexer) == 'B' ||
         peek(lexer) == 'o' || peek(lexer) == 'O')) {
        char prefix = advance(lexer);
        unsigned base =
            prefix == 'x' || prefix == 'X' ? 16U :
            prefix == 'b' || prefix == 'B' ? 2U : 8U;
        bool have_digit = false;
        bool invalid = false;
        while (isalnum((unsigned char)peek(lexer)) != 0 ||
               peek(lexer) == '_') {
            char digit = advance(lexer);
            if (digit == '_') continue;
            have_digit = true;
            unsigned value =
                digit >= '0' && digit <= '9'
                    ? (unsigned)(digit - '0') :
                digit >= 'a' && digit <= 'f'
                    ? (unsigned)(digit - 'a') + 10U :
                digit >= 'A' && digit <= 'F'
                    ? (unsigned)(digit - 'A') + 10U : base;
            if (value >= base) invalid = true;
        }
        if (!have_digit || invalid) {
            LangSpan span = {
                lang_source_path_at(lexer->source, start),
                start, lexer->offset
            };
            lang_diag(
                lexer->diagnostics, span,
                "invalid base-%u integer literal", base);
        }
        return token(lexer, TOK_INT, start);
    }
    while (isdigit((unsigned char)peek(lexer)) != 0 || peek(lexer) == '_')
        (void)advance(lexer);
    TokenKind kind = TOK_INT;
    if (peek(lexer) == '.' && isdigit((unsigned char)peek_next(lexer)) != 0) {
        kind = TOK_FLOAT;
        (void)advance(lexer);
        while (isdigit((unsigned char)peek(lexer)) != 0 || peek(lexer) == '_')
            (void)advance(lexer);
    }
    return token(lexer, kind, start);
}

static Token string(Lexer *lexer, size_t start) {
    while (!at_end(lexer) && peek(lexer) != '"') {
        if (peek(lexer) == '\n') break;
        if (peek(lexer) == '\\' && !at_end(lexer)) {
            (void)advance(lexer);
            char escape = peek(lexer);
            if (escape != 'n' && escape != 'r' && escape != 't' &&
                escape != '\\' && escape != '"' && escape != '0' &&
                escape != '{' && escape != '}') {
                LangSpan span = {
                    lang_source_path_at(
                        lexer->source, lexer->offset - 1U),
                    lexer->offset - 1U, lexer->offset + 1U
                };
                lang_diag(lexer->diagnostics, span, "invalid string escape");
            }
        }
        (void)advance(lexer);
    }
    if (peek(lexer) != '"') {
        LangSpan span = {
            lang_source_path_at(lexer->source, start),
            start, lexer->offset
        };
        lang_diag(lexer->diagnostics, span, "unterminated string literal");
        return token(lexer, TOK_ERROR, start);
    }
    (void)advance(lexer);
    return token(lexer, TOK_STRING, start);
}

static void skip_interpolation_expression_string(Lexer *lexer) {
    (void)advance(lexer);
    while (!at_end(lexer)) {
        if (peek(lexer) == '\\') {
            (void)advance(lexer);
            if (!at_end(lexer))
                (void)advance(lexer);
        } else if (peek(lexer) == '"') {
            (void)advance(lexer);
            return;
        } else {
            (void)advance(lexer);
        }
    }
}

static void skip_interpolation_expression_comment(Lexer *lexer) {
    if (peek_next(lexer) == '/') {
        lexer->offset += 2U;
        while (!at_end(lexer) && peek(lexer) != '\n')
            (void)advance(lexer);
        return;
    }
    lexer->offset += 2U;
    unsigned depth = 1U;
    while (!at_end(lexer) && depth != 0U) {
        if (peek(lexer) == '/' && peek_next(lexer) == '*') {
            lexer->offset += 2U;
            ++depth;
        } else if (
            peek(lexer) == '*' && peek_next(lexer) == '/') {
            lexer->offset += 2U;
            --depth;
        } else {
            (void)advance(lexer);
        }
    }
}

static Token interpolated_string(Lexer *lexer, size_t start) {
    unsigned expression_depth = 0U;
    while (!at_end(lexer)) {
        if (expression_depth == 0U) {
            if (peek(lexer) == '"') {
                (void)advance(lexer);
                return token(lexer, TOK_STRING, start);
            }
            if (peek(lexer) == '\n')
                break;
            if (peek(lexer) == '\\') {
                (void)advance(lexer);
                if (at_end(lexer))
                    break;
                char escape = peek(lexer);
                if (escape != 'n' && escape != 'r' &&
                    escape != 't' && escape != '\\' &&
                    escape != '"' && escape != '0' &&
                    escape != '{' && escape != '}') {
                    LangSpan span = {
                        lang_source_path_at(
                            lexer->source,
                            lexer->offset - 1U),
                        lexer->offset - 1U,
                        lexer->offset + 1U
                    };
                    lang_diag(
                        lexer->diagnostics, span,
                        "invalid string escape");
                }
                (void)advance(lexer);
                continue;
            }
            if (peek(lexer) == '{') {
                ++expression_depth;
                (void)advance(lexer);
                continue;
            }
            (void)advance(lexer);
            continue;
        }

        if (peek(lexer) == '"') {
            skip_interpolation_expression_string(lexer);
        } else if (
            peek(lexer) == '/' &&
            (peek_next(lexer) == '/' ||
             peek_next(lexer) == '*')) {
            skip_interpolation_expression_comment(lexer);
        } else if (peek(lexer) == '{') {
            ++expression_depth;
            (void)advance(lexer);
        } else if (peek(lexer) == '}') {
            --expression_depth;
            (void)advance(lexer);
        } else {
            (void)advance(lexer);
        }
    }

    LangSpan span = {
        lang_source_path_at(lexer->source, start),
        start, lexer->offset
    };
    lang_diag(
        lexer->diagnostics, span,
        "unterminated interpolated string literal");
    return token(lexer, TOK_ERROR, start);
}

Token lang_lexer_next(Lexer *lexer) {
    skip_trivia(lexer);
    size_t start = lexer->offset;
    bool interpolation = lexer->interpolation_pending;
    lexer->interpolation_pending = false;
    if (at_end(lexer)) return token(lexer, TOK_EOF, start);
    char c = advance(lexer);
    if (isalpha((unsigned char)c) != 0 || c == '_') return identifier(lexer, start);
    if (isdigit((unsigned char)c) != 0) return number(lexer, start);
    if (c == '"')
        return interpolation
            ? interpolated_string(lexer, start)
            : string(lexer, start);
#define SIMPLE(character, kind_) case character: return token(lexer, kind_, start)
    switch (c) {
        case '$':
            lexer->interpolation_pending = true;
            return token(lexer, TOK_DOLLAR, start);
        SIMPLE('(', TOK_LPAREN); SIMPLE(')', TOK_RPAREN);
        SIMPLE('{', TOK_LBRACE); SIMPLE('}', TOK_RBRACE);
        SIMPLE('[', TOK_LBRACKET); SIMPLE(']', TOK_RBRACKET);
        SIMPLE(',', TOK_COMMA); SIMPLE(';', TOK_SEMICOLON);
        case '.':
            if (peek(lexer) == '.') {
                (void)advance(lexer);
                if (peek(lexer) == '=') {
                    (void)advance(lexer);
                    return token(lexer, TOK_DOT_DOT_EQUAL, start);
                }
                return token(lexer, TOK_DOT_DOT, start);
            }
            return token(lexer, TOK_DOT, start);
        case '+':
            if (peek(lexer) == '+') { (void)advance(lexer); return token(lexer, TOK_PLUS_PLUS, start); }
            if (peek(lexer) == '=') { (void)advance(lexer); return token(lexer, TOK_PLUS_EQUAL, start); }
            return token(lexer, TOK_PLUS, start);
        case '*':
            if (peek(lexer) == '=') { (void)advance(lexer); return token(lexer, TOK_STAR_EQUAL, start); }
            return token(lexer, TOK_STAR, start);
        case '/':
            if (peek(lexer) == '=') { (void)advance(lexer); return token(lexer, TOK_SLASH_EQUAL, start); }
            return token(lexer, TOK_SLASH, start);
        case '%':
            if (peek(lexer) == '=') { (void)advance(lexer); return token(lexer, TOK_PERCENT_EQUAL, start); }
            return token(lexer, TOK_PERCENT, start);
        case '&':
            if (peek(lexer) == '&') {
                (void)advance(lexer);
                return token(lexer, TOK_AND_AND, start);
            }
            if (peek(lexer) == '=') {
                (void)advance(lexer);
                return token(lexer, TOK_AMP_EQUAL, start);
            }
            return token(lexer, TOK_AMP, start);
        case '|':
            if (peek(lexer) == '|') {
                (void)advance(lexer);
                return token(lexer, TOK_OR_OR, start);
            }
            if (peek(lexer) == '=') {
                (void)advance(lexer);
                return token(lexer, TOK_PIPE_EQUAL, start);
            }
            return token(lexer, TOK_PIPE, start);
        case '^':
            if (peek(lexer) == '=') {
                (void)advance(lexer);
                return token(lexer, TOK_CARET_EQUAL, start);
            }
            return token(lexer, TOK_CARET, start);
        SIMPLE('~', TOK_TILDE);
        case ':':
            return token(lexer, TOK_COLON, start);
        case '-':
            if (peek(lexer) == '-') { (void)advance(lexer); return token(lexer, TOK_MINUS_MINUS, start); }
            if (peek(lexer) == '>') { (void)advance(lexer); return token(lexer, TOK_ARROW, start); }
            if (peek(lexer) == '=') { (void)advance(lexer); return token(lexer, TOK_MINUS_EQUAL, start); }
            return token(lexer, TOK_MINUS, start);
        case '=':
            if (peek(lexer) == '=') { (void)advance(lexer); return token(lexer, TOK_EQUAL_EQUAL, start); }
            if (peek(lexer) == '>') { (void)advance(lexer); return token(lexer, TOK_FAT_ARROW, start); }
            return token(lexer, TOK_EQUAL, start);
        case '!':
            if (peek(lexer) == '=') { (void)advance(lexer); return token(lexer, TOK_BANG_EQUAL, start); }
            return token(lexer, TOK_BANG, start);
        case '?': return token(lexer, TOK_QUESTION, start);
        case '<':
            if (peek(lexer) == '=') { (void)advance(lexer); return token(lexer, TOK_LESS_EQUAL, start); }
            if (peek(lexer) == '<') {
                (void)advance(lexer);
                if (peek(lexer) == '=') {
                    (void)advance(lexer);
                    return token(lexer, TOK_SHIFT_LEFT_EQUAL, start);
                }
                return token(lexer, TOK_SHIFT_LEFT, start);
            }
            return token(lexer, TOK_LESS, start);
        case '>':
            if (peek(lexer) == '=') { (void)advance(lexer); return token(lexer, TOK_GREATER_EQUAL, start); }
            if (peek(lexer) == '>') {
                (void)advance(lexer);
                if (peek(lexer) == '=') {
                    (void)advance(lexer);
                    return token(lexer, TOK_SHIFT_RIGHT_EQUAL, start);
                }
                return token(lexer, TOK_SHIFT_RIGHT, start);
            }
            return token(lexer, TOK_GREATER, start);
        default: break;
    }
#undef SIMPLE
    LangSpan span = {
        lang_source_path_at(lexer->source, start),
        start, lexer->offset
    };
    lang_diag(lexer->diagnostics, span, "unexpected byte 0x%02x", (unsigned char)c);
    return token(lexer, TOK_ERROR, start);
}

const char *lang_token_name(TokenKind kind) {
    static const char *names[] = {
        "eof","error","identifier","integer","float","string","$",
        "var","new","const","ref","out","struct","enum","union","if","else","while","for","foreach","in","switch","case",
        "return","break","continue","true","false","null","unsafe","try","catch","finally","throw","async","await",
        "namespace","using","as","public","private","extern","type","delegate","element",
        "(",")","{","}","[","]",",",":",
        ";",".","..","..=","+","-","*","/","%","++","--","+=","-=","*=","/=","%=",
        "&","|","^","~","&=","|=","^=","<<=",">>=",
        "&&","||",
        "=","==","!","!=","?","<","<=",">",">=",
        "<<",">>",
        "->","=>"
    };
    size_t index = (size_t)kind;
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "?";
}

void lang_dump_tokens(const LangSource *source, LangDiagnostics *diagnostics) {
    Lexer lexer;
    lang_lexer_init(&lexer, source, diagnostics);
    for (;;) {
        Token tok = lang_lexer_next(&lexer);
        printf("%04zu..%-4zu %-14s %.*s\n", tok.span.start, tok.span.end,
               lang_token_name(tok.kind), (int)tok.length, tok.start);
        if (tok.kind == TOK_EOF) break;
    }
}
