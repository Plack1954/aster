#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

#ifndef ASTER_INSTALL_STDLIB_DIR
#define ASTER_INSTALL_STDLIB_DIR ""
#endif

#ifndef ASTER_BUILD_STDLIB_DIR
#define ASTER_BUILD_STDLIB_DIR ""
#endif

struct LangArenaBlock {
    struct LangArenaBlock *next;
    size_t used;
    size_t capacity;
    unsigned char data[];
};

static void *checked_realloc(void *pointer, size_t size) {
    void *result = realloc(pointer, size);
    if (result == NULL && size != 0U) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    return result;
}

typedef struct ModuleLoadEntry {
    char *path;
    bool loading;
    bool loaded;
} ModuleLoadEntry;

typedef struct ModuleLoader {
    ModuleLoadEntry *entries;
    size_t entry_count;
    size_t entry_capacity;
    char *combined;
    size_t combined_length;
    size_t combined_capacity;
    LangSourceSegment *segments;
    size_t segment_count;
    size_t segment_capacity;
    const char *source_root;  /* Borrowed; NULL selects legacy resolution. */
    const char *const *dependency_roots; /* Borrowed project source roots. */
    size_t dependency_root_count;
    const char *project_root; /* Borrowed; used for the bundled std tree. */
    const char *manifest_stdlib_root; /* Borrowed explicit manifest path. */
    char *stdlib_root; /* Owned lazily resolved standard-library root. */
    bool stdlib_resolution_attempted;
    char error[512];
} ModuleLoader;

static char *configured_stdlib_root;
static char *configured_executable_path;

static char *heap_strndup(const char *text, size_t length);

void lang_set_stdlib_path(const char *path) {
    free(configured_stdlib_root);
    configured_stdlib_root = path != NULL && path[0] != '\0'
        ? heap_strndup(path, strlen(path)) : NULL;
}

void lang_set_executable_path(const char *path) {
    free(configured_executable_path);
    configured_executable_path = path != NULL && path[0] != '\0'
        ? heap_strndup(path, strlen(path)) : NULL;
}

static char *namespace_file_name(const char *name, char separator) {
    size_t length = strlen(name);
    char *path = checked_realloc(NULL, length * 2U + 1U);
    size_t output = 0U;
    bool segment_start = true;
    for (size_t i = 0U; i < length; ++i) {
        if ((name[i] == ':' && i + 1U < length && name[i + 1U] == ':') ||
            name[i] == '.' || name[i] == '/') {
            path[output++] = separator;
            segment_start = true;
            if (name[i] == ':') ++i;
            continue;
        }
        unsigned char c = (unsigned char)name[i];
        if (!segment_start && isupper(c) != 0 && output != 0U &&
            path[output - 1U] != '_')
            path[output++] = '_';
        path[output++] = (char)tolower(c);
        segment_start = false;
    }
    path[output] = '\0';
    return path;
}

static void loader_add_segment(ModuleLoader *loader, size_t start,
                               size_t end, const char *path) {
    if (loader->segment_count == loader->segment_capacity) {
        size_t next = loader->segment_capacity == 0U
                    ? 8U : loader->segment_capacity * 2U;
        loader->segments = checked_realloc(
            loader->segments, next * sizeof(*loader->segments));
        loader->segment_capacity = next;
    }
    loader->segments[loader->segment_count++] =
        (LangSourceSegment){
            start, end, heap_strndup(path, strlen(path))
        };
}

static char *heap_strndup(const char *text, size_t length) {
    char *copy = checked_realloc(NULL, length + 1U);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static void loader_append(ModuleLoader *loader, const char *text, size_t length) {
    size_t needed = loader->combined_length + length + 2U;
    if (needed > loader->combined_capacity) {
        size_t capacity = loader->combined_capacity == 0U ? 4096U
                                                          : loader->combined_capacity;
        while (capacity < needed) capacity *= 2U;
        loader->combined = checked_realloc(loader->combined, capacity);
        loader->combined_capacity = capacity;
    }
    memcpy(loader->combined + loader->combined_length, text, length);
    loader->combined_length += length;
    loader->combined[loader->combined_length++] = '\n';
    loader->combined[loader->combined_length] = '\0';
}

static ModuleLoadEntry *loader_entry(ModuleLoader *loader, const char *path) {
    for (size_t i = 0U; i < loader->entry_count; ++i)
        if (strcmp(loader->entries[i].path, path) == 0) return &loader->entries[i];
    if (loader->entry_count == loader->entry_capacity) {
        size_t next = loader->entry_capacity == 0U ? 8U : loader->entry_capacity * 2U;
        loader->entries = checked_realloc(loader->entries,
                                           next * sizeof(*loader->entries));
        loader->entry_capacity = next;
    }
    ModuleLoadEntry *entry = &loader->entries[loader->entry_count++];
    *entry = (ModuleLoadEntry){heap_strndup(path, strlen(path)), false, false};
    return entry;
}

static char *join_path3(const char *first, const char *second,
                        const char *suffix) {
    size_t first_length = strlen(first);
    bool separator = first_length != 0U && first[first_length - 1U] != '/';
    size_t length = first_length + (separator ? 1U : 0U) +
                    strlen(second) + strlen(suffix) + 1U;
    char *path = checked_realloc(NULL, length);
    (void)snprintf(path, length, "%s%s%s%s", first,
                   separator ? "/" : "", second, suffix);
    return path;
}

static const char *standard_module_file(const char *module_path) {
    static const struct {
        const char *module_path;
        const char *file_name;
    } modules[] = {
        {"system/i_o", "file"},
        {"system/text", "text"},
        {"system", "datetime"},
        {"system/text/json", "json"},
        {"system/collections/generic", "collections"},
        {"aster/html", "html"},
        {"aster/memory", "bytes"},
        {"aster/interop", "process"},
        {"aster/data/sqlite", "sqlite"},
        {"aster/command_line", "cli"},
        {"aster/content", "content"},
        {"aster/testing", "testing"},
        {"aster/core", "core"},
        {"aster/net/http", "http"},
        {"system/net/http", "http_client"},
        {"system/security/cryptography", "cryptography"},
        {"system/diagnostics", "diagnostics_process"},
        {"aster/web/http_app", "http_app"},
        {"aster/web/middleware", "middleware"},
        {"aster/web/router", "router"},
    };
    for (size_t i = 0U; i < sizeof(modules) / sizeof(modules[0]); ++i) {
        if (strcmp(module_path, modules[i].module_path) == 0)
            return modules[i].file_name;
    }
    return NULL;
}

static bool path_is_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    (void)fclose(file);
    return true;
}

