#include "vm_builtins_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

bool native_path_string(
    const LangValue *value, char *output, size_t output_capacity) {
    LangStringView path;
    if (!lang_value_string_view(value, &path) ||
        path.length == 0U || path.length >= output_capacity)
        return false;
    for (size_t i = 0U; i < path.length; ++i)
        if (path.data[i] == '\0') return false;
    memcpy(output, path.data, path.length);
    output[path.length] = '\0';
    return true;
}

bool native_path_separator(unsigned char value) {
#if defined(_WIN32)
    return value == (unsigned char)'/' || value == (unsigned char)'\\';
#else
    return value == (unsigned char)'/';
#endif
}

static bool native_path_rooted(LangStringView path) {
    if (path.length == 0U) return false;
    if (native_path_separator((unsigned char)path.data[0])) return true;
#if defined(_WIN32)
    return path.length >= 2U &&
           ((path.data[0] >= 'A' && path.data[0] <= 'Z') ||
            (path.data[0] >= 'a' && path.data[0] <= 'z')) &&
           path.data[1] == ':';
#else
    return false;
#endif
}

LangNativeResult native_path_string_result(
    LangVM *vm, const char *data, size_t length) {
    LangValue result;
    if (!lang_string_value(
            vm, (LangStringView){data, length}, &result))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not allocate path string"
        };
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult native_path_combine_or_join_value(
    LangVM *vm, const LangValue *args, size_t arg_count,
    bool reset_on_rooted_second) {
    LangStringView first;
    LangStringView second;
    if (arg_count != 2U ||
        !lang_value_string_view(&args[0], &first) ||
        !lang_value_string_view(&args[1], &second))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "path combination expects two strings"
        };
    if (first.length == 0U ||
        (reset_on_rooted_second && native_path_rooted(second)))
        return native_path_string_result(vm, second.data, second.length);
    if (second.length == 0U)
        return native_path_string_result(vm, first.data, first.length);
    bool separator =
        native_path_separator((unsigned char)first.data[first.length - 1U]) ||
        native_path_separator((unsigned char)second.data[0]);
    if (first.length > SIZE_MAX - second.length - (separator ? 0U : 1U))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT}, "combined path is too long"
        };
    size_t length = first.length + second.length + (separator ? 0U : 1U);
    char *combined = vm_allocate(length == 0U ? 1U : length, 1U);
    memcpy(combined, first.data, first.length);
    size_t cursor = first.length;
    if (!separator) combined[cursor++] =
#if defined(_WIN32)
        '\\';
#else
        '/';
#endif
    memcpy(combined + cursor, second.data, second.length);
    LangNativeResult result =
        native_path_string_result(vm, combined, length);
    free(combined);
    return result;
}

static LangNativeResult native_path_combine_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_path_combine_or_join_value(
        vm, args, arg_count, true);
}

static LangNativeResult native_path_join_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_path_combine_or_join_value(
        vm, args, arg_count, false);
}

static bool native_path_view_argument(
    const LangValue *args, size_t arg_count, LangStringView *path) {
    return arg_count == 1U && lang_value_string_view(&args[0], path);
}

static size_t native_path_file_name_start(LangStringView path) {
    size_t start = 0U;
    for (size_t i = 0U; i < path.length; ++i)
        if (native_path_separator((unsigned char)path.data[i]))
            start = i + 1U;
    return start;
}

static bool native_path_is_root_view(LangStringView path) {
    if (!native_path_rooted(path)) return false;
#if defined(_WIN32)
    if (path.length == 1U && native_path_separator(
            (unsigned char)path.data[0]))
        return true;
    return path.length <= 3U && path.length >= 2U &&
           path.data[1] == ':' &&
           (path.length == 2U || native_path_separator(
                (unsigned char)path.data[2]));
#else
    for (size_t i = 0U; i < path.length; ++i)
        if (!native_path_separator((unsigned char)path.data[i]))
            return false;
    return true;
#endif
}

static size_t native_path_extension_start(
    LangStringView path, size_t file_name_start) {
    for (size_t i = path.length; i > file_name_start; --i) {
        unsigned char value = (unsigned char)path.data[i - 1U];
        if (value != (unsigned char)'.') continue;
        return i == path.length ? path.length : i - 1U;
    }
    return path.length;
}

static LangNativeResult native_path_get_file_name_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.GetFileName expects one string"
        };
    size_t start = native_path_file_name_start(path);
    return native_path_string_result(
        vm, path.length == start ? NULL : path.data + start,
        path.length - start);
}

