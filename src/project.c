#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
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

typedef enum ProjectTargetKind {
    PROJECT_TARGET_BIN,
    PROJECT_TARGET_LIB,
    PROJECT_TARGET_TEST
} ProjectTargetKind;

typedef struct ProjectTarget {
    char *name;
    char *entry;
    char *browser_entry;
    ProjectTargetKind kind;
    bool has_kind;
} ProjectTarget;

typedef struct ProjectDependency {
    char *name;
    char *path;
    char *source_root;
} ProjectDependency;

typedef struct Project {
    char *name;
    char *root;
    char *source_root;
    char *stdlib_root;
    char *default_target;
    ProjectTarget *targets;
    size_t target_count;
    size_t target_capacity;
    ProjectDependency *dependencies;
    const char **dependency_roots;
    size_t dependency_count;
    size_t dependency_capacity;
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
    free(project->default_target);
    for (size_t i = 0U; i < project->target_count; ++i) {
        free(project->targets[i].name);
        free(project->targets[i].entry);
        free(project->targets[i].browser_entry);
    }
    for (size_t i = 0U; i < project->dependency_count; ++i) {
        free(project->dependencies[i].name);
        free(project->dependencies[i].path);
        free(project->dependencies[i].source_root);
    }
    free(project->dependencies);
    free(project->dependency_roots);
    free(project->targets);
    memset(project, 0, sizeof(*project));
}

static bool valid_dependency_name(const char *name) {
    if (name == NULL || *name == '\0') return false;
    for (const unsigned char *byte = (const unsigned char *)name;
         *byte != 0U; ++byte)
        if (isalnum(*byte) == 0 && *byte != '_' && *byte != '-')
            return false;
    return true;
}

static bool add_dependency(Project *project, const char *name, char *path,
                           const char *manifest, size_t line) {
    if (!valid_dependency_name(name)) {
        fprintf(stderr,
                "error: invalid dependency name `%s`\n  --> %s:%zu\n",
                name, manifest, line);
        free(path);
        return false;
    }
    for (size_t i = 0U; i < project->dependency_count; ++i)
        if (strcmp(project->dependencies[i].name, name) == 0) {
            fprintf(stderr,
                    "error: duplicate dependency `%s`\n  --> %s:%zu\n",
                    name, manifest, line);
            free(path);
            return false;
        }
    if (project->dependency_count == project->dependency_capacity) {
        size_t capacity = project->dependency_capacity == 0U
                        ? 4U : project->dependency_capacity * 2U;
        project->dependencies = project_resize(
            project->dependencies,
            capacity * sizeof(*project->dependencies));
        project->dependency_capacity = capacity;
    }
    ProjectDependency *dependency =
        &project->dependencies[project->dependency_count++];
    *dependency = (ProjectDependency){
        .name=project_strndup(name, strlen(name)), .path=path
    };
    return true;
}

static ProjectTarget *find_target(Project *project, const char *name) {
    for (size_t i = 0U; i < project->target_count; ++i)
        if (strcmp(project->targets[i].name, name) == 0)
            return &project->targets[i];
    return NULL;
}

