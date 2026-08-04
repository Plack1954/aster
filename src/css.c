#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CssParser {
    const LangSource *source;
    const char *text;
    size_t end;
    size_t offset;
    LangArena *arena;
    LangDiagnostics *diagnostics;
    bool failed;
} CssParser;

typedef struct CssNodeBuffer {
    CssNode *items;
    size_t count;
    size_t capacity;
} CssNodeBuffer;

static LangSpan css_span(const CssParser *parser, size_t start, size_t end) {
    return (LangSpan){
        lang_source_path_at(parser->source, start), start, end
    };
}

static bool css_space(char byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' ||
           byte == '\r' || byte == '\f';
}

static void css_error(CssParser *parser, size_t start, size_t end,
                      const char *message) {
    lang_diag(parser->diagnostics, css_span(parser, start, end), "%s", message);
    parser->failed = true;
}

static bool css_comment_at(const CssParser *parser, size_t offset) {
    return offset + 1U < parser->end &&
           parser->text[offset] == '/' && parser->text[offset + 1U] == '*';
}

static bool css_skip_comment(CssParser *parser) {
    size_t start = parser->offset;
    parser->offset += 2U;
    while (parser->offset + 1U < parser->end) {
        if (parser->text[parser->offset] == '*' &&
            parser->text[parser->offset + 1U] == '/') {
            parser->offset += 2U;
            return true;
        }
        ++parser->offset;
    }
    parser->offset = parser->end;
    css_error(parser, start, parser->end, "unterminated CSS comment");
    return false;
}

static bool css_skip_string(CssParser *parser) {
    size_t start = parser->offset;
    char quote = parser->text[parser->offset++];
    while (parser->offset < parser->end) {
        char byte = parser->text[parser->offset++];
        if (byte == quote) return true;
        if (byte == '\\') {
            if (parser->offset < parser->end) {
                if (parser->text[parser->offset] == '\r' &&
                    parser->offset + 1U < parser->end &&
                    parser->text[parser->offset + 1U] == '\n')
                    parser->offset += 2U;
                else
                    ++parser->offset;
            }
        } else if (byte == '\n' || byte == '\r' || byte == '\f') {
            css_error(parser, start, parser->offset,
                      "unterminated CSS string");
            return false;
        }
    }
    css_error(parser, start, parser->end, "unterminated CSS string");
    return false;
}

static void css_skip_trivia(CssParser *parser) {
    for (;;) {
        while (parser->offset < parser->end &&
               css_space(parser->text[parser->offset]))
            ++parser->offset;
        if (!css_comment_at(parser, parser->offset)) return;
        if (!css_skip_comment(parser)) return;
    }
}

static void css_trim(const CssParser *parser, size_t *start, size_t *end) {
    while (*start < *end && css_space(parser->text[*start])) ++*start;
    while (*end > *start && css_space(parser->text[*end - 1U])) --*end;
}

static void css_append_node(CssNodeBuffer *buffer, CssNode node) {
    if (buffer->count == buffer->capacity) {
        size_t capacity = buffer->capacity == 0U
            ? 16U : buffer->capacity * 2U;
        if (capacity < buffer->capacity ||
            capacity > SIZE_MAX / sizeof(*buffer->items)) {
            fputs("fatal: CSS AST is too large\n", stderr);
            exit(2);
        }
        CssNode *items = realloc(
            buffer->items, capacity * sizeof(*items));
        if (items == NULL) {
            fputs("fatal: out of memory\n", stderr);
            exit(2);
        }
        buffer->items = items;
        buffer->capacity = capacity;
    }
    buffer->items[buffer->count++] = node;
}

static void css_finish_nodes(CssParser *parser, CssNodeBuffer *buffer,
                             CssNode **nodes, size_t *count) {
    if (buffer->count != 0U) {
        *nodes = lang_arena_alloc(
            parser->arena, buffer->count * sizeof(**nodes));
        memcpy(*nodes, buffer->items,
               buffer->count * sizeof(**nodes));
    }
    *count = buffer->count;
    free(buffer->items);
    *buffer = (CssNodeBuffer){0};
}

static bool css_ident_start(unsigned char byte) {
    return isalpha(byte) != 0 || byte == '_' || byte == '-' || byte >= 0x80U;
}

static bool css_ident_continue(unsigned char byte) {
    return css_ident_start(byte) || isdigit(byte) != 0;
}

