#include "vm_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Object *vm_html_destination(Object *html) {
    while (html->as.html.destination != NULL)
        html = html->as.html.destination;
    return html;
}

static bool html_style_registered(const Object *root, const char *id) {
    const HtmlStyleState *styles = root->as.html.styles;
    if (styles == NULL) return false;
    for (size_t i = 0U; i < styles->id_count; ++i)
        if (strcmp(styles->ids[i], id) == 0) return true;
    return false;
}

static void html_register_style(Object *root, const char *id) {
    if (html_style_registered(root, id)) return;
    if (root->as.html.styles == NULL)
        root->as.html.styles = vm_allocate(1U, sizeof(*root->as.html.styles));
    HtmlStyleState *styles = root->as.html.styles;
    if (styles->id_count == styles->id_capacity) {
        size_t capacity = styles->id_capacity == 0U
            ? 4U : styles->id_capacity * 2U;
        styles->ids = vm_html_resize(
            styles->ids, capacity, sizeof(*styles->ids));
        styles->id_capacity = capacity;
    }
    styles->ids[styles->id_count++] = id;
}

static void html_record_style(Object *root, const char *id,
                              size_t start, size_t end) {
    if (root->as.html.styles == NULL)
        root->as.html.styles = vm_allocate(1U, sizeof(*root->as.html.styles));
    HtmlStyleState *styles = root->as.html.styles;
    if (styles->occurrence_count == styles->occurrence_capacity) {
        size_t capacity = styles->occurrence_capacity == 0U
            ? 4U : styles->occurrence_capacity * 2U;
        styles->occurrences = vm_html_resize(
            styles->occurrences, capacity, sizeof(*styles->occurrences));
        styles->occurrence_capacity = capacity;
    }
    styles->occurrences[styles->occurrence_count++] =
        (HtmlStyleOccurrence){id, start, end};
}

void vm_html_release_style_state(Object *html) {
    HtmlStyleState *styles = html->as.html.styles;
    if (styles == NULL) return;
    free(styles->ids);
    free(styles->occurrences);
    free(styles);
    html->as.html.styles = NULL;
}

static inline void html_reserve(Object *html, size_t extra) {
    if (html->as.html.suppressed) return;
    html = vm_html_destination(html);
    if (extra > SIZE_MAX - html->as.html.length) {
        fputs("fatal: Html value is too large\n", stderr);
        exit(2);
    }
    size_t required = html->as.html.length + extra;
    if (required <= html->as.html.capacity) return;
    size_t capacity = html->as.html.capacity == 0U
                    ? 128U : html->as.html.capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    char *data = realloc(html->as.html.data, capacity);
    if (data == NULL) { fputs("fatal: out of memory\n", stderr); exit(2); }
    html->as.html.data = data;
    html->as.html.capacity = capacity;
}

void vm_html_bytes(Object *html, const char *data, size_t length) {
    if (html->as.html.suppressed) return;
    html_reserve(html, length);
    html = vm_html_destination(html);
    if (length != 0U)
        memcpy(html->as.html.data + html->as.html.length, data, length);
    html->as.html.length += length;
}

void vm_html_cstr(Object *html, const char *text) {
    vm_html_bytes(html, text, strlen(text));
}

void vm_html_escape(Object *html, LangStringView text, bool attribute) {
    size_t start = 0U;
    for (size_t i = 0U; i < text.length; ++i) {
        const char *replacement = NULL;
        switch (text.data[i]) {
            case '&': replacement = "&amp;"; break;
            case '<': replacement = "&lt;"; break;
            case '>': replacement = "&gt;"; break;
            case '"':
                if (attribute) replacement = "&quot;";
                break;
            case '\'':
                if (attribute) replacement = "&#39;";
                break;
            default: break;
        }
        if (replacement == NULL) continue;
        if (i != start)
            vm_html_bytes(html, text.data + start, i - start);
        vm_html_cstr(html, replacement);
        start = i + 1U;
    }
    if (start != text.length)
        vm_html_bytes(html, text.data + start, text.length - start);
}

static bool html_is_raw_text(const Object *html) {
    return (html->as.html.tag_length == 6U &&
            memcmp(html->as.html.tag, "script", 6U) == 0) ||
           (html->as.html.tag_length == 5U &&
            memcmp(html->as.html.tag, "style", 5U) == 0);
}

void vm_html_append_text(Object *html, LangStringView text) {
    if (html_is_raw_text(html))
        vm_html_bytes(html, text.data, text.length);
    else
        vm_html_escape(html, text, false);
}

void vm_html_ensure_open_closed(Object *html) {
    if (html->as.html.suppressed) {
        html->as.html.open = false;
        return;
    }
    if (html->as.html.open) {
        vm_html_cstr(html, ">");
        html->as.html.open = false;
    }
}

