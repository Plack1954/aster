#include "lang/lang.h"

#include <stddef.h>

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size) {
    (void)count;
    (void)size;
    return NULL;
}

int main(void) {
    LangVM *vm = lang_vm_new();
    if (vm != NULL) {
        lang_vm_free(vm);
        return 1;
    }
    return 0;
}
