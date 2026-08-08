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
    bool ok = lang_parse_module(&source, &diagnostics, &module);
    if (ok) ok = lang_check_module(&module, &diagnostics);
    IrModule ir = {0};
    if (ok) {
        LangTargetInfo target;
        lang_target_host(&target);
        ok = lang_ir_lower_module(&module, &target, &diagnostics, &ir);
    }
    if (ok) (void)lang_ir_verify_module(&ir, &diagnostics);
    lang_ir_free_module(&ir);
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    return 0;
}