static size_t css_scan_ident(CssParser *parser, size_t offset) {
    if (offset >= parser->end ||
        (!css_ident_start((unsigned char)parser->text[offset]) &&
         parser->text[offset] != '\\'))
        return offset;
    while (offset < parser->end) {
        unsigned char byte = (unsigned char)parser->text[offset];
        if (css_ident_continue(byte)) {
            ++offset;
        } else if (byte == '\\' && offset + 1U < parser->end) {
            offset += 2U;
        } else {
            break;
        }
    }
    return offset;
}

static bool css_parse_list(CssParser *parser, CssNodeBuffer *nodes,
                           bool in_block);

static bool css_scan_component(CssParser *parser, char close) {
    char open = parser->text[parser->offset++];
    while (parser->offset < parser->end) {
        char byte = parser->text[parser->offset];
        if (css_comment_at(parser, parser->offset)) {
            if (!css_skip_comment(parser)) return false;
        } else if (byte == '\'' || byte == '"') {
            if (!css_skip_string(parser)) return false;
        } else if (byte == '\\') {
            parser->offset += parser->offset + 1U < parser->end ? 2U : 1U;
        } else if (byte == '(') {
            if (!css_scan_component(parser, ')')) return false;
        } else if (byte == '[') {
            if (!css_scan_component(parser, ']')) return false;
        } else if (byte == open) {
            if (!css_scan_component(parser, close)) return false;
        } else if (byte == close) {
            ++parser->offset;
            return true;
        } else if (byte == ')' || byte == ']') {
            css_error(parser, parser->offset, parser->offset + 1U,
                      "unexpected closing delimiter in CSS");
            ++parser->offset;
            return false;
        } else {
            ++parser->offset;
        }
    }
    css_error(parser, parser->offset, parser->end,
              "unterminated CSS component value");
    return false;
}

static bool css_parse_at_rule(CssParser *parser, CssNode *out) {
    size_t start = parser->offset++;
    size_t name_start = parser->offset;
    size_t name_end = css_scan_ident(parser, name_start);
    if (name_end == name_start) {
        css_error(parser, start, parser->offset,
                  "expected CSS at-rule name after `@`");
        return false;
    }
    parser->offset = name_end;
    size_t value_start = parser->offset;
    for (;;) {
        if (parser->offset >= parser->end) {
            size_t value_end = parser->end;
            css_trim(parser, &value_start, &value_end);
            *out = (CssNode){
                .kind=CSS_AT_RULE,
                .span=css_span(parser, start, parser->end),
                .name=css_span(parser, name_start, name_end),
                .value=css_span(parser, value_start, value_end)
            };
            return true;
        }
        char byte = parser->text[parser->offset];
        if (css_comment_at(parser, parser->offset)) {
            if (!css_skip_comment(parser)) return false;
        } else if (byte == '\'' || byte == '"') {
            if (!css_skip_string(parser)) return false;
        } else if (byte == '(') {
            if (!css_scan_component(parser, ')')) return false;
        } else if (byte == '[') {
            if (!css_scan_component(parser, ']')) return false;
        } else if (byte == ';') {
            size_t value_end = parser->offset++;
            css_trim(parser, &value_start, &value_end);
            *out = (CssNode){
                .kind=CSS_AT_RULE,
                .span=css_span(parser, start, parser->offset),
                .name=css_span(parser, name_start, name_end),
                .value=css_span(parser, value_start, value_end)
            };
            return true;
        } else if (byte == '{') {
            size_t value_end = parser->offset++;
            css_trim(parser, &value_start, &value_end);
            *out = (CssNode){
                .kind=CSS_AT_RULE,
                .name=css_span(parser, name_start, name_end),
                .value=css_span(parser, value_start, value_end)
            };
            CssNodeBuffer children = {0};
            if (!css_parse_list(parser, &children, true)) {
                free(children.items);
                return false;
            }
            css_finish_nodes(
                parser, &children, &out->children, &out->child_count);
            out->span = css_span(parser, start, parser->offset);
            return true;
        } else if (byte == '}') {
            size_t value_end = parser->offset;
            css_trim(parser, &value_start, &value_end);
            *out = (CssNode){
                .kind=CSS_AT_RULE,
                .span=css_span(parser, start, parser->offset),
                .name=css_span(parser, name_start, name_end),
                .value=css_span(parser, value_start, value_end)
            };
            return true;
        } else {
            ++parser->offset;
        }
    }
}

