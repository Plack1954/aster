#include "ir_verify_internal.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static size_t terminator_successor_count(const IrTerminator *term,
                                         size_t block_count) {
    if (term->kind == IR_TERM_JUMP)
        return term->target < block_count ? 1U : 0U;
    if (term->kind == IR_TERM_BRANCH)
        return term->target < block_count &&
               term->alternate < block_count ? 2U : 0U;
    return 0U;
}

static IrBlockId terminator_successor(const IrTerminator *term,
                                      size_t successor) {
    return successor == 0U ? term->target : term->alternate;
}

static void compute_dominators(const IrFunction *function,
                               bool *reachable, bool *dominators) {
    size_t count = function->block_count;
    IrBlockId *work = ir_resize(NULL, count, sizeof(*work));
    size_t work_count = 0U;
    reachable[function->entry_block] = true;
    work[work_count++] = function->entry_block;
    while (work_count != 0U) {
        IrBlockId block_id = work[--work_count];
        const IrTerminator *term =
            &function->blocks[block_id].terminator;
        size_t successors = terminator_successor_count(term, count);
        for (size_t i = 0U; i < successors; ++i) {
            IrBlockId next = terminator_successor(term, i);
            if (!reachable[next]) {
                reachable[next] = true;
                work[work_count++] = next;
            }
        }
    }
    free(work);

    for (size_t block = 0U; block < count; ++block) {
        if (!reachable[block]) {
            dominators[block * count + block] = true;
            continue;
        }
        if (block == function->entry_block) {
            dominators[block * count + block] = true;
            continue;
        }
        for (size_t candidate = 0U; candidate < count; ++candidate)
            dominators[block * count + candidate] = reachable[candidate];
    }

    bool changed;
    do {
        changed = false;
        for (size_t block = 0U; block < count; ++block) {
            if (!reachable[block] || block == function->entry_block)
                continue;
            for (size_t candidate = 0U; candidate < count; ++candidate) {
                bool dominates = true;
                bool has_predecessor = false;
                for (size_t predecessor = 0U;
                     predecessor < count; ++predecessor) {
                    if (!reachable[predecessor]) continue;
                    const IrTerminator *term =
                        &function->blocks[predecessor].terminator;
                    size_t successors =
                        terminator_successor_count(term, count);
                    bool is_predecessor = false;
                    for (size_t edge = 0U; edge < successors; ++edge)
                        if (terminator_successor(term, edge) == block)
                            is_predecessor = true;
                    if (!is_predecessor) continue;
                    has_predecessor = true;
                    if (!dominators[predecessor * count + candidate])
                        dominates = false;
                }
                if (!has_predecessor) dominates = false;
                if (candidate == block) dominates = true;
                size_t slot = block * count + candidate;
                if (dominators[slot] != dominates) {
                    dominators[slot] = dominates;
                    changed = true;
                }
            }
        }
    } while (changed);
}

static bool value_available(const IrFunction *function,
                            const bool *defined,
                            const size_t *definition_blocks,
                            const size_t *definition_instructions,
                            const bool *reachable,
                            const bool *dominators,
                            IrValueId value, size_t use_block,
                            size_t use_instruction) {
    if (!ir_verify_value(function, value) || !defined[value]) return false;
    size_t definition_block = definition_blocks[value];
    if (definition_block == use_block)
        return definition_instructions[value] < use_instruction;
    /* Dominance is only meaningful in the entry-reachable CFG. */
    if (!reachable[use_block]) return true;
    return reachable[definition_block] &&
           dominators[use_block * function->block_count +
                      definition_block];
}

