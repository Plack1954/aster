#ifndef ASTER_VM_BUILTINS_INTERNAL_H
#define ASTER_VM_BUILTINS_INTERNAL_H

#include "vm_internal.h"

LangNativeResult native_result_error(LangVM *vm, const char *message);
LangNativeResult native_result_i64(LangVM *vm, int64_t value);
LangNativeResult native_result_unit(LangVM *vm);
bool native_path_string(
    const LangValue *value, char *output, size_t output_capacity);
bool native_path_separator(unsigned char value);
LangNativeResult native_path_string_result(
    LangVM *vm, const char *data, size_t length);

void vm_register_core_builtins(LangVM *vm);
void vm_register_file_builtins(LangVM *vm);
void vm_register_byte_builtins(LangVM *vm);
void vm_register_file_close_builtin(LangVM *vm);
void vm_register_text_builtins(LangVM *vm);
void vm_register_unicode_builtins(LangVM *vm);
void vm_register_directory_builtins(LangVM *vm);
void vm_register_process_builtins(LangVM *vm);
void vm_register_path_builtins(LangVM *vm);

#endif
