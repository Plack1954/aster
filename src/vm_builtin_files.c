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

typedef struct NativeFile {
    FILE *stream;
} NativeFile;

typedef struct NativeTemporaryFile {
    char *path;
} NativeTemporaryFile;

static void native_file_drop(void *handle) {
    NativeFile *file = handle;
    if (file->stream != NULL) (void)fclose(file->stream);
    free(file);
}

static void native_temporary_file_drop(void *handle) {
    NativeTemporaryFile *file = handle;
    if (file->path != NULL) {
        (void)remove(file->path);
        free(file->path);
    }
    free(file);
}

static LangNativeResult native_file_open_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView path;
    LangStringView mode;
    if (arg_count != 2U ||
        !lang_value_string_view(&args[0], &path) ||
        !lang_value_string_view(&args[1], &mode))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_open expects `(string, string)`"
        };
    bool valid_mode =
        (mode.length == 2U &&
         (memcmp(mode.data, "rb", 2U) == 0 ||
          memcmp(mode.data, "wb", 2U) == 0 ||
          memcmp(mode.data, "ab", 2U) == 0)) ||
        (mode.length == 3U &&
         memcmp(mode.data, "w+b", 3U) == 0);
    if (!valid_mode)
        return native_result_error(vm, "invalid file mode");
    if (path.length == SIZE_MAX)
        return native_result_error(vm, "file path is too long");
    char *path_c = vm_allocate(path.length + 1U, 1U);
    if (path.length != 0U)
        memcpy(path_c, path.data, path.length);
    path_c[path.length] = '\0';
    char mode_c[4] = {0};
    memcpy(mode_c, mode.data, mode.length);
    FILE *stream = fopen(path_c, mode_c);
    free(path_c);
    if (stream == NULL)
        return native_result_error(vm, "could not open file");
    NativeFile *file = vm_allocate(1U, sizeof(*file));
    file->stream = stream;
    LangValue handle;
    if (!lang_native_handle_value(
            vm, file, native_file_drop, &handle)) {
        native_file_drop(file);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not wrap file handle"
        };
    }
    LangValue tagged;
    if (!lang_result_ok_value(vm, handle, &tagged)) {
        lang_value_drop(vm, &handle);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct file Result"
        };
    }
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_file_create_temporary_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    char directory[4096];
    if (arg_count != 1U ||
        !native_path_string(&args[0], directory, sizeof(directory)))
        return native_result_error(
            vm, "temporary-file directory is invalid");

    size_t directory_length = strlen(directory);
    bool has_separator = native_path_separator(
        (unsigned char)directory[directory_length - 1U]);
    char path[4096];
    for (unsigned attempt = 0U; attempt < 128U; ++attempt) {
        uint64_t token = (uint64_t)time(NULL) ^
#if defined(_WIN32)
            ((uint64_t)(unsigned)_getpid() << 32U) ^
#else
            ((uint64_t)(unsigned)getpid() << 32U) ^
#endif
            ((uint64_t)attempt * UINT64_C(0x9e3779b97f4a7c15));
        int length = snprintf(
            path, sizeof(path), "%s%s.aster-upload-%016" PRIx64 ".tmp",
#if defined(_WIN32)
            directory, has_separator ? "" : "\\", token);
#else
            directory, has_separator ? "" : "/", token);
#endif
        if (length <= 0 || (size_t)length >= sizeof(path))
            return native_result_error(
                vm, "temporary-file path is too long");
#if defined(_WIN32)
        int descriptor = _open(
            path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
            _S_IREAD | _S_IWRITE);
        if (descriptor >= 0) (void)_close(descriptor);
#else
        int descriptor = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (descriptor >= 0) (void)close(descriptor);
#endif
        if (descriptor < 0) {
            if (errno == EEXIST) continue;
            return native_result_error(
                vm, "could not create temporary file");
        }

        NativeTemporaryFile *file = vm_allocate(1U, sizeof(*file));
        file->path = vm_allocate((size_t)length + 1U, 1U);
        memcpy(file->path, path, (size_t)length + 1U);
        LangValue handle;
        if (!lang_native_handle_value(
                vm, file, native_temporary_file_drop, &handle)) {
            native_temporary_file_drop(file);
            return (LangNativeResult){
                false, {.tag=LANG_VALUE_UNIT},
                "could not wrap temporary-file handle"
            };
        }
        LangValue tagged;
        if (!lang_result_ok_value(vm, handle, &tagged)) {
            lang_value_drop(vm, &handle);
            return (LangNativeResult){
                false, {.tag=LANG_VALUE_UNIT},
                "could not construct temporary-file Result"
            };
        }
        return (LangNativeResult){true, tagged, NULL};
    }
    return native_result_error(
        vm, "could not allocate a unique temporary file");
}