static LangNativeResult native_path_get_extension_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.GetExtension expects one string"
        };
    size_t file_start = native_path_file_name_start(path);
    size_t extension = native_path_extension_start(path, file_start);
    return native_path_string_result(
        vm, path.length == extension ? NULL : path.data + extension,
        path.length - extension);
}

static LangNativeResult native_path_get_file_name_without_extension_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.GetFileNameWithoutExtension expects one string"
        };
    size_t file_start = native_path_file_name_start(path);
    size_t extension = native_path_extension_start(path, file_start);
    return native_path_string_result(
        vm, extension == file_start ? NULL : path.data + file_start,
        extension - file_start);
}

static LangNativeResult native_path_is_root_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "path root query expects one string"
        };
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_BOOL,
         .as.boolean=native_path_is_root_view(path)},
        NULL
    };
}

static LangNativeResult native_path_get_directory_name_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.GetDirectoryName expects one string"
        };
    size_t end = path.length;
    while (end > 0U &&
           native_path_separator((unsigned char)path.data[end - 1U]))
        --end;
    if (end < path.length)
        return native_path_string_result(vm, path.data, end);
    for (size_t i = end; i > 0U; --i) {
        if (!native_path_separator((unsigned char)path.data[i - 1U]))
            continue;
        size_t directory_length = i - 1U;
        if (directory_length == 0U) directory_length = 1U;
        return native_path_string_result(
            vm, path.data, directory_length);
    }
    return native_path_string_result(vm, "", 0U);
}

static LangNativeResult native_path_get_root_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.GetPathRoot expects one string"
        };
#if defined(_WIN32)
    if (path.length >= 2U && path.data[1] == ':') {
        size_t length = path.length >= 3U && native_path_separator(
            (unsigned char)path.data[2]) ? 3U : 2U;
        return native_path_string_result(vm, path.data, length);
    }
#endif
    if (path.length > 0U && native_path_separator(
            (unsigned char)path.data[0]))
        return native_path_string_result(vm, path.data, 1U);
    return native_path_string_result(vm, "", 0U);
}

static LangNativeResult native_path_change_extension_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    LangStringView extension;
    if (arg_count != 3U ||
        !lang_value_string_view(&args[0], &path) ||
        !lang_value_string_view(&args[1], &extension) ||
        args[2].tag != LANG_VALUE_BOOL)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.ChangeExtension expects path, extension, and removal flag"
        };
    size_t file_start = native_path_file_name_start(path);
    size_t base_length = native_path_extension_start(path, file_start);
    if (args[2].as.boolean)
        return native_path_string_result(vm, path.data, base_length);
    bool has_dot = extension.length > 0U && extension.data[0] == '.';
    if (base_length > SIZE_MAX - extension.length - (has_dot ? 0U : 1U))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT}, "changed path is too long"
        };
    size_t length = base_length + extension.length + (has_dot ? 0U : 1U);
    char *changed = vm_allocate(length == 0U ? 1U : length, 1U);
    if (base_length != 0U)
        memcpy(changed, path.data, base_length);
    size_t cursor = base_length;
    if (!has_dot) changed[cursor++] = '.';
    if (extension.length != 0U)
        memcpy(changed + cursor, extension.data, extension.length);
    LangNativeResult result = native_path_string_result(vm, changed, length);
    free(changed);
    return result;
}

static LangNativeResult native_path_is_fully_qualified_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    LangStringView path;
    if (!native_path_view_argument(args, arg_count, &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Path.IsPathFullyQualified expects one string"
        };
    return (LangNativeResult){
        true,
        {.tag=LANG_VALUE_BOOL, .as.boolean=native_path_rooted(path)},
        NULL
    };
}

static LangNativeResult native_directory_get_current_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)args;
    if (arg_count != 0U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Directory.GetCurrentDirectory expects no arguments"
        };
    char path[4096];
#if defined(_WIN32)
    if (_getcwd(path, (int)sizeof(path)) == NULL)
#else
    if (getcwd(path, sizeof(path)) == NULL)
#endif
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not get current directory"
        };
    return native_path_string_result(vm, path, strlen(path));
}

static LangNativeResult native_directory_set_current_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    char path[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], path, sizeof(path)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Directory.SetCurrentDirectory expects one valid path"
        };
#if defined(_WIN32)
    int status = _chdir(path);
#else
    int status = chdir(path);
#endif
    if (status != 0)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not set current directory"
        };
    return (LangNativeResult){
        true, {.tag=LANG_VALUE_UNIT}, NULL
    };
}

static LangNativeResult native_environment_new_line_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)args;
    if (arg_count != 0U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "Environment.NewLine expects no arguments"
        };
#if defined(_WIN32)
    return native_path_string_result(vm, "\r\n", 2U);
