#ifndef ASTER_C_BACKEND_INTERNAL_H
#define ASTER_C_BACKEND_INTERNAL_H

#include <stdbool.h>
#include <stdio.h>

#include "internal.h"

typedef struct CEmitter {
    const IrModule *ir;
    LangDiagnostics *diagnostics;
    FILE *output;
    bool *reachable_functions;
    bool *used_types;
    LangSpan render_root_span;
    bool render_into;
    bool render_direct;
    const char **direct_local_tags;
    size_t *direct_local_tag_lengths;
    bool needs_native_runtime;
    bool needs_http_client_runtime;
    bool needs_crypto_runtime;
    size_t async_function_index;
    size_t async_await_index;
    bool failed;
    char *css_asset_href;
} CEmitter;

void c_backend_unsupported(CEmitter *emitter, LangSpan span,
                           const char *what);
bool c_backend_function_has_render_root(const IrFunction *function);
bool c_backend_registry_native_symbol(const char *symbol);
bool c_backend_registry_native_call(const IrInstruction *instruction);
const IrInstruction *c_backend_find_direct_render_consumer(
    const IrFunction *function, IrValueId value);
void c_backend_mark_function(CEmitter *emitter,
                             IrFunctionId function_id);
bool c_backend_function_needs_normal_variant(
    const CEmitter *emitter, size_t function_index);
bool c_backend_function_is_entry_module_export(
    const IrModule *ir, size_t function_index, size_t entry);
void c_backend_emit_public_export_wrapper(
    CEmitter *emitter, size_t function_index);
void c_backend_emit_public_async_result_accessor(
    CEmitter *emitter, size_t function_index);
void c_backend_emit_public_aggregate_accessors(
    CEmitter *emitter, size_t function_index);
bool c_backend_web_exports_use_strings(
    const IrModule *ir, size_t entry);
bool c_backend_web_exports_use_tasks(
    const IrModule *ir, size_t entry);
void c_backend_emit_web_string_abi(FILE *output);
bool c_backend_web_exports_use_html_result(
    const IrModule *ir, size_t entry);
void c_backend_emit_web_html_abi(FILE *output);
void c_backend_emit_web_task_abi(FILE *output);
bool c_backend_function_needs_render_into_variant(
    const CEmitter *emitter, size_t function_index);

bool c_backend_type_is_vec(const IrType *type);
bool c_backend_type_is_queue(const IrType *type);
bool c_backend_type_is_dictionary(const IrType *type);
bool c_backend_type_is_native_handle(const IrType *type);
bool c_backend_type_is_buffer(const IrType *type);
bool c_backend_type_is_arena(const IrType *type);
bool c_backend_type_is_task(const IrType *type);
bool c_backend_type_is_cancellation(const IrType *type);
bool c_backend_type_clone_supported(const IrModule *ir,
                                    IrTypeId type_id);
bool c_backend_type_is_supported(const IrModule *ir,
                                 IrTypeId type_id);
void c_backend_emit_type(CEmitter *emitter, IrTypeId type_id);
bool c_backend_emit_aggregate_types(CEmitter *emitter);
bool c_backend_type_needs_drop(const CEmitter *emitter,
                               IrTypeId type_id);
bool c_backend_local_tracks_drop(const CEmitter *emitter,
                                 const IrFunction *function,
                                 uint32_t local);
void c_backend_emit_drop_call(CEmitter *emitter,
                              IrTypeId type_id,
                              const char *prefix, uint32_t index);
void c_backend_emit_virtual_cleanup(
    CEmitter *emitter, const IrFunction *function,
    IrValueId preserved, const char *indent, bool clear);
char *c_backend_emit_static_css_asset(CEmitter *emitter,
                                      const char *directory);
void c_backend_emit_byte_string(FILE *output,
                                const char *data, size_t length);
void c_backend_emit_instruction(CEmitter *emitter,
                                const IrFunction *function,
                                const IrInstruction *instruction);
void c_backend_emit_terminator(CEmitter *emitter,
                               const IrFunction *function,
                               const IrTerminator *terminator);
bool c_backend_function_supports_direct_render(
    const IrModule *ir, size_t function_index);
const IrInstruction *c_backend_find_value_producer(
    const IrFunction *function, IrValueId value);
size_t c_backend_direct_render_initial_capacity(
    const IrModule *ir, size_t function_index);
bool c_backend_emit_direct_html_value(
    CEmitter *emitter, const IrFunction *function,
    const IrType *type, IrValueId value,
    bool attribute, bool raw_text);
const IrInstruction *c_backend_find_element_append_consumer(
    const IrFunction *function, IrValueId value);
bool c_backend_html_tag_is_void(const char *tag, size_t length);
bool c_backend_html_tag_is_fragment(const char *tag, size_t length);
bool c_backend_local_element_is_raw_text(
    const IrFunction *function, uint32_t local);
bool c_backend_value_is_borrowed_projection(
    const IrFunction *function, IrValueId value);
size_t c_backend_html_escaped_length(
    const char *data, size_t length, bool attribute);
void c_backend_emit_html_escaped_byte_string(
    FILE *output, const char *data, size_t length, bool attribute);
bool c_backend_emit_html_property_value(
    CEmitter *emitter, const IrInstruction *instruction,
    const IrType *type, const char *expression,
    const char *indent);
bool c_backend_emit_html_interpolation_value(
    CEmitter *emitter, const IrFunction *function,
    const IrType *type, uint32_t local, IrValueId value,
    bool attribute, bool css_value);
void c_backend_emit_direct_builder_literal(
    FILE *output, const char *data, size_t length);
void c_backend_emit_direct_close_open(CEmitter *emitter,
                                      uint32_t local);

bool c_backend_async_function_supported(
    CEmitter *emitter, const IrFunction *function);
void c_backend_emit_async_runtime(FILE *output);
void c_backend_emit_async_result_helpers(CEmitter *emitter);
void c_backend_emit_async_combinator_helpers(CEmitter *emitter);
void c_backend_emit_async_frame_declaration(
    CEmitter *emitter, size_t function_index);
void c_backend_emit_async_step_prototype(
    CEmitter *emitter, size_t function_index);
void c_backend_emit_async_function(
    CEmitter *emitter, size_t function_index);
void c_backend_emit_async_await(
    CEmitter *emitter, const IrFunction *function,
    const IrInstruction *instruction);
void c_backend_emit_async_terminator(
    CEmitter *emitter, const IrFunction *function,
    const IrTerminator *terminator);

void c_backend_emit_prelude(FILE *output, bool native_runtime);
void c_backend_emit_html_prelude(FILE *output,
                                 const char *css_asset_href);

#endif
