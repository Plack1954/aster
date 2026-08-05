#include "c_backend_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct StaticCssBuffer {
    const char **ids;
    const char **texts;
    size_t *lengths;
    size_t count;
    size_t capacity;
} StaticCssBuffer;

static void static_css_add(StaticCssBuffer *css, const IrStaticCss *entry) {
    const char *id = entry->scope_attribute;
    for (size_t i = 0U; i < css->count; ++i)
        if (strcmp(css->ids[i], id) == 0) return;
    if (css->count == css->capacity) {
        size_t capacity = css->capacity == 0U ? 4U : css->capacity * 2U;
        css->ids = realloc(css->ids, capacity * sizeof(*css->ids));
        css->texts = realloc(css->texts, capacity * sizeof(*css->texts));
        css->lengths = realloc(css->lengths, capacity * sizeof(*css->lengths));
        if (css->ids == NULL || css->texts == NULL || css->lengths == NULL) {
            fputs("fatal: out of memory\n", stderr);
            exit(2);
        }
        css->capacity = capacity;
    }
    css->ids[css->count] = id;
    css->texts[css->count] = entry->text;
    css->lengths[css->count++] = entry->text_length;
}

static uint64_t static_css_hash(uint64_t hash, const char *text, size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        hash ^= (unsigned char)text[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

char *c_backend_emit_static_css_asset(CEmitter *emitter,
                                      const char *directory) {
    StaticCssBuffer css = {0};
    for (size_t f = 0U; f < emitter->ir->function_count; ++f) {
        const IrFunction *function = &emitter->ir->functions[f];
        if (!emitter->reachable_functions[f]) continue;
        for (size_t entry = 0U;
             entry < function->static_css_count; ++entry)
            static_css_add(&css, &function->static_css[entry]);
    }
    if (css.count == 0U) {
        free(css.ids); free(css.texts); free(css.lengths);
        return NULL;
    }
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0U; i < css.count; ++i) {
        hash = static_css_hash(hash, css.texts[i], css.lengths[i]);
        hash = static_css_hash(hash, "\n", 1U);
    }
    char basename[64];
    (void)snprintf(basename, sizeof(basename),
                   "site-%016" PRIx64 ".css", hash);
    size_t path_length = strlen(directory) + strlen(basename) + 2U;
    char *path = malloc(path_length);
    if (path == NULL) { fputs("fatal: out of memory\n", stderr); exit(2); }
    (void)snprintf(path, path_length, "%s/%s", directory, basename);
    FILE *asset = fopen(path, "wb");
    if (asset == NULL) {
        lang_diag(emitter->diagnostics, (LangSpan){NULL, 0U, 0U},
                  "cannot write extracted stylesheet `%s`: %s",
                  path, strerror(errno));
        emitter->failed = true;
    } else {
        for (size_t i = 0U; i < css.count; ++i) {
            if (css.lengths[i] != 0U)
                (void)fwrite(css.texts[i], 1U, css.lengths[i], asset);
            fputc('\n', asset);
        }
        if (fclose(asset) != 0) emitter->failed = true;
    }
    free(path);
    free(css.ids); free(css.texts); free(css.lengths);
    size_t href_length = strlen("/assets/") + strlen(basename) + 1U;
    char *href = malloc(href_length);
    if (href == NULL) { fputs("fatal: out of memory\n", stderr); exit(2); }
    (void)snprintf(href, href_length, "/assets/%s", basename);
    return href;
}
