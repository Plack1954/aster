#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif
#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef ASTER_BROWSER_RUNTIME_DIR
#define ASTER_BROWSER_RUNTIME_DIR "runtime/browser"
#endif

typedef enum ProjectOutputType {
    PROJECT_OUTPUT_EXE,
    PROJECT_OUTPUT_LIBRARY,
    PROJECT_OUTPUT_TEST,
    PROJECT_OUTPUT_WEB
} ProjectOutputType;

typedef struct ProjectReference {
    char *name;
    char *path;
} ProjectReference;

typedef struct Project {
    char *name;
    char *root;
    char *source_root;
    char *stdlib_root;
    char *entry;
    char *browser_entry;
    ProjectOutputType output_type;
    bool has_output_type;
    ProjectReference *references;
    size_t reference_count;
    size_t reference_capacity;
    const char **dependency_roots;
    size_t dependency_count;
} Project;

static void *project_resize(void *pointer, size_t size) {
    void *result = realloc(pointer, size);
    if (result == NULL && size != 0U) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    return result;
}

static char *project_strndup(const char *text, size_t length) {
    char *copy = project_resize(NULL, length + 1U);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static char *trim_left(char *text) {
    while (isspace((unsigned char)*text) != 0) ++text;
    return text;
}

static void trim_right(char *text) {
    size_t length = strlen(text);
    while (length != 0U &&
           isspace((unsigned char)text[length - 1U]) != 0)
        text[--length] = '\0';
}

static char *directory_of(const char *path) {
    const char *slash = strrchr(path, '/');
#if defined(_WIN32)
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
#endif
    if (slash == NULL) return project_strndup(".", 1U);
    if (slash == path) return project_strndup("/", 1U);
    return project_strndup(path, (size_t)(slash - path));
}

static char *join_path(const char *root, const char *relative) {
    if (relative[0] == '/')
        return project_strndup(relative, strlen(relative));
    size_t root_length = strlen(root);
    bool separator = root_length != 0U && root[root_length - 1U] != '/';
    size_t length = root_length + (separator ? 1U : 0U) +
                    strlen(relative) + 1U;
    char *path = project_resize(NULL, length);
    (void)snprintf(path, length, "%s%s%s", root,
                   separator ? "/" : "", relative);
    return path;
}

static void project_free(Project *project) {
    free(project->name);
    free(project->root);
    free(project->source_root);
    free(project->stdlib_root);
    free(project->entry);
    free(project->browser_entry);
    for (size_t i = 0U; i < project->reference_count; ++i) {
        free(project->references[i].name);
        free(project->references[i].path);
    }
    free(project->references);
    for (size_t i = 0U; i < project->dependency_count; ++i)
        free((char *)project->dependency_roots[i]);
    free(project->dependency_roots);
    memset(project, 0, sizeof(*project));
}

static bool valid_project_name(const char *name) {
    if (name == NULL || *name == '\0') return false;
    bool segment_has_character = false;
    for (const unsigned char *byte = (const unsigned char *)name;
         *byte != 0U; ++byte) {
        if (*byte == '.') {
            if (!segment_has_character) return false;
            segment_has_character = false;
        } else if (isalnum(*byte) != 0 || *byte == '_' || *byte == '-') {
            segment_has_character = true;
        } else {
            return false;
        }
    }
    return segment_has_character;
}

static bool has_suffix(const char *value, const char *suffix) {
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    return value_length >= suffix_length &&
           strcmp(value + value_length - suffix_length, suffix) == 0;
}

static bool add_reference(Project *project, const char *name, char *path,
                          const char *manifest, size_t line) {
    if (!valid_project_name(name)) {
        fprintf(stderr,
                "error: invalid project reference name `%s`\n"
                "  --> %s:%zu\n",
                name, manifest, line);
        free(path);
        return false;
    }
    if (!has_suffix(path, ".asproj")) {
        fprintf(stderr,
                "error: project reference `%s` must name an `.asproj` file\n"
                "  --> %s:%zu\n", name, manifest, line);
        free(path);
        return false;
    }
    for (size_t i = 0U; i < project->reference_count; ++i)
        if (strcmp(project->references[i].name, name) == 0) {
            fprintf(stderr,
                    "error: duplicate project reference `%s`\n"
                    "  --> %s:%zu\n",
                    name, manifest, line);
            free(path);
            return false;
        }
    if (project->reference_count == project->reference_capacity) {
        size_t capacity = project->reference_capacity == 0U
                        ? 4U : project->reference_capacity * 2U;
        project->references = project_resize(
            project->references,
            capacity * sizeof(*project->references));
        project->reference_capacity = capacity;
    }
    ProjectReference *reference =
        &project->references[project->reference_count++];
    *reference = (ProjectReference){
        .name=project_strndup(name, strlen(name)), .path=path
    };
    return true;
}

static char *parse_string_value(char *value, const char *path, size_t line) {
    value = trim_left(value);
    trim_right(value);
    size_t length = strlen(value);
    if (length < 2U || value[0] != '"' || value[length - 1U] != '"') {
        fprintf(stderr,
                "error: manifest values must be quoted strings\n"
                "  --> %s:%zu\n", path, line);
        return NULL;
    }
    for (size_t i = 1U; i + 1U < length; ++i)
        if (value[i] == '"' || value[i] == '\n' || value[i] == '\r') {
            fprintf(stderr,
                    "error: unsupported character in manifest string\n"
                    "  --> %s:%zu\n", path, line);
            return NULL;
        }
    return project_strndup(value + 1U, length - 2U);
}

static bool assign_owned(char **slot, char *value, const char *key,
                         const char *path, size_t line) {
    if (*slot != NULL) {
        fprintf(stderr,
                "error: duplicate manifest key `%s`\n  --> %s:%zu\n",
                key, path, line);
        free(value);
        return false;
    }
    *slot = value;
    return true;
}

static bool parse_output_type(Project *project, char *value,
                              const char *path, size_t line) {
    if (project->has_output_type) {
        fprintf(stderr,
                "error: duplicate manifest key `output_type`\n"
                "  --> %s:%zu\n",
                path, line);
        free(value);
        return false;
    }
    if (strcmp(value, "exe") == 0)
        project->output_type = PROJECT_OUTPUT_EXE;
    else if (strcmp(value, "library") == 0)
        project->output_type = PROJECT_OUTPUT_LIBRARY;
    else if (strcmp(value, "test") == 0)
        project->output_type = PROJECT_OUTPUT_TEST;
    else if (strcmp(value, "web") == 0)
        project->output_type = PROJECT_OUTPUT_WEB;
    else {
        fprintf(stderr,
                "error: output_type must be `exe`, `library`, `test`, "
                "or `web`\n"
                "  --> %s:%zu\n", path, line);
        free(value);
        return false;
    }
    project->has_output_type = true;
    free(value);
    return true;
}

static bool valid_module_name(const char *name) {
    if (name == NULL || *name == '\0') return false;
    bool need_identifier = true;
    for (size_t i = 0U; name[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (need_identifier) {
            if (isalpha(c) == 0 && c != '_') return false;
            need_identifier = false;
        } else if (isalnum(c) != 0 || c == '_') {
            continue;
        } else if (c == '.') {
            need_identifier = true;
        } else {
            return false;
        }
    }
    return !need_identifier;
}

static bool parse_project_file(const char *path, Project *project) {
    memset(project, 0, sizeof(*project));
    if (!has_suffix(path, ".asproj")) {
        fprintf(stderr, "error: project file must use `.asproj`: `%s`\n",
                path);
        return false;
    }
    project->root = directory_of(path);
    LangSource source = {0};
    if (!lang_source_load(path, &source)) {
        fprintf(stderr, "error: cannot load project `%s`\n", path);
        project_free(project);
        return false;
    }
    bool references_section = false;
    char *cursor = source.text;
    size_t line_number = 0U;
    bool ok = true;
    while (*cursor != '\0' && ok) {
        ++line_number;
        char *line = cursor;
        char *newline = strchr(cursor, '\n');
        if (newline != NULL) {
            *newline = '\0';
            cursor = newline + 1U;
        } else {
            cursor += strlen(cursor);
        }
        bool quoted = false;
        for (char *comment = line; *comment != '\0'; ++comment) {
            if (*comment == '"') quoted = !quoted;
            if (*comment == '#' && !quoted) {
                *comment = '\0';
                break;
            }
        }
        line = trim_left(line);
        trim_right(line);
        if (*line == '\0') continue;
        size_t length = strlen(line);
        if (line[0] == '[') {
            if (strcmp(line, "[project_references]") == 0) {
                references_section = true;
                continue;
            }
            (void)length;
            fprintf(stderr,
                    "error: expected `[project_references]`\n"
                    "  --> %s:%zu\n",
                    path, line_number);
            ok = false;
            break;
        }
        char *equals = strchr(line, '=');
        if (equals == NULL) {
            fprintf(stderr,
                    "error: expected `key = \"value\"`\n  --> %s:%zu\n",
                    path, line_number);
            ok = false;
            break;
        }
        *equals = '\0';
        char *key = trim_left(line);
        trim_right(key);
        char *value = parse_string_value(
            equals + 1U, path, line_number);
        if (value == NULL) {
            ok = false;
            break;
        }
        if (references_section) {
            char *quoted_key = NULL;
            size_t key_length = strlen(key);
            if (key_length >= 2U && key[0] == '"' &&
                key[key_length - 1U] == '"') {
                quoted_key = project_strndup(
                    key + 1U, key_length - 2U);
                key = quoted_key;
            } else if (strchr(key, '.') != NULL) {
                fprintf(stderr,
                        "error: dotted project reference names must be "
                        "quoted TOML keys\n  --> %s:%zu\n",
                        path, line_number);
                free(value);
                ok = false;
                continue;
            }
            ok = add_reference(project, key, value, path, line_number);
            free(quoted_key);
        } else {
            if (strcmp(key, "name") == 0)
                ok = assign_owned(
                    &project->name, value, key, path, line_number);
            else if (strcmp(key, "output_type") == 0)
                ok = parse_output_type(
                    project, value, path, line_number);
            else if (strcmp(key, "source_root") == 0)
                ok = assign_owned(
                    &project->source_root, value, key, path, line_number);
            else if (strcmp(key, "entry") == 0)
                ok = assign_owned(
                    &project->entry, value, key, path, line_number);
            else if (strcmp(key, "browser_entry") == 0)
                ok = assign_owned(
                    &project->browser_entry, value, key, path, line_number);
            else if (strcmp(key, "stdlib") == 0)
                ok = assign_owned(
                    &project->stdlib_root, value, key, path, line_number);
            else {
                fprintf(stderr,
                        "error: unknown project key `%s`\n  --> %s:%zu\n",
                        key, path, line_number);
                free(value);
                ok = false;
            }
        }
    }
    lang_source_free(&source);
    if (!ok) {
        project_free(project);
        return false;
    }
    if (project->name == NULL || !valid_project_name(project->name) ||
        project->source_root == NULL || project->entry == NULL ||
        !project->has_output_type) {
        fprintf(stderr,
                "error: project requires valid `name`, `output_type`, "
                "`source_root`, and `entry`\n");
        project_free(project);
        return false;
    }
    if (!valid_module_name(project->entry)) {
        fprintf(stderr,
                "error: project `entry` must be a valid namespace\n");
        project_free(project);
        return false;
    }
    if (project->output_type == PROJECT_OUTPUT_WEB) {
        if (!valid_module_name(project->browser_entry)) {
            fputs("error: web project requires a valid `browser_entry`\n",
                  stderr);
            project_free(project);
            return false;
        }
    } else if (project->browser_entry != NULL) {
        fputs("error: `browser_entry` is valid only for a web project\n",
              stderr);
        project_free(project);
        return false;
    }
    char *full_source_root = join_path(
        project->root, project->source_root);
    free(project->source_root);
    project->source_root = full_source_root;
    if (project->stdlib_root != NULL) {
        char *full_stdlib_root = join_path(
            project->root, project->stdlib_root);
        free(project->stdlib_root);
        project->stdlib_root = full_stdlib_root;
    }
    return true;
}

typedef enum ProjectVisitState {
    PROJECT_VISITING,
    PROJECT_VISITED
} ProjectVisitState;

typedef struct ProjectGraphNode {
    char *name;
    char *path;
    ProjectVisitState state;
} ProjectGraphNode;

typedef struct ProjectGraph {
    ProjectGraphNode *nodes;
    size_t count;
    size_t capacity;
} ProjectGraph;

static void project_graph_free(ProjectGraph *graph) {
    for (size_t i = 0U; i < graph->count; ++i) {
        free(graph->nodes[i].name);
        free(graph->nodes[i].path);
    }
    free(graph->nodes);
    memset(graph, 0, sizeof(*graph));
}

static char *canonical_project_path(const char *path) {
#if defined(_WIN32)
    char *resolved = _fullpath(NULL, path, 0U);
#else
    char *resolved = realpath(path, NULL);
#endif
    return resolved != NULL
         ? resolved : project_strndup(path, strlen(path));
}

static ProjectGraphNode *project_graph_find_path(
    ProjectGraph *graph, const char *path) {
    for (size_t i = 0U; i < graph->count; ++i)
        if (strcmp(graph->nodes[i].path, path) == 0)
            return &graph->nodes[i];
    return NULL;
}

static ProjectGraphNode *project_graph_find_name(
    ProjectGraph *graph, const char *name) {
    for (size_t i = 0U; i < graph->count; ++i)
        if (strcmp(graph->nodes[i].name, name) == 0)
            return &graph->nodes[i];
    return NULL;
}

static ProjectGraphNode *project_graph_add(
    ProjectGraph *graph, const char *name, char *path) {
    if (graph->count == graph->capacity) {
        size_t capacity = graph->capacity == 0U
                        ? 8U : graph->capacity * 2U;
        graph->nodes = project_resize(
            graph->nodes, capacity * sizeof(*graph->nodes));
        graph->capacity = capacity;
    }
    ProjectGraphNode *node = &graph->nodes[graph->count++];
    *node = (ProjectGraphNode){
        .name=project_strndup(name, strlen(name)),
        .path=path,
        .state=PROJECT_VISITING
    };
    return node;
}

static void project_add_dependency_root(Project *root, const char *path) {
    root->dependency_roots = project_resize(
        root->dependency_roots,
        (root->dependency_count + 1U) *
            sizeof(*root->dependency_roots));
    root->dependency_roots[root->dependency_count++] =
        project_strndup(path, strlen(path));
}

static bool resolve_project_references(
    Project *owner, Project *root, ProjectGraph *graph);

static bool resolve_project_reference(
    const Project *owner, const ProjectReference *reference,
    Project *root, ProjectGraph *graph) {
    char *referenced_path = join_path(owner->root, reference->path);
    Project referenced;
    if (!parse_project_file(referenced_path, &referenced)) {
        fprintf(stderr,
                "error: cannot load project reference `%s` from `%s`\n",
                reference->name, referenced_path);
        free(referenced_path);
        return false;
    }
    if (strcmp(reference->name, referenced.name) != 0) {
        fprintf(stderr,
                "error: project reference `%s` points to project `%s`\n",
                reference->name, referenced.name);
        project_free(&referenced);
        free(referenced_path);
        return false;
    }
    if (referenced.output_type != PROJECT_OUTPUT_LIBRARY) {
        fprintf(stderr,
                "error: project reference `%s` must reference a library\n",
                reference->name);
        project_free(&referenced);
        free(referenced_path);
        return false;
    }
    char *canonical = canonical_project_path(referenced_path);
    free(referenced_path);
    ProjectGraphNode *path_node = project_graph_find_path(graph, canonical);
    if (path_node != NULL) {
        if (path_node->state == PROJECT_VISITING) {
            fprintf(stderr,
                    "error: project reference cycle reaches `%s`\n",
                    referenced.name);
            free(canonical);
            project_free(&referenced);
            return false;
        }
        free(canonical);
        project_free(&referenced);
        return true;
    }
    ProjectGraphNode *name_node = project_graph_find_name(
        graph, referenced.name);
    if (name_node != NULL) {
        fprintf(stderr,
                "error: duplicate project identity `%s` at `%s` and `%s`\n",
                referenced.name, name_node->path, canonical);
        free(canonical);
        project_free(&referenced);
        return false;
    }
    ProjectGraphNode *node = project_graph_add(
        graph, referenced.name, canonical);
    project_add_dependency_root(root, referenced.source_root);
    bool ok = resolve_project_references(
        &referenced, root, graph);
    if (ok) node->state = PROJECT_VISITED;
    project_free(&referenced);
    return ok;
}

static bool resolve_project_references(
    Project *owner, Project *root, ProjectGraph *graph) {
    for (size_t i = 0U; i < owner->reference_count; ++i)
        if (!resolve_project_reference(
                owner, &owner->references[i], root, graph))
            return false;
    return true;
}

static bool parse_project(const char *path, Project *project) {
    if (!parse_project_file(path, project)) return false;
    ProjectGraph graph = {0};
    char *canonical = canonical_project_path(path);
    ProjectGraphNode *root_node = project_graph_add(
        &graph, project->name, canonical);
    bool ok = resolve_project_references(
        project, project, &graph);
    if (ok) root_node->state = PROJECT_VISITED;
    project_graph_free(&graph);
    if (!ok) project_free(project);
    return ok;
}

static char *module_file_path(const Project *project,
                              const char *module_name) {
    size_t length = strlen(module_name);
    char *relative = project_resize(NULL, length * 2U + 4U);
    size_t output = 0U;
    bool segment_start = true;
    for (size_t i = 0U; i < length; ++i) {
        if (module_name[i] == '.') {
            relative[output++] = '/';
            segment_start = true;
        } else {
            unsigned char c = (unsigned char)module_name[i];
            if (!segment_start && isupper(c) != 0)
                relative[output++] = '_';
            relative[output++] = (char)tolower(c);
            segment_start = false;
        }
    }
    memcpy(relative + output, ".as", 4U);
    char *path = join_path(project->source_root, relative);
    free(relative);
    return path;
}

static int run_project_output(const Project *project, bool check_only,
                              const char *dump_kind) {
    if (!check_only && project->output_type == PROJECT_OUTPUT_LIBRARY) {
        fprintf(stderr,
                "error: library project `%s` cannot be run\n",
                project->name);
        return 1;
    }
    char *entry_path = module_file_path(project, project->entry);
    int status = lang_run_file_with_roots(
        entry_path, project->source_root,
        project->dependency_roots, project->dependency_count,
        project->root, project->stdlib_root,
        check_only, dump_kind,
        project->output_type != PROJECT_OUTPUT_LIBRARY);
    free(entry_path);
    return status;
}

static int run_project_output_args(
    const Project *project, const char *dump_kind, size_t argument_count,
    const char *const *arguments) {
    if (project->output_type != PROJECT_OUTPUT_EXE &&
        project->output_type != PROJECT_OUTPUT_WEB) {
        fprintf(stderr,
                "error: project `%s` is not runnable\n", project->name);
        return 1;
    }
    char *entry_path = module_file_path(project, project->entry);
    int status = lang_run_file_with_roots_args(
        entry_path, project->source_root,
        project->dependency_roots, project->dependency_count,
        project->root, project->stdlib_root,
        false, dump_kind, true, argument_count, arguments);
    free(entry_path);
    return status;
}

int lang_project_run(const char *project_path, bool check_only) {
    if (project_path == NULL) return 1;
    Project project;
    if (!parse_project(project_path, &project)) return 1;
    int status = run_project_output(
        &project, check_only,
        check_only ? NULL : "run-ir");
    project_free(&project);
    return status;
}

int lang_project_run_args(const char *project_path, size_t argument_count,
                          const char *const *arguments) {
    if (project_path == NULL) return 1;
    Project project;
    if (!parse_project(project_path, &project)) return 1;
    int status = run_project_output_args(
        &project, "run-ir", argument_count, arguments);
    project_free(&project);
    return status;
}

int lang_project_run_ir(const char *project_path) {
    if (project_path == NULL) return 1;
    Project project;
    if (!parse_project(project_path, &project)) return 1;
    int status = run_project_output(&project, false, "run-ir");
    project_free(&project);
    return status;
}

int lang_project_build_site(const char *project_path,
                            const char *output_directory) {
    if (project_path == NULL || output_directory == NULL) return 1;
    Project project;
    if (!parse_project(project_path, &project)) return 1;
    const char *arguments[] = {output_directory};
    int status = run_project_output_args(
        &project, "run-ir", 1U, arguments);
    project_free(&project);
    return status;
}

static int emit_project_output(const char *project_path,
                               const char *backend,
                               const char *css_directory) {
    if (project_path == NULL) return 1;
    Project project;
    if (!parse_project(project_path, &project)) return 1;
    int status;
    if (css_directory == NULL) {
        status = run_project_output(&project, true, backend);
    } else {
        char *entry_path = module_file_path(&project, project.entry);
        status = lang_emit_c_site_with_roots(
            entry_path, project.source_root,
            project.dependency_roots, project.dependency_count,
            project.root, project.stdlib_root,
            css_directory,
            project.output_type != PROJECT_OUTPUT_LIBRARY);
        free(entry_path);
    }
    project_free(&project);
    return status;
}

int lang_project_emit_c(const char *project_path) {
    return emit_project_output(project_path, "c", NULL);
}

int lang_project_emit_c_site(const char *project_path,
                             const char *css_directory) {
    return emit_project_output(project_path, "c", css_directory);
}

static const char *browser_tool(const char *variable,
                                const char *fallback) {
    const char *value = getenv(variable);
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static char *browser_output_path(const char *directory,
                                 const char *target,
                                 const char *suffix) {
    size_t length = strlen(directory) + strlen(target) +
                    strlen(suffix) + 2U;
    char *path = project_resize(NULL, length);
    (void)snprintf(path, length, "%s/%s%s", directory, target, suffix);
    return path;
}

static bool browser_ensure_directory(const char *path) {
#if defined(_WIN32)
    int status = _mkdir(path);
#else
    int status = mkdir(path, 0777);
#endif
    if (status == 0 || errno == EEXIST) return true;
    fprintf(stderr, "error: cannot create browser output directory `%s`: %s\n",
            path, strerror(errno));
    return false;
}

static int browser_run(char *const arguments[]) {
#if defined(_WIN32)
    (void)arguments;
    fputs("error: Aster Web browser builds are not yet supported on Windows\n",
          stderr);
    return 1;
#else
    pid_t child = fork();
    if (child < 0) {
        fprintf(stderr, "error: cannot start `%s`: %s\n",
                arguments[0], strerror(errno));
        return 1;
    }
    if (child == 0) {
        execvp(arguments[0], arguments);
        fprintf(stderr, "error: cannot execute `%s`: %s\n",
                arguments[0], strerror(errno));
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        fprintf(stderr, "error: browser tool `%s` failed\n", arguments[0]);
        return 1;
    }
    return 0;
#endif
}

static char *browser_clang_resource(const char *clang) {
#if defined(_WIN32)
    (void)clang;
    return NULL;
#else
    int descriptors[2];
    if (pipe(descriptors) != 0) return NULL;
    pid_t child = fork();
    if (child < 0) {
        (void)close(descriptors[0]);
        (void)close(descriptors[1]);
        return NULL;
    }
    if (child == 0) {
        (void)close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(127);
        (void)close(descriptors[1]);
        execlp(clang, clang, "-print-resource-dir", (char *)NULL);
        _exit(127);
    }
    (void)close(descriptors[1]);
    char buffer[1024];
    ssize_t count = read(descriptors[0], buffer, sizeof(buffer) - 1U);
    (void)close(descriptors[0]);
    int status = 0;
    if (waitpid(child, &status, 0) != child || count <= 0 ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return NULL;
    buffer[(size_t)count] = '\0';
    while (count > 0 && isspace((unsigned char)buffer[count - 1]) != 0)
        buffer[--count] = '\0';
    return count == 0 ? NULL : project_strndup(buffer, (size_t)count);
#endif
}

static bool browser_copy_file(const char *source_path,
                              const char *destination_path) {
    FILE *source = fopen(source_path, "rb");
    if (source == NULL) return false;
    FILE *destination = fopen(destination_path, "wb");
    if (destination == NULL) {
        (void)fclose(source);
        return false;
    }
    unsigned char buffer[8192];
    bool ok = true;
    for (;;) {
        size_t count = fread(buffer, 1U, sizeof(buffer), source);
        if (count != 0U && fwrite(buffer, 1U, count, destination) != count) {
            ok = false;
            break;
        }
        if (count < sizeof(buffer)) {
            if (ferror(source) != 0) ok = false;
            break;
        }
    }
    if (fclose(destination) != 0) ok = false;
    (void)fclose(source);
    return ok;
}

typedef struct BrowserExports {
    char **items;
    size_t count;
    size_t capacity;
} BrowserExports;

static void browser_exports_free(BrowserExports *exports) {
    for (size_t i = 0U; i < exports->count; ++i) free(exports->items[i]);
    free(exports->items);
    memset(exports, 0, sizeof(*exports));
}

static bool browser_exports_add(BrowserExports *exports,
                                const char *name, size_t length) {
    for (size_t i = 0U; i < exports->count; ++i)
        if (strlen(exports->items[i]) == length &&
            memcmp(exports->items[i], name, length) == 0)
            return true;
    if (exports->count == exports->capacity) {
        size_t capacity = exports->capacity == 0U
                        ? 8U : exports->capacity * 2U;
        exports->items = project_resize(
            exports->items, capacity * sizeof(*exports->items));
        exports->capacity = capacity;
    }
    exports->items[exports->count++] = project_strndup(name, length);
    return true;
}

static bool browser_collect_exports(const char *path,
                                    BrowserExports *exports) {
    LangSource source = {0};
    if (!lang_source_load(path, &source)) return false;
    const char prefix[] = "aster_export_";
    const char *cursor = source.text;
    while ((cursor = strstr(cursor, prefix)) != NULL) {
        const char *end = cursor + sizeof(prefix) - 1U;
        while (isalnum((unsigned char)*end) != 0 || *end == '_') ++end;
        (void)browser_exports_add(
            exports, cursor, (size_t)(end - cursor));
        cursor = end;
    }
    lang_source_free(&source);
    return exports->count != 0U;
}

static int browser_emit_entry(const Project *project,
                              const char *entry,
                              const char *output_path) {
    char *entry_path = module_file_path(project, entry);
    FILE *output = fopen(output_path, "wb");
    if (output == NULL) {
        fprintf(stderr, "error: cannot create `%s`: %s\n",
                output_path, strerror(errno));
        free(entry_path);
        return 1;
    }
    int status = lang_emit_c_with_roots_to_file(
        entry_path, project->source_root,
        project->dependency_roots, project->dependency_count,
        project->root, project->stdlib_root, true, output);
    if (fclose(output) != 0) status = 1;
    free(entry_path);
    return status;
}

int lang_project_build_web(const char *project_path,
                           const char *output_directory) {
    if (project_path == NULL || output_directory == NULL) return 1;
    Project project;
    if (!parse_project(project_path, &project)) return 1;
    if (project.output_type != PROJECT_OUTPUT_WEB) {
        fputs("error: browser build requires a web project\n", stderr);
        project_free(&project);
        return 1;
    }
    if (!browser_ensure_directory(output_directory)) {
        project_free(&project);
        return 1;
    }

    char *server_c = browser_output_path(
        output_directory, project.name, "-server.c");
    char *browser_c = browser_output_path(
        output_directory, project.name, "-browser.c");
    char *browser_o = browser_output_path(
        output_directory, project.name, "-browser.o");
    char *runtime_o = browser_output_path(
        output_directory, project.name, "-runtime.o");
    char *unoptimized = browser_output_path(
        output_directory, project.name, ".raw.wasm");
    char *wasm = browser_output_path(
        output_directory, project.name, ".wasm");
    char *loader = browser_output_path(
        output_directory, project.name, ".js");
    char *runtime_js = join_path(output_directory, "aster.js");
    const char *runtime_root_env = getenv("ASTER_BROWSER_RUNTIME_DIR");
    const char *runtime_root = runtime_root_env != NULL &&
                               runtime_root_env[0] != '\0'
                             ? runtime_root_env
                             : ASTER_BROWSER_RUNTIME_DIR;
    char *runtime_c = join_path(runtime_root, "wasm_libc.c");
    char *runtime_include = join_path(runtime_root, "include");
    char *runtime_js_source = join_path(runtime_root, "aster.js");
    int result = browser_emit_entry(&project, project.entry, server_c);
    if (result == 0)
        result = browser_emit_entry(
            &project, project.browser_entry, browser_c);

    const char *clang = browser_tool("ASTER_WASM_CLANG", "clang");
    char *resource = result == 0 ? browser_clang_resource(clang) : NULL;
    char *resource_include = resource != NULL
                           ? join_path(resource, "include") : NULL;
    if (result == 0 && resource == NULL) {
        fputs("error: cannot discover the Clang resource directory\n",
              stderr);
        result = 1;
    }
    char *compile_browser[] = {
        (char *)clang, "--target=wasm32-unknown-unknown", "-Oz",
        "-ffreestanding", "-ffunction-sections", "-fdata-sections",
        "-nostdinc", "-isystem", resource_include,
        "-I", runtime_include, "-c", browser_c, "-o", browser_o, NULL
    };
    char *compile_runtime[] = {
        (char *)clang, "--target=wasm32-unknown-unknown", "-Oz",
        "-ffreestanding", "-ffunction-sections", "-fdata-sections",
        "-nostdinc", "-isystem", resource_include,
        "-I", runtime_include, "-c", runtime_c, "-o", runtime_o, NULL
    };
    if (result == 0) result = browser_run(compile_browser);
    if (result == 0) result = browser_run(compile_runtime);

    BrowserExports exports = {0};
    if (result == 0 && !browser_collect_exports(browser_c, &exports)) {
        fputs("error: browser target generated no public Aster exports\n",
              stderr);
        result = 1;
    }
    char **link = NULL;
    char **export_flags = NULL;
    if (result == 0) {
        size_t base_count = 11U;
        link = project_resize(
            NULL, (base_count + exports.count + 1U) * sizeof(*link));
        export_flags = project_resize(
            NULL, exports.count * sizeof(*export_flags));
        size_t at = 0U;
        link[at++] = (char *)browser_tool(
            "ASTER_WASM_LINKER", "wasm-ld");
        link[at++] = "--no-entry";
        link[at++] = "--gc-sections";
        link[at++] = "--strip-all";
        link[at++] = "--export-memory";
        link[at++] = "--initial-memory=131072";
        link[at++] = "--max-memory=16777216";
        for (size_t i = 0U; i < exports.count; ++i) {
            size_t length = strlen(exports.items[i]) + 10U;
            export_flags[i] = project_resize(NULL, length);
            (void)snprintf(export_flags[i], length, "--export=%s",
                           exports.items[i]);
            link[at++] = export_flags[i];
        }
        link[at++] = browser_o;
        link[at++] = runtime_o;
        link[at++] = "-o";
        link[at++] = unoptimized;
        link[at] = NULL;
        result = browser_run(link);
    }
    char *optimize[] = {
        (char *)browser_tool("ASTER_WASM_OPTIMIZER", "wasm-opt"),
        "-Oz", unoptimized, "-o", wasm, NULL
    };
    if (result == 0) result = browser_run(optimize);
    if (result == 0 &&
        !browser_copy_file(runtime_js_source, runtime_js)) {
        fprintf(stderr, "error: cannot copy Aster Web browser runtime: %s\n",
                strerror(errno));
        result = 1;
    }
    if (result == 0) {
        FILE *application_loader = fopen(loader, "wb");
        bool loader_ok = application_loader != NULL;
        if (loader_ok)
            loader_ok = fprintf(
                application_loader,
                "import { hydrateAster } from \"./aster.js\";\n"
                "await hydrateAster({ wasmUrl: "
                "new URL(\"./%s.wasm\", import.meta.url) });\n",
                project.name) >= 0;
        if (application_loader != NULL &&
            fclose(application_loader) != 0)
            loader_ok = false;
        if (!loader_ok) {
            fputs("error: cannot write Aster Web browser loader\n", stderr);
            result = 1;
        }
    }
    if (result == 0) {
        (void)remove(browser_c);
        (void)remove(browser_o);
        (void)remove(runtime_o);
        (void)remove(unoptimized);
        printf("built web project `%s`: %s, %s, %s, %s\n",
               project.name, server_c, wasm, runtime_js, loader);
    }

    if (export_flags != NULL)
        for (size_t i = 0U; i < exports.count; ++i)
            free(export_flags[i]);
    free(export_flags);
    free(link);
    browser_exports_free(&exports);
    free(resource_include);
    free(resource);
    free(runtime_js_source);
    free(runtime_include);
    free(runtime_c);
    free(runtime_js);
    free(loader);
    free(wasm);
    free(unoptimized);
    free(runtime_o);
    free(browser_o);
    free(browser_c);
    free(server_c);
    project_free(&project);
    return result;
}

int lang_project_test(const char *project_path) {
    if (project_path == NULL) return 1;
    Project project;
    if (!parse_project(project_path, &project)) return 1;
    if (project.output_type != PROJECT_OUTPUT_TEST) {
        fprintf(stderr, "error: project `%s` is not a test project\n",
                project.name);
        project_free(&project);
        return 1;
    }
    int status = run_project_output(&project, false, "run-ir");
    printf("[%s] %s\n", status == 0 ? "pass" : "FAIL", project.name);
    printf("1 project tests, %d failures\n", status == 0 ? 0 : 1);
    project_free(&project);
    return status;
}

int lang_project_restore(const char *project_path) {
    if (project_path == NULL) return 1;
    Project project;
    if (!parse_project(project_path, &project)) return 1;
    printf("  Restored %s\n", project_path);
    project_free(&project);
    return 0;
}