static bool css_parse_qualified(CssParser *parser, CssNode *out) {
    size_t start = parser->offset;
    for (;;) {
        if (parser->offset >= parser->end) {
            css_error(parser, start, parser->end,
                      "expected `{` after CSS selector");
            return false;
        }
        char byte = parser->text[parser->offset];
        if (css_comment_at(parser, parser->offset)) {
            if (!css_skip_comment(parser)) return false;
        } else if (byte == '\'' || byte == '"') {
            if (!css_skip_string(parser)) return false;
        } else if (byte == '(') {
            if (!css_scan_component(parser, ')')) return false;
        } else if (byte == '[') {
            if (!css_scan_component(parser, ']')) return false;
        } else if (byte == '{') {
            size_t prelude_start = start;
            size_t prelude_end = parser->offset++;
            css_trim(parser, &prelude_start, &prelude_end);
            if (prelude_start == prelude_end) {
                css_error(parser, start, parser->offset,
                          "expected CSS selector before `{`");
                return false;
            }
            *out = (CssNode){
                .kind=CSS_STYLE_RULE,
                .name=css_span(parser, prelude_start, prelude_end)
            };
            CssNodeBuffer children = {0};
            if (!css_parse_list(parser, &children, true)) {
                free(children.items);
                return false;
            }
            css_finish_nodes(
                parser, &children, &out->children, &out->child_count);
            out->span = css_span(parser, start, parser->offset);
            return true;
        } else if (byte == ';' || byte == '}') {
            css_error(parser, start, parser->offset + 1U,
                      "expected `{` after CSS selector");
            if (byte == ';') ++parser->offset;
            return false;
        } else {
            ++parser->offset;
        }
    }
}

static bool css_parse_block_item(CssParser *parser, CssNode *out) {
    if (parser->text[parser->offset] == '@')
        return css_parse_at_rule(parser, out);
    size_t start = parser->offset;
    size_t colon = SIZE_MAX;
    for (;;) {
        if (parser->offset >= parser->end) {
            css_error(parser, start, parser->end,
                      "unterminated CSS block");
            return false;
        }
        char byte = parser->text[parser->offset];
        if (css_comment_at(parser, parser->offset)) {
            if (!css_skip_comment(parser)) return false;
        } else if (byte == '\'' || byte == '"') {
            if (!css_skip_string(parser)) return false;
        } else if (byte == '(') {
            if (!css_scan_component(parser, ')')) return false;
        } else if (byte == '[') {
            if (!css_scan_component(parser, ']')) return false;
        } else if (byte == ':' && colon == SIZE_MAX) {
            colon = parser->offset++;
        } else if (byte == '{') {
            /* Braces in custom-property values are component values, not
             * nested rules. Keep them opaque through the declaration end. */
            size_t name_start = start;
            size_t name_end = colon;
            if (colon != SIZE_MAX) css_trim(parser, &name_start, &name_end);
            bool custom = colon != SIZE_MAX && name_end >= name_start + 2U &&
                parser->text[name_start] == '-' &&
                parser->text[name_start + 1U] == '-';
            if (custom) {
                if (!css_scan_component(parser, '}')) return false;
                continue;
            }
            parser->offset = start;
            return css_parse_qualified(parser, out);
        } else if (byte == ';' || byte == '}') {
            size_t item_end = parser->offset;
            bool closes = byte == '}';
            if (!closes) ++parser->offset;
            size_t trimmed_start = start;
            size_t trimmed_end = item_end;
            css_trim(parser, &trimmed_start, &trimmed_end);
            if (trimmed_start == trimmed_end) return false;
            if (colon == SIZE_MAX || colon >= trimmed_end) {
                css_error(parser, trimmed_start, trimmed_end,
                          "expected `:` in CSS declaration");
                return false;
            }
            size_t name_start = trimmed_start;
            size_t name_end = colon;
            size_t value_start = colon + 1U;
            size_t value_end = trimmed_end;
            css_trim(parser, &name_start, &name_end);
            css_trim(parser, &value_start, &value_end);
            *out = (CssNode){
                .kind=CSS_DECLARATION,
                .span=css_span(parser, start,
                               closes ? item_end : parser->offset),
                .name=css_span(parser, name_start, name_end),
                .value=css_span(parser, value_start, value_end)
            };
            return true;
        } else {
            ++parser->offset;
        }
    }
}

