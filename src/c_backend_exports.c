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
                instruction->opcode == IR_OP_CALL_VIRTUAL ||
                instruction->opcode == IR_OP_FUNCTION_REF ||
                instruction->opcode == IR_OP_BOUND_METHOD_REF)
                c_backend_mark_function(emitter, instruction->index);
            if (instruction->opcode == IR_OP_CALL_VIRTUAL &&
                instruction->index < emitter->ir->function_count) {
                IrFunctionId root = emitter->ir->functions[
                    instruction->index].virtual_root;
                if (root == IR_INVALID_ID) root = instruction->index;
                for (size_t target = 0U;
                     target < emitter->ir->function_count; ++target)
                    if (emitter->ir->functions[target].virtual_root == root &&
                        !emitter->ir->functions[target].is_abstract)
                        c_backend_mark_function(
                            emitter, (IrFunctionId)target);
                for (size_t entry = 0U;
                     entry < emitter->ir->interface_dispatch_count; ++entry)
                    if (emitter->ir->interface_dispatches[entry]
                            .interface_function == root)
                        c_backend_mark_function(
                            emitter, emitter->ir->interface_dispatches[entry]
                                .target_function);
            }
            if (instruction->opcode == IR_OP_BOUND_METHOD_REF &&
                instruction->index < emitter->ir->function_count &&
                emitter->ir->functions[instruction->index].is_virtual) {
                IrFunctionId root = emitter->ir->functions[
                    instruction->index].virtual_root;
                if (root == IR_INVALID_ID) root = instruction->index;
                for (size_t target = 0U;
                     target < emitter->ir->function_count; ++target)
                    if (emitter->ir->functions[target].virtual_root == root &&
                        !emitter->ir->functions[target].is_abstract)
                        c_backend_mark_function(
                            emitter, (IrFunctionId)target);
                for (size_t entry = 0U;
                     entry < emitter->ir->interface_dispatch_count; ++entry)
                    if (emitter->ir->interface_dispatches[entry]
                            .interface_function == root)
                        c_backend_mark_function(
                            emitter, emitter->ir->interface_dispatches[entry]
                                .target_function);
            }
            if (instruction->opcode == IR_OP_CALL_NATIVE &&
                c_backend_registry_native_call(instruction)) {
                emitter->needs_native_runtime = true;
                if (instruction->symbol != NULL &&
                    strncmp(instruction->symbol,
                            "NativeHttpClient", 16U) == 0)
                    emitter->needs_http_client_runtime = true;
                if (instruction->symbol != NULL &&
                    strncmp(instruction->symbol, "NativeCrypto", 12U) == 0)
                    emitter->needs_crypto_runtime = true;
            }
        }
}

