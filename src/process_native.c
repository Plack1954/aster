#include "lang/lang.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

typedef struct ProcessStringList {
    char **items;
    size_t count;
    size_t capacity;
} ProcessStringList;

typedef struct NativeProcess {
    char *executable;
    char *working_directory;
    ProcessStringList arguments;
    ProcessStringList environment_names;
    ProcessStringList environment_values;
    bool redirect_stdin;
    bool redirect_stdout;
    bool redirect_stderr;
    bool launched;
    bool exited;
    int exit_code;
#if !defined(_WIN32)
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
#endif
} NativeProcess;

static char *process_copy_text(LangStringView text) {
    if (text.length != 0U && text.data == NULL) return NULL;
    if (text.length != 0U &&
        memchr(text.data, '\0', text.length) != NULL)
        return NULL;
    char *copy = malloc(text.length + 1U);
    if (copy == NULL) return NULL;
    if (text.length != 0U) memcpy(copy, text.data, text.length);
    copy[text.length] = '\0';
    return copy;
}

static bool process_list_append(ProcessStringList *list, char *value) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0U ? 8U : list->capacity * 2U;
        if (capacity < list->capacity) return false;
        char **items = realloc(list->items, capacity * sizeof(*items));
        if (items == NULL) return false;
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = value;
    return true;
}

static void process_list_drop(ProcessStringList *list) {
    for (size_t i = 0U; i < list->count; ++i) free(list->items[i]);
    free(list->items);
}

static void process_close_fd(int *fd) {
#if !defined(_WIN32)
    if (*fd >= 0) (void)close(*fd);
#endif
    *fd = -1;
}

static void native_process_drop(void *data) {
    NativeProcess *process = data;
    if (process == NULL) return;
#if !defined(_WIN32)
    if (process->launched && !process->exited) {
        int status = 0;
        pid_t waited = waitpid(process->pid, &status, WNOHANG);
        if (waited == 0) {
            (void)kill(process->pid, SIGTERM);
            while (waitpid(process->pid, &status, 0) < 0 && errno == EINTR) {}
        }
    }
    process_close_fd(&process->stdin_fd);
    process_close_fd(&process->stdout_fd);
    process_close_fd(&process->stderr_fd);
#endif
    free(process->executable);
    free(process->working_directory);
    process_list_drop(&process->arguments);
    process_list_drop(&process->environment_names);
    process_list_drop(&process->environment_values);
    free(process);
}

static LangNativeResult process_error(LangVM *vm, const char *message) {
    LangValue text;
    LangValue result;
    if (!lang_string_value(vm, (LangStringView){message, strlen(message)},
                           &text))
        return lang_native_result_error("could not allocate process error");
    if (!lang_result_err_value(vm, text, &result)) {
        lang_value_drop(vm, &text);
        return lang_native_result_error("could not allocate process Result");
    }
    return (LangNativeResult){true, result, NULL};
}

static LangNativeResult process_errno(LangVM *vm, const char *operation) {
    char message[512];
    (void)snprintf(message, sizeof(message), "%s: %s", operation,
                   strerror(errno));
    return process_error(vm, message);
}

static LangNativeResult process_success(LangVM *vm, LangValue payload) {
    LangValue result;
    if (!lang_result_ok_value(vm, payload, &result)) {
        lang_value_drop(vm, &payload);
        return lang_native_result_error("could not allocate process Result");
    }
    return (LangNativeResult){true, result, NULL};
}

static NativeProcess *process_from(const LangValue *args, size_t count) {
    return count >= 1U ? lang_native_handle_data(&args[0]) : NULL;
}

static LangNativeResult native_process_create(
    LangVM *vm, const LangValue *args, size_t count
) {
    LangStringView executable;
    LangStringView working_directory;
    if (count != 5U || !lang_value_string_view(&args[0], &executable) ||
        !lang_value_string_view(&args[1], &working_directory) ||
        args[2].tag != LANG_VALUE_BOOL || args[3].tag != LANG_VALUE_BOOL ||
        args[4].tag != LANG_VALUE_BOOL || executable.length == 0U)
        return lang_native_result_error("NativeProcessCreate invalid arguments");
    NativeProcess *process = calloc(1U, sizeof(*process));
    if (process == NULL) return process_error(vm, "could not allocate process");
#if !defined(_WIN32)
    process->stdin_fd = -1;
    process->stdout_fd = -1;
    process->stderr_fd = -1;
#endif
    process->executable = process_copy_text(executable);
    process->working_directory = process_copy_text(working_directory);
    process->redirect_stdin = args[2].as.boolean;
    process->redirect_stdout = args[3].as.boolean;
    process->redirect_stderr = args[4].as.boolean;
    if (process->executable == NULL || process->working_directory == NULL) {
        native_process_drop(process);
        return process_error(vm, "invalid or unallocatable process path");
    }
    LangValue handle;
    if (!lang_native_handle_value(vm, process, native_process_drop, &handle)) {
        native_process_drop(process);
        return process_error(vm, "could not allocate process handle");
    }
    return process_success(vm, handle);
}

