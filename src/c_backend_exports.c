#include "c_backend_internal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

void c_backend_mark_function(CEmitter *emitter, IrFunctionId function_id);

static void mark_c_type(CEmitter *emitter, IrTypeId type_id) {
    if (type_id == IR_INVALID_ID ||
        type_id >= emitter->ir->type_count ||
        emitter->used_types[type_id])
        return;
    emitter->used_types[type_id] = true;
    const IrType *type = &emitter->ir->types[type_id];
    if (c_backend_type_is_native_handle(type))
        emitter->needs_native_runtime = true;
    if (type->destructor_function != IR_INVALID_ID)
        c_backend_mark_function(emitter, type->destructor_function);
    if (type->element_type != IR_INVALID_ID)
        mark_c_type(emitter, type->element_type);
    for (size_t i = 0U; i < type->argument_count; ++i)
        mark_c_type(emitter, type->argument_types[i]);
    for (size_t i = 0U; i < type->field_count; ++i)
        mark_c_type(emitter, type->field_types[i]);
    for (size_t i = 0U; i < type->variant_count; ++i)
        mark_c_type(emitter, type->variant_payload_types[i]);
}
void c_backend_mark_function(CEmitter *emitter,
                             IrFunctionId function_id) {
    if (function_id == IR_INVALID_ID ||
        function_id >= emitter->ir->function_count ||
        emitter->reachable_functions[function_id])
        return;
    emitter->reachable_functions[function_id] = true;
    const IrFunction *function = &emitter->ir->functions[function_id];
    mark_c_type(emitter, function->return_type);
    for (size_t i = 0U; i < function->parameter_count; ++i)
        mark_c_type(emitter, function->parameters[i].type);
    for (size_t i = 0U; i < function->local_count; ++i)
        mark_c_type(emitter, function->locals[i].type);
    for (size_t i = 0U; i < function->value_count; ++i)
        mark_c_type(emitter, function->value_types[i]);
    for (size_t b = 0U; b < function->block_count; ++b)
        for (size_t i = 0U;
             i < function->blocks[b].instruction_count; ++i) {
            const IrInstruction *instruction =
                &function->blocks[b].instructions[i];
            if (instruction->opcode == IR_OP_CALL_DIRECT ||
                instruction->opcode == IR_OP_FUNCTION_REF)
                c_backend_mark_function(emitter, instruction->index);
            if (instruction->opcode == IR_OP_CALL_NATIVE &&
                c_backend_registry_native_symbol(instruction->symbol))
                emitter->needs_native_runtime = true;
        }
}

bool c_backend_function_needs_normal_variant(
    const CEmitter *emitter, size_t function_index) {
    const IrModule *ir = emitter->ir;
    if (function_index >= ir->function_count) return false;
    if (ir->functions[function_index].is_entry) return true;
    if (ir->functions[function_index].is_web_export)
        return true;
    for (size_t t = 0U; t < ir->type_count; ++t)
        if (emitter->used_types[t] &&
            ir->types[t].destructor_function == function_index)
            return true;
    for (size_t f = 0U; f < ir->function_count; ++f) {
        if (!emitter->reachable_functions[f]) continue;
        const IrFunction *caller = &ir->functions[f];
        for (size_t b = 0U; b < caller->block_count; ++b)
            for (size_t i = 0U;
                 i < caller->blocks[b].instruction_count; ++i) {
                const IrInstruction *instruction =
                    &caller->blocks[b].instructions[i];
                if (instruction->index != function_index) continue;
                if (instruction->opcode == IR_OP_FUNCTION_REF)
                    return true;
                if (instruction->opcode != IR_OP_CALL_DIRECT)
                    continue;
                if (instruction->render_destination != IR_INVALID_ID &&
                    c_backend_function_has_render_root(
                        &ir->functions[function_index]))
                    continue;
                if (c_backend_function_supports_direct_render(
                        ir, function_index) &&
                    c_backend_find_direct_render_consumer(
                        caller, instruction->result) != NULL)
                    continue;
                return true;
            }
    }
    return false;
}

