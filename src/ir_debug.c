#include "ir_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump_symbol(const IrInstruction *instruction) {
    if (instruction->symbol == NULL) return;
    fputs(" \"", stdout);
    for (size_t i = 0U; i < instruction->symbol_length; ++i) {
        unsigned char byte = (unsigned char)instruction->symbol[i];
        if (byte == '"' || byte == '\\') {
            fputc('\\', stdout);
            fputc((int)byte, stdout);
        } else if (byte >= 32U && byte < 127U) {
            fputc((int)byte, stdout);
        } else {
            printf("\\x%02x", (unsigned)byte);
        }
    }
    fputc('"', stdout);
}

static const char *type_shape_name(IrTypeShape shape) {
    static const char *names[] = {
        "error", "unit", "never", "bool", "signed-int",
        "unsigned-int", "float", "char", "string-view",
        "builtin-object", "array", "raw-pointer", "slice", "iterator",
        "element-builder", "function", "struct", "enum", "union"
    };
    size_t index = (size_t)shape;
    return index < sizeof(names) / sizeof(names[0])
         ? names[index] : "<invalid-shape>";
}

void lang_ir_dump_module(const IrModule *ir) {
    printf("target pointer=%u endian=%s\n",
           (unsigned)ir->target.pointer_size,
           ir->target.endianness == LANG_ENDIAN_LITTLE
               ? "little" : "big");
    fputs("types:\n", stdout);
    for (size_t i = 0U; i < ir->type_count; ++i) {
        const IrType *type = &ir->types[i];
        printf("  t%zu = %s shape=%s", i, type->name,
               type_shape_name(type->shape));
        if (type->bit_width != 0U)
            printf(" bits=%u", (unsigned)type->bit_width);
        if (type->target_layout_known)
            printf(" size=%zu align=%zu",
                   type->target_size,
                   type->target_alignment);
        if (type->element_type != IR_INVALID_ID)
            printf(" element=t%" PRIu32, type->element_type);
        if (type->error_type != IR_INVALID_ID)
            printf(" error=t%" PRIu32, type->error_type);
        if (type->base_type != IR_INVALID_ID)
            printf(" base=t%" PRIu32, type->base_type);
        if (type->interface_count != 0U) {
            fputs(" interfaces=[", stdout);
            for (size_t interface = 0U;
                 interface < type->interface_count; ++interface)
                printf("%st%" PRIu32,
                       interface == 0U ? "" : ",",
                       type->interface_types[interface]);
            fputc(']', stdout);
        }
        if (type->argument_count != 0U) {
            fputs(" args=[", stdout);
            for (size_t a = 0U; a < type->argument_count; ++a)
                printf("%st%" PRIu32, a == 0U ? "" : ",",
                       type->argument_types[a]);
            fputc(']', stdout);
        }
        if (type->field_count != 0U) {
            fputs(" fields=[", stdout);
            for (size_t field = 0U;
                 field < type->field_count; ++field)
                printf("%s%s:t%" PRIu32,
                       field == 0U ? "" : ",",
                       type->field_names[field],
                       type->field_types[field]);
            fputc(']', stdout);
        }
        if (type->variant_count != 0U) {
            fputs(" variants=[", stdout);
            for (size_t variant = 0U;
                 variant < type->variant_count; ++variant)
                if (type->variant_payload_types[variant] ==
                    IR_INVALID_ID)
                    printf("%s%s", variant == 0U ? "" : ",",
                           type->variant_names[variant]);
                else
                    printf("%s%s:t%" PRIu32,
                           variant == 0U ? "" : ",",
                           type->variant_names[variant],
                           type->variant_payload_types[variant]);
            fputc(']', stdout);
        }
        if (type->shape == IR_TYPE_ARRAY)
            printf(" length=%zu", type->array_length);
        if (type->shape == IR_TYPE_RAW_POINTER)
            printf(" %s", type->pointer_mutable ? "mut" : "const");
        if (type->module_name != NULL)
            printf(" module=%s", type->module_name);
        if (type->destructor_function != IR_INVALID_ID)
            printf(" destructor=f%" PRIu32,
                   type->destructor_function);
        if (type->requires_cleanup) fputs(" cleanup", stdout);
        fputc('\n', stdout);
    }
    for (size_t f = 0U; f < ir->function_count; ++f) {
        const IrFunction *function = &ir->functions[f];
        printf("\n%s%sfunction %s%s%s -> t%" PRIu32,
               function->is_entry ? "" :
                   function->is_public ? "public " : "private ",
               function->is_async ? "async " : "",
               function->module_name != NULL
                   ? function->module_name : "",
               function->module_name != NULL ? "::" : "",
               function->name, function->return_type);
        if (function->is_async)
            printf(" completes t%" PRIu32, function->async_result_type);
        fputs(":\n", stdout);
        for (size_t l = 0U; l < function->local_count; ++l)
            printf("  local %zu `%s` binding=%zu type=t%" PRIu32 "%s\n",
                   l, function->locals[l].name,
                   function->locals[l].binding_id,
                   function->locals[l].type,
                   function->locals[l].mutable_ ? " mut" :
                   function->locals[l].borrowed ? " readonly" : "");
        for (size_t b = 0U; b < function->block_count; ++b) {
            const IrBlock *block = &function->blocks[b];
            printf("b%zu:\n", b);
            for (size_t i = 0U; i < block->instruction_count; ++i) {
                const IrInstruction *instruction =
                    &block->instructions[i];
                fputs("  ", stdout);
                if (instruction->result != IR_INVALID_ID)
                    printf("v%" PRIu32 ":t%" PRIu32 " = ",
                           instruction->result,
                           instruction->result_type);
                fputs(ir_opcode_name(instruction->opcode), stdout);
                if (instruction->index != UINT32_MAX)
                    printf(" #%u", instruction->index);
                if (instruction->auxiliary != UINT32_MAX)
                    printf(" aux=%u", instruction->auxiliary);
                if (instruction->render_destination != UINT32_MAX)
                    printf(" render-to=#%u",
                           instruction->render_destination);
                if (instruction->opcode == IR_OP_CONST_INT ||
                    instruction->opcode == IR_OP_CONST_BOOL)
                    printf(" %" PRIu64, instruction->integer);
                if (instruction->opcode == IR_OP_CONST_FLOAT)
                    printf(" %.17g", instruction->floating);
                dump_symbol(instruction);
                for (size_t o = 0U;
                     o < instruction->operand_count; ++o)
                    if (instruction->label_count ==
                        instruction->operand_count)
                        printf("%s field%u=v%" PRIu32,
                               o == 0U ? "" : ",",
                               instruction->labels[o],
                               instruction->operands[o]);
                    else
                        printf("%s v%" PRIu32,
                               o == 0U ? "" : ",",
                               instruction->operands[o]);
                fputc('\n', stdout);
            }
            const IrTerminator *term = &block->terminator;
            switch (term->kind) {
                case IR_TERM_JUMP:
                    printf("  jump b%" PRIu32 "\n", term->target);
                    break;
                case IR_TERM_BRANCH:
                    printf("  branch v%" PRIu32 " b%" PRIu32
                           " b%" PRIu32 "\n",
                           term->value, term->target, term->alternate);
                    break;
                case IR_TERM_RETURN:
                    printf("  return v%" PRIu32 "\n", term->value);
                    break;
                case IR_TERM_PROPAGATE_EXCEPTION:
                    fputs("  propagate_exception\n", stdout);
                    break;
                case IR_TERM_TRAP:
                    fputs("  trap\n", stdout);
                    break;
                case IR_TERM_NONE:
                    fputs("  <missing terminator>\n", stdout);
                    break;
            }
        }
    }
}

