#ifndef LANG_LANG_H
#define LANG_LANG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct LangSpan {
    const char *file; /* Borrowed; owned by LangSource. */
    size_t start;
    size_t end;
} LangSpan;

typedef struct LangSourceSegment {
    size_t start;
    size_t end;
    char *path; /* Owned by LangSource. */
} LangSourceSegment;

typedef struct LangSource {
    char *text;       /* Owned. */
    size_t length;
    char *path;       /* Owned. */
    LangSourceSegment *segments; /* Owned import-origin map. */
    size_t segment_count;
} LangSource;

typedef enum LangSeverity {
    LANG_DIAG_ERROR,
    LANG_DIAG_WARNING,
    LANG_DIAG_NOTE
} LangSeverity;

typedef enum LangEndianness {
    LANG_ENDIAN_LITTLE,
    LANG_ENDIAN_BIG
} LangEndianness;

typedef struct LangTargetInfo {
    uint8_t pointer_size;
    uint8_t pointer_alignment;
    uint8_t enum_tag_size;
    uint8_t enum_tag_alignment;
    LangEndianness endianness;
    bool c_abi_supported;
} LangTargetInfo;

typedef struct LangSecondarySpan {
    LangSpan span;
    char label[128];
} LangSecondarySpan;

typedef struct LangDiagnostic {
    LangSeverity severity;
    LangSpan span;
    char message[256];
    LangSecondarySpan secondary[4];
    size_t secondary_count;
    char notes[4][256];
    size_t note_count;
    char help[256];
    bool has_help;
} LangDiagnostic;

typedef struct LangDiagnostics {
    LangDiagnostic *items; /* Owned. */
    size_t count;
    size_t capacity;
} LangDiagnostics;

typedef enum LangValueTag {
    LANG_VALUE_UNIT,
    LANG_VALUE_BOOL,
    LANG_VALUE_I64,
    LANG_VALUE_U64,
    LANG_VALUE_F64,
    LANG_VALUE_STRING_VIEW,
    LANG_VALUE_BYTE_SLICE,
    LANG_VALUE_OBJECT,
    LANG_VALUE_RAW_POINTER,
    LANG_VALUE_FUNCTION,
    LANG_VALUE_BOUND_FUNCTION,
    /* Reserved for diagnostics owned by a failed LangNativeResult. */
    LANG_VALUE_NATIVE_ERROR
} LangValueTag;

typedef struct LangStringView {
    /* Borrowed for the duration of a native call.  data may be NULL only
     * when length is zero. */
    const char *data;
    size_t length;
} LangStringView;

typedef struct LangByteSlice {
    /* Borrowed for the native call.  data may be NULL only when length is
     * zero. */
    uint8_t *data;
    size_t length;
} LangByteSlice;

typedef struct LangValue {
    LangValueTag tag;
    union {
        bool boolean;
        int64_t i64;
        uint64_t u64;
        double f64;
        LangStringView string;
        LangByteSlice bytes;
        void *object;
        void *pointer;
        size_t function;
        struct {
            size_t function;
            void *receiver;
        } bound_function;
    } as;
} LangValue;

typedef struct LangVM LangVM;

typedef struct LangNativeResult {
    bool ok;
    LangValue value;
    /* Optional static-lifetime diagnostic; dynamic text uses the constructor. */
    const char *error;
} LangNativeResult;

/*
 * `vm` and `args` are borrowed for this call. The callback must not retain
 * either pointer or any borrowed view reachable from `args`. On success,
 * `value` transfers to the VM or embedding caller. On failure, return
 * `lang_native_result_error`; it copies even stack-backed messages into the
 * result before the callback returns.
 */
typedef LangNativeResult (*LangNativeFn)(
    LangVM *vm,
    const LangValue *args,
    size_t arg_count
);

typedef void (*LangNativeHandleDropFn)(void *handle);
typedef void (*LangNativeRegistrar)(LangVM *vm);

/* `path` is borrowed. On success, `out_source` owns all loaded storage. */
bool lang_source_load(const char *path, LangSource *out_source);
/* Releases owned storage and clears `source`. */
void lang_source_free(LangSource *source);
/*
 * Overrides standard-library discovery for subsequent compilation requests.
 * The path is copied. Passing NULL clears the override.
 */
void lang_set_stdlib_path(const char *path);
/* Supplies an executable-path hint for platforms without self-discovery. */
void lang_set_executable_path(const char *path);
/* Initializes caller-owned diagnostic storage. */
void lang_diagnostics_init(LangDiagnostics *diagnostics);
/* Releases diagnostic storage and clears `diagnostics`. */
void lang_diagnostics_free(LangDiagnostics *diagnostics);
/* All arguments are borrowed; `stream` remains caller-owned. */
void lang_diagnostics_print(const LangSource *source,
                            const LangDiagnostics *diagnostics, FILE *stream);

/* `path` and `dump_kind` are borrowed. Returns a process-style status code. */
int lang_run_file(const char *path, bool check_only, const char *dump_kind);
/* Emits external-CSS generated C to stdout and one hashed CSS asset. */
int lang_emit_c_site_file(const char *path, const char *css_directory);
/*
 * As `lang_run_file`, with borrowed application arguments exposed through
 * `std::process` for the duration of execution.
 */
int lang_run_file_args(const char *path, bool check_only,
                       const char *dump_kind, size_t argument_count,
                       const char *const *arguments);
/*
 * Loads `manifest_path`, resolves `target_name` (or the manifest default when
 * NULL), and checks or runs it. All string arguments are borrowed.
 */
int lang_project_run(const char *manifest_path, const char *target_name,
                     bool check_only);