bool c_backend_function_is_entry_module_export(
    const IrModule *ir, size_t function_index, size_t entry) {
    const IrFunction *function = &ir->functions[function_index];
    return function->is_web_export &&
           function->is_public &&
           !function->is_destructor &&
           function->module_name != NULL &&
           ir->functions[entry].module_name != NULL &&
           strcmp(function->module_name,
                  ir->functions[entry].module_name) == 0;
}

static void emit_web_identifier(FILE *output, const char *name) {
    for (const char *byte = name; *byte != '\0'; ++byte)
        fputc(((*byte >= 'a' && *byte <= 'z') ||
               (*byte >= 'A' && *byte <= 'Z') ||
               (*byte >= '0' && *byte <= '9') || *byte == '_')
                  ? *byte : '_',
              output);
}

static const char *web_function_basename(const IrFunction *function) {
    const char *name = strrchr(function->name, ':');
    return name == NULL ? function->name : name + 1U;
}

static bool web_parameter_is_string(const IrType *type) {
    return type->shape == IR_TYPE_STRING_VIEW ||
           (type->shape == IR_TYPE_BUILTIN_OBJECT &&
            strcmp(type->name, "string") == 0);
}

static void emit_public_export_signature(
    CEmitter *emitter, size_t function_index) {
    const IrFunction *function =
        &emitter->ir->functions[function_index];
    const IrType *result =
        &emitter->ir->types[function->return_type];
    c_backend_emit_type(emitter, function->return_type);
    if (result->shape == IR_TYPE_STRUCT)
        fputs(" *", emitter->output);
    fputs(" aster_export_", emitter->output);
    emit_web_identifier(
        emitter->output, web_function_basename(function));
    fputc('(', emitter->output);
    if (function->parameter_count == 0U) {
        fputs("void", emitter->output);
    } else {
        for (size_t parameter = 0U;
             parameter < function->parameter_count; ++parameter) {
            if (parameter != 0U) fputs(", ", emitter->output);
            const IrType *type = &emitter->ir->types[
                function->parameters[parameter].type];
            if (web_parameter_is_string(type)) {
                fprintf(emitter->output,
                        "const unsigned char *p%zu_data, size_t p%zu_length",
                        parameter, parameter);
            } else {
                c_backend_emit_type(
                    emitter, function->parameters[parameter].type);
                fprintf(emitter->output, " p%zu", parameter);
            }
        }
    }
    fputc(')', emitter->output);
}

void c_backend_emit_public_export_wrapper(
    CEmitter *emitter, size_t function_index) {
    const IrFunction *function =
        &emitter->ir->functions[function_index];
    emit_public_export_signature(emitter, function_index);
    fputs(";\n", emitter->output);
    emit_public_export_signature(emitter, function_index);
    const IrType *result =
        &emitter->ir->types[function->return_type];
    fputs(" {\n", emitter->output);
    for (size_t parameter = 0U;
         parameter < function->parameter_count; ++parameter) {
        const IrType *type = &emitter->ir->types[
            function->parameters[parameter].type];
        if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
            strcmp(type->name, "string") == 0)
            fprintf(emitter->output,
                    "    aster_string *p%zu_value = aster_string_from("
                    "(aster_str){p%zu_data, p%zu_length});\n",
                    parameter, parameter, parameter);
    }
    if (result->shape == IR_TYPE_STRUCT) {
        fputs("    ", emitter->output);
        c_backend_emit_type(emitter, function->return_type);
        fputs(" *result = malloc(sizeof(*result));\n"
              "    if (result == NULL) aster_trap(\"out of memory\");\n"
              "    *result = aster_fn_", emitter->output);
    } else {
        fputs("    ", emitter->output);
        c_backend_emit_type(emitter, function->return_type);
        fputs(" result = aster_fn_", emitter->output);
    }
    fprintf(emitter->output, "%zu(", function_index);
    for (size_t parameter = 0U;
         parameter < function->parameter_count; ++parameter) {
        if (parameter != 0U) fputs(", ", emitter->output);
        const IrType *type = &emitter->ir->types[
            function->parameters[parameter].type];
        if (type->shape == IR_TYPE_STRING_VIEW)
            fprintf(emitter->output,
                    "(aster_str){p%zu_data, p%zu_length}",
                    parameter, parameter);
        else if (web_parameter_is_string(type))
            fprintf(emitter->output, "p%zu_value", parameter);
        else
            fprintf(emitter->output, "p%zu", parameter);
    }
    fputs(");\n", emitter->output);
    fputs("    return result;\n", emitter->output);
    fputs("}\n\n", emitter->output);
}