static const char *ownership_reason_name(IrOwnershipReason reason) {
    switch (reason) {
        case IR_OWNERSHIP_LAST_USE: return "last-use";
        case IR_OWNERSHIP_LATER_USE: return "later-use";
        case IR_OWNERSHIP_BORROWED_SOURCE: return "borrowed-source";
        case IR_OWNERSHIP_EXPLICIT_COPY: return "explicit-copy";
        case IR_OWNERSHIP_REQUIRED_COPY: return "required-copy";
        case IR_OWNERSHIP_COLLECTION_CALLBACK:
            return "collection-callback";
    }
    return "unknown";
}

static int compare_ownership_decisions(const void *left,
                                       const void *right) {
    const IrOwnershipDecision *a =
        *(const IrOwnershipDecision *const *)left;
    const IrOwnershipDecision *b =
        *(const IrOwnershipDecision *const *)right;
    if (a->span.start < b->span.start) return -1;
    if (a->span.start > b->span.start) return 1;
    if (a->span.end < b->span.end) return -1;
    if (a->span.end > b->span.end) return 1;
    return 0;
}

void lang_ir_dump_ownership(const IrModule *ir, const LangSource *source,
                            bool copies_only) {
    size_t shown = 0U;
    for (size_t f = 0U; f < ir->function_count; ++f) {
        const IrFunction *function = &ir->functions[f];
        const IrOwnershipDecision **ordered = malloc(
            function->ownership_decision_count * sizeof(*ordered));
        if (ordered == NULL && function->ownership_decision_count != 0U) {
            fputs("fatal: out of memory while reporting ownership\n", stderr);
            return;
        }
        for (size_t i = 0U;
             i < function->ownership_decision_count; ++i)
            ordered[i] = &function->ownership_decisions[i];
        qsort(ordered, function->ownership_decision_count,
              sizeof(*ordered), compare_ownership_decisions);
        bool heading = false;
        for (size_t i = 0U;
             i < function->ownership_decision_count; ++i) {
            const IrOwnershipDecision *decision =
                ordered[i];
            if (copies_only && decision->kind != IR_OWNERSHIP_COPY)
                continue;
            if (!heading) {
                printf("function %s%s%s:\n",
                       function->module_name != NULL
                           ? function->module_name : "",
                       function->module_name != NULL ? "::" : "",
                       function->name);
                heading = true;
            }
            size_t line, column, begin, end;
            lang_source_line_info(
                source, decision->span.start,
                &line, &column, &begin, &end);
            const char *type = decision->type < ir->type_count
                ? ir->types[decision->type].name : "<unknown>";
            const char *local =
                decision->local < function->local_count
                ? function->locals[decision->local].name : "<value>";
            printf("  %s `%s%s%s` type=`%s` reason=%s at %s:%zu:%zu\n",
                   decision->kind == IR_OWNERSHIP_COPY ? "copy" : "move",
                   local, decision->field != NULL ? "." : "",
                   decision->field != NULL ? decision->field : "",
                   type, ownership_reason_name(decision->reason),
                   lang_source_path_at(source, decision->span.start),
                   line, column);
            ++shown;
        }
        free(ordered);
    }
    if (shown == 0U)
        fputs(copies_only ? "no semantic copies\n"
                          : "no ownership transfers\n", stdout);
}