size_t vm_format_u64(char *buffer, uint64_t value) {
    size_t length = 0U;
    do {
        buffer[length++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    for (size_t left = 0U, right = length - 1U; left < right;
         ++left, --right) {
        char byte = buffer[left];
        buffer[left] = buffer[right];
        buffer[right] = byte;
    }
    return length;
}

size_t vm_format_i64(char *buffer, int64_t value) {
    if (value >= 0) return vm_format_u64(buffer, (uint64_t)value);
    buffer[0] = '-';
    uint64_t magnitude = (uint64_t)(-(value + 1)) + 1U;
    return 1U + vm_format_u64(buffer + 1U, magnitude);
}

void vm_html_append_formatted_value(
    LangVM *vm, Object *html, LangValue value,
    bool attribute) {
    (void)vm;
    LangStringView string;
    if (lang_value_string_view(&value, &string)) {
        if (!attribute && html_is_raw_text(html))
            vm_html_bytes(html, string.data, string.length);
        else
            vm_html_escape(html, string, attribute);
        return;
    }
    char text[64];
    size_t length = 0U;
    if (value.tag == LANG_VALUE_I64)
        length = vm_format_i64(text, value.as.i64);
    else if (value.tag == LANG_VALUE_U64)
        length = vm_format_u64(text, value.as.u64);
    else if (value.tag == LANG_VALUE_F64) {
        int formatted = snprintf(
            text, sizeof(text), "%g", value.as.f64);
        if (formatted > 0) length = (size_t)formatted;
    }
    else if (value.tag == LANG_VALUE_BOOL) {
        const char *boolean = value.as.boolean ? "true" : "false";
        vm_html_bytes(html, boolean, value.as.boolean ? 4U : 5U);
    }
    if (length != 0U)
        vm_html_bytes(html, text, length);
}

void vm_html_append_formatted(
    LangVM *vm, Object *html, LangValue value,
    bool attribute) {
    vm_html_append_formatted_value(vm, html, value, attribute);
    vm_value_drop_owned(vm, value);
}

bool vm_css_custom_property_atom(LangStringView value) {
    if (value.length == 0U) return false;
    for (size_t i = 0U; i < value.length; ++i) {
        unsigned char byte = (unsigned char)value.data[i];
        if ((byte >= 'a' && byte <= 'z') ||
            (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') ||
            byte == '#' || byte == '_' || byte == '-' ||
            byte == '.' || byte == '%' || byte == '+')
            continue;
        return false;
    }
    return true;
}

void vm_html_set_attribute(LangVM *vm, Object *html,
                               LangStringView name, LangValue value) {
    static const char static_style_prefix[] =
        "data-aster-static-style-";
    if (html->as.html.tag_length == 5U &&
        memcmp(html->as.html.tag, "style", 5U) == 0 &&
        name.length > sizeof(static_style_prefix) - 1U &&
        memcmp(name.data, static_style_prefix,
               sizeof(static_style_prefix) - 1U) == 0) {
        Object *root = vm_html_destination(html);
        html->as.html.static_style_id = name.data;
        if (html_style_registered(root, name.data)) {
            root->as.html.length = html->as.html.start;
            html->as.html.suppressed = true;
            html->as.html.open = false;
        } else {
            html_register_style(root, name.data);
        }
        vm_value_drop_owned(vm, value);
        return;
    }
    Object *object = value.tag == LANG_VALUE_OBJECT
                   ? value.as.object : NULL;
    if (object != NULL &&
        object->kind == OBJECT_STRUCT &&
        strcmp(object->as.structure.metadata,
               "Option::Some") == 0 &&
        object->as.structure.count == 1U) {
        Object *option = object;
        LangValue payload = option->as.structure.fields[0];
        option->as.structure.fields[0] =
            (LangValue){.tag=LANG_VALUE_UNIT};
        vm_object_free(vm, option);
        vm_html_set_attribute(vm, html, name, payload);
        return;
    }
    if (object != NULL &&
        object->kind == OBJECT_STRUCT &&
        strcmp(object->as.structure.metadata,
               "Option::None") == 0) {
        vm_object_free(vm, object);
        return;
    }
    if (value.tag == LANG_VALUE_BOOL) {
        if (value.as.boolean) {
            vm_html_cstr(html, " ");
            vm_html_bytes(html, name.data, name.length);
        }
        vm_value_drop_owned(vm, value);
        return;
    }
    vm_html_cstr(html, " ");
    vm_html_bytes(html, name.data, name.length);
    vm_html_cstr(html, "=\"");
    vm_html_append_formatted(vm, html, value, true);
    vm_html_cstr(html, "\"");
}

static inline bool html_is_void(const Object *html) {
    const char *tag = html->as.html.tag;
    switch (html->as.html.tag_length) {
        case 2U:
            return memcmp(tag, "br", 2U) == 0 ||
                   memcmp(tag, "hr", 2U) == 0;
        case 3U:
            return memcmp(tag, "col", 3U) == 0 ||
                   memcmp(tag, "img", 3U) == 0 ||
                   memcmp(tag, "wbr", 3U) == 0;
        case 4U:
            return memcmp(tag, "area", 4U) == 0 ||
                   memcmp(tag, "base", 4U) == 0 ||
                   memcmp(tag, "link", 4U) == 0 ||
                   memcmp(tag, "meta", 4U) == 0;
        case 5U:
            return memcmp(tag, "embed", 5U) == 0 ||
                   memcmp(tag, "input", 5U) == 0 ||
                   memcmp(tag, "param", 5U) == 0 ||
                   memcmp(tag, "track", 5U) == 0;
        case 6U:
            return memcmp(tag, "source", 6U) == 0;
        default:
            return false;
    }
}

void vm_html_finish(Object *html) {
    if (html->as.html.suppressed) return;
    vm_html_ensure_open_closed(html);
    if (html->as.html.tag_length != 0U &&
        !html_is_void(html)) {
        vm_html_cstr(html, "</");
        vm_html_bytes(
            html, html->as.html.tag,
            html->as.html.tag_length);
        vm_html_cstr(html, ">");
    }
    if (html->as.html.static_style_id != NULL) {
        Object *root = vm_html_destination(html);
        html_record_style(root, html->as.html.static_style_id,
                          html->as.html.start, root->as.html.length);
    }
}

static void html_append_document(Object *html, const Object *child) {
    Object *root = vm_html_destination(html);
    const HtmlStyleState *styles = child->as.html.styles;
    if (styles == NULL || styles->occurrence_count == 0U) {
        vm_html_bytes(html, child->as.html.data, child->as.html.length);
        return;
    }
    size_t cursor = 0U;
    for (size_t i = 0U; i < styles->occurrence_count; ++i) {
        const HtmlStyleOccurrence *occurrence = &styles->occurrences[i];
        if (occurrence->start < cursor || occurrence->end < occurrence->start ||
            occurrence->end > child->as.html.length)
            continue;
        if (occurrence->start != cursor)
            vm_html_bytes(html, child->as.html.data + cursor,
                          occurrence->start - cursor);
        if (!html_style_registered(root, occurrence->id)) {
            size_t output_start = root->as.html.length;
            html_register_style(root, occurrence->id);
            vm_html_bytes(html, child->as.html.data + occurrence->start,
                       occurrence->end - occurrence->start);
            html_record_style(root, occurrence->id, output_start,
                              root->as.html.length);
        }
        cursor = occurrence->end;
    }
    if (child->as.html.length != cursor)
        vm_html_bytes(html, child->as.html.data + cursor,
                      child->as.html.length - cursor);
}

void vm_html_append_value(LangVM *vm, Object *html, LangValue child) {
    if (child.tag == LANG_VALUE_STRING_VIEW) {
        vm_html_append_text(html, child.as.string);
        return;
    }
    if (child.tag == LANG_VALUE_OBJECT &&
        child.as.object != NULL) {
        Object *object = child.as.object;
        if (object->kind == OBJECT_HTML) {
            Object *destination = vm_html_destination(html);
            Object *child_destination =
                vm_html_destination(object);
            if (destination != child_destination)
                html_append_document(html, child_destination);
            vm_object_free(vm, object);
            return;
        }
        if (object->kind == OBJECT_STRING) {
            vm_html_append_text(html, (LangStringView){
                object->as.string.data, object->as.string.length
            });
            vm_object_free(vm, object);
            return;
        }
        if (object->kind == OBJECT_STRUCT &&
            strcmp(object->as.structure.metadata,
                   "Option::Some") == 0 &&
            object->as.structure.count == 1U) {
            LangValue payload = object->as.structure.fields[0];
            object->as.structure.fields[0] =
                (LangValue){.tag=LANG_VALUE_UNIT};
            vm_object_free(vm, object);
            vm_html_append_value(vm, html, payload);
            return;
        }
        if (object->kind == OBJECT_STRUCT &&
            strcmp(object->as.structure.metadata,
                   "Option::None") == 0) {
            vm_object_free(vm, object);
            return;
        }
        if (object->kind == OBJECT_ARRAY) {
            for (size_t i = 0U; i < object->as.array.count; ++i) {
                LangValue item = object->as.array.items[i];
                object->as.array.items[i] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
                vm_html_append_value(vm, html, item);
            }
            vm_object_free(vm, object);
            return;
        }
        if (object->kind == OBJECT_VEC) {
            for (size_t i = 0U; i < object->as.vector.count; ++i) {
                LangValue item = object->as.vector.items[i];
                object->as.vector.items[i] =
                    (LangValue){.tag=LANG_VALUE_UNIT};
                vm_html_append_value(vm, html, item);
            }
            vm_object_free(vm, object);
            return;
        }
    }
    if (child.tag != LANG_VALUE_UNIT) {
        char number[64];
        size_t length;
        if (child.tag == LANG_VALUE_U64)
            length = vm_format_u64(number, child.as.u64);
        else if (child.tag == LANG_VALUE_F64) {
            int formatted = snprintf(
                number, sizeof(number), "%g", child.as.f64);
            length = formatted > 0 ? (size_t)formatted : 0U;
        }
        else
            length = vm_format_i64(number, child.as.i64);
        vm_html_bytes(html, number, length);
    }
    vm_value_drop_owned(vm, child);
}