static char web_ir_type_code(const IrType *type) {
    if (type->shape == IR_TYPE_BOOL) return 'b';
    if (type->shape == IR_TYPE_SIGNED_INT ||
        type->shape == IR_TYPE_UNSIGNED_INT ||
        type->shape == IR_TYPE_CHAR)
        return 'l';
    if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
        strcmp(type->name, "string") == 0)
        return 'o';
    if (type->shape == IR_TYPE_BUILTIN_OBJECT &&
        strcmp(type->name, "Html") == 0)
        return 'h';
    return '?';
}

static void emit_aggregate_accessor_name(
    CEmitter *emitter, const IrFunction *function,
    char code, const char *field_name) {
    fputs("aster_export_", emitter->output);
    emit_web_identifier(
        emitter->output, web_function_basename(function));
    fprintf(emitter->output, "_result_%c_", code);
    emit_web_identifier(emitter->output, field_name);
}

void c_backend_emit_public_aggregate_accessors(
    CEmitter *emitter, size_t function_index) {
    const IrFunction *function =
        &emitter->ir->functions[function_index];
    const IrType *result =
        &emitter->ir->types[function->return_type];
    if (result->shape != IR_TYPE_STRUCT) return;
    for (size_t field = 0U; field < result->field_count; ++field) {
        IrTypeId field_type_id = result->field_types[field];
        const IrType *field_type =
            &emitter->ir->types[field_type_id];
        char code = web_ir_type_code(field_type);
        if (code == '?') {
            c_backend_unsupported(emitter, function->span,
                        "browser aggregate result field type");
            return;
        }
        c_backend_emit_type(emitter, field_type_id);
        fputc(' ', emitter->output);
        emit_aggregate_accessor_name(
            emitter, function, code, result->field_names[field]);
        fputc('(', emitter->output);
        c_backend_emit_type(emitter, function->return_type);
        fputs(" *value);\n", emitter->output);
        c_backend_emit_type(emitter, field_type_id);
        fputc(' ', emitter->output);
        emit_aggregate_accessor_name(
            emitter, function, code, result->field_names[field]);
        fputc('(', emitter->output);
        c_backend_emit_type(emitter, function->return_type);
        fputs(" *value) {\n    ", emitter->output);
        if (code == 'o' || code == 'h') {
            c_backend_emit_type(emitter, field_type_id);
            fprintf(emitter->output,
                    " field = value->f%zu;\n"
                    "    value->f%zu = NULL;\n"
                    "    return field;\n",
                    field, field);
        } else {
            fprintf(emitter->output,
                    "return value->f%zu;\n", field);
        }
        fputs("}\n", emitter->output);
    }
    fputs("void aster_export_", emitter->output);
    emit_web_identifier(
        emitter->output, web_function_basename(function));
    fputs("_result_drop(", emitter->output);
    c_backend_emit_type(emitter, function->return_type);
    fputs(" *value);\nvoid aster_export_", emitter->output);
    emit_web_identifier(
        emitter->output, web_function_basename(function));
    fputs("_result_drop(", emitter->output);
    c_backend_emit_type(emitter, function->return_type);
    fputs(" *value) {\n    if (value == NULL) return;\n", emitter->output);
    if (c_backend_type_needs_drop(emitter, function->return_type))
        fprintf(emitter->output,
                "    aster_drop_%" PRIu32 "(value);\n",
                function->return_type);
    fputs("    free(value);\n}\n\n", emitter->output);
}

