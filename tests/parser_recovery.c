#include "internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

int main(void) {
    LangSource source;
    if (!lang_source_load("tests/parser/recovery.as", &source))
        return 1;
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool parsed = lang_parse_module(&source, &diagnostics, &module);
    bool first = false;
    bool second = false;
    for (size_t i = 0U; i < diagnostics.count; ++i) {
        size_t line = 0U;
        size_t column = 0U;
        size_t line_start = 0U;
        size_t line_end = 0U;
        lang_source_line_info(
            &source, diagnostics.items[i].span.start, &line, &column,
            &line_start, &line_end);
        (void)column;
        (void)line_start;
        (void)line_end;
        if (line == 2U) first = true;
        if (line == 7U) second = true;
    }
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    return !parsed && first && second ? 0 : 2;
}