#else
    return native_path_string_result(vm, "\n", 1U);
#endif
}

LangNativeResult native_result_unit(LangVM *vm) {
    LangValue result;
    if (!lang_result_ok_value(
            vm, (LangValue){.tag=LANG_VALUE_UNIT}, &result))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct filesystem Result"
        };
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult native_result_bool(LangVM *vm, bool value) {
    LangValue result;
    if (!lang_result_ok_value(
            vm,
            (LangValue){.tag=LANG_VALUE_BOOL, .as.boolean=value},
            &result))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct filesystem Result"
        };
    return (LangNativeResult){true, result, NULL};
}

typedef enum NativePathQuery {
    NATIVE_PATH_EXISTS,
    NATIVE_PATH_IS_FILE,
    NATIVE_PATH_IS_DIRECTORY
} NativePathQuery;

static LangNativeResult native_path_query(
    LangVM *vm, const LangValue *args, size_t arg_count,
    NativePathQuery query) {
    char path[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], path, sizeof(path)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "filesystem query expects one valid path"
        };
#if defined(_WIN32)
    struct _stat metadata;
    int status = _stat(path, &metadata);
#else
    struct stat metadata;
    int status = stat(path, &metadata);
#endif
    if (status != 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return native_result_bool(vm, false);
        return native_result_error(vm, "could not inspect path");
    }
    bool result = true;
    if (query == NATIVE_PATH_IS_FILE)
#if defined(_WIN32)
        result = (metadata.st_mode & _S_IFMT) == _S_IFREG;
#else
        result = S_ISREG(metadata.st_mode);
#endif
    else if (query == NATIVE_PATH_IS_DIRECTORY)
#if defined(_WIN32)
        result = (metadata.st_mode & _S_IFMT) == _S_IFDIR;
#else
        result = S_ISDIR(metadata.st_mode);
#endif
    return native_result_bool(vm, result);
}

static LangNativeResult native_path_exists_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_path_query(
        vm, args, arg_count, NATIVE_PATH_EXISTS);
}

static LangNativeResult native_path_is_file_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_path_query(
        vm, args, arg_count, NATIVE_PATH_IS_FILE);
}

static LangNativeResult native_path_is_directory_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    return native_path_query(
        vm, args, arg_count, NATIVE_PATH_IS_DIRECTORY);
}

static LangNativeResult native_create_directory_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    char path[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], path, sizeof(path)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_create_directory expects one valid path"
        };
#if defined(_WIN32)
    int status = _mkdir(path);
#else
    int status = mkdir(path, (mode_t)0777);
#endif
    if (status != 0)
        return native_result_error(vm, "could not create directory");
    return native_result_unit(vm);
}

static LangNativeResult native_rename_path_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    char source[4096];
    char destination[4096];
    if (arg_count != 2U ||
        !native_path_string(&args[0], source, sizeof(source)) ||
        !native_path_string(&args[1], destination, sizeof(destination)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_rename_path expects two valid paths"
        };
    if (rename(source, destination) != 0)
        return native_result_error(vm, "could not rename path");
    return native_result_unit(vm);
}

static LangNativeResult native_remove_file_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    char path[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], path, sizeof(path)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_remove_file expects one valid path"
        };
#if defined(_WIN32)
    int status = remove(path);
#else
    int status = unlink(path);
#endif
    if (status != 0)
        return native_result_error(vm, "could not remove file");
    return native_result_unit(vm);
}

static LangNativeResult native_remove_directory_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    char path[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], path, sizeof(path)))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_remove_directory expects one valid path"
        };
#if defined(_WIN32)
    int status = _rmdir(path);
#else
    int status = rmdir(path);
#endif
    if (status != 0)
        return native_result_error(vm, "could not remove directory");
    return native_result_unit(vm);
}

typedef struct NativeDirectory {
#if defined(_WIN32)
    int unavailable;
#else
    DIR *stream;
#endif
} NativeDirectory;

static void native_directory_drop(void *handle) {
    NativeDirectory *directory = handle;
#if !defined(_WIN32)
    if (directory->stream != NULL)
        (void)closedir(directory->stream);
#endif
    free(directory);
}

static LangNativeResult native_directory_open_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    if (arg_count != 1U ||
        !lang_value_string_view(&args[0], &path))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_directory_open expects one path"
        };
#if defined(_WIN32)
    (void)path;
    return native_result_error(
        vm, "directory traversal is not implemented on Windows yet");