bool c_backend_web_exports_use_strings(
    const IrModule *ir, size_t entry) {
    for (size_t function = 0U;
         function < ir->function_count; ++function) {
        if (!c_backend_function_is_entry_module_export(
                ir, function, entry))
            continue;
        const IrFunction *candidate = &ir->functions[function];
        const IrType *result =
            &ir->types[candidate->return_type];
        if (result->shape == IR_TYPE_BUILTIN_OBJECT &&
            (strcmp(result->name, "string") == 0 ||
             strcmp(result->name, "Html") == 0))
            return true;
        if (result->shape == IR_TYPE_STRUCT)
            for (size_t field = 0U;
                 field < result->field_count; ++field) {
                const IrType *field_type =
                    &ir->types[result->field_types[field]];
                if (field_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                    (strcmp(field_type->name, "string") == 0 ||
                     strcmp(field_type->name, "Html") == 0))
                    return true;
            }
        for (size_t parameter = 0U;
             parameter < candidate->parameter_count; ++parameter)
            if (web_parameter_is_string(
                    &ir->types[candidate->parameters[parameter].type]))
                return true;
    }
    return false;
}

void c_backend_emit_web_string_abi(FILE *output) {
    fputs(
        "void *aster_export_memory_alloc(size_t size);\n"
        "void aster_export_memory_free(void *pointer);\n"
        "const unsigned char *aster_export_string_data(\n"
        "        const aster_string *value);\n"
        "size_t aster_export_string_length(\n"
        "        const aster_string *value);\n"
        "void aster_export_string_drop(aster_string *value);\n"
        "void *aster_export_memory_alloc(size_t size) {\n"
        "    return malloc(size == 0U ? 1U : size);\n"
        "}\n"
        "void aster_export_memory_free(void *pointer) {\n"
        "    free(pointer);\n"
        "}\n"
        "const unsigned char *aster_export_string_data(\n"
        "        const aster_string *value) {\n"
        "    return value == NULL ? NULL : value->data;\n"
        "}\n"
        "size_t aster_export_string_length(\n"
        "        const aster_string *value) {\n"
        "    return value == NULL ? 0U : value->length;\n"
        "}\n"
        "void aster_export_string_drop(aster_string *value) {\n"
        "    aster_string_drop(value);\n"
        "}\n\n",
        output);
}

bool c_backend_web_exports_use_html_result(
    const IrModule *ir, size_t entry) {
    for (size_t function = 0U;
         function < ir->function_count; ++function) {
        if (!c_backend_function_is_entry_module_export(ir, function, entry))
            continue;
        const IrType *result =
            &ir->types[ir->functions[function].return_type];
        if (result->shape == IR_TYPE_BUILTIN_OBJECT &&
            strcmp(result->name, "Html") == 0)
            return true;
        if (result->shape != IR_TYPE_STRUCT) continue;
        for (size_t field = 0U; field < result->field_count; ++field) {
            const IrType *field_type =
                &ir->types[result->field_types[field]];
            if (field_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                strcmp(field_type->name, "Html") == 0)
                return true;
        }
    }
    return false;
}

void c_backend_emit_web_html_abi(FILE *output) {
    fputs(
        "aster_string *aster_export_html_render(aster_html *value);\n"
        "aster_string *aster_export_html_render(aster_html *value) {\n"
        "    return aster_html_render(value);\n"
        "}\n\n",
        output);
}

bool c_backend_function_needs_render_into_variant(
    const CEmitter *emitter, size_t function_index) {
    const IrModule *ir = emitter->ir;
    if (function_index >= ir->function_count ||
        !c_backend_function_has_render_root(&ir->functions[function_index]))
        return false;
    for (size_t f = 0U; f < ir->function_count; ++f) {
        if (!emitter->reachable_functions[f]) continue;
        const IrFunction *caller = &ir->functions[f];
        for (size_t b = 0U; b < caller->block_count; ++b)
            for (size_t i = 0U;
                 i < caller->blocks[b].instruction_count; ++i) {
                const IrInstruction *instruction =
                    &caller->blocks[b].instructions[i];
                if (instruction->opcode == IR_OP_CALL_DIRECT &&
                    instruction->index == function_index &&
                    instruction->render_destination != IR_INVALID_ID)
                    return true;
            }
    }
    return false;
}