bool c_backend_function_needs_normal_variant(
    const CEmitter *emitter, size_t function_index) {
    const IrModule *ir = emitter->ir;
    if (function_index >= ir->function_count) return false;
    if (ir->functions[function_index].is_virtual &&
        !ir->functions[function_index].is_abstract)
        return true;
    for (size_t entry = 0U;
         entry < ir->interface_dispatch_count; ++entry)
        if (ir->interface_dispatches[entry].target_function == function_index)
            return true;
    if (ir->functions[function_index].is_entry) return true;
    if (ir->functions[function_index].is_web_export)
        return true;
    if ((ir->functions[function_index].is_constructor ||
         ir->functions[function_index].is_component_render) &&
        ir->functions[function_index].owner_type != NULL)
        for (size_t method = 0U; method < ir->function_count; ++method)
            if (ir->functions[method].is_web_export &&
                ir->functions[method].owner_type != NULL &&
                strcmp(ir->functions[method].owner_type,
                       ir->functions[function_index].owner_type) == 0)
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
                if (instruction->opcode == IR_OP_FUNCTION_REF ||
                    instruction->opcode == IR_OP_BOUND_METHOD_REF)
                    return true;
                if (instruction->opcode != IR_OP_CALL_DIRECT &&
                    instruction->opcode != IR_OP_CALL_VIRTUAL)
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
           (function->is_public || function->owner_type != NULL) &&
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

static void emit_web_function_identifier(
    FILE *output, const IrFunction *function) {
    if (function->owner_type != NULL) {
        emit_web_identifier(output, function->owner_type);
        fputc('_', output);
    }
    emit_web_identifier(output, web_function_basename(function));
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
    emit_web_function_identifier(emitter->output, function);
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
    const IrType *result =
        &emitter->ir->types[function->return_type];
    emit_public_export_signature(emitter, function_index);
    fputs(";\n", emitter->output);
    emit_public_export_signature(emitter, function_index);
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
        fputs(" *value = malloc(sizeof(*value));\n"
              "    if (value == NULL) aster_trap(\"out of memory\");\n"
              "    *value = aster_fn_", emitter->output);
    } else {
        fputs("    ", emitter->output);
        c_backend_emit_type(emitter, function->return_type);
        fputs(" value = aster_fn_", emitter->output);
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
    fputs(");\n    return value;\n}\n\n", emitter->output);
}

void c_backend_emit_public_async_result_accessor(
    CEmitter *emitter, size_t function_index) {
    const IrFunction *function =
        &emitter->ir->functions[function_index];
    if (!function->is_async) return;
    IrTypeId result_type_id = function->async_result_type;
    const IrType *result = &emitter->ir->types[result_type_id];
    c_backend_emit_type(emitter, result_type_id);
    if (result->shape == IR_TYPE_STRUCT) fputs(" *", emitter->output);
    fputs(" aster_export_", emitter->output);
    emit_web_function_identifier(emitter->output, function);
    fputs("_task_result(aster_task *task);\n", emitter->output);
    c_backend_emit_type(emitter, result_type_id);
    if (result->shape == IR_TYPE_STRUCT) fputs(" *", emitter->output);
    fputs(" aster_export_", emitter->output);
    emit_web_function_identifier(emitter->output, function);
    fputs("_task_result(aster_task *task) {\n"
          "    if (task == NULL || task->state != ASTER_TASK_SUCCEEDED ||\n"
          "        task->result == NULL || task->result_size != sizeof(",
          emitter->output);
    c_backend_emit_type(emitter, result_type_id);
    fputs("))\n        aster_trap(\"browser Task result is unavailable\");\n",
          emitter->output);
    if (result->shape == IR_TYPE_STRUCT) {
        fputs("    ", emitter->output);
        c_backend_emit_type(emitter, result_type_id);
        fputs(" *value = aster_allocate(sizeof(*value));\n"
              "    *value = ", emitter->output);
    } else {
        fputs("    return ", emitter->output);
    }
    if (c_backend_type_needs_drop(emitter, result_type_id))
        fprintf(emitter->output, "aster_clone_%" PRIu32 "(*(",
                result_type_id);
    else
        fputs("*(", emitter->output);
    c_backend_emit_type(emitter, result_type_id);
    fputs(" *)task->result", emitter->output);
    if (c_backend_type_needs_drop(emitter, result_type_id))
        fputc(')', emitter->output);
    fputs(";\n", emitter->output);
    if (result->shape == IR_TYPE_STRUCT)
        fputs("    return value;\n", emitter->output);
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
    emit_web_function_identifier(emitter->output, function);
    fprintf(emitter->output, "_result_%c_", code);
    emit_web_identifier(emitter->output, field_name);
}

void c_backend_emit_public_aggregate_accessors(
    CEmitter *emitter, size_t function_index) {
    const IrFunction *function =
        &emitter->ir->functions[function_index];
    IrTypeId result_type = function->is_async
        ? function->async_result_type : function->return_type;
    const IrType *result = &emitter->ir->types[result_type];
    if (result->shape != IR_TYPE_STRUCT)
        return;
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
        c_backend_emit_type(emitter, result_type);
        fputs(" *value);\n", emitter->output);
        c_backend_emit_type(emitter, field_type_id);
        fputc(' ', emitter->output);
        emit_aggregate_accessor_name(
            emitter, function, code, result->field_names[field]);
        fputc('(', emitter->output);
        c_backend_emit_type(emitter, result_type);
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
    emit_web_function_identifier(emitter->output, function);
    fputs("_result_drop(", emitter->output);
    c_backend_emit_type(emitter, result_type);
    fputs(" *value);\nvoid aster_export_", emitter->output);
    emit_web_function_identifier(emitter->output, function);
    fputs("_result_drop(", emitter->output);
    c_backend_emit_type(emitter, result_type);
    fputs(" *value) {\n    if (value == NULL) return;\n", emitter->output);
    if (c_backend_type_needs_drop(emitter, result_type))
        fprintf(emitter->output,
                "    aster_drop_%" PRIu32 "(value);\n",
                result_type);
    fputs("    free(value);\n}\n\n", emitter->output);
}

static void emit_component_list_state_abi(
    CEmitter *emitter, const char *owner,
    IrTypeId class_type_id, const IrType *class_type) {
    const IrModule *ir = emitter->ir;
    size_t list_field = class_type->field_count;
    IrTypeId list_type_id = IR_INVALID_ID;
    const IrType *list_type = NULL;
    const IrType *item_type = NULL;
    for (size_t field = 0U; field < class_type->field_count; ++field) {
        IrTypeId candidate_id = class_type->field_types[field];
        const IrType *candidate = &ir->types[candidate_id];
        if (!c_backend_type_is_vec(candidate) ||
            candidate->element_type == IR_INVALID_ID)
            continue;
        const IrType *item = &ir->types[candidate->element_type];
        if (item->shape != IR_TYPE_STRUCT || item->field_count == 0U)
            continue;
        bool supported = true;
        for (size_t item_field = 0U;
             item_field < item->field_count; ++item_field) {
            const IrType *field_type = &ir->types[item->field_types[item_field]];
            bool string_type = field_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                field_type->name != NULL &&
                strcmp(field_type->name, "string") == 0;
            if (!(string_type || field_type->shape == IR_TYPE_BOOL ||
                  field_type->shape == IR_TYPE_SIGNED_INT ||
                  field_type->shape == IR_TYPE_UNSIGNED_INT))
                supported = false;
        }
        if (!supported) continue;
        list_field = field;
        list_type_id = candidate_id;
        list_type = candidate;
        item_type = item;
        break;
    }
    if (list_type == NULL || item_type == NULL) return;
    fputs("void aster_export_component_", emitter->output);
    emit_web_identifier(emitter->output, owner);
    fputs("_state_clear(", emitter->output);
    c_backend_emit_type(emitter, class_type_id);
    fputs(" value) {\n", emitter->output);
    fprintf(emitter->output,
            "    aster_drop_%" PRIu32 "(&value->f%zu);\n",
            list_type_id, list_field);
    fprintf(emitter->output,
            "    value->f%zu = calloc(1U, sizeof(*value->f%zu));\n"
            "    if (value->f%zu == NULL) aster_trap(\"out of memory\");\n"
            "}\n",
            list_field, list_field, list_field);
    fputs("void aster_export_component_", emitter->output);
    emit_web_identifier(emitter->output, owner);
    fputs("_state_add(", emitter->output);
    c_backend_emit_type(emitter, class_type_id);
    fputs(" value", emitter->output);
    for (size_t field = 0U; field < item_type->field_count; ++field) {
        const IrType *field_type = &ir->types[item_type->field_types[field]];
        if (field_type->shape == IR_TYPE_BUILTIN_OBJECT &&
            field_type->name != NULL &&
            strcmp(field_type->name, "string") == 0)
            fprintf(emitter->output,
                    ", const unsigned char *p%zu_data, size_t p%zu_length",
                    field, field);
        else {
            fputs(", ", emitter->output);
            c_backend_emit_type(emitter, item_type->field_types[field]);
            fprintf(emitter->output, " p%zu", field);
        }
    }
    fputs(") {\n    ", emitter->output);
    c_backend_emit_type(emitter, list_type->element_type);
    fputs(" item = {0};\n", emitter->output);
    for (size_t field = 0U; field < item_type->field_count; ++field) {
        const IrType *field_type = &ir->types[item_type->field_types[field]];
        if (field_type->shape == IR_TYPE_BUILTIN_OBJECT &&
            field_type->name != NULL &&
            strcmp(field_type->name, "string") == 0)
            fprintf(emitter->output,
                    "    item.f%zu = aster_string_from((aster_str){"
                    "p%zu_data, p%zu_length});\n",
                    field, field, field);
        else
            fprintf(emitter->output, "    item.f%zu = p%zu;\n", field, field);
    }
    fprintf(emitter->output,
            "    if (value->f%zu->length == value->f%zu->capacity) {\n"
            "        size_t capacity = value->f%zu->capacity == 0U ? 4U : "
            "value->f%zu->capacity * 2U;\n"
            "        if (capacity < value->f%zu->capacity) "
            "aster_trap(\"list capacity overflow\");\n"
            "        void *data = realloc(value->f%zu->data, "
            "capacity * sizeof(*value->f%zu->data));\n"
            "        if (data == NULL) aster_trap(\"out of memory\");\n"
            "        value->f%zu->data = data;\n"
            "        value->f%zu->capacity = capacity;\n"
            "    }\n"
            "    value->f%zu->data[value->f%zu->length++] = item;\n"
            "}\n",
            list_field, list_field, list_field, list_field,
            list_field, list_field, list_field, list_field,
            list_field, list_field, list_field);
}

void c_backend_emit_web_component_abis(
    CEmitter *emitter, size_t entry) {
    const IrModule *ir = emitter->ir;
    for (size_t method_index = 0U;
         method_index < ir->function_count; ++method_index) {
        const IrFunction *method = &ir->functions[method_index];
        if (!c_backend_function_is_entry_module_export(
                ir, method_index, entry) || method->owner_type == NULL)
            continue;
        bool emitted = false;
        for (size_t prior = 0U; prior < method_index; ++prior)
            if (c_backend_function_is_entry_module_export(
                    ir, prior, entry) &&
                ir->functions[prior].owner_type != NULL &&
                strcmp(ir->functions[prior].owner_type,
                       method->owner_type) == 0)
                emitted = true;
        if (emitted) continue;
        size_t constructor_index = ir->function_count;
        for (size_t candidate = 0U;
             candidate < ir->function_count; ++candidate)
            if (ir->functions[candidate].is_constructor &&
                ir->functions[candidate].owner_type != NULL &&
                strcmp(ir->functions[candidate].owner_type,
                       method->owner_type) == 0) {
                constructor_index = candidate;
                break;
            }
        if (constructor_index == ir->function_count) {
            c_backend_unsupported(
                emitter, method->span,
                "a browser class component without a supported constructor");
            continue;
        }
        size_t render_index = ir->function_count;
        for (size_t candidate = 0U;
             candidate < ir->function_count; ++candidate)
            if (ir->functions[candidate].is_component_render &&
                ir->functions[candidate].owner_type != NULL &&
                strcmp(ir->functions[candidate].owner_type,
                       method->owner_type) == 0) {
                render_index = candidate;
                break;
            }
        if (render_index == ir->function_count) {
            c_backend_unsupported(
                emitter, method->span,
                "a browser class component without Render()");
            continue;
        }
        const IrFunction *constructor = &ir->functions[constructor_index];
        const IrFunction *render = &ir->functions[render_index];
        IrTypeId type_id = constructor->return_type;
        const IrType *type = &ir->types[type_id];
        c_backend_emit_type(emitter, type_id);
        fputs(" aster_export_component_", emitter->output);
        emit_web_identifier(emitter->output, method->owner_type);
        fputs("_new(", emitter->output);
        if (constructor->parameter_count == 0U) {
            fputs("void", emitter->output);
        } else {
            for (size_t parameter = 0U;
                 parameter < constructor->parameter_count; ++parameter) {
                if (parameter != 0U) fputs(", ", emitter->output);
                const IrType *parameter_type = &ir->types[
                    constructor->parameters[parameter].type];
                if (web_parameter_is_string(parameter_type))
                    fprintf(emitter->output,
                            "const unsigned char *p%zu_data, size_t p%zu_length",
                            parameter, parameter);
                else {
                    c_backend_emit_type(
                        emitter, constructor->parameters[parameter].type);
                    fprintf(emitter->output, " p%zu", parameter);
                }
            }
        }
        fputs(") {\n", emitter->output);
        for (size_t parameter = 0U;
             parameter < constructor->parameter_count; ++parameter) {
            const IrType *parameter_type = &ir->types[
                constructor->parameters[parameter].type];
            if (parameter_type->shape == IR_TYPE_BUILTIN_OBJECT &&
                strcmp(parameter_type->name, "string") == 0)
                fprintf(emitter->output,
                        "    aster_string *p%zu_value = aster_string_from("
                        "(aster_str){p%zu_data, p%zu_length});\n",
                        parameter, parameter, parameter);
        }
        fprintf(emitter->output, "    return aster_fn_%zu(", constructor_index);
        for (size_t parameter = 0U;
             parameter < constructor->parameter_count; ++parameter) {
            if (parameter != 0U) fputs(", ", emitter->output);
            const IrType *parameter_type = &ir->types[
                constructor->parameters[parameter].type];
            if (parameter_type->shape == IR_TYPE_STRING_VIEW)
                fprintf(emitter->output,
                        "(aster_str){p%zu_data, p%zu_length}",
                        parameter, parameter);
            else if (web_parameter_is_string(parameter_type))
                fprintf(emitter->output, "p%zu_value", parameter);
            else
                fprintf(emitter->output, "p%zu", parameter);
        }
        fputs(");\n}\n", emitter->output);
        c_backend_emit_type(emitter, render->return_type);
        fputs(" aster_export_component_", emitter->output);
        emit_web_identifier(emitter->output, method->owner_type);
        fputs("_render(", emitter->output);
        c_backend_emit_type(emitter, type_id);
        fprintf(emitter->output,
                " value) {\n    return aster_fn_%zu(value);\n}\n",
                render_index);
        emit_component_list_state_abi(
            emitter, method->owner_type, type_id, type);
        fputs("void aster_export_component_", emitter->output);
        emit_web_identifier(emitter->output, method->owner_type);
        fputs("_drop(", emitter->output);
        c_backend_emit_type(emitter, type_id);
        fputs(" value) {\n    if (value == NULL) return;\n", emitter->output);
        if (type->destructor_function != IR_INVALID_ID)
            fprintf(emitter->output,
                    "    (void)aster_fn_%" PRIu32 "(value);\n",
                    type->destructor_function);
        for (size_t field = type->field_count; field > 0U; --field) {
            IrTypeId field_type = type->field_types[field - 1U];
            if (c_backend_type_needs_drop(emitter, field_type))
                fprintf(emitter->output,
                        "    aster_drop_%" PRIu32 "(&value->f%zu);\n",
                        field_type, field - 1U);
        }
        fputs("    free(value);\n}\n\n", emitter->output);
    }
}

bool c_backend_web_exports_use_strings(
    const IrModule *ir, size_t entry) {
    for (size_t function = 0U;
         function < ir->function_count; ++function) {
        if (!c_backend_function_is_entry_module_export(
                ir, function, entry))
            continue;
        const IrFunction *candidate = &ir->functions[function];
        if (candidate->is_async) return true;
        IrTypeId result_type = candidate->return_type;
        const IrType *result = &ir->types[result_type];
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

bool c_backend_web_exports_use_tasks(
    const IrModule *ir, size_t entry) {
    for (size_t function = 0U;
         function < ir->function_count; ++function)
        if (c_backend_function_is_entry_module_export(
                ir, function, entry) &&
            ir->functions[function].is_async)
            return true;
    return false;
}

void c_backend_emit_web_exception_abi(FILE *output) {
    fputs(
        "int aster_export_exception_pending(void);\n"
        "aster_string *aster_export_exception_take(void);\n"
        "int aster_export_exception_pending(void) {\n"
        "    return aster_exception_pending ? 1 : 0;\n"
        "}\n"
        "aster_string *aster_export_exception_take(void) {\n"
        "    if (!aster_exception_pending) return NULL;\n"
        "    aster_string *message = aster_exception_message;\n"
        "    aster_exception_message = NULL;\n"
        "    aster_exception_type = NULL;\n"
        "    aster_exception_pending = false;\n"
        "    if (message == NULL)\n"
        "        message = aster_string_from((aster_str){\n"
        "            (const unsigned char *)\"browser handler failed\", 22U\n"
        "        });\n"
        "    return message;\n"
        "}\n\n",
        output);
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
        if (ir->functions[function].is_component_render)
            return true;
        if (!c_backend_function_is_entry_module_export(ir, function, entry))
            continue;
        IrTypeId result_type = ir->functions[function].is_async
            ? ir->functions[function].async_result_type
            : ir->functions[function].return_type;
        const IrType *result = &ir->types[result_type];
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

void c_backend_emit_web_task_abi(FILE *output) {
    fputs(
        "int aster_export_task_status(aster_task *task);\n"
        "aster_string *aster_export_task_error(aster_task *task);\n"
        "void aster_export_task_drop(aster_task *task);\n"
        "int aster_export_task_status(aster_task *task) {\n"
        "    (void)aster_task_run_until;\n"
        "    (void)aster_task_restore_fault;\n"
        "    if (task == NULL) aster_trap(\"browser Task is null\");\n"
        "    (void)aster_task_process_timers();\n"
        "    return (int)task->state;\n"
        "}\n"
        "aster_string *aster_export_task_error(aster_task *task) {\n"
        "    if (task == NULL || (task->state != ASTER_TASK_FAULTED &&\n"
        "        task->state != ASTER_TASK_CANCELED))\n"
        "        aster_trap(\"browser Task error is unavailable\");\n"
        "    return aster_string_clone(task->exception_message);\n"
        "}\n"
        "void aster_export_task_drop(aster_task *task) {\n"
        "    aster_task_drop(task);\n"
        "}\n\n",
        output);
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
                if ((instruction->opcode == IR_OP_CALL_DIRECT ||
                     instruction->opcode == IR_OP_CALL_VIRTUAL) &&
                    instruction->index == function_index &&
                    instruction->render_destination != IR_INVALID_ID)
                    return true;
            }
    }
    return false;
}