static char *path_directory(const char *path) {
    const char *slash = strrchr(path, '/');
#if defined(_WIN32)
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
#endif
    if (slash == NULL) return heap_strndup(".", 1U);
    if (slash == path) return heap_strndup(path, 1U);
    return heap_strndup(path, (size_t)(slash - path));
}

static char *running_executable_path(void) {
#if defined(_WIN32)
    char buffer[32768];
    DWORD length = GetModuleFileNameA(
        NULL, buffer, (DWORD)(sizeof(buffer) / sizeof(buffer[0])));
    if (length != 0U && (size_t)length < sizeof(buffer))
        return heap_strndup(buffer, (size_t)length);
#elif defined(__APPLE__)
    uint32_t capacity = 0U;
    (void)_NSGetExecutablePath(NULL, &capacity);
    if (capacity != 0U) {
        char *buffer = checked_realloc(NULL, (size_t)capacity);
        if (_NSGetExecutablePath(buffer, &capacity) == 0)
            return buffer;
        free(buffer);
    }
#elif defined(__linux__)
    char buffer[4096];
    ssize_t length = readlink(
        "/proc/self/exe", buffer, sizeof(buffer) - 1U);
    if (length > 0) {
        buffer[(size_t)length] = '\0';
        return heap_strndup(buffer, (size_t)length);
    }
#endif
    return configured_executable_path != NULL
        ? heap_strndup(configured_executable_path,
                       strlen(configured_executable_path))
        : NULL;
}

static char *existing_stdlib_root(const char *root) {
    if (root == NULL || root[0] == '\0') return NULL;
    char *core = join_path3(root, "core", ".as");
    bool exists = path_is_file(core);
    free(core);
    return exists ? heap_strndup(root, strlen(root)) : NULL;
}

static char *stdlib_below(const char *root, const char *relative) {
    char *candidate = join_path3(root, relative, "");
    char *resolved = existing_stdlib_root(candidate);
    free(candidate);
    return resolved;
}

static char *resolve_stdlib_root(ModuleLoader *loader) {
    if (loader->stdlib_resolution_attempted)
        return loader->stdlib_root;
    loader->stdlib_resolution_attempted = true;

    /* Explicit process configuration is authoritative, even when invalid. */
    if (configured_stdlib_root != NULL) {
        loader->stdlib_root = heap_strndup(
            configured_stdlib_root, strlen(configured_stdlib_root));
        return loader->stdlib_root;
    }
    const char *environment = getenv("ASTER_STDLIB_PATH");
    if (environment != NULL && environment[0] != '\0') {
        loader->stdlib_root =
            heap_strndup(environment, strlen(environment));
        return loader->stdlib_root;
    }
    if (loader->manifest_stdlib_root != NULL) {
        loader->stdlib_root = heap_strndup(
            loader->manifest_stdlib_root,
            strlen(loader->manifest_stdlib_root));
        return loader->stdlib_root;
    }

    if (loader->project_root != NULL)
        loader->stdlib_root =
            stdlib_below(loader->project_root, "std");

    char *executable = NULL;
    char *executable_directory = NULL;
    if (loader->stdlib_root == NULL) {
        executable = running_executable_path();
        if (executable != NULL)
            executable_directory = path_directory(executable);
    }
    static const char *executable_relatives[] = {
        "std", "../std", "../share/aster/std", "../lib/aster/std"
    };
    for (size_t i = 0U;
         loader->stdlib_root == NULL && executable_directory != NULL &&
         i < sizeof(executable_relatives) /
                 sizeof(executable_relatives[0]); ++i)
        loader->stdlib_root = stdlib_below(
            executable_directory, executable_relatives[i]);
    free(executable_directory);
    free(executable);

    if (loader->stdlib_root == NULL)
        loader->stdlib_root = existing_stdlib_root(
            ASTER_INSTALL_STDLIB_DIR);
    if (loader->stdlib_root == NULL)
        loader->stdlib_root = existing_stdlib_root(
            ASTER_BUILD_STDLIB_DIR);
    if (loader->stdlib_root == NULL)
        loader->stdlib_root = stdlib_below(".", "std");
    return loader->stdlib_root;
}