static LangNativeResult native_file_temporary_path_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    NativeTemporaryFile *file = arg_count == 1U
        ? lang_native_handle_data(&args[0]) : NULL;
    if (file == NULL || file->path == NULL)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "temporary-file path expects one live handle"
        };
    return native_path_string_result(vm, file->path, strlen(file->path));
}

static LangNativeResult native_file_read_all_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    if (arg_count != 1U)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_read_all expects one file handle"
        };
    NativeFile *file = lang_native_handle_data(&args[0]);
    if (file == NULL || file->stream == NULL)
        return native_result_error(vm, "invalid file handle");
    if (fseek(file->stream, 0L, SEEK_END) != 0)
        return native_result_error(vm, "could not seek file");
    long end = ftell(file->stream);
    if (end < 0L || fseek(file->stream, 0L, SEEK_SET) != 0)
        return native_result_error(vm, "could not measure file");
    size_t length = (size_t)end;
    char *bytes = vm_allocate(length + 1U, 1U);
    size_t read = length == 0U
                ? 0U : fread(bytes, 1U, length, file->stream);
    if (read != length) {
        free(bytes);
        return native_result_error(vm, "could not read complete file");
    }
    bytes[length] = '\0';
    LangValue string;
    bool made_string = lang_string_value(
        vm, (LangStringView){bytes, length}, &string);
    free(bytes);
    if (!made_string)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not allocate file contents"
        };
    LangValue tagged;
    if (!lang_result_ok_value(vm, string, &tagged)) {
        lang_value_drop(vm, &string);
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct read Result"
        };
    }
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_file_write_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangStringView data;
    if (arg_count != 2U ||
        !lang_value_string_view(&args[1], &data))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_write expects `(file, string)`"
        };
    NativeFile *file = lang_native_handle_data(&args[0]);
    if (file == NULL || file->stream == NULL)
        return native_result_error(vm, "invalid file handle");
    size_t written = data.length == 0U
                   ? 0U : fwrite(data.data, 1U, data.length, file->stream);
    if (written != data.length || fflush(file->stream) != 0)
        return native_result_error(vm, "could not write complete file");
    LangValue count = {
        .tag=LANG_VALUE_U64, .as.u64=(uint64_t)written
    };
    LangValue tagged;
    if (!lang_result_ok_value(vm, count, &tagged))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct write Result"
        };
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_file_read_bytes_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangByteSlice bytes;
    if (arg_count != 2U ||
        !lang_value_byte_slice(&args[1], &bytes))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_read_into expects `(file, Span<u8>)`"
        };
    NativeFile *file = lang_native_handle_data(&args[0]);
    if (file == NULL || file->stream == NULL)
        return native_result_error(vm, "invalid file handle");
    size_t read = bytes.length == 0U
                ? 0U : fread(bytes.data, 1U, bytes.length, file->stream);
    if (read == 0U && ferror(file->stream) != 0)
        return native_result_error(vm, "could not read file bytes");
    LangValue count = {
        .tag=LANG_VALUE_U64, .as.u64=(uint64_t)read
    };
    LangValue tagged;
    if (!lang_result_ok_value(vm, count, &tagged))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct byte-read Result"
        };
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_file_write_bytes_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    LangByteSlice bytes;
    if (arg_count != 3U ||
        !lang_value_byte_slice(&args[1], &bytes) ||
        args[2].tag != LANG_VALUE_U64 ||
        args[2].as.u64 > (uint64_t)bytes.length)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_write_bytes expects `(file, Span<u8>, nuint)`"
        };
    NativeFile *file = lang_native_handle_data(&args[0]);
    if (file == NULL || file->stream == NULL)
        return native_result_error(vm, "invalid file handle");
    size_t requested = (size_t)args[2].as.u64;
    size_t written = 0U;
    while (written < requested) {
        size_t amount = fwrite(
            bytes.data + written, 1U, requested - written, file->stream);
        if (amount == 0U)
            return native_result_error(
                vm, "could not write complete file bytes");
        written += amount;
    }
    if (fflush(file->stream) != 0)
        return native_result_error(vm, "could not flush file bytes");
    LangValue count = {
        .tag=LANG_VALUE_U64, .as.u64=(uint64_t)written
    };
    LangValue tagged;
    if (!lang_result_ok_value(vm, count, &tagged))
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "could not construct byte-write Result"
        };
    return (LangNativeResult){true, tagged, NULL};
}

