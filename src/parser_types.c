#include "parser_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (probe_accept(probe, TOK_LPAREN)) {
        if (!probe_type(probe)) return false;
        while (probe_accept(probe, TOK_COMMA))
            if (!probe_type(probe)) return false;
        return probe_accept(probe, TOK_RPAREN);
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

bool looks_like_deconstruction(const Parser *parser) {
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

bool parser_looks_like_cast(const Parser *parser) {
    if (parser->current.kind != TOK_LPAREN) return false;
    TypeProbe probe = {parser->lexer, parser->current};
    probe_next(&probe);
    if (!probe_type(&probe) || !probe_accept(&probe, TOK_RPAREN))
        return false;
    switch (probe.current.kind) {
        case TOK_IDENT:
        case TOK_INT:
        case TOK_FLOAT:
        case TOK_STRING:
        case TOK_DOLLAR:
        case TOK_TRUE:
        case TOK_FALSE:
        case TOK_NULL:
        case TOK_NEW:
        case TOK_LPAREN:
        case TOK_LBRACKET:
        case TOK_LESS:
        case TOK_MINUS:
        case TOK_BANG:
        case TOK_TILDE:
        case TOK_STAR:
        case TOK_TRY:
        case TOK_AWAIT:
            return true;
        default:
            return false;
    }
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

static TypeSyntax *parse_type_syntax(Parser *parser);

TypeSyntax *new_type_syntax(Parser *parser, TypeSyntaxKind kind,
                            LangSpan span) {
    TypeSyntax *syntax = lang_arena_alloc(
        &parser->module->arena, sizeof(*syntax));
    syntax->kind = kind;
    syntax->span = span;
    return syntax;
}

static size_t type_syntax_text_length(const TypeSyntax *syntax) {
    switch (syntax->kind) {
        case TYPE_SYNTAX_NAMED: return strlen(syntax->as.name);
        case TYPE_SYNTAX_GENERIC: {
            size_t length = type_syntax_text_length(syntax->as.generic.base) + 2U;
            for (size_t i = 0U; i < syntax->as.generic.argument_count; ++i)
                length += type_syntax_text_length(
                    syntax->as.generic.arguments[i]) + (i == 0U ? 0U : 1U);
            return length;
        }
        case TYPE_SYNTAX_FUNCTION: {
            size_t length = sizeof("Func<>") - 1U +
                type_syntax_text_length(syntax->as.function.return_type);
            for (size_t i = 0U; i < syntax->as.function.parameter_count; ++i) {
                ParameterMode mode = syntax->as.function.parameter_modes[i];
                length += type_syntax_text_length(
                    syntax->as.function.parameters[i]) +
                    1U +
                    (mode == PARAMETER_MODE_VALUE ? 0U : 4U);
            }
            return length;
        }
        case TYPE_SYNTAX_POINTER:
            return (syntax->as.pointer.mutable_ ? 1U : 7U) +
                type_syntax_text_length(syntax->as.pointer.element);
        case TYPE_SYNTAX_ARRAY: {
            char count[32];
            int written = snprintf(count, sizeof(count), "%zu",
                                   syntax->as.array.count);
            return type_syntax_text_length(syntax->as.array.element) +
                (written > 0 ? (size_t)written : 0U) + 2U;
        }
        case TYPE_SYNTAX_TUPLE: {
            size_t length = 2U;
            for (size_t i = 0U; i < syntax->as.tuple.element_count; ++i)
                length += type_syntax_text_length(
                    syntax->as.tuple.elements[i]) + (i == 0U ? 0U : 1U);
            return length;
        }
        case TYPE_SYNTAX_ERROR: return sizeof("error") - 1U;
    }
    return 0U;
}

static char *write_type_syntax(char *out, const TypeSyntax *syntax) {
    switch (syntax->kind) {
        case TYPE_SYNTAX_NAMED: {
            size_t length = strlen(syntax->as.name);
            memcpy(out, syntax->as.name, length);
            return out + length;
        }
        case TYPE_SYNTAX_GENERIC:
            out = write_type_syntax(out, syntax->as.generic.base);
            *out++ = '<';
            for (size_t i = 0U; i < syntax->as.generic.argument_count; ++i) {
                if (i != 0U) *out++ = ',';
                out = write_type_syntax(out, syntax->as.generic.arguments[i]);
            }
            *out++ = '>';
            return out;
        case TYPE_SYNTAX_FUNCTION:
            memcpy(out, "Func<", 5U);
            out += 5U;
            for (size_t i = 0U; i < syntax->as.function.parameter_count; ++i) {
                if (i != 0U) *out++ = ',';
                ParameterMode mode = syntax->as.function.parameter_modes[i];
                if (mode != PARAMETER_MODE_VALUE) {
                    memcpy(out, mode == PARAMETER_MODE_OUT ? "out " : "ref ", 4U);
                    out += 4U;
                }
                out = write_type_syntax(out, syntax->as.function.parameters[i]);
            }
            if (syntax->as.function.parameter_count != 0U) *out++ = ',';
            out = write_type_syntax(out, syntax->as.function.return_type);
            *out++ = '>';
            return out;
        case TYPE_SYNTAX_POINTER: {
            if (!syntax->as.pointer.mutable_) {
                memcpy(out, "const ", 6U);
                out += 6U;
            }
            out = write_type_syntax(out, syntax->as.pointer.element);
            *out++ = '*';
            return out;
        }
        case TYPE_SYNTAX_ARRAY:
            out = write_type_syntax(out, syntax->as.array.element);
            *out++ = '[';
            out += snprintf(out, 32U, "%zu", syntax->as.array.count);
            *out++ = ']';
            return out;
        case TYPE_SYNTAX_TUPLE:
            *out++ = '(';
            for (size_t i = 0U; i < syntax->as.tuple.element_count; ++i) {
                if (i != 0U) *out++ = ',';
                out = write_type_syntax(out, syntax->as.tuple.elements[i]);
            }
            *out++ = ')';
            return out;
        case TYPE_SYNTAX_ERROR:
            memcpy(out, "error", 5U);
            return out + 5U;
    }
    return out;
}

const char *format_type_syntax(Parser *parser,
                               const TypeSyntax *syntax) {
    /* Retained for diagnostics and legacy symbol spelling. The checker uses
     * the TypeSyntax tree rather than interpreting this text. */
    size_t length = type_syntax_text_length(syntax);
    char *text = lang_arena_alloc(&parser->module->arena, length + 1U);
    char *end = write_type_syntax(text, syntax);
    *end = '\0';
    return text;
}

static size_t parse_array_count(Parser *parser, Token token) {
    char *text = lang_arena_alloc(
        &parser->module->arena, token.length + 1U);
    size_t length = 0U;
    for (size_t i = 0U; i < token.length; ++i)
        if (token.start[i] != '_') text[length++] = token.start[i];
    text[length] = '\0';
    errno = 0;
    unsigned long long value = strtoull(text, NULL, 10);
    if (errno == ERANGE || value > SIZE_MAX) {
        lang_diag(parser->diagnostics, token.span,
                  "array length is too large");
        return 0U;
    }
    return (size_t)value;
}

static TypeSyntax *parse_type_syntax(Parser *parser) {
    Token start = parser->current;
    bool constant = parser_accept(parser, TOK_CONST);
    if (parser_accept(parser, TOK_LPAREN)) {
        ParserArrayBuilder elements = parser_array_builder(
            sizeof(TypeSyntax *));
        while (parser->current.kind != TOK_RPAREN &&
               parser->current.kind != TOK_EOF) {
            TypeSyntax *element = parse_type_syntax(parser);
            parser_array_push(&elements, &element);
            if (!parser_accept(parser, TOK_COMMA)) break;
        }
        Token close = parser_expect(
            parser, TOK_RPAREN, "expected `)` after tuple type");
        TypeSyntax *tuple = new_type_syntax(
            parser, TYPE_SYNTAX_TUPLE,
            (LangSpan){start.span.file, start.span.start, close.span.end});
        tuple->as.tuple.element_count = elements.count;
        tuple->as.tuple.elements = parser_array_freeze(parser, &elements);
        return tuple;
    }
    Token name_token = parser_expect(
        parser, TOK_IDENT, "expected type name");
    const char *name = parser_copy_token(parser, name_token);
    while (parser->current.kind == TOK_DOT) {
        parser_next(parser);
        Token part = parser_expect(
            parser, TOK_IDENT, "expected qualified type name");
        name = join_text(
            parser, name, "::", parser_copy_token(parser, part));
    }
    TypeSyntax *syntax = new_type_syntax(
        parser, TYPE_SYNTAX_NAMED, name_token.span);
    syntax->as.name = name;
    if (parser_accept(parser, TOK_LESS)) {
        ParserArrayBuilder arguments = parser_array_builder(
            sizeof(TypeSyntax *));
        while (parser->current.kind != TOK_GREATER &&
               parser->current.kind != TOK_EOF) {
            TypeSyntax *argument = parse_type_syntax(parser);
            parser_array_push(&arguments, &argument);
            if (!parser_accept(parser, TOK_COMMA)) break;
        }
        Token close = parser->current;
        expect_type_greater(parser);
        if (strcmp(name, "Func") == 0 || strcmp(name, "Action") == 0) {
            bool action = strcmp(name, "Action") == 0;
            if (!action && arguments.count == 0U) {
                lang_diag(parser->diagnostics, name_token.span,
                          "`Func` requires at least a return type");
                syntax->kind = TYPE_SYNTAX_ERROR;
                free(arguments.items);
            } else {
                TypeSyntax **items = arguments.items;
                TypeSyntax *return_type;
                if (action) {
                    return_type = new_type_syntax(
                        parser, TYPE_SYNTAX_NAMED, name_token.span);
                    return_type->as.name = "Unit";
                } else {
                    return_type = items[arguments.count - 1U];
                }
                TypeSyntax *function = new_type_syntax(
                    parser, TYPE_SYNTAX_FUNCTION,
                    (LangSpan){name_token.span.file, name_token.span.start,
                               close.span.end});
                function->as.function.parameter_count =
                    action ? arguments.count : arguments.count - 1U;
                if (function->as.function.parameter_count != 0U) {
                    function->as.function.parameters = lang_arena_alloc(
                        &parser->module->arena,
                        function->as.function.parameter_count *
                            sizeof(*function->as.function.parameters));
                    memcpy(function->as.function.parameters, items,
                           function->as.function.parameter_count *
                               sizeof(*items));
                    function->as.function.parameter_modes = lang_arena_alloc(
                        &parser->module->arena,
                        function->as.function.parameter_count *
                            sizeof(*function->as.function.parameter_modes));
                    for (size_t i = 0U;
                         i < function->as.function.parameter_count; ++i)
                        function->as.function.parameter_modes[i] =
                            PARAMETER_MODE_VALUE;
                }
                function->as.function.return_type = return_type;
                free(arguments.items);
                syntax = function;
            }
        } else {
            TypeSyntax *generic = new_type_syntax(
                parser, TYPE_SYNTAX_GENERIC,
                (LangSpan){name_token.span.file, name_token.span.start,
                           close.span.end});
            generic->as.generic.base = syntax;
            generic->as.generic.argument_count = arguments.count;
            generic->as.generic.arguments = parser_array_freeze(
                parser, &arguments);
            syntax = generic;
        }
    } else if (strcmp(name, "Action") == 0) {
        TypeSyntax *return_type = new_type_syntax(
            parser, TYPE_SYNTAX_NAMED, name_token.span);
        return_type->as.name = "Unit";
        TypeSyntax *function = new_type_syntax(
            parser, TYPE_SYNTAX_FUNCTION, name_token.span);
        function->as.function.return_type = return_type;
        syntax = function;
    }
    if (parser_accept(parser, TOK_QUESTION)) {
        TypeSyntax *base = new_type_syntax(
            parser, TYPE_SYNTAX_NAMED, syntax->span);
        base->as.name = "Option";
        TypeSyntax *option = new_type_syntax(
            parser, TYPE_SYNTAX_GENERIC,
            (LangSpan){syntax->span.file, syntax->span.start,
                       parser->previous.span.end});
        option->as.generic.base = base;
        option->as.generic.argument_count = 1U;
        option->as.generic.arguments = lang_arena_alloc(
            &parser->module->arena, sizeof(TypeSyntax *));
        option->as.generic.arguments[0] = syntax;
        syntax = option;
    }
    if (parser_accept(parser, TOK_STAR)) {
        TypeSyntax *pointer = new_type_syntax(
            parser, TYPE_SYNTAX_POINTER,
            (LangSpan){syntax->span.file, syntax->span.start,
                       parser->previous.span.end});
        pointer->as.pointer.mutable_ = !constant;
        pointer->as.pointer.element = syntax;
        syntax = pointer;
        constant = false;
    }
    if (constant)
        lang_diag(parser->diagnostics, start.span,
                  "`const` currently requires a pointer type such as `const long*`");
    ParserArrayBuilder suffix_counts = parser_array_builder(sizeof(size_t));
    ParserArrayBuilder suffix_closes = parser_array_builder(sizeof(Token));
    while (parser_accept(parser, TOK_LBRACKET)) {
        Token count = parser_expect(
            parser, TOK_INT, "expected fixed array length");
        Token close = parser_expect(
            parser, TOK_RBRACKET, "expected `]` after fixed array length");
        size_t value = parse_array_count(parser, count);
        parser_array_push(&suffix_counts, &value);
        parser_array_push(&suffix_closes, &close);
    }
    if (suffix_counts.count > 16U)
        lang_diag(parser->diagnostics, syntax->span,
                  "fixed arrays are limited to 16 dimensions");
    size_t retained = suffix_counts.count < 16U
        ? suffix_counts.count : 16U;
    size_t *counts = suffix_counts.items;
    Token *closes = suffix_closes.items;
    while (retained != 0U) {
        --retained;
        TypeSyntax *array = new_type_syntax(
            parser, TYPE_SYNTAX_ARRAY,
            (LangSpan){syntax->span.file, syntax->span.start,
                       closes[retained].span.end});
        array->as.array.element = syntax;
        array->as.array.count = counts[retained];
        syntax = array;
    }
    free(suffix_counts.items);
    free(suffix_closes.items);
    return syntax;
}

const char *parse_type(Parser *parser, TypeSyntax **out_syntax) {
    TypeSyntax *syntax = parse_type_syntax(parser);
    if (out_syntax != NULL) *out_syntax = syntax;
    return format_type_syntax(parser, syntax);
}

void parse_declarator_suffix(Parser *parser, TypeSyntax **syntax,
                                    const char **type_name) {
    ParserArrayBuilder counts = parser_array_builder(sizeof(size_t));
    ParserArrayBuilder closes = parser_array_builder(sizeof(Token));
    while (parser_accept(parser, TOK_LBRACKET)) {
        Token count_token = parser_expect(
            parser, TOK_INT, "expected fixed array length");
        Token close = parser_expect(
            parser, TOK_RBRACKET, "expected `]` after fixed array length");
        size_t count = parse_array_count(parser, count_token);
        parser_array_push(&counts, &count);
        parser_array_push(&closes, &close);
    }
    if (counts.count > 16U)
        lang_diag(parser->diagnostics, (*syntax)->span,
                  "fixed arrays are limited to 16 dimensions");
    size_t retained = counts.count < 16U ? counts.count : 16U;
    size_t *count_items = counts.items;
    Token *close_items = closes.items;
    while (retained != 0U) {
        --retained;
        TypeSyntax *array = new_type_syntax(
            parser, TYPE_SYNTAX_ARRAY,
            (LangSpan){(*syntax)->span.file, (*syntax)->span.start,
                       close_items[retained].span.end});
        array->as.array.element = *syntax;
        array->as.array.count = count_items[retained];
        *syntax = array;
    }
    if (counts.count != 0U) *type_name = format_type_syntax(parser, *syntax);
    free(counts.items);
    free(closes.items);
}

void parse_switch_binding(
    Parser *parser, const char **binding,
    const char **binding_type_name, TypeSyntax **binding_type_syntax) {
    Parser probe = *parser;
    TypeSyntax *candidate_syntax = NULL;
    const char *candidate_type = parse_type(&probe, &candidate_syntax);
    if (probe.current.kind == TOK_IDENT) {
        *parser = probe;
        Token name = parser->current;
        parser_next(parser);
        *binding = parser_copy_token(parser, name);
        *binding_type_name = candidate_type;
        *binding_type_syntax = candidate_syntax;
        return;
    }
    Token name = parser_expect(parser, TOK_IDENT,
                        "expected payload binding name");
    *binding = parser_copy_token(parser, name);
}

Expr *parser_parse_element(Parser *parser, LangSpan open_span);

void parse_type_parameters(Parser *parser, Decl *decl) {
    if (!parser_accept(parser, TOK_LESS)) return;
    ParserArrayBuilder parameters = parser_array_builder(
        sizeof(*decl->type_params));
    while (parser->current.kind != TOK_GREATER &&
           parser->current.kind != TOK_EOF) {
        Token parameter = parser_expect(
            parser, TOK_IDENT, "expected generic type parameter");
        const char *name = parser_copy_token(parser, parameter);
        const char **parameter_items = parameters.items;
        for (size_t i = 0U; i < parameters.count; ++i)
            if (strcmp(parameter_items[i], name) == 0)
                lang_diag(parser->diagnostics, parameter.span,
                          "duplicate generic type parameter `%s`",
                          name);
        parser_array_push(&parameters, &name);
        if (!parser_accept(parser, TOK_COMMA)) break;
    }
    expect_type_greater(parser);
    decl->type_param_count = parameters.count;
    decl->type_params = parser_array_freeze(parser, &parameters);
}