#else
    if (path.length == SIZE_MAX)
        return native_result_error(vm, "directory path is too long");
    char *path_c = vm_allocate(path.length + 1U, 1U);
    if (path.length != 0U)
        memcpy(path_c, path.data, path.length);
    path_c[path.length] = '\0';
    DIR *stream = opendir(path_c);
    free(path_c);
    if (stream == NULL)
        return native_result_error(vm, "could not open directory");
    NativeDirectory *directory = vm_allocate(1U, sizeof(*directory));
    directory->stream = stream;
    LangValue handle;
    if (!lang_native_handle_value(
            vm, directory, native_directory_drop, &handle)) {
        native_directory_drop(directory);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not wrap directory handle"
        };
    }
    LangValue result;
    if (!lang_result_ok_value(vm, handle, &result)) {
        lang_value_drop(vm, &handle);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct directory Result"
        };
    }
    return (LangNativeResult){true, result, NULL};
#endif
}

static LangNativeResult native_directory_next_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    if (arg_count != 1U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_directory_next expects one directory"
        };
    NativeDirectory *directory = lang_native_handle_data(&args[0]);
    if (directory == NULL)
        return native_result_error(vm, "invalid directory handle");
#if defined(_WIN32)
    return native_result_error(
        vm, "directory traversal is not implemented on Windows yet");
#else
    struct dirent *entry = NULL;
    errno = 0;
    do {
        entry = readdir(directory->stream);
    } while (entry != NULL &&
             (strcmp(entry->d_name, ".") == 0 ||
              strcmp(entry->d_name, "..") == 0));
    if (entry == NULL)
        return native_result_error(
            vm, errno == 0
                ? "end of directory"
                : "could not read directory");
    LangValue name;
    if (!lang_string_value(
            vm,
            (LangStringView){
                entry->d_name, strlen(entry->d_name)
            },
            &name))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not allocate directory entry"
        };
    LangValue result;
    if (!lang_result_ok_value(vm, name, &result)) {
        lang_value_drop(vm, &name);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct directory entry Result"
        };
    }
    return (LangNativeResult){true, result, NULL};
#endif
}


void vm_register_directory_builtins(LangVM *vm) {
    (void)lang_register_native(vm, "NativeDirectoryOpen",
                               native_directory_open_value, 1U);
    (void)lang_register_native(vm, "NativeDirectoryNext",
                               native_directory_next_value, 1U);
}

void vm_register_path_builtins(LangVM *vm) {
    (void)lang_register_native(vm, "NativePathExists",
                               native_path_exists_value, 1U);
    (void)lang_register_native(vm, "NativePathIsFile",
                               native_path_is_file_value, 1U);
    (void)lang_register_native(vm, "NativePathIsDirectory",
                               native_path_is_directory_value, 1U);
    (void)lang_register_native(vm, "NativePathCombine",
                               native_path_combine_value, 2U);
    (void)lang_register_native(vm, "NativePathJoin",
                               native_path_join_value, 2U);
    (void)lang_register_native(vm, "NativePathGetFileName",
                               native_path_get_file_name_value, 1U);
    (void)lang_register_native(
        vm, "NativePathGetFileNameWithoutExtension",
        native_path_get_file_name_without_extension_value, 1U);
    (void)lang_register_native(vm, "NativePathGetExtension",
                               native_path_get_extension_value, 1U);
    (void)lang_register_native(vm, "NativePathIsRoot",
                               native_path_is_root_value, 1U);
    (void)lang_register_native(vm, "NativePathGetDirectoryName",
                               native_path_get_directory_name_value, 1U);
    (void)lang_register_native(vm, "NativePathGetPathRoot",
                               native_path_get_root_value, 1U);
    (void)lang_register_native(vm, "NativePathChangeExtension",
                               native_path_change_extension_value, 3U);
    (void)lang_register_native(vm, "NativePathIsPathFullyQualified",
                               native_path_is_fully_qualified_value, 1U);
    (void)lang_register_native(vm, "NativeDirectoryGetCurrentDirectory",
                               native_directory_get_current_value, 0U);
    (void)lang_register_native(vm, "NativeDirectorySetCurrentDirectory",
                               native_directory_set_current_value, 1U);
    (void)lang_register_native(vm, "NativeEnvironmentNewLine",
                               native_environment_new_line_value, 0U);
    (void)lang_register_native(vm, "NativeCreateDirectory",
                               native_create_directory_value, 1U);
    (void)lang_register_native(vm, "NativeRenamePath",
                               native_rename_path_value, 2U);
    (void)lang_register_native(vm, "NativeRemoveFile",
                               native_remove_file_value, 1U);
    (void)lang_register_native(vm, "NativeRemoveDirectory",
                               native_remove_directory_value, 1U);
}