/*
 * Runs a manifest binary/test target through the verified typed IR backend.
 * `manifest_path` and `target_name` are borrowed.
 */
int lang_project_run_ir(const char *manifest_path, const char *target_name);
/* Emits a complete manifest target through a native backend. */
int lang_project_emit_c(const char *manifest_path,
                        const char *target_name);
int lang_project_emit_c_site(const char *manifest_path,
                             const char *target_name,
                             const char *css_directory);
int lang_project_build_web(const char *manifest_path,
                           const char *output_directory,
                           const char *target_name);
/* Runs an SSG binary target with `output_directory` as its sole argument. */
int lang_project_build_site(const char *manifest_path,
                            const char *output_directory,
                            const char *target_name);
/* Checks and executes every manifest target whose kind is `test`. */
int lang_project_test(const char *manifest_path);
/* `name`, `text`, and `dump_kind` are borrowed for the complete call. */
int lang_run_text(const char *name, const char *text, bool check_only,
                  const char *dump_kind);
/* `path` is borrowed; all benchmark-owned compiler data is released. */
int lang_benchmark_file(const char *path, size_t iterations);
/* Writes the host data-layout description used by the current VM/FFI target. */
void lang_target_host(LangTargetInfo *out_target);

/* Returns a caller-owned VM, or NULL on recoverable allocation failure. */
LangVM *lang_vm_new(void);
/* Releases the VM and its registry/tracking data; accepts NULL. */
void lang_vm_free(LangVM *vm);
/*
 * `vm` is borrowed. `name` is copied. `callback` must remain valid until the
 * VM is freed. Duplicate names return false.
 */
bool lang_register_native(LangVM *vm, const char *name, LangNativeFn callback,
                          size_t arity);
/* Returns a failed result owning a copy of `message`. */
LangNativeResult lang_native_result_error(const char *message);
/* Returns the result-owned or static failure diagnostic, or NULL. */
const char *lang_native_result_error_message(const LangNativeResult *result);
/* Releases an owned failure diagnostic and clears the result; accepts NULL. */
void lang_native_result_drop(LangNativeResult *result);
/*
 * `vm`, `name`, and `args` are borrowed; arguments are not consumed. On a
 * successful callback, `out_result->value` is caller-owned and must eventually
 * be passed to `lang_value_drop` when it owns storage. The complete result,
 * including a failure diagnostic, is caller-owned and may outlive the call.
 * Failed results must be passed to `lang_native_result_drop` after inspection.
 */
bool lang_vm_call_native(LangVM *vm, const char *name, const LangValue *args,
                         size_t arg_count, LangNativeResult *out_result);
/*
 * On success, transfers caller's opaque `handle` to `out_value` and retains
 * `destructor` for exactly-once cleanup. On failure, caller still owns it.
 */
bool lang_native_handle_value(LangVM *vm, void *handle,
                              LangNativeHandleDropFn destructor,
                              LangValue *out_value);
/* Borrowed payload; valid only while the native-handle `value` remains live. */
void *lang_native_handle_data(const LangValue *value);
/* Consumes and clears an embedding-owned value; `vm` is borrowed. */
void lang_value_drop(LangVM *vm, LangValue *value);
/* Copies a value. Resource handles share their underlying resource. */
bool lang_value_copy(LangVM *vm, const LangValue *source,
                     LangValue *out_value);
/* Returned view is borrowed and valid only while `value` remains live. */
bool lang_value_string_view(const LangValue *value, LangStringView *out_view);
bool lang_value_html_view(const LangValue *value, LangStringView *out_view);
/* Copies borrowed `source`; `out_value` owns the resulting String. */
bool lang_string_value(LangVM *vm, LangStringView source,
                       LangValue *out_value);
/* Returned mutable view is call-scoped and does not extend Buffer lifetime. */
bool lang_value_byte_slice(const LangValue *value, LangByteSlice *out_slice);
/* Takes ownership of `payload` when it returns true. */
bool lang_result_ok_value(LangVM *vm, LangValue payload,
                          LangValue *out_value);
/* Takes ownership of `payload` when it returns true. */
bool lang_result_err_value(LangVM *vm, LangValue payload,
                           LangValue *out_value);
/*
 * Consumes a Result value, transferring its single payload to `out_payload`.
 * `out_is_ok` is true for Result::Ok and false for Result::Err.
 */
bool lang_result_take(LangVM *vm, LangValue *result, bool *out_is_ok,
                      LangValue *out_payload);
/* Registers the standard file, directory, process, HTTP, and SQLite natives. */
void lang_vm_register_builtins(LangVM *vm);
/* `vm` is borrowed; registered names are copied into its registry. */
void lang_register_http_natives(LangVM *vm);
/* Installs an optional HTTP-client registrar for subsequently initialized VMs. */
void lang_configure_http_client_registrar(LangNativeRegistrar registrar);
/* Registers the configured optional HTTP client into one VM. */
void lang_register_configured_http_client_natives(LangVM *vm);
/* Registrar supplied by the separately linked libcurl component. */
void lang_register_http_client_natives(LangVM *vm);
void lang_configure_crypto_registrar(LangNativeRegistrar registrar);
void lang_register_configured_crypto_natives(LangVM *vm);
void lang_register_crypto_natives(LangVM *vm);
void lang_register_h2o_natives(LangVM *vm);
/* Registers the optional SQLite adapter, or typed unavailable stubs. */
void lang_register_sqlite_natives(LangVM *vm);
void lang_register_process_spawn_natives(LangVM *vm);

/* Emits the reusable runtime used with ASTER_EXTERNAL_RUNTIME C output. */
bool lang_c_emit_runtime(FILE *output);

#endif