static bool css_parse_list(CssParser *parser, CssNodeBuffer *nodes,
                           bool in_block) {
    for (;;) {
        css_skip_trivia(parser);
        if (parser->offset >= parser->end) {
            if (in_block) {
                css_error(parser, parser->end, parser->end,
                          "unterminated CSS block");
                return false;
            }
            return !parser->failed;
        }
        if (!in_block && parser->offset + 4U <= parser->end &&
            memcmp(parser->text + parser->offset, "<!--", 4U) == 0) {
            parser->offset += 4U;
            continue;
        }
        if (!in_block && parser->offset + 3U <= parser->end &&
            memcmp(parser->text + parser->offset, "-->", 3U) == 0) {
            parser->offset += 3U;
            continue;
        }
        if (parser->text[parser->offset] == '}') {
            if (!in_block) {
                css_error(parser, parser->offset, parser->offset + 1U,
                          "unexpected `}` in CSS stylesheet");
                ++parser->offset;
                continue;
            }
            ++parser->offset;
            return !parser->failed;
        }
        CssNode node = {0};
        size_t before = parser->offset;
        bool parsed = in_block
            ? css_parse_block_item(parser, &node)
            : parser->text[parser->offset] == '@'
                ? css_parse_at_rule(parser, &node)
                : css_parse_qualified(parser, &node);
        if (parsed)
            css_append_node(nodes, node);
        if (parser->offset == before) ++parser->offset;
    }
}

bool lang_css_parse(const LangSource *source, size_t start, size_t end,
                    LangArena *arena, LangDiagnostics *diagnostics,
                    CssStylesheet **out_sheet) {
    if (source == NULL || arena == NULL || diagnostics == NULL ||
        out_sheet == NULL || start > end || end > source->length)
        return false;
    CssParser parser = {
        .source=source,
        .text=source->text,
        .end=end,
        .offset=start,
        .arena=arena,
        .diagnostics=diagnostics
    };
    CssStylesheet *sheet = lang_arena_alloc(arena, sizeof(*sheet));
    sheet->span = css_span(&parser, start, end);
    CssNodeBuffer children = {0};
    bool ok = css_parse_list(&parser, &children, false);
    css_finish_nodes(
        &parser, &children, &sheet->children, &sheet->child_count);
    *out_sheet = sheet;
    return ok && !parser.failed;
}

typedef struct CssOffsetBuffer {
    size_t *items;
    size_t count;
    size_t capacity;
} CssOffsetBuffer;

static bool css_offsets_push(CssOffsetBuffer *buffer, size_t offset) {
    if (buffer->count == buffer->capacity) {
        size_t capacity = buffer->capacity == 0U ? 16U : buffer->capacity * 2U;
        if (capacity < buffer->capacity || capacity > SIZE_MAX / sizeof(size_t))
            return false;
        size_t *items = realloc(buffer->items, capacity * sizeof(size_t));
        if (items == NULL) return false;
        buffer->items = items;
        buffer->capacity = capacity;
    }
    buffer->items[buffer->count++] = offset;
    return true;
}

static size_t css_scope_skip_comment(const char *text, size_t offset,
                                     size_t end) {
    offset += 2U;
    while (offset + 1U < end) {
        if (text[offset] == '*' && text[offset + 1U] == '/')
            return offset + 2U;
        ++offset;
    }
    return end;
}

static size_t css_scope_skip_string(const char *text, size_t offset,
                                    size_t end) {
    char quote = text[offset++];
    while (offset < end) {
        if (text[offset] == '\\') {
            offset += offset + 1U < end ? 2U : 1U;
            continue;
        }
        if (text[offset++] == quote) break;
    }
    return offset;
}