static LangNativeResult native_process_add_argument(
    LangVM *vm, const LangValue *args, size_t count
) {
    NativeProcess *process = process_from(args, count);
    LangStringView argument;
    if (count != 2U || process == NULL || process->launched ||
        !lang_value_string_view(&args[1], &argument))
        return process_error(vm, "invalid process argument append");
    char *copy = process_copy_text(argument);
    if (copy == NULL || !process_list_append(&process->arguments, copy)) {
        free(copy);
        return process_error(vm, "could not allocate process argument");
    }
    return process_success(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult native_process_set_environment(
    LangVM *vm, const LangValue *args, size_t count
) {
    NativeProcess *process = process_from(args, count);
    LangStringView name;
    LangStringView value;
    if (count != 3U || process == NULL || process->launched ||
        !lang_value_string_view(&args[1], &name) ||
        !lang_value_string_view(&args[2], &value) || name.length == 0U ||
        memchr(name.data, '=', name.length) != NULL)
        return process_error(vm, "invalid process environment override");
    char *name_copy = process_copy_text(name);
    char *value_copy = process_copy_text(value);
    if (name_copy == NULL || value_copy == NULL) {
        free(name_copy);
        free(value_copy);
        return process_error(vm, "could not allocate environment override");
    }
    if (!process_list_append(&process->environment_names, name_copy)) {
        free(name_copy);
        free(value_copy);
        return process_error(vm, "could not allocate environment override");
    }
    if (!process_list_append(&process->environment_values, value_copy)) {
        --process->environment_names.count;
        free(name_copy);
        free(value_copy);
        return process_error(vm, "could not allocate environment override");
    }
    return process_success(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

#if !defined(_WIN32)
static void process_pipe_close_pair(int pipe_fds[2]) {
    process_close_fd(&pipe_fds[0]);
    process_close_fd(&pipe_fds[1]);
}

static LangNativeResult native_process_launch(
    LangVM *vm, const LangValue *args, size_t count
) {
    NativeProcess *process = process_from(args, count);
    if (count != 1U || process == NULL || process->launched)
        return process_error(vm, "invalid process launch");
    int input[2] = {-1, -1};
    int output[2] = {-1, -1};
    int error_output[2] = {-1, -1};
    int launch_error[2] = {-1, -1};
    if ((process->redirect_stdin && pipe(input) != 0) ||
        (process->redirect_stdout && pipe(output) != 0) ||
        (process->redirect_stderr && pipe(error_output) != 0) ||
        pipe(launch_error) != 0) {
        int saved = errno;
        process_pipe_close_pair(input);
        process_pipe_close_pair(output);
        process_pipe_close_pair(error_output);
        process_pipe_close_pair(launch_error);
        errno = saved;
        return process_errno(vm, "could not create process pipes");
    }
    (void)fcntl(launch_error[1], F_SETFD, FD_CLOEXEC);
    char **argv = calloc(process->arguments.count + 2U, sizeof(*argv));
    if (argv == NULL) {
        process_pipe_close_pair(input);
        process_pipe_close_pair(output);
        process_pipe_close_pair(error_output);
        process_pipe_close_pair(launch_error);
        return process_error(vm, "could not allocate process argument vector");
    }
    argv[0] = process->executable;
    for (size_t i = 0U; i < process->arguments.count; ++i)
        argv[i + 1U] = process->arguments.items[i];
    (void)signal(SIGPIPE, SIG_IGN);
    pid_t pid = fork();
    if (pid == 0) {
        (void)signal(SIGPIPE, SIG_DFL);
        process_close_fd(&launch_error[0]);
        if (process->redirect_stdin && dup2(input[0], STDIN_FILENO) < 0)
            goto child_error;
        if (process->redirect_stdout && dup2(output[1], STDOUT_FILENO) < 0)
            goto child_error;
        if (process->redirect_stderr && dup2(error_output[1], STDERR_FILENO) < 0)
            goto child_error;
        process_pipe_close_pair(input);
        process_pipe_close_pair(output);
        process_pipe_close_pair(error_output);
        if (process->working_directory[0] != '\0' &&
            chdir(process->working_directory) != 0)
            goto child_error;
        for (size_t i = 0U; i < process->environment_names.count; ++i)
            if (setenv(process->environment_names.items[i],
                       process->environment_values.items[i], 1) != 0)
                goto child_error;
        execvp(process->executable, argv);
child_error: {
            int child_errno = errno;
            const unsigned char *bytes =
                (const unsigned char *)&child_errno;
            size_t remaining = sizeof(child_errno);
            while (remaining != 0U) {
                ssize_t written = write(launch_error[1], bytes, remaining);
                if (written > 0) {
                    bytes += (size_t)written;
                    remaining -= (size_t)written;
                } else if (written < 0 && errno == EINTR) {
                    continue;
                } else {
                    break;
                }
            }
            _exit(127);
        }
    }
    free(argv);
    if (pid < 0) {
        int saved = errno;
        process_pipe_close_pair(input);
        process_pipe_close_pair(output);
        process_pipe_close_pair(error_output);
        process_pipe_close_pair(launch_error);
        errno = saved;
        return process_errno(vm, "could not fork process");
    }
    process_close_fd(&launch_error[1]);
    process_close_fd(&input[0]);
    process_close_fd(&output[1]);
    process_close_fd(&error_output[1]);
    int child_errno = 0;
    ssize_t read_count;
    do {
        read_count = read(launch_error[0], &child_errno, sizeof(child_errno));
    } while (read_count < 0 && errno == EINTR);
    process_close_fd(&launch_error[0]);
    if (read_count > 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        process_pipe_close_pair(input);
        process_pipe_close_pair(output);
        process_pipe_close_pair(error_output);
        errno = child_errno;
        return process_errno(vm, "could not execute process");
    }
    process->pid = pid;
    process->stdin_fd = input[1];
    process->stdout_fd = output[0];
    process->stderr_fd = error_output[0];
    process->launched = true;
    return process_success(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static void process_record_status(NativeProcess *process, int status) {
    process->exited = true;
    if (WIFEXITED(status)) process->exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) process->exit_code = 128 + WTERMSIG(status);
    else process->exit_code = 1;
}

static LangNativeResult native_process_wait(
    LangVM *vm, const LangValue *args, size_t count
) {
    NativeProcess *process = process_from(args, count);
    if (count != 1U || process == NULL || !process->launched)
        return process_error(vm, "process has not been launched");
    if (!process->exited) {
        int status = 0;
        pid_t waited;
        do { waited = waitpid(process->pid, &status, 0); }
        while (waited < 0 && errno == EINTR);
        if (waited < 0) return process_errno(vm, "could not wait for process");
        process_record_status(process, status);
    }
    return process_success(vm, (LangValue){
        .tag=LANG_VALUE_I64, .as.i64=process->exit_code});
}

static LangNativeResult native_process_has_exited(
    LangVM *vm, const LangValue *args, size_t count
) {
    NativeProcess *process = process_from(args, count);
    if (count != 1U || process == NULL || !process->launched)
        return process_error(vm, "process has not been launched");
    if (!process->exited) {
        int status = 0;
        pid_t waited = waitpid(process->pid, &status, WNOHANG);
        if (waited < 0) return process_errno(vm, "could not query process");
        if (waited == process->pid) process_record_status(process, status);
    }
    return process_success(vm, (LangValue){
        .tag=LANG_VALUE_BOOL, .as.boolean=process->exited});
}

static LangNativeResult native_process_exit_code(
    LangVM *vm, const LangValue *args, size_t count
) {
    NativeProcess *process = process_from(args, count);
    if (count != 1U || process == NULL || !process->launched)
        return process_error(vm, "process has not been launched");
    if (!process->exited) {
        int status = 0;
        pid_t waited = waitpid(process->pid, &status, WNOHANG);
        if (waited < 0) return process_errno(vm, "could not query process");
        if (waited == 0)
            return process_error(vm, "process has not exited");
        process_record_status(process, status);
    }
    return process_success(vm, (LangValue){
        .tag=LANG_VALUE_I64, .as.i64=process->exit_code});
}

static LangNativeResult process_io(
    LangVM *vm, const LangValue *args, size_t count, bool write_input,
    bool read_error
) {
    NativeProcess *process = process_from(args, count);
    LangByteSlice bytes;
    if (count != 2U || process == NULL || !process->launched ||
        !lang_value_byte_slice(&args[1], &bytes))
        return process_error(vm, "invalid process I/O operation");
    int fd = write_input ? process->stdin_fd
        : read_error ? process->stderr_fd : process->stdout_fd;
    if (fd < 0) return process_error(vm, "process stream is not redirected");
    ssize_t transferred;
    do {
        transferred = write_input
            ? write(fd, bytes.data, bytes.length)
            : read(fd, bytes.data, bytes.length);
    } while (transferred < 0 && errno == EINTR);
    if (transferred < 0) return process_errno(vm, "process I/O failed");
    return process_success(vm, (LangValue){
        .tag=LANG_VALUE_U64, .as.u64=(uint64_t)transferred});
}

static LangNativeResult native_process_write_input(
    LangVM *vm, const LangValue *args, size_t count
) { return process_io(vm, args, count, true, false); }
static LangNativeResult native_process_read_output(
    LangVM *vm, const LangValue *args, size_t count
) { return process_io(vm, args, count, false, false); }
static LangNativeResult native_process_read_error(
    LangVM *vm, const LangValue *args, size_t count
) { return process_io(vm, args, count, false, true); }

static LangNativeResult native_process_close_input(
    LangVM *vm, const LangValue *args, size_t count
) {
    NativeProcess *process = process_from(args, count);
    if (count != 1U || process == NULL)
        return process_error(vm, "invalid process input close");
    process_close_fd(&process->stdin_fd);
    return process_success(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}

static LangNativeResult native_process_kill(
    LangVM *vm, const LangValue *args, size_t count
) {
    NativeProcess *process = process_from(args, count);
    if (count != 1U || process == NULL || !process->launched)
        return process_error(vm, "process has not been launched");
    if (!process->exited && kill(process->pid, SIGTERM) != 0 && errno != ESRCH)
        return process_errno(vm, "could not terminate process");
    return process_success(vm, (LangValue){.tag=LANG_VALUE_UNIT});
}
#else
static LangNativeResult process_unavailable(
    LangVM *vm, const LangValue *args, size_t count
) {
    (void)args;
    (void)count;
    return process_error(vm, "process spawning is not implemented on Windows");
}
#endif

void lang_register_process_spawn_natives(LangVM *vm) {
    (void)lang_register_native(vm, "NativeProcessCreate",
                               native_process_create, 5U);
    (void)lang_register_native(vm, "NativeProcessAddArgument",
                               native_process_add_argument, 2U);
    (void)lang_register_native(vm, "NativeProcessSetEnvironment",
                               native_process_set_environment, 3U);
#if !defined(_WIN32)
    (void)lang_register_native(vm, "NativeProcessLaunch",
                               native_process_launch, 1U);
    (void)lang_register_native(vm, "NativeProcessWait",
                               native_process_wait, 1U);
    (void)lang_register_native(vm, "NativeProcessHasExited",
                               native_process_has_exited, 1U);
    (void)lang_register_native(vm, "NativeProcessExitCode",
                               native_process_exit_code, 1U);
    (void)lang_register_native(vm, "NativeProcessWriteInput",
                               native_process_write_input, 2U);
    (void)lang_register_native(vm, "NativeProcessReadOutput",
                               native_process_read_output, 2U);
    (void)lang_register_native(vm, "NativeProcessReadError",
                               native_process_read_error, 2U);
    (void)lang_register_native(vm, "NativeProcessCloseInput",
                               native_process_close_input, 1U);
    (void)lang_register_native(vm, "NativeProcessKill",
                               native_process_kill, 1U);
#else
    (void)lang_register_native(vm, "NativeProcessLaunch", process_unavailable, 1U);
    (void)lang_register_native(vm, "NativeProcessWait", process_unavailable, 1U);
    (void)lang_register_native(vm, "NativeProcessHasExited", process_unavailable, 1U);
    (void)lang_register_native(vm, "NativeProcessExitCode", process_unavailable, 1U);
    (void)lang_register_native(vm, "NativeProcessWriteInput", process_unavailable, 2U);
    (void)lang_register_native(vm, "NativeProcessReadOutput", process_unavailable, 2U);
    (void)lang_register_native(vm, "NativeProcessReadError", process_unavailable, 2U);
    (void)lang_register_native(vm, "NativeProcessCloseInput", process_unavailable, 1U);
    (void)lang_register_native(vm, "NativeProcessKill", process_unavailable, 1U);
#endif
}