static LangNativeResult native_file_close_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    (void)vm;
    NativeFile *file = arg_count == 1U
        ? lang_native_handle_data(&args[0]) : NULL;
    if (file == NULL)
        return (LangNativeResult){
            false, {.tag=LANG_VALUE_UNIT},
            "native_file_close expects one file handle"
        };
    if (file->stream != NULL) {
        (void)fclose(file->stream);
        file->stream = NULL;
    }
    return (LangNativeResult){true, {.tag=LANG_VALUE_UNIT}, NULL};
}

static LangNativeResult native_file_seek_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    NativeFile *file = arg_count == 3U
        ? lang_native_handle_data(&args[0]) : NULL;
    if (file == NULL || args[1].tag != LANG_VALUE_I64 ||
        args[2].tag != LANG_VALUE_I64 || args[2].as.i64 < 0 ||
        args[2].as.i64 > 2)
        return native_result_error(vm, "invalid file seek");
    int origin = args[2].as.i64 == 0 ? SEEK_SET
               : args[2].as.i64 == 1 ? SEEK_CUR : SEEK_END;
    if (fseek(file->stream, (long)args[1].as.i64, origin) != 0)
        return native_result_error(vm, "could not seek file");
    long position = ftell(file->stream);
    if (position < 0)
        return native_result_error(vm, "could not read file position");
    return native_result_i64(vm, (int64_t)position);
}

static LangNativeResult native_file_length_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    NativeFile *file = arg_count == 1U
        ? lang_native_handle_data(&args[0]) : NULL;
    if (file == NULL)
        return native_result_error(vm, "invalid file handle");
    long position = ftell(file->stream);
    if (position < 0 || fseek(file->stream, 0L, SEEK_END) != 0)
        return native_result_error(vm, "could not measure file");
    long length = ftell(file->stream);
    if (length < 0 || fseek(file->stream, position, SEEK_SET) != 0)
        return native_result_error(vm, "could not restore file position");
    return native_result_i64(vm, (int64_t)length);
}

static LangNativeResult native_file_flush_value(
    LangVM *vm, const LangValue *args, size_t arg_count) {
    NativeFile *file = arg_count == 1U
        ? lang_native_handle_data(&args[0]) : NULL;
    if (file == NULL)
        return native_result_error(vm, "invalid file handle");
    if (fflush(file->stream) != 0)
        return native_result_error(vm, "could not flush file");
    return native_result_unit(vm);
}


void vm_register_file_builtins(LangVM *vm) {
    (void)lang_register_native(vm, "NativeFileOpen",
                               native_file_open_value, 2U);
    (void)lang_register_native(vm, "NativeFileCreateTemporary",
                               native_file_create_temporary_value, 1U);
    (void)lang_register_native(vm, "NativeFileTemporaryPath",
                               native_file_temporary_path_value, 1U);
    (void)lang_register_native(vm, "NativeFileReadAll",
                               native_file_read_all_value, 1U);
    (void)lang_register_native(vm, "NativeFileWrite",
                               native_file_write_value, 2U);
    (void)lang_register_native(vm, "NativeFileReadInto",
                               native_file_read_bytes_value, 2U);
    (void)lang_register_native(vm, "NativeFileWriteBytes",
                               native_file_write_bytes_value, 3U);
    (void)lang_register_native(vm, "NativeFileSeek",
                               native_file_seek_value, 3U);
    (void)lang_register_native(vm, "NativeFileLength",
                               native_file_length_value, 1U);
    (void)lang_register_native(vm, "NativeFileFlush",
                               native_file_flush_value, 1U);
}

void vm_register_file_close_builtin(LangVM *vm) {
    (void)lang_register_native(vm, "NativeFileClose",
                               native_file_close_value, 1U);
}