static bool css_scope_selector(const char *text, size_t start, size_t end,
                               CssOffsetBuffer *offsets) {
    size_t segment_start = start;
    unsigned parentheses = 0U;
    unsigned brackets = 0U;
    for (size_t cursor = start; cursor <= end;) {
        bool boundary = cursor == end;
        if (!boundary && text[cursor] == '/' && cursor + 1U < end &&
            text[cursor + 1U] == '*') {
            cursor = css_scope_skip_comment(text, cursor, end);
            continue;
        }
        if (!boundary && (text[cursor] == '\'' || text[cursor] == '"')) {
            cursor = css_scope_skip_string(text, cursor, end);
            continue;
        }
        if (!boundary) {
            if (text[cursor] == '(') ++parentheses;
            else if (text[cursor] == ')' && parentheses != 0U) --parentheses;
            else if (text[cursor] == '[') ++brackets;
            else if (text[cursor] == ']' && brackets != 0U) --brackets;
            else if (text[cursor] == ',' && parentheses == 0U && brackets == 0U)
                boundary = true;
        }
        if (!boundary) {
            ++cursor;
            continue;
        }

        size_t segment_end = cursor;
        while (segment_start < segment_end && css_space(text[segment_start]))
            ++segment_start;
        while (segment_end > segment_start && css_space(text[segment_end - 1U]))
            --segment_end;
        if (segment_start < segment_end) {
            size_t insertion = segment_end;
            parentheses = 0U;
            brackets = 0U;
            for (size_t scan = segment_start; scan < segment_end;) {
                if (text[scan] == '/' && scan + 1U < segment_end &&
                    text[scan + 1U] == '*') {
                    scan = css_scope_skip_comment(text, scan, segment_end);
                    continue;
                }
                if (text[scan] == '\'' || text[scan] == '"') {
                    scan = css_scope_skip_string(text, scan, segment_end);
                    continue;
                }
                if (text[scan] == '(') ++parentheses;
                else if (text[scan] == ')' && parentheses != 0U) --parentheses;
                else if (text[scan] == '[') ++brackets;
                else if (text[scan] == ']' && brackets != 0U) --brackets;
                else if (parentheses == 0U && brackets == 0U &&
                         text[scan] == ':' && scan + 1U < segment_end &&
                         text[scan + 1U] == ':') {
                    insertion = scan;
                    break;
                }
                ++scan;
            }
            if (!css_offsets_push(offsets, insertion)) return false;
        }
        if (cursor == end) break;
        segment_start = ++cursor;
        parentheses = 0U;
        brackets = 0U;
    }
    return true;
}

static bool css_name_is_keyframes(const LangSource *source, LangSpan name) {
    static const char suffix[] = "keyframes";
    size_t length = name.end - name.start;
    if (length < sizeof(suffix) - 1U) return false;
    size_t start = name.end - (sizeof(suffix) - 1U);
    for (size_t i = 0U; i < sizeof(suffix) - 1U; ++i)
        if ((char)tolower((unsigned char)source->text[start + i]) != suffix[i])
            return false;
    return true;
}

static bool css_collect_scope_offsets(const LangSource *source,
                                      const CssNode *nodes, size_t count,
                                      bool in_keyframes,
                                      CssOffsetBuffer *offsets) {
    for (size_t i = 0U; i < count; ++i) {
        const CssNode *node = &nodes[i];
        bool child_keyframes = in_keyframes;
        if (node->kind == CSS_STYLE_RULE && !in_keyframes &&
            !css_scope_selector(source->text, node->name.start,
                                node->name.end, offsets))
            return false;
        if (node->kind == CSS_AT_RULE &&
            css_name_is_keyframes(source, node->name))
            child_keyframes = true;
        if (node->child_count != 0U &&
            !css_collect_scope_offsets(source, node->children,
                                       node->child_count, child_keyframes,
                                       offsets))
            return false;
    }
    return true;
}

bool lang_css_scope(const LangSource *source, const CssStylesheet *sheet,
                    const char *attribute, LangArena *arena,
                    const char **out_text, size_t *out_length) {
    if (source == NULL || sheet == NULL || attribute == NULL || arena == NULL ||
        out_text == NULL || out_length == NULL)
        return false;
    CssOffsetBuffer offsets = {0};
    if (!css_collect_scope_offsets(source, sheet->children,
                                   sheet->child_count, false, &offsets)) {
        free(offsets.items);
        return false;
    }
    size_t marker_length = strlen(attribute) + 2U;
    size_t source_length = sheet->span.end - sheet->span.start;
    if (offsets.count > (SIZE_MAX - source_length) / marker_length) {
        free(offsets.items);
        return false;
    }
    size_t length = source_length + offsets.count * marker_length;
    char *result = lang_arena_alloc(arena, length + 1U);
    size_t input = sheet->span.start;
    size_t output = 0U;
    for (size_t i = 0U; i < offsets.count; ++i) {
        size_t chunk = offsets.items[i] - input;
        memcpy(result + output, source->text + input, chunk);
        output += chunk;
        result[output++] = '[';
        size_t attribute_length = strlen(attribute);
        memcpy(result + output, attribute, attribute_length);
        output += attribute_length;
        result[output++] = ']';
        input = offsets.items[i];
    }
    size_t tail = sheet->span.end - input;
    memcpy(result + output, source->text + input, tail);
    output += tail;
    result[output] = '\0';
    free(offsets.items);
    *out_text = result;
    *out_length = output;
    return true;
}
