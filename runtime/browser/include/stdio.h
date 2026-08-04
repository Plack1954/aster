#ifndef ASTER_WASM_STDIO_H
#define ASTER_WASM_STDIO_H

#include <stddef.h>

typedef struct aster_wasm_file FILE;
extern FILE *stdout;
extern FILE *stderr;

int fputc(int byte, FILE *stream);
int fflush(FILE *stream);
int fputs(const char *text, FILE *stream);
size_t fwrite(const void *data, size_t size, size_t count, FILE *stream);
int snprintf(char *buffer, size_t size, const char *format, ...);

#endif