static char *resolve_import_path(ModuleLoader *loader,
                                 const char *from_path,
                                 const char *module_path,
                                 const char *last) {
    const char *standard_file = standard_module_file(module_path);
    if (standard_file != NULL) {
        /* Public namespaces are stable API. Physical std filenames are an
         * implementation detail and deliberately need not mirror them. */
        char *standard_root = resolve_stdlib_root(loader);
        return standard_root != NULL
            ? join_path3(standard_root, standard_file, ".as")
            : NULL;
    }
    if (loader->source_root != NULL) {
        char *application_path = join_path3(
            loader->source_root, module_path, ".as");
        FILE *application_file = fopen(application_path, "rb");
        if (application_file != NULL) {
            (void)fclose(application_file);
            return application_path;
        }
        for (size_t i = 0U; i < loader->dependency_root_count; ++i) {
            char *dependency_path = join_path3(
                loader->dependency_roots[i], module_path, ".as");
            FILE *dependency_file = fopen(dependency_path, "rb");
            if (dependency_file != NULL) {
                (void)fclose(dependency_file);
                free(application_path);
                return dependency_path;
            }
            free(dependency_path);
        }
        return application_path;
    }
    const char *directory_end = strrchr(from_path, '/');
    size_t directory_length = directory_end != NULL
                            ? (size_t)(directory_end - from_path) : 0U;
    size_t length = directory_length + (directory_length != 0U ? 1U : 0U) +
                    strlen(last) + strlen(".as") + 1U;
    char *path = checked_realloc(NULL, length);
    if (directory_length != 0U)
        (void)snprintf(path, length, "%.*s/%s.as", (int)directory_length,
                       from_path, last);
    else
        (void)snprintf(path, length, "%s.as", last);
    return path;
}

static bool load_module_recursive(ModuleLoader *loader, const char *path,
                                  const char *expected_module) {
    ModuleLoadEntry *entry = loader_entry(loader, path);
    if (entry->loading) {
        (void)snprintf(loader->error, sizeof(loader->error),
                       "cyclic namespace dependency involving `%s`", path);
        return false;
    }
    if (entry->loaded) return true;
    entry->loading = true;
    LangSource source = {0};
    if (!lang_source_load(path, &source)) {
        (void)snprintf(loader->error, sizeof(loader->error),
                       "cannot load used namespace file `%s`", path);
        entry->loading = false;
        return false;
    }
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Lexer lexer;
    lang_lexer_init(&lexer, &source, &diagnostics);
    Token token_value;
    bool found_module = false;
    do {
        token_value = lang_lexer_next(&lexer);
        if (token_value.kind == TOK_NAMESPACE) {
            Token part = lang_lexer_next(&lexer);
            if (part.kind != TOK_IDENT) continue;
            char *declared = heap_strndup(part.start, part.length);
            Token next_token = lang_lexer_next(&lexer);
            while (next_token.kind == TOK_DOT) {
                part = lang_lexer_next(&lexer);
                if (part.kind != TOK_IDENT) break;
                size_t old_length = strlen(declared);
                declared = checked_realloc(
                    declared, old_length + part.length + 3U);
                declared[old_length] = ':';
                declared[old_length + 1U] = ':';
                memcpy(declared + old_length + 2U,
                       part.start, part.length);
                declared[old_length + part.length + 2U] = '\0';
                next_token = lang_lexer_next(&lexer);
            }
            found_module = true;
            if (expected_module != NULL &&
                strcmp(declared, expected_module) != 0) {
                (void)snprintf(
                    loader->error, sizeof(loader->error),
                    "namespace file `%s` declares `%s`, expected `%s`",
                    path, declared, expected_module);
                free(declared);
                lang_diagnostics_free(&diagnostics);
                lang_source_free(&source);
                entry = loader_entry(loader, path);
                entry->loading = false;
                return false;
            }
            free(declared);
            token_value = next_token;
            continue;
        }
        if (token_value.kind != TOK_USING) continue;
        Token part = lang_lexer_next(&lexer);
        if (part.kind != TOK_IDENT) continue;
        Token separator = lang_lexer_next(&lexer);
        if (separator.kind == TOK_EQUAL) {
            part = lang_lexer_next(&lexer);
            if (part.kind != TOK_IDENT) continue;
            separator = lang_lexer_next(&lexer);
            /* Unqualified `using Name = Type;` is a type alias, not a
             * namespace dependency. */
            if (separator.kind != TOK_DOT) {
                token_value = separator;
                continue;
            }
        }
        char *first = heap_strndup(part.start, part.length);
        char *last = heap_strndup(part.start, part.length);
        char *module_path = heap_strndup(part.start, part.length);
        char *module_name = heap_strndup(part.start, part.length);
        while (separator.kind == TOK_DOT) {
            part = lang_lexer_next(&lexer);
            if (part.kind != TOK_IDENT) break;
            free(last);
            last = heap_strndup(part.start, part.length);
            size_t old_length = strlen(module_path);
            module_path = checked_realloc(
                module_path, old_length + part.length + 2U);
            module_path[old_length] = '/';
            memcpy(module_path + old_length + 1U,
                   part.start, part.length);
            module_path[old_length + part.length + 1U] = '\0';
            size_t name_length = strlen(module_name);
            module_name = checked_realloc(
                module_name, name_length + part.length + 3U);
            module_name[name_length] = ':';
            module_name[name_length + 1U] = ':';
            memcpy(module_name + name_length + 2U,
                   part.start, part.length);
            module_name[name_length + part.length + 2U] = '\0';
            separator = lang_lexer_next(&lexer);
        }
        char *file_module_path = namespace_file_name(module_path, '/');
        char *file_last = namespace_file_name(last, '/');
        char *import_path = resolve_import_path(
            loader, path, file_module_path, file_last);
        free(file_module_path);
        free(file_last);
        free(first);
        free(last);
        free(module_path);
        if (import_path == NULL) {
            (void)snprintf(
                loader->error, sizeof(loader->error),
                "cannot locate the Aster standard library; set ASTER_STDLIB_PATH");
            free(module_name);
            lang_diagnostics_free(&diagnostics);
            lang_source_free(&source);
            entry = loader_entry(loader, path);
            entry->loading = false;
            return false;
        }
        bool imported = load_module_recursive(
            loader, import_path,
            loader->source_root != NULL ? module_name : NULL);
        free(import_path);
        free(module_name);
        entry = loader_entry(loader, path);
        if (!imported) {
            lang_diagnostics_free(&diagnostics);
            lang_source_free(&source);
            entry->loading = false;
            return false;
        }
        token_value = separator;
    } while (token_value.kind != TOK_EOF);
    if (expected_module != NULL && !found_module) {
        (void)snprintf(
            loader->error, sizeof(loader->error),
            "namespace file `%s` has no namespace declaration; expected `%s`",
            path, expected_module);
        lang_diagnostics_free(&diagnostics);
        lang_source_free(&source);
        entry = loader_entry(loader, path);
        entry->loading = false;
        return false;
    }
    /*
     * This lexer pass exists only to discover imports. Keep loading when it
     * sees malformed source so the normal combined-source lexer can emit the
     * precise, source-mapped diagnostics instead of a lossy loader error.
     */
    lang_diagnostics_free(&diagnostics);
    entry = loader_entry(loader, path);
    size_t module_index =
        (size_t)(entry - loader->entries);
    char boundary[64];
    int boundary_length = snprintf(
        boundary, sizeof(boundary), "namespace Loaded%zu;\n",
        module_index);
    if (boundary_length < 0 ||
        (size_t)boundary_length >= sizeof(boundary)) {
        (void)snprintf(loader->error, sizeof(loader->error),
                       "too many loaded modules");
        lang_source_free(&source);
        entry->loading = false;
        return false;
    }
    loader_append(loader, boundary, (size_t)boundary_length);
    size_t content_start = loader->combined_length;
    loader_append(loader, source.text, source.length);
    loader_add_segment(loader, content_start,
                       content_start + source.length, path);
    lang_source_free(&source);
    entry->loading = false;
    entry->loaded = true;
    return true;
}