bool lang_ir_verify_module(const IrModule *ir,
                           LangDiagnostics *diagnostics) {
    if (ir == NULL ||
        (ir->type_count != 0U && ir->types == NULL) ||
        (ir->function_count != 0U && ir->functions == NULL) ||
        (ir->static_field_count != 0U && ir->static_fields == NULL)) {
        lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                  "IR module has invalid top-level storage");
        return false;
    }
    bool ok = true;
    for (size_t field = 0U; field < ir->static_field_count; ++field) {
        const IrStaticField *value = &ir->static_fields[field];
        if (value->owner_declaration != NULL || value->name == NULL ||
            value->owner_name == NULL || !ir_verify_type(ir, value->type)) {
            lang_diag(diagnostics, value->span,
                      "IR static field %zu is malformed", field);
            ok = false;
        }
    }
    for (size_t t = 0U; t < ir->type_count; ++t) {
        const IrType *type = &ir->types[t];
        if (type->checked_type != NULL || type->name == NULL ||
            !ir_verify_type_shape(type->shape) ||
            !ir_verify_copy_policy(type->copy_policy) ||
            !ir_verify_drop_policy(type->drop_policy) ||
            (type->copy_policy == IR_COPY_TRIVIAL &&
             (type->requires_cleanup || type->managed)) ||
            (type->drop_policy == IR_DROP_TRIVIAL &&
             (type->requires_cleanup || type->managed)) ||
            (type->copy_function != IR_INVALID_ID &&
             type->copy_policy != IR_COPY_CUSTOM) ||
            (type->destructor_function != IR_INVALID_ID &&
             type->drop_policy != IR_DROP_CUSTOM &&
             !(type->shape == IR_TYPE_CLASS_REFERENCE &&
               type->drop_policy == IR_DROP_TRIVIAL))) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu has invalid identity or copy/drop policy", t);
            ok = false;
        }
        if (type->copy_function != IR_INVALID_ID &&
            type->copy_function >= ir->function_count) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu has an invalid copy function", t);
            ok = false;
        }
        if (type->copy_function != IR_INVALID_ID &&
            type->copy_function < ir->function_count) {
            const IrFunction *copy =
                &ir->functions[type->copy_function];
            if (copy->return_type != (IrTypeId)t ||
                copy->parameter_count != 1U ||
                copy->parameters[0].type != (IrTypeId)t ||
                copy->parameters[0].mode !=
                    PARAMETER_MODE_IMMUTABLE_REFERENCE) {
                lang_diag(
                    diagnostics, copy->span,
                    "IR type t%zu has an invalid custom copy function signature",
                    t);
                ok = false;
            }
        }
        if (type->target_layout_known &&
            type->target_alignment == 0U) {
            lang_diag(
                diagnostics, (LangSpan){NULL, 0U, 0U},
                "IR type t%zu has an invalid target alignment",
                t);
            ok = false;
        }
        if (type->element_type != IR_INVALID_ID &&
            !ir_verify_type(ir, type->element_type)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu has an invalid element type", t);
            ok = false;
        }
        if (type->base_type != IR_INVALID_ID &&
            (!ir_verify_type(ir, type->base_type) ||
             type->shape != IR_TYPE_CLASS_REFERENCE ||
             ir->types[type->base_type].shape !=
                 IR_TYPE_CLASS_REFERENCE)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu (`%s`) has invalid base t%" PRIu32
                      " (`%s`, shape %d)",
                      t, type->name, type->base_type,
                      ir_verify_type(ir, type->base_type)
                          ? ir->types[type->base_type].name : "<invalid>",
                      ir_verify_type(ir, type->base_type)
                          ? (int)ir->types[type->base_type].shape : -1);
            ok = false;
        }
        if (type->interface_count != 0U &&
            type->interface_types == NULL) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu is missing interface metadata", t);
            ok = false;
        }
        for (size_t interface = 0U;
             type->interface_types != NULL &&
             interface < type->interface_count; ++interface)
            if (!ir_verify_type(ir, type->interface_types[interface]) ||
                type->shape != IR_TYPE_CLASS_REFERENCE ||
                ir->types[type->interface_types[interface]].shape !=
                    IR_TYPE_CLASS_REFERENCE) {
                lang_diag(
                    diagnostics, (LangSpan){NULL, 0U, 0U},
                    "IR type t%zu has invalid interface metadata", t);
                ok = false;
            }
        if (type->base_type != IR_INVALID_ID &&
            ir_verify_type(ir, type->base_type)) {
            IrTypeId base = type->base_type;
            size_t depth = 0U;
            while (base != IR_INVALID_ID && ir_verify_type(ir, base) &&
                   depth++ < ir->type_count) {
                if (base == (IrTypeId)t) {
                    lang_diag(
                        diagnostics, (LangSpan){NULL, 0U, 0U},
                        "IR class type t%zu has an inheritance cycle", t);
                    ok = false;
                    break;
                }
                base = ir->types[base].base_type;
            }
            if (base != IR_INVALID_ID && depth > ir->type_count) {
                lang_diag(
                    diagnostics, (LangSpan){NULL, 0U, 0U},
                    "IR class type t%zu has an invalid inheritance chain", t);
                ok = false;
            }
        }
        if (type->error_type != IR_INVALID_ID &&
            !ir_verify_type(ir, type->error_type)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu has an invalid error type", t);
            ok = false;
        }
        if (type->destructor_function != IR_INVALID_ID) {
            if (type->destructor_function >=
                ir->function_count) {
                lang_diag(
                    diagnostics, (LangSpan){NULL, 0U, 0U},
                    "IR type t%zu has an invalid destructor function",
                    t);
                ok = false;
            } else {
                const IrFunction *destructor =
                    &ir->functions[
                        type->destructor_function];
                if (!destructor->is_destructor ||
                    destructor->parameter_count != 1U ||
                    destructor->parameters == NULL ||
                    !ir_verify_type_assignable(
                        ir, destructor->parameters[0].type,
                        (IrTypeId)t)) {
                    lang_diag(
                        diagnostics,
                        (LangSpan){NULL, 0U, 0U},
                        "IR type t%zu `%s` has incompatible destructor f%" PRIu32,
                        t, type->name != NULL ? type->name : "<invalid>",
                        type->destructor_function);
                    ok = false;
                }
            }
        }
        if (type->argument_count != 0U &&
            type->argument_types == NULL) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR type t%zu is missing argument metadata", t);
            ok = false;
        }
        for (size_t a = 0U;
             type->argument_types != NULL && a < type->argument_count; ++a)
            if (!ir_verify_type(ir, type->argument_types[a])) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR type t%zu has an invalid argument type", t);
                ok = false;
            }
        if (type->shape == IR_TYPE_FUNCTION)
            for (size_t a = 0U; a < type->argument_count; ++a)
                if (type->parameter_modes == NULL ||
                    !ir_verify_parameter_mode(type->parameter_modes[a])) {
                    lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                              "IR function type t%zu has invalid parameter metadata",
                              t);
                    ok = false;
                    break;
                }
        if ((type->shape == IR_TYPE_ARRAY ||
            type->shape == IR_TYPE_RAW_POINTER ||
             type->shape == IR_TYPE_SLICE ||
             type->shape == IR_TYPE_ITERATOR ||
             type->shape == IR_TYPE_ELEMENT_BUILDER) &&
            !ir_verify_type(ir, type->element_type)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR aggregate type t%zu is missing its element type",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_ITERATOR &&
            (type->argument_count != 1U ||
             type->argument_types == NULL ||
             !ir_verify_type(ir, type->argument_types[0]))) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR iterator type t%zu has invalid source metadata",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_FUNCTION &&
            (!ir_verify_type(ir, type->element_type) ||
             (type->argument_count != 0U &&
              type->parameter_modes == NULL))) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR function type t%zu has invalid result metadata",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_ELEMENT_BUILDER &&
            (!ir_verify_type(ir, type->element_type) ||
             ir->types[type->element_type].name == NULL ||
             strcmp(ir->types[type->element_type].name, "Html") != 0)) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR element builder type t%zu has invalid result metadata",
                      t);
            ok = false;
        }
        if (type->shape == IR_TYPE_STRUCT ||
            type->shape == IR_TYPE_CLASS_REFERENCE) {
            if (type->field_count != 0U &&
                (type->field_names == NULL ||
                 type->field_types == NULL ||
                 type->field_spans == NULL ||
                 type->field_offsets == NULL)) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR aggregate type t%zu has incomplete field metadata",
                          t);
                ok = false;
            } else if (type->field_count != 0U) {
                for (size_t field = 0U;
                     field < type->field_count; ++field) {
                    if (type->field_names[field] == NULL) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR aggregate type t%zu has an invalid field name",
                            t);
                        ok = false;
                    }
                    if (!ir_verify_type(ir, type->field_types[field])) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR struct type t%zu has an invalid field type",
                            t);
                        ok = false;
                    } else {
                        size_t storage_size =
                            type->shape == IR_TYPE_CLASS_REFERENCE
                            ? type->object_size : type->target_size;
                        if (type->member_layout_known &&
                        (type->field_offsets[field] %
                             ir->types[type->field_types[field]].target_alignment !=
                             0U ||
                         type->field_offsets[field] > storage_size ||
                         ir->types[type->field_types[field]].target_size >
                             storage_size - type->field_offsets[field])) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR aggregate type t%zu has an invalid field offset",
                            t);
                        ok = false;
                        }
                    }
                }
            }
            if (type->target_layout_known && !type->member_layout_known) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR struct type t%zu is missing member layout", t);
                ok = false;
            }
            if (type->shape == IR_TYPE_CLASS_REFERENCE &&
                (!type->object_layout_known || type->object_size == 0U ||
                 type->object_alignment == 0U)) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR class type t%zu is missing object layout", t);
                ok = false;
            }
        }
        if (type->shape == IR_TYPE_ENUM ||
            type->shape == IR_TYPE_UNION) {
            if (type->variant_count == 0U ||
                type->variant_names == NULL ||
                type->variant_payload_types == NULL ||
                type->variant_spans == NULL ||
                type->variant_discriminants == NULL ||
                type->variant_payload_offsets == NULL) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR %s type t%zu has incomplete variant metadata",
                          type->shape == IR_TYPE_UNION ? "union" : "enum",
                          t);
                ok = false;
            } else {
                for (size_t variant = 0U;
                     variant < type->variant_count; ++variant) {
                    if (type->variant_names[variant] == NULL) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR enum type t%zu has an invalid variant name",
                            t);
                        ok = false;
                    }
                    if (type->variant_discriminants[variant] !=
                        (uint32_t)variant) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR enum type t%zu has an invalid discriminant",
                            t);
                        ok = false;
                    }
                    if (type->variant_payload_types[variant] !=
                            IR_INVALID_ID &&
                        !ir_verify_type(
                            ir,
                            type->variant_payload_types[variant])) {
                        lang_diag(
                            diagnostics, (LangSpan){NULL, 0U, 0U},
                            "IR enum type t%zu has an invalid payload type",
                            t);
                        ok = false;
                    }
                    if (type->member_layout_known &&
                        type->variant_payload_types[variant] !=
                            IR_INVALID_ID &&
                        ir_verify_type(
                            ir, type->variant_payload_types[variant])) {
                        const IrType *payload = &ir->types[
                            type->variant_payload_types[variant]];
                        if (type->variant_payload_offsets[variant] <
                                ir->target.enum_tag_size ||
                            type->variant_payload_offsets[variant] %
                                payload->target_alignment != 0U) {
                            lang_diag(
                                diagnostics, (LangSpan){NULL, 0U, 0U},
                                "IR union type t%zu has an invalid payload offset",
                                t);
                            ok = false;
                        }
                    }
                }
            }
            if (type->target_layout_known && !type->member_layout_known) {
                lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                          "IR enum type t%zu is missing member layout", t);
                ok = false;
            }
        }
    }
    if (ir->interface_dispatch_count != 0U &&
        ir->interface_dispatches == NULL) {
        lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                  "IR module is missing interface dispatch metadata");
        ok = false;
    }
    for (size_t entry = 0U;
         ir->interface_dispatches != NULL &&
         entry < ir->interface_dispatch_count; ++entry) {
        const IrInterfaceDispatch *dispatch =
            &ir->interface_dispatches[entry];
        if (!ir_verify_type(ir, dispatch->interface_type) ||
            !ir_verify_type(ir, dispatch->runtime_type) ||
            ir->types[dispatch->interface_type].shape !=
                IR_TYPE_CLASS_REFERENCE ||
            ir->types[dispatch->runtime_type].shape !=
                IR_TYPE_CLASS_REFERENCE ||
            dispatch->interface_function >= ir->function_count ||
            dispatch->target_function >= ir->function_count) {
            lang_diag(diagnostics, (LangSpan){NULL, 0U, 0U},
                      "IR interface dispatch entry %zu is invalid", entry);
            ok = false;
            continue;
        }
        const IrFunction *contract =
            &ir->functions[dispatch->interface_function];
        const IrFunction *target =
            &ir->functions[dispatch->target_function];
        bool signature = contract->is_abstract && contract->is_virtual &&
            contract->parameter_count != 0U &&
            contract->parameter_count == target->parameter_count &&
            contract->return_type == target->return_type &&
            contract->parameters[0].type == dispatch->interface_type &&
            ir_verify_type_assignable(
                ir, dispatch->interface_type, dispatch->runtime_type) &&
            ir_verify_type_assignable(
                ir, target->parameters[0].type, dispatch->runtime_type);
        for (size_t parameter = 1U;
             signature && parameter < contract->parameter_count;
             ++parameter)
            signature = contract->parameters[parameter].type ==
                    target->parameters[parameter].type &&
                contract->parameters[parameter].mode ==
                    target->parameters[parameter].mode;
        if (!signature) {
            lang_diag(
                diagnostics, (LangSpan){NULL, 0U, 0U},
                "IR interface dispatch entry %zu has an incompatible signature",
                entry);
            ok = false;
        }
    }
    for (size_t f = 0U; f < ir->function_count; ++f) {
        const IrFunction *function = &ir->functions[f];
        LangSpan function_span = function->span;
        if (function->declaration != NULL || function->name == NULL ||
            !ir_verify_type(ir, function->return_type) ||
            !ir_verify_type(ir, function->async_result_type) ||
            function->blocks == NULL ||
            function->abi.calling_convention !=
                IR_CALLING_CONVENTION_ASTER ||
            function->abi.returns_async_task != function->is_async ||
            (function->parameter_count != 0U &&
             function->parameters == NULL) ||
            (function->local_count != 0U &&
             function->locals == NULL) ||
            (function->value_count != 0U &&
             function->value_types == NULL) ||
            (function->is_async &&
             (ir->types[function->return_type].shape !=
                  IR_TYPE_BUILTIN_OBJECT ||
              ir->types[function->return_type].name == NULL ||
              strncmp(ir->types[function->return_type].name,
                      "Task", 4U) != 0 ||
              ir->types[function->return_type].element_type !=
                  function->async_result_type)) ||
            function->block_count == 0U ||
            function->entry_block >= function->block_count) {
            lang_diag(diagnostics, function_span,
                      "malformed IR function `%s`",
                      function->name != NULL
                          ? function->name : "<invalid>");
            ok = false;
            continue;
        }
        if ((function->static_css_count != 0U &&
             function->static_css == NULL) ||
            function->static_css_count > function->static_css_capacity) {
            lang_diag(diagnostics, function_span,
                      "IR function `%s` has invalid static CSS metadata",
                      function->name);
            ok = false;
        }
        for (size_t css = 0U;
             function->static_css != NULL &&
             css < function->static_css_count; ++css)
            if (function->static_css[css].scope_attribute == NULL ||
                (function->static_css[css].text == NULL &&
                 function->static_css[css].text_length != 0U)) {
                lang_diag(diagnostics, function_span,
                          "IR function `%s` has invalid static CSS entry",
                          function->name);
                ok = false;
            }
        size_t suspension_count = 0U;
        for (size_t b = 0U; b < function->block_count; ++b)
            for (size_t i = 0U;
                 i < function->blocks[b].instruction_count; ++i)
                if (function->blocks[b].instructions[i].opcode == IR_OP_AWAIT)
                    ++suspension_count;
        if (suspension_count != function->async_suspension_count ||
            (!function->is_async && suspension_count != 0U)) {
            lang_diag(diagnostics, function_span,
                      "IR function `%s` has invalid async metadata",
                      function->name);
            ok = false;
        }
        for (size_t p = 0U; p < function->parameter_count; ++p)
            if (function->parameters[p].name == NULL ||
                !ir_verify_type(ir, function->parameters[p].type) ||
                !ir_verify_parameter_mode(function->parameters[p].mode)) {
                lang_diag(diagnostics, function_span,
                          "IR function `%s` has an invalid parameter type",
                          function->name);
                ok = false;
            }
        for (size_t l = 0U; l < function->local_count; ++l)
            if (!ir_verify_type(ir, function->locals[l].type)) {
                lang_diag(diagnostics, function_span,
                          "IR function `%s` has an invalid local type",
                          function->name);
                ok = false;
            }
        size_t value_slots = function->value_count == 0U
                           ? 1U : function->value_count;
        bool *defined = ir_resize(NULL, value_slots, sizeof(*defined));
        size_t *definition_blocks = ir_resize(
            NULL, value_slots, sizeof(*definition_blocks));
        size_t *definition_instructions = ir_resize(
            NULL, value_slots, sizeof(*definition_instructions));
        memset(defined, 0, value_slots * sizeof(*defined));
        for (size_t value = 0U; value < value_slots; ++value) {
            definition_blocks[value] = function->block_count;
            definition_instructions[value] = SIZE_MAX;
        }
        for (size_t b = 0U; b < function->block_count; ++b) {
            const IrBlock *block = &function->blocks[b];
            if (block->instruction_count != 0U &&
                block->instructions == NULL) {
                lang_diag(diagnostics, function_span,
                          "IR block b%zu in `%s` has no instruction storage",
                          b, function->name);
                ok = false;
                continue;
            }
            const IrTerminator *term = &block->terminator;
            bool valid_terminator = true;
            switch (term->kind) {
                case IR_TERM_JUMP:
                    valid_terminator =
                        term->target < function->block_count &&
                        term->value == IR_INVALID_ID &&
                        term->alternate == IR_INVALID_ID;
                    break;
                case IR_TERM_BRANCH:
                    valid_terminator =
                        term->target < function->block_count &&
                        term->alternate < function->block_count;
                    break;
                case IR_TERM_RETURN:
                    valid_terminator =
                        term->target == IR_INVALID_ID &&
                        term->alternate == IR_INVALID_ID;
                    break;
                case IR_TERM_PROPAGATE_EXCEPTION:
                case IR_TERM_TRAP:
                    valid_terminator =
                        term->value == IR_INVALID_ID &&
                        term->target == IR_INVALID_ID &&
                        term->alternate == IR_INVALID_ID;
                    break;
                case IR_TERM_NONE:
                default:
                    valid_terminator = false;
                    break;
            }
            if (!valid_terminator) {
                lang_diag(diagnostics, function_span,
                          "IR block b%zu in `%s` has an invalid terminator",
                          b, function->name);
                ok = false;
            }
            for (size_t i = 0U; i < block->instruction_count; ++i) {
                const IrInstruction *instruction =
                    &block->instructions[i];
                bool known_opcode = ir_verify_opcode_valid(instruction->opcode);
                bool valid_operands = known_opcode &&
                    ir_verify_operand_count(instruction) &&
                    (instruction->operand_count == 0U ||
                     instruction->operands != NULL);
                bool valid_labels = instruction->label_count == 0U ||
                                    instruction->labels != NULL;
                bool valid_result = known_opcode &&
                    ir_verify_result_type(ir, function, instruction);
                if (!known_opcode) {
                    lang_diag(diagnostics, instruction->span,
                              "IR instruction has unknown opcode %u",
                              (unsigned)instruction->opcode);
                    ok = false;
                } else if (!valid_operands) {
                    lang_diag(diagnostics, instruction->span,
                              "IR `%s` has an invalid operand count",
                              ir_opcode_name(instruction->opcode));
                    ok = false;
                }
                if (known_opcode && !valid_result) {
                    lang_diag(diagnostics, instruction->span,
                              "IR `%s` has an incompatible result type",
                              ir_opcode_name(instruction->opcode));
                    ok = false;
                }
                if (known_opcode && valid_operands && valid_labels &&
                    valid_result &&
                    !ir_verify_instruction_signature(
                        ir, function, instruction)) {
                    lang_diag(diagnostics, instruction->span,
                              "IR `%s` has an invalid typed signature",
                              ir_opcode_name(instruction->opcode));
                    ok = false;
                }
                if (instruction->result == IR_INVALID_ID) {
                    if (instruction->result_type != IR_INVALID_ID) {
                        lang_diag(diagnostics, instruction->span,
                                  "IR effect instruction has a result type");
                        ok = false;
                    }
                } else if (!ir_verify_value(function, instruction->result) ||
                           !ir_verify_type(ir, instruction->result_type) ||
                           defined[instruction->result] ||
                           function->value_types[instruction->result] !=
                           instruction->result_type) {
                    lang_diag(diagnostics, instruction->span,
                              "IR instruction has an invalid result");
                    ok = false;
                } else {
                    defined[instruction->result] = true;
                    definition_blocks[instruction->result] = b;
                    definition_instructions[instruction->result] = i;
                }
                if (((instruction->opcode == IR_OP_ELEMENT_BEGIN &&
                      instruction->index != IR_INVALID_ID) ||
                     instruction->opcode == IR_OP_LOCAL_LOAD ||
                     instruction->opcode == IR_OP_LOCAL_MOVE ||
                     instruction->opcode == IR_OP_LOCAL_STORE ||
                     instruction->opcode == IR_OP_LOCAL_DROP ||
                     instruction->opcode == IR_OP_LOCAL_INVALIDATE ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_GET ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_MOVE ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_BORROW ||
                     instruction->opcode == IR_OP_LOCAL_FIELD_SET ||
                     instruction->opcode == IR_OP_LOCAL_INDEX_GET ||
                     instruction->opcode == IR_OP_LOCAL_INDEX_SET ||
                     instruction->opcode == IR_OP_LOCAL_ENUM_IS ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ENUM_PAYLOAD_MOVE ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ITERATOR_HAS_NEXT ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ITERATOR_NEXT ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_PROPERTY ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_CSS_VALUE ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_PROPERTY_END ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_APPEND ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED ||
                     instruction->opcode ==
                         IR_OP_LOCAL_ELEMENT_FINISH) &&
                    instruction->index >= function->local_count) {
                    lang_diag(diagnostics, instruction->span,
                              "IR instruction references invalid local %u",
                              instruction->index);
                    ok = false;
                }
                if (instruction->opcode == IR_OP_PARAMETER &&
                    instruction->index >= function->parameter_count) {
                    lang_diag(diagnostics, instruction->span,
                              "IR parameter index is out of range");
                    ok = false;
                }
                if ((instruction->opcode == IR_OP_CALL_DIRECT ||
                     instruction->opcode == IR_OP_CALL_VIRTUAL) &&
                    instruction->index >= ir->function_count) {
                    lang_diag(diagnostics, instruction->span,
                              "IR direct call target is invalid");
                    ok = false;
                }
                if (instruction->label_count != 0U &&
                    instruction->labels == NULL) {
                    lang_diag(diagnostics, instruction->span,
                              "IR aggregate field mapping is missing");
                    ok = false;
                } else if (instruction->label_count != 0U &&
                    (instruction->opcode != IR_OP_AGGREGATE_MAKE ||
                     instruction->label_count !=
                         instruction->operand_count)) {
                    lang_diag(diagnostics, instruction->span,
                              "IR aggregate field mapping is malformed");
                    ok = false;
                }
                if (instruction->label_count != 0U &&
                    instruction->labels != NULL) {
                    bool *seen = ir_resize(
                        NULL, instruction->label_count,
                        sizeof(*seen));
                    memset(seen, 0,
                           instruction->label_count * sizeof(*seen));
                    for (size_t label = 0U;
                         label < instruction->label_count; ++label) {
                        uint32_t field = instruction->labels[label];
                        if (field >= instruction->label_count ||
                            seen[field]) {
                            lang_diag(
                                diagnostics, instruction->span,
                                "IR aggregate field mapping is not a permutation");
                            ok = false;
                            break;
                        }
                        seen[field] = true;
                    }
                    free(seen);
                }
            }
        }
        if (function->block_count > SIZE_MAX / function->block_count) {
            lang_diag(diagnostics, function_span,
                      "IR function `%s` has too many CFG blocks",
                      function->name);
            free(definition_instructions);
            free(definition_blocks);
            free(defined);
            ok = false;
            continue;
        }
        size_t dominator_slots =
            function->block_count * function->block_count;
        bool *reachable = ir_resize(
            NULL, function->block_count, sizeof(*reachable));
        bool *dominators = ir_resize(
            NULL, dominator_slots, sizeof(*dominators));
        memset(reachable, 0,
               function->block_count * sizeof(*reachable));
        memset(dominators, 0,
               dominator_slots * sizeof(*dominators));
        compute_dominators(function, reachable, dominators);
        for (size_t b = 0U; b < function->block_count; ++b) {
            const IrBlock *block = &function->blocks[b];
            if (block->instruction_count != 0U &&
                block->instructions == NULL)
                continue;
            for (size_t i = 0U; i < block->instruction_count; ++i) {
                const IrInstruction *instruction =
                    &block->instructions[i];
                if (instruction->operand_count != 0U &&
                    instruction->operands == NULL)
                    continue;
                for (size_t o = 0U; o < instruction->operand_count; ++o)
                    if (!value_available(
                            function, defined, definition_blocks,
                            definition_instructions, reachable,
                            dominators, instruction->operands[o], b, i)) {
                        lang_diag(diagnostics, instruction->span,
                                  "IR instruction operand is undefined or does not dominate its use");
                        ok = false;
                    }
            }
            const IrTerminator *term = &block->terminator;
            switch (term->kind) {
                case IR_TERM_BRANCH: {
                    IrTypeId condition_type;
                    if (!value_available(
                            function, defined, definition_blocks,
                            definition_instructions, reachable,
                            dominators, term->value, b,
                            block->instruction_count) ||
                        !ir_verify_value_type(ir, function, term->value,
                                    &condition_type) ||
                        ir->types[condition_type].shape != IR_TYPE_BOOL) {
                        lang_diag(diagnostics, term->span,
                                  "IR branch has invalid or non-dominating condition");
                        ok = false;
                    }
                    break;
                }
                case IR_TERM_RETURN:
                    if (!value_available(
                            function, defined, definition_blocks,
                            definition_instructions, reachable,
                            dominators, term->value, b,
                            block->instruction_count) ||
                        !ir_verify_value_is_type(function, term->value,
                                       function->async_result_type)) {
                        lang_diag(diagnostics, term->span,
                                  "IR return has invalid or non-dominating value");
                        ok = false;
                    }
                    break;
                case IR_TERM_JUMP:
                case IR_TERM_PROPAGATE_EXCEPTION:
                case IR_TERM_TRAP:
                case IR_TERM_NONE:
                default:
                    break;
            }
        }
        free(dominators);
        free(reachable);
        free(definition_instructions);
        free(definition_blocks);
        free(defined);
    }
    return ok;
}
