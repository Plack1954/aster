#include "variation_source.h"

#include <stddef.h>
#include <stdint.h>

int ASTER_VARIATION_ENTRY(const uint8_t *data, size_t size);

int ASTER_VARIATION_ENTRY(const uint8_t *data, size_t size) {
    LangSource source;
    if (!aster_variation_source_init(data, size, &source)) return 0;
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    (void)lang_parse_module(&source, &diagnostics, &module);
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    return 0;
}