static void loader_free(ModuleLoader *loader) {
    for (size_t i = 0U; i < loader->entry_count; ++i)
        free(loader->entries[i].path);
    free(loader->entries);
    for (size_t i = 0U; i < loader->segment_count; ++i)
        free(loader->segments[i].path);
    free(loader->segments);
    free(loader->combined);
    free(loader->stdlib_root);
}

void lang_arena_init(LangArena *arena) { arena->head = NULL; }

void *lang_arena_alloc(LangArena *arena, size_t size) {
    const size_t alignment = _Alignof(max_align_t);
    size = (size + alignment - 1U) & ~(alignment - 1U);
    if (size == 0U) size = alignment;
    LangArenaBlock *block = arena->head;
    if (block == NULL || block->capacity - block->used < size) {
        size_t capacity = 4096U;
        if (capacity < size) capacity = size;
        block = checked_realloc(NULL, sizeof(*block) + capacity);
        block->next = arena->head;
        block->used = 0U;
        block->capacity = capacity;
        arena->head = block;
    }
    void *result = block->data + block->used;
    block->used += size;
    memset(result, 0, size);
    return result;
}

char *lang_arena_strndup(LangArena *arena, const char *text, size_t length) {
    char *copy = lang_arena_alloc(arena, length + 1U);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

void lang_arena_free(LangArena *arena) {
    LangArenaBlock *block = arena->head;
    while (block != NULL) {
        LangArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    arena->head = NULL;
}

bool lang_source_load(const char *path, LangSource *out_source) {
    memset(out_source, 0, sizeof(*out_source));
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    if (fseek(file, 0L, SEEK_END) != 0) { fclose(file); return false; }
    long length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0) { fclose(file); return false; }
    out_source->text = checked_realloc(NULL, (size_t)length + 1U);
    size_t read = fread(out_source->text, 1U, (size_t)length, file);
    fclose(file);
    if (read != (size_t)length) { free(out_source->text); return false; }
    out_source->text[read] = '\0';
    out_source->length = read;
    size_t path_length = strlen(path);
    out_source->path = checked_realloc(NULL, path_length + 1U);
    memcpy(out_source->path, path, path_length + 1U);
    return true;
}

void lang_source_free(LangSource *source) {
    free(source->text);
    free(source->path);
    for (size_t i = 0U; i < source->segment_count; ++i)
        free(source->segments[i].path);
    free(source->segments);
    memset(source, 0, sizeof(*source));
}

void lang_diagnostics_init(LangDiagnostics *diagnostics) {
    memset(diagnostics, 0, sizeof(*diagnostics));
}

void lang_diagnostics_free(LangDiagnostics *diagnostics) {
    free(diagnostics->items);
    memset(diagnostics, 0, sizeof(*diagnostics));
}

LangDiagnostic *lang_diag(LangDiagnostics *diagnostics, LangSpan span,
                          const char *format, ...) {
    if (diagnostics->count == diagnostics->capacity) {
        size_t next = diagnostics->capacity == 0U ? 8U : diagnostics->capacity * 2U;
        diagnostics->items = checked_realloc(diagnostics->items,
                                              next * sizeof(*diagnostics->items));
        diagnostics->capacity = next;
    }
    LangDiagnostic *item = &diagnostics->items[diagnostics->count++];
    memset(item, 0, sizeof(*item));
    item->severity = LANG_DIAG_ERROR;
    item->span = span;
    va_list args;
    va_start(args, format);
    (void)vsnprintf(item->message, sizeof(item->message), format, args);
    va_end(args);
    return item;
}

void lang_diag_secondary(LangDiagnostic *diagnostic, LangSpan span,
                         const char *label) {
    if (diagnostic == NULL ||
        diagnostic->secondary_count >=
            sizeof(diagnostic->secondary) /
            sizeof(diagnostic->secondary[0]))
        return;
    LangSecondarySpan *secondary =
        &diagnostic->secondary[diagnostic->secondary_count++];
    secondary->span = span;
    (void)snprintf(secondary->label, sizeof(secondary->label),
                   "%s", label != NULL ? label : "");
}

void lang_diag_note(LangDiagnostic *diagnostic, const char *note) {
    if (diagnostic == NULL ||
        diagnostic->note_count >=
            sizeof(diagnostic->notes) /
            sizeof(diagnostic->notes[0]))
        return;
    (void)snprintf(
        diagnostic->notes[diagnostic->note_count++],
        sizeof(diagnostic->notes[0]), "%s",
        note != NULL ? note : "");
}

void lang_diag_help(LangDiagnostic *diagnostic, const char *help) {
    if (diagnostic == NULL) return;
    (void)snprintf(diagnostic->help,
                   sizeof(diagnostic->help), "%s",
                   help != NULL ? help : "");
    diagnostic->has_help = true;
}

const char *lang_source_path_at(const LangSource *source, size_t offset) {
    if (source == NULL) return "?";
    for (size_t i = 0U; i < source->segment_count; ++i)
        if (offset >= source->segments[i].start &&
            offset <= source->segments[i].end)
            return source->segments[i].path;
    return source->path != NULL ? source->path : "?";
}

void lang_source_line_info(const LangSource *source, size_t offset,
                           size_t *line, size_t *column,
                           size_t *line_start, size_t *line_end) {
    size_t origin = 0U;
    for (size_t i = 0U; i < source->segment_count; ++i)
        if (offset >= source->segments[i].start &&
            offset <= source->segments[i].end) {
            origin = source->segments[i].start;
            break;
        }
    *line = 1U; *column = 1U; *line_start = origin;
    if (offset > source->length) offset = source->length;
    for (size_t i = origin; i < offset; ++i) {
        if (source->text[i] == '\n') {
            ++*line; *column = 1U; *line_start = i + 1U;
        } else {
            ++*column;
        }
    }
    *line_end = *line_start;
    while (*line_end < source->length && source->text[*line_end] != '\n') ++*line_end;
}

void lang_diagnostics_print(const LangSource *source,
                            const LangDiagnostics *diagnostics, FILE *stream) {
    for (size_t i = 0U; i < diagnostics->count; ++i) {
        const LangDiagnostic *item = &diagnostics->items[i];
        size_t line, column, begin, end;
        lang_source_line_info(source, item->span.start,
                              &line, &column, &begin, &end);
        const char *severity =
            item->severity == LANG_DIAG_WARNING ? "warning" :
            item->severity == LANG_DIAG_NOTE ? "note" : "error";
        fprintf(stream, "%s: %s\n  --> %s:%zu:%zu\n   |\n%3zu | %.*s\n   | ",
                severity, item->message,
                lang_source_path_at(source, item->span.start),
                line, column, line,
                (int)(end - begin), source->text + begin);
        for (size_t j = 1U; j < column; ++j) fputc(' ', stream);
        size_t width = item->span.end > item->span.start
                     ? item->span.end - item->span.start : 1U;
        for (size_t j = 0U; j < width; ++j) fputc('^', stream);
        fputc('\n', stream);
        for (size_t s = 0U; s < item->secondary_count; ++s) {
            const LangSecondarySpan *secondary =
                &item->secondary[s];
            size_t secondary_line, secondary_column;
            size_t secondary_begin, secondary_end;
            lang_source_line_info(
                source, secondary->span.start,
                &secondary_line, &secondary_column,
                &secondary_begin, &secondary_end);
            fprintf(stream, "  ::: %s:%zu:%zu: %s\n",
                    lang_source_path_at(
                        source, secondary->span.start),
                    secondary_line, secondary_column,
                    secondary->label);
        }
        for (size_t n = 0U; n < item->note_count; ++n)
            fprintf(stream, "  note: %s\n", item->notes[n]);
        if (item->has_help)
            fprintf(stream, "  help: %s\n", item->help);
    }
}

static int process_source(LangSource *source, bool check_only,
                          const char *dump_kind, bool require_entrypoint,
                          bool strict_imports, size_t argument_count,
                          const char *const *arguments,
                          FILE *emit_output,
                          const char *css_directory) {
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    if (dump_kind != NULL && strcmp(dump_kind, "tokens") == 0) {
        lang_dump_tokens(source, &diagnostics);
        if (diagnostics.count != 0U)
            lang_diagnostics_print(source, &diagnostics, stderr);
        int status = diagnostics.count == 0U ? 0 : 1;
        lang_diagnostics_free(&diagnostics);
        return status;
    }
    Module module;
    bool ok = lang_parse_module(source, &diagnostics, &module);
    module.require_entrypoint = require_entrypoint;
    module.strict_imports = strict_imports;
    if (ok && dump_kind != NULL && strcmp(dump_kind, "ast") == 0)
        lang_dump_ast(&module);
    if (ok) ok = lang_check_module(&module, &diagnostics);
    if (ok && dump_kind != NULL && strcmp(dump_kind, "types") == 0)
        lang_dump_types(&module);
    if (ok && dump_kind != NULL && strcmp(dump_kind, "layout") == 0) {
        LangTargetInfo target;
        lang_target_host(&target);
        lang_dump_layout(&module, &target);
    }
    IrModule ir;
    memset(&ir, 0, sizeof(ir));
    bool need_ir = !check_only ||
        (dump_kind != NULL &&
         (strcmp(dump_kind, "ir") == 0 ||
          strcmp(dump_kind, "bytecode") == 0 ||
          strcmp(dump_kind, "ir-bytecode") == 0));
    bool emit_c =
        dump_kind != NULL && strcmp(dump_kind, "c") == 0;
    if (ok && dump_kind != NULL &&
        strcmp(dump_kind, "ir") == 0) {
        LangTargetInfo target;
        lang_target_host(&target);
        ok = lang_ir_lower_module(
            &module, &target, &diagnostics, &ir);
        if (ok) ok = lang_ir_verify_module(&ir, &diagnostics);
        if (ok) lang_ir_dump_module(&ir);
    } else if (ok && (need_ir || emit_c)) {
        LangTargetInfo target;
        lang_target_host(&target);
        ok = lang_ir_lower_module(
            &module, &target, &diagnostics, &ir);
        if (ok) ok = lang_ir_verify_module(&ir, &diagnostics);
    }
    if (ok && emit_c)
        ok = css_directory == NULL
            ? lang_c_emit_module(&ir, &diagnostics, emit_output)
            : lang_c_emit_site(
                &ir, &diagnostics, emit_output, css_directory);
    BytecodeModule bytecode;
    memset(&bytecode, 0, sizeof(bytecode));
    if (ok && (!check_only || (dump_kind != NULL &&
        (strcmp(dump_kind, "bytecode") == 0 ||
         strcmp(dump_kind, "ir-bytecode") == 0))))
        ok = lang_ir_compile_bytecode(&ir, &diagnostics, &bytecode);
    if (ok && dump_kind != NULL && strcmp(dump_kind, "bytecode") == 0)
        lang_dump_bytecode(&bytecode);
    if (ok && dump_kind != NULL &&
        strcmp(dump_kind, "ir-bytecode") == 0)
        lang_dump_bytecode(&bytecode);
    int status = 0;
    if (!ok || diagnostics.count != 0U) {
        lang_diagnostics_print(source, &diagnostics, stderr);
        status = 1;
    } else if (!check_only) {
        LangVM *vm = lang_vm_new();
        lang_vm_register_builtins(vm);
        lang_vm_set_process_arguments(vm, argument_count, arguments);
        status = lang_vm_run_module(vm, &bytecode, source);
        lang_vm_free(vm);
    }
    lang_bytecode_free(&bytecode);
    lang_ir_free_module(&ir);
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    return status;
}

static char *module_name_from_path(const char *source_root,
                                   const char *path) {
    if (source_root == NULL) return NULL;
    size_t root_length = strlen(source_root);
    const char *relative = path;
    if (strncmp(path, source_root, root_length) == 0 &&
        (path[root_length] == '/' || path[root_length] == '\0')) {
        relative = path + root_length;
        if (*relative == '/') ++relative;
    }
    size_t relative_length = strlen(relative);
    if (relative_length > 3U &&
        strcmp(relative + relative_length - 3U, ".as") == 0)
        relative_length -= 3U;
    char *module_name = checked_realloc(
        NULL, relative_length * 2U + 1U);
    size_t output = 0U;
    bool capitalize = true;
    for (size_t i = 0U; i < relative_length; ++i) {
        if (relative[i] == '/') {
            module_name[output++] = ':';
            module_name[output++] = ':';
            capitalize = true;
        } else if (relative[i] == '_') {
            capitalize = true;
        } else {
            unsigned char c = (unsigned char)relative[i];
            module_name[output++] = capitalize
                                  ? (char)toupper(c) : relative[i];
            capitalize = false;
        }
    }
    module_name[output] = '\0';
    return module_name;
}

static bool load_program_source(
    const char *path, const char *source_root,
    const char *const *dependency_roots, size_t dependency_root_count,
    const char *project_root, const char *stdlib_root,
    LangSource *source,
                                char *error, size_t error_capacity) {
    size_t path_length = strlen(path);
    if (path_length < 3U ||
        strcmp(path + path_length - 3U, ".as") != 0) {
        if (error != NULL && error_capacity != 0U)
            (void)snprintf(
                error, error_capacity,
                "Aster source path must end in `.as`");
        return false;
    }
    ModuleLoader loader;
    memset(&loader, 0, sizeof(loader));
    loader.source_root = source_root;
    loader.dependency_roots = dependency_roots;
    loader.dependency_root_count = dependency_root_count;
    loader.project_root = project_root;
    loader.manifest_stdlib_root = stdlib_root;
    char *expected_module = module_name_from_path(source_root, path);
    if (!load_module_recursive(&loader, path, expected_module)) {
        free(expected_module);
        if (error != NULL && error_capacity != 0U)
            (void)snprintf(
                error, error_capacity, "%s",
                loader.error[0] != '\0'
                    ? loader.error : strerror(errno));
        loader_free(&loader);
        return false;
    }
    free(expected_module);
    memset(source, 0, sizeof(*source));
    source->text = loader.combined;
    source->length = loader.combined_length;
    source->path = heap_strndup(path, strlen(path));
    source->segments = loader.segments;
    source->segment_count = loader.segment_count;
    loader.segments = NULL;
    loader.segment_count = 0U;
    loader.segment_capacity = 0U;
    loader.combined = NULL;
    loader_free(&loader);
    return true;
}

int lang_run_file(const char *path, bool check_only, const char *dump_kind) {
    return lang_run_file_args(path, check_only, dump_kind, 0U, NULL);
}

int lang_run_file_args(const char *path, bool check_only,
                       const char *dump_kind, size_t argument_count,
                       const char *const *arguments) {
    if (argument_count != 0U && arguments == NULL) return 1;
    LangSource source;
    char error[512];
    if (!load_program_source(path, NULL, NULL, 0U, NULL, NULL, &source,
                             error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        return 1;
    }
    int status = process_source(
        &source, check_only, dump_kind, true, false,
        argument_count, arguments, stdout, NULL);
    lang_source_free(&source);
    return status;
}

int lang_run_file_with_roots(const char *path, const char *source_root,
                             const char *const *dependency_roots,
                             size_t dependency_root_count,
                             const char *project_root,
                             const char *stdlib_root, bool check_only,
                             const char *dump_kind,
                             bool require_entrypoint) {
    return lang_run_file_with_roots_args(
        path, source_root, dependency_roots, dependency_root_count,
        project_root, stdlib_root, check_only, dump_kind,
        require_entrypoint, 0U, NULL);
}

int lang_run_file_with_roots_args(
    const char *path, const char *source_root,
    const char *const *dependency_roots, size_t dependency_root_count,
    const char *project_root, const char *stdlib_root,
    bool check_only, const char *dump_kind, bool require_entrypoint,
    size_t argument_count, const char *const *arguments) {
    if (argument_count != 0U && arguments == NULL) return 1;
    LangSource source;
    char error[512];
    if (!load_program_source(
            path, source_root, dependency_roots, dependency_root_count,
            project_root, stdlib_root, &source,
                             error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        return 1;
    }
    int status = process_source(
        &source, check_only, dump_kind, require_entrypoint,
        source_root != NULL, argument_count, arguments, stdout, NULL);
    lang_source_free(&source);
    return status;
}

int lang_run_text(const char *name, const char *text, bool check_only,
                  const char *dump_kind) {
    if (name == NULL || text == NULL) return 1;
    LangSource source = {0};
    size_t text_length = strlen(text), name_length = strlen(name);
    source.text = checked_realloc(NULL, text_length + 1U);
    source.path = checked_realloc(NULL, name_length + 1U);
    memcpy(source.text, text, text_length + 1U);
    memcpy(source.path, name, name_length + 1U);
    source.length = text_length;
    int status = process_source(
        &source, check_only, dump_kind, true, false,
        0U, NULL, stdout, NULL);
    lang_source_free(&source);
    return status;
}

int lang_emit_c_site_file(const char *path, const char *css_directory) {
    if (path == NULL || css_directory == NULL) return 1;
    LangSource source;
    char error[512];
    if (!load_program_source(path, NULL, NULL, 0U, NULL, NULL, &source,
                             error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        return 1;
    }
    int status = process_source(
        &source, true, "c", true, false,
        0U, NULL, stdout, css_directory);
    lang_source_free(&source);
    return status;
}

int lang_emit_c_with_roots_to_file(
    const char *path,
    const char *source_root,
    const char *const *dependency_roots,
    size_t dependency_root_count,
    const char *project_root,
    const char *stdlib_root,
    bool require_entrypoint,
    FILE *output
) {
    if (output == NULL) return 1;
    LangSource source;
    char error[512];
    if (!load_program_source(
            path, source_root, dependency_roots, dependency_root_count,
            project_root, stdlib_root, &source,
                             error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        return 1;
    }
    int status = process_source(
        &source, true, "c", require_entrypoint, source_root != NULL,
        0U, NULL, output, NULL);
    lang_source_free(&source);
    return status;
}

int lang_emit_c_site_with_roots(const char *path, const char *source_root,
                                const char *const *dependency_roots,
                                size_t dependency_root_count,
                                const char *project_root,
                                const char *stdlib_root,
                                const char *css_directory,
                                bool require_entrypoint) {
    LangSource source;
    char error[512];
    if (!load_program_source(
            path, source_root, dependency_roots, dependency_root_count,
            project_root, stdlib_root, &source,
                             error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        return 1;
    }
    int status = process_source(
        &source, true, "c", require_entrypoint, source_root != NULL,
        0U, NULL, stdout, css_directory);
    lang_source_free(&source);
    return status;
}

static double elapsed_seconds(clock_t begin) {
    return (double)(clock() - begin) / (double)CLOCKS_PER_SEC;
}

int lang_benchmark_file(const char *path, size_t iterations) {
    LangSource source;
    char error[512];
    if (!load_program_source(path, NULL, NULL, 0U, NULL, NULL, &source,
                             error, sizeof(error)) ||
        iterations == 0U)
        return 1;
    clock_t begin = clock();
    for (size_t i = 0U; i < iterations; ++i) {
        LangDiagnostics diagnostics;
        lang_diagnostics_init(&diagnostics);
        Lexer lexer;
        lang_lexer_init(&lexer, &source, &diagnostics);
        while (lang_lexer_next(&lexer).kind != TOK_EOF) {}
        if (diagnostics.count != 0U) {
            lang_diagnostics_free(&diagnostics);
            lang_source_free(&source);
            return 1;
        }
        lang_diagnostics_free(&diagnostics);
    }
    double lex_time = elapsed_seconds(begin);

    begin = clock();
    for (size_t i = 0U; i < iterations; ++i) {
        LangDiagnostics diagnostics;
        lang_diagnostics_init(&diagnostics);
        Module module;
        bool ok = lang_parse_module(&source, &diagnostics, &module);
        lang_module_free(&module);
        lang_diagnostics_free(&diagnostics);
        if (!ok) { lang_source_free(&source); return 1; }
    }
    double parse_time = elapsed_seconds(begin);

    begin = clock();
    for (size_t i = 0U; i < iterations; ++i) {
        LangDiagnostics diagnostics;
        lang_diagnostics_init(&diagnostics);
        Module module;
        bool ok = lang_parse_module(&source, &diagnostics, &module) &&
                  lang_check_module(&module, &diagnostics);
        lang_module_free(&module);
        lang_diagnostics_free(&diagnostics);
        if (!ok) { lang_source_free(&source); return 1; }
    }
    double check_time = elapsed_seconds(begin);

    begin = clock();
    for (size_t i = 0U; i < iterations; ++i) {
        LangDiagnostics diagnostics;
        lang_diagnostics_init(&diagnostics);
        Module module;
        IrModule ir;
        BytecodeModule bytecode;
        memset(&ir, 0, sizeof(ir));
        memset(&bytecode, 0, sizeof(bytecode));
        LangTargetInfo target;
        lang_target_host(&target);
        bool ok = lang_parse_module(&source, &diagnostics, &module) &&
                  lang_check_module(&module, &diagnostics) &&
                  lang_ir_lower_module(
                      &module, &target, &diagnostics, &ir) &&
                  lang_ir_verify_module(&ir, &diagnostics) &&
                  lang_ir_compile_bytecode(
                      &ir, &diagnostics, &bytecode);
        lang_bytecode_free(&bytecode);
        lang_ir_free_module(&ir);
        lang_module_free(&module);
        lang_diagnostics_free(&diagnostics);
        if (!ok) { lang_source_free(&source); return 1; }
    }
    double compile_time = elapsed_seconds(begin);

    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    IrModule ir;
    BytecodeModule bytecode;
    memset(&ir, 0, sizeof(ir));
    memset(&bytecode, 0, sizeof(bytecode));
    LangTargetInfo target;
    lang_target_host(&target);
    bool ok = lang_parse_module(&source, &diagnostics, &module) &&
              lang_check_module(&module, &diagnostics) &&
              lang_ir_lower_module(
                  &module, &target, &diagnostics, &ir) &&
              lang_ir_verify_module(&ir, &diagnostics) &&
              lang_ir_compile_bytecode(
                  &ir, &diagnostics, &bytecode);
    begin = clock();
    uint64_t vm_instructions = 0U;
    if (ok) {
        for (size_t i = 0U; i < iterations; ++i) {
            LangVM *vm = lang_vm_new();
            lang_vm_register_builtins(vm);
            if (lang_vm_run_module(
                    vm, &bytecode, &source) != 0)
                ok = false;
            vm_instructions =
                lang_vm_instruction_count(vm);
            lang_vm_free(vm);
            if (!ok) break;
        }
    }
    double vm_time = elapsed_seconds(begin);
    lang_bytecode_free(&bytecode);
    lang_ir_free_module(&ir);
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    if (!ok) return 1;

    printf("benchmark %s (%zu iterations)\n", path, iterations);
    printf("  lexer:    %.6f s, %.0f files/s\n", lex_time,
           lex_time > 0.0 ? (double)iterations / lex_time : 0.0);
    printf("  parser:   %.6f s, %.0f files/s\n", parse_time,
           parse_time > 0.0 ? (double)iterations / parse_time : 0.0);
    printf("  checker:  %.6f s, %.0f files/s (includes parsing)\n", check_time,
           check_time > 0.0 ? (double)iterations / check_time : 0.0);
    printf("  compiler: %.6f s, %.0f files/s (typed IR and bytecode)\n",
           compile_time,
           compile_time > 0.0
               ? (double)iterations / compile_time : 0.0);
    printf("  VM:       %.6f s, %.0f runs/s\n",
           vm_time,
           vm_time > 0.0 ? (double)iterations / vm_time : 0.0);
    printf("  instructions/run: %" PRIu64 "\n", vm_instructions);
    return 0;
}

uint64_t lang_dom_part_id(
    const char *module_name, const char *type_name, size_t field_index) {
    uint64_t hash = UINT64_C(14695981039346656037);
    const char *segments[] = {
        module_name == NULL ? "" : module_name,
        "::",
        type_name == NULL ? "" : type_name
    };
    for (size_t segment = 0U; segment < 3U; ++segment)
        for (const unsigned char *byte =
                 (const unsigned char *)segments[segment];
             *byte != 0U; ++byte) {
            hash ^= *byte;
            hash *= UINT64_C(1099511628211);
        }
    uint64_t index = (uint64_t)field_index;
    for (size_t byte = 0U; byte < sizeof(index); ++byte) {
        hash ^= (unsigned char)(index >> (byte * 8U));
        hash *= UINT64_C(1099511628211);
    }
    return hash == 0U ? 1U : hash;
}

size_t lang_dom_part_format(uint64_t part_id, char output[14]) {
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char reversed[13];
    size_t length = 0U;
    do {
        reversed[length++] = digits[part_id % 36U];
        part_id /= 36U;
    } while (part_id != 0U);
    for (size_t index = 0U; index < length; ++index)
        output[index] = reversed[length - index - 1U];
    output[length] = '\0';
    return length;
}