void lang_ir_free_module(IrModule *ir) {
    for (size_t f = 0U; f < ir->function_count; ++f) {
        IrFunction *function = &ir->functions[f];
        for (size_t b = 0U; b < function->block_count; ++b) {
            IrBlock *block = &function->blocks[b];
            for (size_t i = 0U; i < block->instruction_count; ++i) {
                free(block->instructions[i].labels);
                free(block->instructions[i].argument_modes);
                if (block->instructions[i].native_call != NULL) {
                    free(block->instructions[i].native_call->parameter_types);
                    free(block->instructions[i].native_call->parameter_modes);
                    free(block->instructions[i].native_call);
                }
            }
            for (size_t i = 0U; i < block->instruction_count; ++i)
                free(block->instructions[i].operands);
            free(block->instructions);
        }
        free(function->parameters);
        free(function->ownership_decisions);
        free(function->static_css);
        free(function->locals);
        free(function->value_types);
        free(function->blocks);
    }
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].argument_types);
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].interface_types);
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].parameter_modes);
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].field_names);
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].field_types);
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].field_spans);
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].field_offsets);
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].variant_names);
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].variant_payload_types);
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].variant_spans);
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].variant_discriminants);
    for (size_t t = 0U; t < ir->type_count; ++t)
        free(ir->types[t].variant_payload_offsets);
    free(ir->functions);
    free(ir->static_fields);
    free(ir->interface_dispatches);
    free(ir->types);
    memset(ir, 0, sizeof(*ir));
}
