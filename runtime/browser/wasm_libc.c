#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct aster_wasm_file {
    int descriptor;
};

static FILE aster_wasm_stdout = {1};
static FILE aster_wasm_stderr = {2};
FILE *stdout = &aster_wasm_stdout;
FILE *stderr = &aster_wasm_stderr;

typedef struct aster_wasm_block {
    size_t size;
    struct aster_wasm_block *next;
} aster_wasm_block;

extern unsigned char __heap_base;
static uintptr_t aster_wasm_heap_cursor;
static aster_wasm_block *aster_wasm_free_list;

static void aster_wasm_free_block(aster_wasm_block *block) {
    aster_wasm_block **link = &aster_wasm_free_list;
    aster_wasm_block *previous = NULL;
    while (*link != NULL && (uintptr_t)*link < (uintptr_t)block) {
        previous = *link;
        link = &(*link)->next;
    }
    block->next = *link;
    *link = block;
    if (block->next != NULL &&
        (unsigned char *)(block + 1) + block->size ==
            (unsigned char *)block->next) {
        block->size += sizeof(*block) + block->next->size;
        block->next = block->next->next;
    }
    if (previous != NULL &&
        (unsigned char *)(previous + 1) + previous->size ==
            (unsigned char *)block) {
        previous->size += sizeof(*block) + block->size;
        previous->next = block->next;
        block = previous;
    }
    for (;;) {
        uintptr_t end = (uintptr_t)(block + 1) + block->size;
        if (end != aster_wasm_heap_cursor) break;
        aster_wasm_heap_cursor = (uintptr_t)block;
        aster_wasm_block **remove = &aster_wasm_free_list;
        while (*remove != NULL && *remove != block)
            remove = &(*remove)->next;
        if (*remove == block) *remove = block->next;
        block = NULL;
        for (aster_wasm_block *candidate = aster_wasm_free_list;
             candidate != NULL; candidate = candidate->next)
            if ((uintptr_t)(candidate + 1) + candidate->size ==
                aster_wasm_heap_cursor) {
                block = candidate;
                break;
            }
        if (block == NULL) break;
    }
}

static size_t aster_wasm_align(size_t size) {
    const size_t alignment = 8U;
    if (size > SIZE_MAX - (alignment - 1U)) return 0U;
    return (size + alignment - 1U) & ~(alignment - 1U);
}

static int aster_wasm_reserve(uintptr_t end) {
    const size_t page_size = 65536U;
    size_t pages = __builtin_wasm_memory_size(0);
    if (end <= pages * page_size) return 1;
    size_t additional =
        ((size_t)end - pages * page_size + page_size - 1U) / page_size;
    return __builtin_wasm_memory_grow(0, additional) != (size_t)-1;
}

void *malloc(size_t size) {
    size = aster_wasm_align(size == 0U ? 1U : size);
    if (size == 0U) return NULL;
    aster_wasm_block **link = &aster_wasm_free_list;
    while (*link != NULL) {
        aster_wasm_block *block = *link;
        if (block->size >= size) {
            *link = block->next;
            size_t remainder = block->size - size;
            if (remainder >= sizeof(*block) + 8U) {
                aster_wasm_block *split = (aster_wasm_block *)(
                    (unsigned char *)(block + 1) + size);
                split->size = remainder - sizeof(*split);
                split->next = NULL;
                aster_wasm_free_block(split);
                block->size = size;
            }
            return block + 1;
        }
        link = &(*link)->next;
    }
    if (aster_wasm_heap_cursor == 0U)
        aster_wasm_heap_cursor = aster_wasm_align(
            (size_t)(uintptr_t)&__heap_base);
    if (aster_wasm_heap_cursor > UINTPTR_MAX - sizeof(aster_wasm_block) - size)
        return NULL;
    uintptr_t end = aster_wasm_heap_cursor + sizeof(aster_wasm_block) + size;
    if (!aster_wasm_reserve(end)) return NULL;
    aster_wasm_block *block =
        (aster_wasm_block *)(uintptr_t)aster_wasm_heap_cursor;
    block->size = size;
    block->next = NULL;
    aster_wasm_heap_cursor = end;
    return block + 1;
}

void free(void *pointer) {
    if (pointer == NULL) return;
    aster_wasm_block *block = (aster_wasm_block *)pointer - 1;
    aster_wasm_free_block(block);
}

void *calloc(size_t count, size_t size) {
    if (size != 0U && count > SIZE_MAX / size) return NULL;
    size_t total = count * size;
    void *result = malloc(total);
    if (result != NULL) memset(result, 0, total);
    return result;
}

void *realloc(void *pointer, size_t size) {
    if (pointer == NULL) return malloc(size);
    if (size == 0U) {
        free(pointer);
        return NULL;
    }
    aster_wasm_block *block = (aster_wasm_block *)pointer - 1;
    if (block->size >= size) return pointer;
    void *replacement = malloc(size);
    if (replacement == NULL) return NULL;
    memcpy(replacement, pointer, block->size);
    free(pointer);
    return replacement;
}

void *memcpy(void *destination, const void *source, size_t count) {
    unsigned char *output = destination;
    const unsigned char *input = source;
    for (size_t index = 0U; index < count; ++index)
        output[index] = input[index];
    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    unsigned char *output = destination;
    const unsigned char *input = source;
    if (output < input) {
        for (size_t index = 0U; index < count; ++index)
            output[index] = input[index];
    } else if (output > input) {
        for (size_t index = count; index > 0U; --index)
            output[index - 1U] = input[index - 1U];
    }
    return destination;
}

void *memset(void *destination, int value, size_t count) {
    unsigned char *output = destination;
    for (size_t index = 0U; index < count; ++index)
        output[index] = (unsigned char)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t count) {
    const unsigned char *a = left;
    const unsigned char *b = right;
    for (size_t index = 0U; index < count; ++index) {
        if (a[index] < b[index]) return -1;
        if (a[index] > b[index]) return 1;
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    return length;
}

__attribute__((import_module("aster"), import_name("trap")))
void aster_host_trap(const char *message, size_t length);

static size_t aster_wasm_strlen(const char *text) {
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    return length;
}

int fputs(const char *text, FILE *stream) {
    (void)stream;
    aster_host_trap(text, aster_wasm_strlen(text));
    return 0;
}

int fputc(int byte, FILE *stream) {
    (void)byte;
    (void)stream;
    return byte;
}

_Noreturn void exit(int status) {
    (void)status;
    __builtin_unreachable();
}
