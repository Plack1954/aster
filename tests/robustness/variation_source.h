#ifndef ASTER_VARIATION_SOURCE_H
#define ASTER_VARIATION_SOURCE_H

#include "internal.h"
#include "variation_entry.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ASTER_VARIATION_MAX_SOURCE_SIZE (256U * 1024U)

static inline bool aster_variation_source_init(
    const uint8_t *data, size_t size, LangSource *source) {
    if (size > ASTER_VARIATION_MAX_SOURCE_SIZE) return false;
    memset(source, 0, sizeof(*source));
    source->text = malloc(size + 1U);
    source->path = malloc(sizeof("<variation>"));
    if (source->text == NULL || source->path == NULL) {
        free(source->text);
        free(source->path);
        memset(source, 0, sizeof(*source));
        return false;
    }
    if (size != 0U) memcpy(source->text, data, size);
    source->text[size] = '\0';
    memcpy(source->path, "<variation>", sizeof("<variation>"));
    source->length = size;
    return true;
}

#endif