static ProjectTarget *add_target(Project *project, const char *name,
                                 const char *path, size_t line) {
    if (find_target(project, name) != NULL) {
        fprintf(stderr,
                "error: duplicate target `%s`\n  --> %s:%zu\n",
                name, path, line);
        return NULL;
    }
    if (project->target_count == project->target_capacity) {
        size_t capacity = project->target_capacity == 0U
                        ? 4U : project->target_capacity * 2U;
        project->targets = project_resize(
            project->targets, capacity * sizeof(*project->targets));
        project->target_capacity = capacity;
    }
    ProjectTarget *target = &project->targets[project->target_count++];
    *target = (ProjectTarget){
        .name=project_strndup(name, strlen(name)),
        .kind=PROJECT_TARGET_BIN
    };
    return target;
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

static bool parse_target_kind(ProjectTarget *target, char *value,
                              const char *path, size_t line) {
    if (target->has_kind) {
        fprintf(stderr,
                "error: duplicate target key `kind`\n  --> %s:%zu\n",
                path, line);
        free(value);
        return false;
    }
    if (strcmp(value, "bin") == 0)
        target->kind = PROJECT_TARGET_BIN;
    else if (strcmp(value, "lib") == 0)
        target->kind = PROJECT_TARGET_LIB;
    else if (strcmp(value, "test") == 0)
        target->kind = PROJECT_TARGET_TEST;
    else {
        fprintf(stderr,
                "error: target kind must be `bin`, `lib`, or `test`\n"
                "  --> %s:%zu\n", path, line);
        free(value);
        return false;
    }
    target->has_kind = true;
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

static bool parse_manifest_internal(
    const char *path, Project *project, bool resolve_dependencies) {
    memset(project, 0, sizeof(*project));
    project->root = directory_of(path);
    LangSource source = {0};
    if (!lang_source_load(path, &source)) {
        fprintf(stderr, "error: cannot load project manifest `%s`\n", path);
        project_free(project);
        return false;
    }
    ProjectTarget *section_target = NULL;
    bool dependencies_section = false;
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
            if (strcmp(line, "[dependencies]") == 0) {
                section_target = NULL;
                dependencies_section = true;
                continue;
            }
            if (length < 10U || line[length - 1U] != ']' ||
                strncmp(line, "[target.", 8U) != 0) {
                fprintf(stderr,
                        "error: expected `[dependencies]` or `[target.NAME]`\n"
                        "  --> %s:%zu\n",
                        path, line_number);
                ok = false;
                break;
            }
            line[length - 1U] = '\0';
            const char *name = line + 8U;
            if (!valid_module_name(name) || strchr(name, '.') != NULL) {
                fprintf(stderr,
                        "error: invalid target name `%s`\n  --> %s:%zu\n",
                        name, path, line_number);
                ok = false;
                break;
            }
            section_target = add_target(
                project, name, path, line_number);
            dependencies_section = false;
            ok = section_target != NULL;
            continue;
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
        if (dependencies_section) {
            ok = add_dependency(
                project, key, value, path, line_number);
        } else if (section_target == NULL) {
            if (strcmp(key, "name") == 0)
                ok = assign_owned(
                    &project->name, value, key, path, line_number);
            else if (strcmp(key, "source_root") == 0)
                ok = assign_owned(
                    &project->source_root, value, key, path, line_number);
            else if (strcmp(key, "stdlib") == 0)
                ok = assign_owned(
                    &project->stdlib_root, value, key, path, line_number);
            else if (strcmp(key, "default_target") == 0)
                ok = assign_owned(
                    &project->default_target, value, key, path, line_number);
            else {
                fprintf(stderr,
                        "error: unknown project key `%s`\n  --> %s:%zu\n",
                        key, path, line_number);
                free(value);
                ok = false;
            }
        } else if (strcmp(key, "kind") == 0) {
            ok = parse_target_kind(
                section_target, value, path, line_number);
        } else if (strcmp(key, "entry") == 0) {
            ok = assign_owned(
                &section_target->entry, value, key, path, line_number);
        } else if (strcmp(key, "browser_entry") == 0) {
            ok = assign_owned(
                &section_target->browser_entry,
                value, key, path, line_number);
        } else {
            fprintf(stderr,
                    "error: unknown target key `%s`\n  --> %s:%zu\n",
                    key, path, line_number);
            free(value);
            ok = false;
        }
    }
    lang_source_free(&source);
    if (!ok) {
        project_free(project);
        return false;
    }
    if (project->name == NULL || project->source_root == NULL ||
        project->target_count == 0U) {
        fprintf(stderr,
                "error: manifest requires `name`, `source_root`, and at least one target\n");
        project_free(project);
        return false;
    }
    for (size_t i = 0U; i < project->target_count; ++i)
        if (project->targets[i].entry == NULL ||
            !valid_module_name(project->targets[i].entry)) {
            fprintf(stderr,
                    "error: target `%s` requires a valid namespace `entry`\n",
                    project->targets[i].name);
            project_free(project);
            return false;
        }
    for (size_t i = 0U; i < project->target_count; ++i) {
        ProjectTarget *target = &project->targets[i];
        if (target->browser_entry != NULL &&
            (!valid_module_name(target->browser_entry) ||
             target->kind != PROJECT_TARGET_BIN)) {
            fprintf(stderr,
                    "error: target `%s` browser_entry requires a valid namespace and bin kind\n",
                    target->name);
            project_free(project);
            return false;
        }
    }
    if (project->default_target != NULL &&
        find_target(project, project->default_target) == NULL) {
        fprintf(stderr,
                "error: default target `%s` is not declared\n",
                project->default_target);
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
    if (resolve_dependencies && project->dependency_count != 0U) {
        project->dependency_roots = project_resize(
            NULL, project->dependency_count *
                sizeof(*project->dependency_roots));
        for (size_t i = 0U; i < project->dependency_count; ++i) {
            ProjectDependency *dependency = &project->dependencies[i];
            char *directory = join_path(project->root, dependency->path);
            char *manifest = join_path(directory, "aster.toml");
            Project package;
            if (!parse_manifest_internal(manifest, &package, false)) {
                fprintf(stderr,
                        "error: cannot load local dependency `%s` from `%s`\n",
                        dependency->name, directory);
                free(manifest);
                free(directory);
                project_free(project);
                return false;
            }
            if (strcmp(package.name, dependency->name) != 0) {
                fprintf(stderr,
                        "error: dependency `%s` points to package `%s`\n",
                        dependency->name, package.name);
                project_free(&package);
                free(manifest);
                free(directory);
                project_free(project);
                return false;
            }
            dependency->source_root = project_strndup(
                package.source_root, strlen(package.source_root));
            project->dependency_roots[i] = dependency->source_root;
            project_free(&package);
            free(manifest);
            free(directory);
        }
    }
    return true;
}

static bool parse_manifest(const char *path, Project *project) {
    return parse_manifest_internal(path, project, true);
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

static int run_target(const Project *project, const ProjectTarget *target,
                      bool check_only, const char *dump_kind) {
    if (!check_only && target->kind == PROJECT_TARGET_LIB) {
        fprintf(stderr,
                "error: library target `%s` can be checked but not run\n",
                target->name);
        return 1;
    }
    char *entry_path = module_file_path(project, target->entry);
    int status = lang_run_file_with_roots(
        entry_path, project->source_root,
        project->dependency_roots, project->dependency_count,
        project->root, project->stdlib_root,
        check_only, dump_kind, target->kind != PROJECT_TARGET_LIB);
    free(entry_path);
    return status;
}

static int run_target_args(
    const Project *project, const ProjectTarget *target,
    const char *dump_kind, size_t argument_count,
    const char *const *arguments) {
    if (target->kind != PROJECT_TARGET_BIN) {
        fprintf(stderr,
                "error: site build target `%s` must have bin kind\n",
                target->name);
        return 1;
    }
    char *entry_path = module_file_path(project, target->entry);
    int status = lang_run_file_with_roots_args(
        entry_path, project->source_root,
        project->dependency_roots, project->dependency_count,
        project->root, project->stdlib_root,
        false, dump_kind, true, argument_count, arguments);
    free(entry_path);
    return status;
}

int lang_project_run(const char *manifest_path, const char *target_name,
                     bool check_only) {
    if (manifest_path == NULL) return 1;
    Project project;
    if (!parse_manifest(manifest_path, &project)) return 1;
    const char *selected = target_name != NULL
                         ? target_name : project.default_target;
    if (selected == NULL) {
        fprintf(stderr,
                "error: no target specified and manifest has no default target\n");
        project_free(&project);
        return 1;
    }
    ProjectTarget *target = find_target(&project, selected);
    if (target == NULL) {
        fprintf(stderr, "error: unknown project target `%s`\n", selected);
        project_free(&project);
        return 1;
    }
    int status = run_target(
        &project, target, check_only,
        check_only ? NULL : "run-ir");
    project_free(&project);
    return status;
}

int lang_project_run_ir(const char *manifest_path,
                        const char *target_name) {
    if (manifest_path == NULL) return 1;
    Project project;
    if (!parse_manifest(manifest_path, &project)) return 1;
    const char *selected = target_name != NULL
                         ? target_name : project.default_target;
    if (selected == NULL) {
        fprintf(stderr,
                "error: no target specified and manifest has no default target\n");
        project_free(&project);
        return 1;
    }
    ProjectTarget *target = find_target(&project, selected);
    if (target == NULL) {
        fprintf(stderr, "error: unknown project target `%s`\n", selected);
        project_free(&project);
        return 1;
    }
    int status = run_target(&project, target, false, "run-ir");
    project_free(&project);
    return status;
}

int lang_project_build_site(const char *manifest_path,
                            const char *output_directory,
                            const char *target_name) {
    if (manifest_path == NULL || output_directory == NULL) return 1;
    Project project;
    if (!parse_manifest(manifest_path, &project)) return 1;
    const char *selected = target_name != NULL
                         ? target_name : project.default_target;
    if (selected == NULL) {
        fprintf(stderr,
                "error: no target specified and manifest has no default target\n");
        project_free(&project);
        return 1;
    }
    ProjectTarget *target = find_target(&project, selected);
    if (target == NULL) {
        fprintf(stderr, "error: unknown project target `%s`\n", selected);
        project_free(&project);
        return 1;
    }
    const char *arguments[] = {output_directory};
    int status = run_target_args(
        &project, target, "run-ir", 1U, arguments);
    project_free(&project);
    return status;
}

static int emit_project_target(const char *manifest_path,
                               const char *target_name,
                               const char *backend,
                               const char *css_directory) {
    if (manifest_path == NULL) return 1;
    Project project;
    if (!parse_manifest(manifest_path, &project)) return 1;
    const char *selected = target_name != NULL
                         ? target_name : project.default_target;
    if (selected == NULL) {
        fprintf(stderr,
                "error: no target specified and manifest has no default target\n");
        project_free(&project);
        return 1;
    }
    ProjectTarget *target = find_target(&project, selected);
    if (target == NULL) {
        fprintf(stderr, "error: unknown project target `%s`\n", selected);
        project_free(&project);
        return 1;
    }
    int status;
    if (css_directory == NULL) {
        status = run_target(&project, target, true, backend);
    } else {
        char *entry_path = module_file_path(&project, target->entry);
        status = lang_emit_c_site_with_roots(
            entry_path, project.source_root,
            project.dependency_roots, project.dependency_count,
            project.root, project.stdlib_root,
            css_directory, target->kind != PROJECT_TARGET_LIB);
        free(entry_path);
    }
    project_free(&project);
    return status;
}

int lang_project_emit_c(const char *manifest_path,
                        const char *target_name) {
    return emit_project_target(manifest_path, target_name, "c", NULL);
}

int lang_project_emit_c_site(const char *manifest_path,
                             const char *target_name,
                             const char *css_directory) {
    return emit_project_target(
        manifest_path, target_name, "c", css_directory);
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
    fputs("error: Lime browser builds are not yet supported on Windows\n",
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

int lang_project_build_web(const char *manifest_path,
                           const char *output_directory,
                           const char *target_name) {
    if (manifest_path == NULL || output_directory == NULL) return 1;
    Project project;
    if (!parse_manifest(manifest_path, &project)) return 1;
    const char *selected = target_name != NULL
                         ? target_name : project.default_target;
    ProjectTarget *target = selected != NULL
                          ? find_target(&project, selected) : NULL;
    if (target == NULL || target->browser_entry == NULL) {
        fprintf(stderr,
                "error: web build target requires `browser_entry`\n");
        project_free(&project);
        return 1;
    }
    if (!browser_ensure_directory(output_directory)) {
        project_free(&project);
        return 1;
    }

    char *server_c = browser_output_path(
        output_directory, target->name, "-server.c");
    char *browser_c = browser_output_path(
        output_directory, target->name, "-browser.c");
    char *browser_o = browser_output_path(
        output_directory, target->name, "-browser.o");
    char *runtime_o = browser_output_path(
        output_directory, target->name, "-runtime.o");
    char *unoptimized = browser_output_path(
        output_directory, target->name, ".raw.wasm");
    char *wasm = browser_output_path(
        output_directory, target->name, ".wasm");
    char *loader = browser_output_path(
        output_directory, target->name, ".js");
    char *runtime_js = join_path(output_directory, "aster.js");
    const char *runtime_root_env = getenv("ASTER_BROWSER_RUNTIME_DIR");
    const char *runtime_root = runtime_root_env != NULL &&
                               runtime_root_env[0] != '\0'
                             ? runtime_root_env
                             : ASTER_BROWSER_RUNTIME_DIR;
    char *runtime_c = join_path(runtime_root, "wasm_libc.c");
    char *runtime_include = join_path(runtime_root, "include");
    char *runtime_js_source = join_path(runtime_root, "aster.js");
    int result = browser_emit_entry(&project, target->entry, server_c);
    if (result == 0)
        result = browser_emit_entry(
            &project, target->browser_entry, browser_c);

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
        fprintf(stderr, "error: cannot copy Lime browser runtime: %s\n",
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
                target->name) >= 0;
        if (application_loader != NULL &&
            fclose(application_loader) != 0)
            loader_ok = false;
        if (!loader_ok) {
            fputs("error: cannot write Lime browser loader\n", stderr);
            result = 1;
        }
    }
    if (result == 0) {
        (void)remove(browser_c);
        (void)remove(browser_o);
        (void)remove(runtime_o);
        (void)remove(unoptimized);
        printf("built web target `%s`: %s, %s, %s, %s\n",
               target->name, server_c, wasm, runtime_js, loader);
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

int lang_project_test(const char *manifest_path) {
    if (manifest_path == NULL) return 1;
    Project project;
    if (!parse_manifest(manifest_path, &project)) return 1;
    size_t tests = 0U;
    int failures = 0;
    for (size_t i = 0U; i < project.target_count; ++i) {
        if (project.targets[i].kind != PROJECT_TARGET_TEST) continue;
        ++tests;
        int status = run_target(
            &project, &project.targets[i], false, "run-ir");
        printf("[%s] %s\n",
               status == 0 ? "pass" : "FAIL",
               project.targets[i].name);
        if (status != 0) ++failures;
    }
    if (tests == 0U) {
        fputs("error: project declares no test targets\n", stderr);
        failures = 1;
    }
    printf("%zu project tests, %d failures\n", tests, failures);
    project_free(&project);
    return failures == 0 ? 0 : 1;
}
