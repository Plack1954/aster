#include "ir_bytecode_internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *ir_bc_resize(void *pointer, size_t count, size_t size) {
    if (size != 0U && count > SIZE_MAX / size) {
        fputs("fatal: IR bytecode allocation is too large\n", stderr);
        exit(2);
    }
    void *result = realloc(pointer, count * size);
    if (result == NULL && count != 0U) {
        fputs("fatal: out of memory in IR bytecode backend\n", stderr);
        exit(2);
    }
    return result;
}

size_t emit_instruction(IrBytecodeBuilder *builder, OpCode opcode,
                               int32_t a, int32_t b, LangSpan span) {
    BytecodeFunction *function = builder->function;
    if (opcode == OP_BINARY_LOCAL_IMMEDIATE &&
        function->code_count != 0U) {
        Instruction *first =
            &function->code[function->code_count - 1U];
        if (first->op == OP_BINARY_LOCALS) {
            uint32_t first_slots = (uint32_t)first->b;
            uint32_t second = (uint32_t)b;
            size_t first_destination = (size_t)(
                (first_slots >> 20U) & UINT32_C(0x3ff));
            size_t second_source = (size_t)(
                second & UINT32_C(0x3ff));
            int32_t immediate = (int32_t)second >> 20U;
            uint32_t first_operation =
                (uint32_t)first->a & UINT32_C(0xff);
            uint32_t type =
                ((uint32_t)first->a >> 8U) & UINT32_C(0xff);
            uint32_t second_operation =
                (uint32_t)a & UINT32_C(0xff);
            uint32_t second_type =
                ((uint32_t)a >> 8U) & UINT32_C(0xff);
            if (first_destination == second_source &&
                type == second_type &&
                immediate >= -128 && immediate <= 127 &&
                first_operation >= OP_ADD_I64 &&
                first_operation <= OP_MUL_I64 &&
                second_operation >= OP_ADD_I64 &&
                second_operation <= OP_MUL_I64) {
                uint32_t destination =
                    (second >> 10U) & UINT32_C(0x3ff);
                first_slots &= ~(UINT32_C(0x3ff) << 20U);
                first_slots |= destination << 20U;
                first->op = OP_BINARY_LOCALS_IMMEDIATE;
                first->a = (int32_t)(
                    first_operation |
                    (type << 8U) |
                    (second_operation << 16U) |
                    (((uint32_t)immediate & UINT32_C(0xff)) << 24U));
                first->b = (int32_t)first_slots;
                function->spans[function->code_count - 1U] = span;
                return function->code_count - 1U;
            }
        }
    }
    if (opcode == OP_RETURN &&
        function->code_count > builder->block_start) {
        Instruction *move =
            &function->code[function->code_count - 1U];
        if (move->op == OP_MOVE_LOCAL && move->a >= 0 &&
            move->a < 1024) {
            if (!function->may_have_object_locals &&
                function->code_count >= builder->block_start + 2U) {
                Instruction *copy =
                    &function->code[function->code_count - 2U];
                if (copy->op == OP_COPY_LOCAL_TO &&
                    copy->b == move->a && copy->a >= 0 &&
                    copy->a < 1024) {
                    *copy = (Instruction){
                        OP_RETURN_LOCAL, copy->a, 0
                    };
                    function->spans[function->code_count - 2U] = span;
                    --function->code_count;
                    return function->code_count - 1U;
                }
            }
            *move = (Instruction){OP_RETURN_LOCAL, move->a, 1};
            function->spans[function->code_count - 1U] = span;
            return function->code_count - 1U;
        }
    }
    if ((opcode == OP_HTML_APPEND_FORMATTED_LOCAL ||
         opcode == OP_HTML_ATTR_APPEND_LOCAL ||
         opcode == OP_HTML_ATTR_LOCAL) &&
        function->code_count >= builder->block_start + 2U) {
        const Instruction *move =
            &function->code[function->code_count - 1U];
        const Instruction *constant =
            &function->code[function->code_count - 2U];
        if (move->op == OP_MOVE_LOCAL &&
            constant->op == OP_CONSTANT_LOCAL &&
            move->a == constant->b &&
            constant->a >= 0 &&
            (size_t)constant->a <
                builder->module->constant_count &&
            builder->module->constants[
                (size_t)constant->a].value.tag ==
                LANG_VALUE_STRING_VIEW) {
            OpCode replacement_op =
                opcode == OP_HTML_APPEND_FORMATTED_LOCAL
                ? OP_HTML_APPEND_CONSTANT_LOCAL
                : opcode == OP_HTML_ATTR_APPEND_LOCAL
                ? OP_HTML_ATTR_APPEND_CONSTANT_LOCAL
                : OP_HTML_ATTR_CONSTANT_LOCAL;
            int32_t replacement_b = constant->a;
            if (opcode == OP_HTML_ATTR_LOCAL) {
                if ((uint32_t)constant->a > UINT16_MAX ||
                    (uint32_t)b > UINT16_MAX)
                    goto no_html_constant_fusion;
                replacement_b = (int32_t)(
                    (uint32_t)constant->a |
                    ((uint32_t)b << 16U));
            }
            size_t replacement = function->code_count - 2U;
            function->code[replacement] = (Instruction){
                replacement_op, a, replacement_b
            };
            function->spans[replacement] = span;
            --function->code_count;
            return replacement;
        }
    }
no_html_constant_fusion:
    if ((opcode == OP_HTML_APPEND_FORMATTED_LOCAL ||
         opcode == OP_HTML_ATTR_APPEND_LOCAL) &&
        function->code_count >= builder->block_start + 2U) {
        const Instruction *move =
            &function->code[function->code_count - 1U];
        const Instruction *copy =
            &function->code[function->code_count - 2U];
        if (move->op == OP_MOVE_LOCAL &&
            copy->op == OP_COPY_LOCAL_TO &&
            move->a == copy->b &&
            copy->a >= 0 && copy->a < 1024) {
            size_t replacement = function->code_count - 2U;
            function->code[replacement] = (Instruction){
                opcode == OP_HTML_APPEND_FORMATTED_LOCAL
                    ? OP_HTML_APPEND_VALUE_LOCAL
                    : OP_HTML_ATTR_APPEND_VALUE_LOCAL,
                a, copy->a
            };
            function->spans[replacement] = span;
            --function->code_count;
            return replacement;
        }
    }
    if (opcode == OP_JUMP_IF_FALSE &&
        function->code_count >= builder->block_start + 3U) {
        const Instruction *comparison =
            &function->code[function->code_count - 1U];
        const Instruction *right =
            &function->code[function->code_count - 2U];
        const Instruction *left =
            &function->code[function->code_count - 3U];
        TypeKind kind = (TypeKind)comparison->a;
        bool integer_kind =
            (kind >= TYPE_I8 && kind <= TYPE_USIZE) ||
            kind == TYPE_CHAR;
        if (comparison->op >= OP_EQ &&
            comparison->op <= OP_GE_I64 &&
            integer_kind &&
            left->op == OP_MOVE_LOCAL &&
            right->op == OP_MOVE_LOCAL &&
            function->code_count >=
                builder->block_start + 5U) {
            const Instruction *left_source =
                &function->code[
                    function->code_count - 5U];
            const Instruction *right_source =
                &function->code[
                    function->code_count - 4U];
            if (left_source->op == OP_COPY_LOCAL_TO &&
                left_source->b == left->a &&
                right_source->op == OP_CONSTANT_LOCAL &&
                right_source->b == right->a &&
                left_source->a >= 0 &&
                left_source->a < 1024 &&
                right_source->a >= 0 &&
                right_source->a <= UINT16_MAX) {
                uint32_t packed =
                    (uint32_t)left_source->a |
                    ((uint32_t)right_source->a << 10U) |
                    ((uint32_t)comparison->op << 26U);
                size_t replacement =
                    function->code_count - 5U;
                function->code[replacement] =
                    (Instruction){
                        OP_COMPARE_LOCAL_CONSTANT_BRANCH,
                        a, (int32_t)packed
                    };
                function->spans[replacement] = span;
                function->code_count -= 4U;
                return replacement;
            }
        }
        if (comparison->op >= OP_EQ &&
            comparison->op <= OP_GE_I64 &&
            integer_kind &&
            left->op == OP_MOVE_LOCAL &&
            right->op == OP_MOVE_LOCAL &&
            left->a >= 0 && left->a < 1024 &&
            right->a >= 0 && right->a < 1024) {
            uint32_t packed =
                (uint32_t)left->a |
                ((uint32_t)right->a << 10U) |
                ((uint32_t)comparison->op << 20U);
            function->code[function->code_count - 3U] =
                (Instruction){
                    OP_COMPARE_BRANCH, a, (int32_t)packed
                };
            function->spans[function->code_count - 3U] = span;
            function->code_count -= 2U;
            return function->code_count - 1U;
        }
    }
    if (opcode == OP_STORE_LOCAL &&
        function->code_count >= builder->block_start + 10U) {
        Instruction *search_call =
            &function->code[function->code_count - 1U];
        Instruction *needle_move =
            &function->code[function->code_count - 2U];
        Instruction *value_move =
            &function->code[function->code_count - 3U];
        Instruction *needle_copy =
            &function->code[function->code_count - 4U];
        Instruction *discard =
            &function->code[function->code_count - 5U];
        Instruction *needle_set =
            &function->code[function->code_count - 6U];
        Instruction *from_call =
            &function->code[function->code_count - 7U];
        Instruction *constant_move =
            &function->code[function->code_count - 8U];
        Instruction *constant =
            &function->code[function->code_count - 9U];
        Instruction *source_copy =
            &function->code[function->code_count - 10U];
        int kind = search_call->a == -91 ? 0
            : search_call->a == -92 ? 2
            : search_call->a == -93 ? 3
            : search_call->a == -94 ? 4 : -1;
        if (search_call->op == OP_CALL && search_call->b == 2 &&
            kind >= 0 && needle_move->op == OP_MOVE_LOCAL &&
            value_move->op == OP_MOVE_LOCAL &&
            needle_copy->op == OP_COPY_LOCAL_TO &&
            discard->op == OP_POP && needle_set->op == OP_SET_LOCAL &&
            from_call->op == OP_CALL && from_call->a == -11 &&
            from_call->b == 1 &&
            constant_move->op == OP_MOVE_LOCAL &&
            constant->op == OP_CONSTANT_LOCAL &&
            source_copy->op == OP_COPY_LOCAL_TO &&
            needle_move->a == needle_copy->b &&
            needle_copy->a == needle_set->a &&
            constant_move->a == constant->b &&
            value_move->a == source_copy->b &&
            source_copy->a >= 0 && source_copy->a < 1024 &&
            a >= 0 && a < 1024) {
            uint32_t packed = (uint32_t)source_copy->a |
                ((uint32_t)a << 10U) |
                ((uint32_t)kind << 20U);
            free(function->call_sites[
                function->code_count - 7U].argument_modes);
            free(function->call_sites[
                function->code_count - 1U].argument_modes);
            memset(&function->call_sites[
                       function->code_count - 7U], 0,
                   sizeof(*function->call_sites));
            memset(&function->call_sites[
                       function->code_count - 1U], 0,
                   sizeof(*function->call_sites));
            *source_copy = (Instruction){
                OP_STRING_SEARCH_LOCAL_CONSTANT,
                (int32_t)packed, constant->a
            };
            function->spans[function->code_count - 10U] = span;
            function->code_count -= 9U;
            return function->code_count - 1U;
        }
    }
    if (opcode == OP_STORE_LOCAL &&
        function->code_count >= builder->block_start + 11U) {
        Instruction *search_call =
            &function->code[function->code_count - 1U];
        Instruction *needle_move =
            &function->code[function->code_count - 2U];
        Instruction *value_move =
            &function->code[function->code_count - 3U];
        Instruction *needle_store =
            &function->code[function->code_count - 4U];
        Instruction *from_call =
            &function->code[function->code_count - 5U];
        Instruction *constant_move =
            &function->code[function->code_count - 6U];
        Instruction *constant =
            &function->code[function->code_count - 7U];
        Instruction *value_store =
            &function->code[function->code_count - 8U];
        Instruction *clone =
            &function->code[function->code_count - 9U];
        Instruction *source_move =
            &function->code[function->code_count - 10U];
        Instruction *source_copy =
            &function->code[function->code_count - 11U];
        int kind = search_call->a == -91 ? 0
            : search_call->a == -92 ? 2
            : search_call->a == -93 ? 3
            : search_call->a == -94 ? 4 : -1;
        if (search_call->op == OP_CALL && search_call->b == 2 &&
            kind >= 0 && needle_move->op == OP_MOVE_LOCAL &&
            value_move->op == OP_MOVE_LOCAL &&
            needle_store->op == OP_STORE_LOCAL &&
            from_call->op == OP_CALL && from_call->a == -11 &&
            from_call->b == 1 &&
            constant_move->op == OP_MOVE_LOCAL &&
            constant->op == OP_CONSTANT_LOCAL &&
            value_store->op == OP_STORE_LOCAL &&
            clone->op == OP_CLONE && clone->a == 0 &&
            source_move->op == OP_MOVE_LOCAL &&
            source_copy->op == OP_COPY_LOCAL_TO &&
            needle_move->a == needle_store->a &&
            constant_move->a == constant->b &&
            value_move->a == value_store->a &&
            source_move->a == source_copy->b &&
            source_copy->a >= 0 && source_copy->a < 1024 &&
            a >= 0 && a < 1024) {
            uint32_t packed = (uint32_t)source_copy->a |
                ((uint32_t)a << 10U) |
                ((uint32_t)kind << 20U);
            free(function->call_sites[
                function->code_count - 5U].argument_modes);
            free(function->call_sites[
                function->code_count - 1U].argument_modes);
            memset(&function->call_sites[
                       function->code_count - 5U], 0,
                   sizeof(*function->call_sites));
            memset(&function->call_sites[
                       function->code_count - 1U], 0,
                   sizeof(*function->call_sites));
            *source_copy = (Instruction){
                OP_STRING_SEARCH_LOCAL_CONSTANT,
                (int32_t)packed, constant->a
            };
            function->spans[function->code_count - 11U] = span;
            function->code_count -= 10U;
            return function->code_count - 1U;
        }
    }
    if (opcode == OP_STORE_LOCAL &&
        function->code_count >= builder->block_start + 4U) {
        Instruction *call =
            &function->code[function->code_count - 1U];
        Instruction *start =
            &function->code[function->code_count - 2U];
        Instruction *needle =
            &function->code[function->code_count - 3U];
        Instruction *value =
            &function->code[function->code_count - 4U];
        if (call->op == OP_CALL && call->a == -91 && call->b == 3 &&
            value->op == OP_MOVE_LOCAL &&
            needle->op == OP_MOVE_LOCAL &&
            start->op == OP_MOVE_LOCAL &&
            value->a >= 0 && value->a < 1024 &&
            needle->a >= 0 && needle->a < 1024 &&
            start->a >= 0 && start->a < 1024 &&
            a >= 0 && a < 1024) {
            uint32_t packed = (uint32_t)value->a |
                ((uint32_t)needle->a << 10U) |
                ((uint32_t)a << 20U);
            free(function->call_sites[
                function->code_count - 1U].argument_modes);
            function->call_sites[
                function->code_count - 1U].argument_modes = NULL;
            function->call_sites[
                function->code_count - 1U].argument_count = 0U;
            *value = (Instruction){
                OP_STRING_SEARCH_LOCAL, (int32_t)packed,
                start->a | (1 << 10)
            };
            function->spans[function->code_count - 4U] = span;
            function->code_count -= 3U;
            return function->code_count - 1U;
        }
    }
    if (opcode == OP_STORE_LOCAL &&
        function->code_count >= builder->block_start + 3U) {
        Instruction *call =
            &function->code[function->code_count - 1U];
        Instruction *needle =
            &function->code[function->code_count - 2U];
        Instruction *value =
            &function->code[function->code_count - 3U];
        int kind = call->a == -91 ? 0
            : call->a == -92 ? 2
            : call->a == -93 ? 3
            : call->a == -94 ? 4 : -1;
        if (call->op == OP_CALL && call->b == 2 && kind >= 0 &&
            value->op == OP_MOVE_LOCAL &&
            needle->op == OP_MOVE_LOCAL &&
            value->a >= 0 && value->a < 1024 &&
            needle->a >= 0 && needle->a < 1024 &&
            a >= 0 && a < 1024) {
            uint32_t packed = (uint32_t)value->a |
                ((uint32_t)needle->a << 10U) |
                ((uint32_t)a << 20U);
            free(function->call_sites[
                function->code_count - 1U].argument_modes);
            function->call_sites[
                function->code_count - 1U].argument_modes = NULL;
            function->call_sites[
                function->code_count - 1U].argument_count = 0U;
            *value = (Instruction){
                OP_STRING_SEARCH_LOCAL, (int32_t)packed, kind << 10
            };
            function->spans[function->code_count - 3U] = span;
            function->code_count -= 2U;
            return function->code_count - 1U;
        }
    }
    if (opcode == OP_STORE_LOCAL &&
        function->code_count >= builder->block_start + 3U) {
        Instruction *call =
            &function->code[function->code_count - 1U];
        Instruction *move =
            &function->code[function->code_count - 2U];
        Instruction *copy =
            &function->code[function->code_count - 3U];
        if (call->op == OP_CALL && call->a == -22 &&
            ((uint32_t)call->b & UINT32_C(0xff)) == 1U &&
            move->op == OP_MOVE_LOCAL &&
            copy->op == OP_COPY_LOCAL_TO &&
            copy->b == move->a &&
            a >= 0 && a < 1024) {
            *copy = (Instruction){
                OP_TEXT_LEN_LOCAL, copy->a, a
            };
            function->spans[function->code_count - 3U] = span;
            function->code_count -= 2U;
            return function->code_count - 1U;
        }
    }
    if (opcode == OP_STORE_LOCAL &&
        function->code_count >= builder->block_start + 2U) {
        Instruction *call =
            &function->code[function->code_count - 1U];
        Instruction *move =
            &function->code[function->code_count - 2U];
        if (call->op == OP_CALL && call->a == -14 &&
            call->b == 1 && move->op == OP_MOVE_LOCAL &&
            a >= 0 && a < 1024) {
            *move = (Instruction){
                OP_STRING_BUILDER_FINISH_LOCAL, move->a, a
            };
            function->spans[function->code_count - 2U] = span;
            --function->code_count;
            return function->code_count - 1U;
        }
    }
    if (opcode == OP_STORE_LOCAL &&
        function->code_count >= builder->block_start + 3U) {
        const Instruction *operation =
            &function->code[function->code_count - 1U];
        const Instruction *right =
            &function->code[function->code_count - 2U];
        const Instruction *left =
            &function->code[function->code_count - 3U];
        bool binary =
            operation->op >= OP_ADD_I64 &&
            operation->op <= OP_REM_I64;
        if (binary &&
            left->op == OP_MOVE_LOCAL &&
            right->op == OP_MOVE_LOCAL &&
            left->a >= 0 && left->a < 1024 &&
            right->a >= 0 && right->a < 1024 &&
            a >= 0 && a < 1024) {
            uint32_t packed_slots =
                (uint32_t)left->a |
                ((uint32_t)right->a << 10U) |
                ((uint32_t)a << 20U);
            int32_t packed_operation =
                (int32_t)((uint32_t)operation->op |
                          ((uint32_t)operation->a << 8U));
            if (function->code_count >=
                    builder->block_start + 5U) {
                const Instruction *left_source =
                    &function->code[function->code_count - 5U];
                const Instruction *right_source =
                    &function->code[function->code_count - 4U];
                if (left_source->op == OP_COPY_LOCAL_TO &&
                    right_source->op == OP_COPY_LOCAL_TO &&
                    left_source->b == left->a &&
                    right_source->b == right->a &&
                    left_source->a >= 0 &&
                    left_source->a < 1024 &&
                    right_source->a >= 0 &&
                    right_source->a < 1024) {
                    uint32_t direct_slots =
                        (uint32_t)left_source->a |
                        ((uint32_t)right_source->a << 10U) |
                        ((uint32_t)a << 20U);
                    size_t replacement =
                        function->code_count - 5U;
                    function->code[replacement] =
                        (Instruction){
                            OP_BINARY_LOCALS,
                            packed_operation,
                            (int32_t)direct_slots
                        };
                    function->spans[replacement] =
                        function->spans[
                            function->code_count - 1U];
                    function->code_count -= 4U;
                    return replacement;
                }
                if (right_source->op == OP_CONSTANT_LOCAL &&
                    right_source->b == right->a &&
                    right_source->a >= 0 &&
                    (size_t)right_source->a <
                        builder->module->constant_count &&
                    builder->module->constants[
                        (size_t)right_source->a].value.tag ==
                        LANG_VALUE_I64) {
                    int64_t immediate =
                        builder->module->constants[
                            (size_t)right_source->a].value.as.i64;
                    if (immediate >= -2048 && immediate <= 2047) {
                        bool direct_copy =
                            left_source->op == OP_COPY_LOCAL_TO &&
                            left_source->b == left->a &&
                            left_source->a >= 0 &&
                            left_source->a < 1024;
                        uint32_t packed =
                            (uint32_t)(direct_copy
                                ? left_source->a : left->a) |
                            ((uint32_t)a << 10U) |
                            (((uint32_t)(int32_t)immediate &
                              UINT32_C(0xfff)) << 20U);
                        size_t replacement =
                            function->code_count -
                            (direct_copy ? 5U : 4U);
                        function->code[replacement] =
                            (Instruction){
                                OP_BINARY_LOCAL_IMMEDIATE,
                                packed_operation, (int32_t)packed
                            };
                        function->spans[replacement] =
                            function->spans[
                                function->code_count - 1U];
                        function->code_count -=
                            direct_copy ? 4U : 3U;
                        return replacement;
                    }
                }
            }
            function->code[function->code_count - 3U] =
                (Instruction){
                    OP_BINARY_LOCALS, packed_operation,
                    (int32_t)packed_slots
                };
            function->spans[function->code_count - 3U] =
                function->spans[function->code_count - 1U];
            function->code_count -= 2U;
            return function->code_count - 1U;
        }
    }
    if (opcode == OP_STORE_LOCAL &&
        function->code_count > builder->block_start) {
        Instruction *previous =
            &function->code[function->code_count - 1U];
        if (previous->op == OP_MOVE_LOCAL &&
            function->code_count >= builder->block_start + 2U &&
            previous->a >= 0 && previous->a < 1024 &&
            a >= 0 && a < 1024) {
            Instruction *producer =
                &function->code[function->code_count - 2U];
            if (producer->op == OP_BINARY_LOCALS) {
                uint32_t packed_slots = (uint32_t)producer->b;
                size_t producer_destination =
                    (size_t)((packed_slots >> 20U) &
                             UINT32_C(0x3ff));
                if (producer_destination ==
                    (size_t)previous->a) {
                    packed_slots &=
                        ~(UINT32_C(0x3ff) << 20U);
                    packed_slots |= (uint32_t)a << 20U;
                    producer->b = (int32_t)packed_slots;
                    --function->code_count;
                    return function->code_count - 1U;
                }
            } else if (producer->op ==
                       OP_BINARY_LOCAL_IMMEDIATE) {
                uint32_t packed =
                    (uint32_t)producer->b;
                size_t producer_destination =
                    (size_t)((packed >> 10U) &
                             UINT32_C(0x3ff));
                if (producer_destination ==
                    (size_t)previous->a) {
                    packed &=
                        ~(UINT32_C(0x3ff) << 10U);
                    packed |= (uint32_t)a << 10U;
                    producer->b = (int32_t)packed;
                    --function->code_count;
                    return function->code_count - 1U;
                }
            }
        }
        if (previous->op == OP_CONSTANT) {
            previous->op = OP_CONSTANT_LOCAL;
            previous->b = a;
            return function->code_count - 1U;
        }
        if (previous->op == OP_LOAD_LOCAL) {
            previous->op = OP_COPY_LOCAL_TO;
            previous->b = a;
            return function->code_count - 1U;
        }
        if (previous->op == OP_MOVE_LOCAL) {
            previous->op = OP_MOVE_LOCAL_TO;
            previous->b = a;
            return function->code_count - 1U;
        }
    }
    if (opcode == OP_POP &&
        function->code_count > builder->block_start) {
        const Instruction *previous =
            &function->code[function->code_count - 1U];
        if ((previous->op == OP_CALL ||
             previous->op == OP_CALL_NATIVE) &&
            ((uint32_t)previous->b & UINT32_C(0xff)) == 2U &&
            function->code_count >= builder->block_start + 4U) {
            bool plain_append =
                previous->op == OP_CALL && previous->a == -13;
            bool formatted_append = false;
            if (previous->op == OP_CALL_NATIVE &&
                previous->a >= 0 &&
                (size_t)previous->a <
                    builder->module->constant_count) {
                LangStringView name = builder->module->constants[
                    (size_t)previous->a].value.as.string;
                static const char formatted_name[] =
                    "__interpolation_builder_append_formatted";
                formatted_append =
                    name.length == sizeof(formatted_name) - 1U &&
                    memcmp(name.data, formatted_name,
                           sizeof(formatted_name) - 1U) == 0;
            }
            const Instruction *value_move =
                &function->code[function->code_count - 2U];
            const Instruction *builder_move =
                &function->code[function->code_count - 3U];
            const Instruction *builder_copy =
                &function->code[function->code_count - 4U];
            if ((plain_append || formatted_append) &&
                value_move->op == OP_MOVE_LOCAL &&
                builder_move->op == OP_MOVE_LOCAL &&
                builder_copy->op == OP_COPY_LOCAL_TO &&
                builder_copy->b == builder_move->a) {
                size_t replacement = function->code_count - 4U;
                int32_t value_operand = value_move->a;
                OpCode replacement_op =
                    OP_STRING_BUILDER_APPEND_VALUE_LOCAL;
                if (function->code_count >=
                    builder->block_start + 5U) {
                    const Instruction *value_source =
                        &function->code[
                            function->code_count - 5U];
                    if (value_source->op == OP_COPY_LOCAL_TO &&
                        value_source->b == value_move->a) {
                        value_operand = value_source->a;
                        --replacement;
                    } else if (plain_append &&
                               value_source->op ==
                                   OP_CONSTANT_LOCAL &&
                               value_source->b == value_move->a &&
                               value_source->a >= 0 &&
                               (size_t)value_source->a <
                                   builder->module->constant_count &&
                               builder->module->constants[
                                   (size_t)value_source->a].value.tag ==
                                   LANG_VALUE_STRING_VIEW) {
                        value_operand = value_source->a;
                        replacement_op =
                            OP_STRING_BUILDER_APPEND_CONSTANT_LOCAL;
                        --replacement;
                    }
                }
                function->code[replacement] = (Instruction){
                    replacement_op, builder_copy->a, value_operand
                };
                function->spans[replacement] = span;
                function->code_count = replacement + 1U;
                return function->code_count;
            }
        }
        if (previous->op == OP_SET_LOCAL &&
            function->code_count >= builder->block_start + 2U) {
            Instruction *producer =
                &function->code[function->code_count - 2U];
            if (producer->op == OP_CALL &&
                producer->a == -12 && producer->b == 0 &&
                previous->a >= 0 && previous->a < 1024) {
                producer->op = OP_STRING_BUILDER_NEW_LOCAL;
                producer->a = previous->a;
                --function->code_count;
                return function->code_count;
            }
            if ((producer->op == OP_HTML_BEGIN ||
                 producer->op == OP_HTML_FRAGMENT) &&
                previous->a >= 0 && previous->a < 1024 &&
                producer->b >= 0 && producer->b <= 1024) {
                producer->op = producer->op == OP_HTML_BEGIN
                             ? OP_HTML_BEGIN_LOCAL
                             : OP_HTML_FRAGMENT_LOCAL;
                producer->b = (int32_t)(
                    (uint32_t)previous->a |
                    ((uint32_t)producer->b << 10U));
                --function->code_count;
                return function->code_count;
            }
        }
        if (previous->op == OP_UNIT) {
            --function->code_count;
            return function->code_count;
        }
        if (previous->op == OP_SET_LOCAL &&
            function->code_count >= builder->block_start + 2U) {
            Instruction *source =
                &function->code[function->code_count - 2U];
            if (source->op == OP_CALL &&
                source->a >= 0 &&
                source->b >= 0 && source->b < 1024 &&
                previous->a >= 0 && previous->a < 1024) {
                if (source->b == 2 &&
                    function->code_count >=
                        builder->block_start + 6U) {
                    const Instruction *first_copy =
                        &function->code[
                            function->code_count - 6U];
                    const Instruction *second_copy =
                        &function->code[
                            function->code_count - 5U];
                    const Instruction *first_move =
                        &function->code[
                            function->code_count - 4U];
                    const Instruction *second_move =
                        &function->code[
                            function->code_count - 3U];
                    if (first_copy->op == OP_COPY_LOCAL_TO &&
                        second_copy->op == OP_COPY_LOCAL_TO &&
                        first_move->op == OP_MOVE_LOCAL &&
                        second_move->op == OP_MOVE_LOCAL &&
                        first_copy->b == first_move->a &&
                        second_copy->b == second_move->a &&
                        first_copy->a >= 0 &&
                        first_copy->a < 1024 &&
                        second_copy->a >= 0 &&
                        second_copy->a < 1024) {
                        uint32_t packed =
                            (uint32_t)first_copy->a |
                            ((uint32_t)second_copy->a << 10U) |
                            ((uint32_t)previous->a << 20U);
                        function->code[
                            function->code_count - 6U] =
                            (Instruction){
                                OP_CALL_LOCAL_2_COPY,
                                source->a, (int32_t)packed
                            };
                        function->spans[
                            function->code_count - 6U] =
                            function->spans[
                                function->code_count - 1U];
                        function->code_count -= 5U;
                        return function->code_count;
                    }
                }
                source->op = OP_CALL_LOCAL;
                source->b =
                    source->b | (previous->a << 10);
                --function->code_count;
                return function->code_count;
            }
            if (source->op == OP_MOVE_LOCAL) {
                if (function->code_count >=
                        builder->block_start + 3U) {
                    Instruction *producer =
                        &function->code[
                            function->code_count - 3U];
                    if (producer->op == OP_BINARY_LOCALS &&
                        source->a >= 0 && source->a < 1024 &&
                        previous->a >= 0 &&
                        previous->a < 1024) {
                        uint32_t packed_slots =
                            (uint32_t)producer->b;
                        size_t producer_destination =
                            (size_t)((packed_slots >> 20U) &
                                     UINT32_C(0x3ff));
                        if (producer_destination ==
                            (size_t)source->a) {
                            packed_slots &=
                                ~(UINT32_C(0x3ff) << 20U);
                            packed_slots |=
                                (uint32_t)previous->a << 20U;
                            producer->b =
                                (int32_t)packed_slots;
                            function->code_count -= 2U;
                            return function->code_count;
                        }
                    } else if (producer->op ==
                                   OP_BINARY_LOCAL_IMMEDIATE &&
                               source->a >= 0 &&
                               source->a < 1024 &&
                               previous->a >= 0 &&
                               previous->a < 1024) {
                        uint32_t packed =
                            (uint32_t)producer->b;
                        size_t producer_destination =
                            (size_t)((packed >> 10U) &
                                     UINT32_C(0x3ff));
                        if (producer_destination ==
                            (size_t)source->a) {
                            packed &=
                                ~(UINT32_C(0x3ff) << 10U);
                            packed |=
                                (uint32_t)previous->a << 10U;
                            producer->b = (int32_t)packed;
                            function->code_count -= 2U;
                            return function->code_count;
                        }
                    }
                    if (producer->op == OP_MOVE_LOCAL_TO &&
                        producer->a == previous->a &&
                        producer->b == source->a) {
                        function->code_count -= 3U;
                        return function->code_count;
                    }
                }
                source->op = OP_MOVE_LOCAL_TO;
                source->b = previous->a;
                --function->code_count;
                return function->code_count;
            }
        }
    }
    if (opcode == OP_MOVE_LOCAL &&
        function->code_count > builder->block_start) {
        const Instruction *previous =
            &function->code[function->code_count - 1U];
        if (previous->op == OP_STORE_LOCAL &&
            previous->a == a) {
            --function->code_count;
            return function->code_count;
        }
    }
    if (function->code_count == function->code_capacity) {
        size_t old_capacity = function->code_capacity;
        size_t next = function->code_capacity == 0U
                    ? 32U : function->code_capacity * 2U;
        function->code = ir_bc_resize(
            function->code, next, sizeof(*function->code));
        function->spans = ir_bc_resize(
            function->spans, next, sizeof(*function->spans));
        function->call_sites = ir_bc_resize(
            function->call_sites, next, sizeof(*function->call_sites));
        memset(function->call_sites + old_capacity, 0,
               (next - old_capacity) * sizeof(*function->call_sites));
        function->code_capacity = next;
    }
    size_t index = function->code_count++;
    free(function->call_sites[index].argument_modes);
    function->call_sites[index] = (BytecodeCallSite){0};
    function->code[index] = (Instruction){opcode, a, b};
    function->spans[index] = span;
    return index;
}

size_t add_constant(IrBytecodeBuilder *builder, LangValue value,
                           const char *data, size_t length) {
    BytecodeModule *module = builder->module;
    if (module->constant_count == module->constant_capacity) {
        size_t next = module->constant_capacity == 0U
                    ? 16U : module->constant_capacity * 2U;
        module->constants = ir_bc_resize(
            module->constants, next, sizeof(*module->constants));
        memset(module->constants + module->constant_capacity, 0,
               (next - module->constant_capacity) *
               sizeof(*module->constants));
        module->constant_capacity = next;
    }
    size_t index = module->constant_count++;
    Constant *constant = &module->constants[index];
    memset(constant, 0, sizeof(*constant));
    constant->value = value;
    if (data != NULL) {
        constant->owned_string = ir_bc_resize(NULL, length + 1U, 1U);
        if (length != 0U)
            memcpy(constant->owned_string, data, length);
        constant->owned_string[length] = '\0';
        constant->value.tag = LANG_VALUE_STRING_VIEW;
        constant->value.as.string.data = constant->owned_string;
        constant->value.as.string.length = length;
    }
    return index;
}

bool as_i32(IrBytecodeBuilder *builder, size_t value,
                   LangSpan span, int32_t *result) {
    if (value > (size_t)INT32_MAX) {
        lang_diag(builder->diagnostics, span,
                  "IR bytecode operand exceeds the VM encoding limit");
        builder->failed = true;
        return false;
    }
    *result = (int32_t)value;
    return true;
}

size_t value_slot(const IrBytecodeBuilder *builder,
                         IrValueId value) {
    return builder->value_base + (size_t)value;
}

void move_value(IrBytecodeBuilder *builder, IrValueId value,
                       LangSpan span) {
    int32_t slot;
    if (as_i32(builder, value_slot(builder, value), span, &slot))
        (void)emit_instruction(
            builder, OP_MOVE_LOCAL, slot, 0, span);
}

void store_result(IrBytecodeBuilder *builder,
                         const IrInstruction *instruction) {
    if (instruction->result == IR_INVALID_ID) return;
    int32_t slot;
    if (as_i32(builder,
               value_slot(builder, instruction->result),
               instruction->span, &slot))
        (void)emit_instruction(
            builder, OP_STORE_LOCAL, slot, 0, instruction->span);
}

void add_patch(IrBytecodeBuilder *builder, size_t instruction,
                      IrBlockId target) {
    if (builder->patch_count == builder->patch_capacity) {
        size_t next = builder->patch_capacity == 0U
                    ? 16U : builder->patch_capacity * 2U;
        builder->patches = ir_bc_resize(
            builder->patches, next, sizeof(*builder->patches));
        builder->patch_capacity = next;
    }
    builder->patches[builder->patch_count++] =
        (IrBytecodePatch){instruction, target};
}

int32_t builtin_index(const char *name) {
    if (name == NULL) return INT32_MIN;
    if (strcmp(name, "Console::WriteLine") == 0) return -1;
    if (strcmp(name, "Console::Error::WriteLine") == 0) return -2;
    if (strcmp(name, "Html::ToHtmlString") == 0) return -3;
    if (strcmp(name, "Buffer::allocate") == 0) return -4;
    if (strcmp(name, "Arena::new") == 0) return -5;
    if (strcmp(name, "ArenaAlloc") == 0) return -6;
    if (strcmp(name, "ArenaReset") == 0) return -7;
    if (strcmp(name, "raw_load_i64") == 0) return -8;
    if (strcmp(name, "raw_store_i64") == 0) return -9;
    if (strcmp(name, "panic") == 0 ||
        strcmp(name, "trap") == 0) return -10;
    if (strcmp(name, "String::from") == 0) return -11;
    if (strcmp(name, "StringBuilder::New") == 0) return -12;
    if (strcmp(name, "StringBuilder::Append") == 0) return -13;
    if (strcmp(name, "StringBuilder::Finish") == 0) return -14;
    if (strcmp(name, "Url::relative") == 0) return -15;
    if (strcmp(name, "Url::fragment") == 0) return -16;
    if (strcmp(name, "List::New") == 0) return -17;
    if (strcmp(name, "List::Add") == 0) return -18;
    if (strcmp(name, "List::Count") == 0) return -19;
    if (strcmp(name, "Html::UnsafeRaw") == 0) return -20;
    if (strcmp(name, "BufferAsMutSlice") == 0) return -21;
    if (strcmp(name, "TextLen") == 0) return -22;
    if (strcmp(name, "List::Get") == 0) return -23;
    if (strcmp(name, "StringBuilder::AppendByte") == 0) return -25;
    if (strcmp(name, "StringBuilder::ToString") == 0) return -26;
    if (strcmp(name, "StringBuilder::Length") == 0) return -27;
    if (strcmp(name, "StringBuilder::Clear") == 0) return -28;
    if (strcmp(name, "List::Capacity") == 0) return -29;
    if (strcmp(name, "List::Clear") == 0) return -30;
    if (strcmp(name, "List::Insert") == 0) return -31;
    if (strcmp(name, "List::RemoveAt") == 0) return -32;
    if (strcmp(name, "List::Set") == 0) return -33;
    if (strcmp(name, "List::Contains") == 0) return -34;
    if (strcmp(name, "List::IndexOf") == 0) return -35;
    if (strcmp(name, "List::LastIndexOf") == 0) return -36;
    if (strcmp(name, "List::Remove") == 0) return -37;
    if (strcmp(name, "List::AddRange") == 0) return -38;
    if (strcmp(name, "List::InsertRange") == 0) return -39;
    if (strcmp(name, "List::RemoveRange") == 0) return -40;
    if (strcmp(name, "List::GetRange") == 0) return -41;
    if (strcmp(name, "List::Reverse") == 0) return -42;
    if (strcmp(name, "List::EnsureCapacity") == 0) return -43;
    if (strcmp(name, "List::TrimExcess") == 0) return -44;
    if (strcmp(name, "List::SetCapacity") == 0) return -45;
    if (strcmp(name, "List::Exists") == 0) return -46;
    if (strcmp(name, "List::FindAll") == 0) return -47;
    if (strcmp(name, "List::FindIndex") == 0) return -48;
    if (strcmp(name, "List::FindLastIndex") == 0) return -49;
    if (strcmp(name, "List::RemoveAll") == 0) return -50;
    if (strcmp(name, "List::ForEach") == 0) return -51;
    if (strcmp(name, "List::TrueForAll") == 0) return -52;
    if (strcmp(name, "Dictionary::New") == 0) return -53;
    if (strcmp(name, "Dictionary::Add") == 0) return -54;
    if (strcmp(name, "Dictionary::Count") == 0) return -55;
    if (strcmp(name, "Dictionary::ContainsKey") == 0) return -56;
    if (strcmp(name, "Dictionary::Remove") == 0) return -57;
    if (strcmp(name, "Dictionary::Clear") == 0) return -58;
    if (strcmp(name, "Dictionary::Get") == 0) return -59;
    if (strcmp(name, "Dictionary::Set") == 0) return -60;
    if (strcmp(name, "Dictionary::TryAdd") == 0) return -61;
    if (strcmp(name, "Dictionary::ContainsValue") == 0) return -62;
    if (strcmp(name, "Dictionary::EnsureCapacity") == 0) return -63;
    if (strcmp(name, "Dictionary::TrimExcess") == 0) return -64;
    if (strcmp(name, "Dictionary::Capacity") == 0) return -65;
    if (strcmp(name, "Queue::New") == 0) return -66;
    if (strcmp(name, "Queue::Enqueue") == 0) return -67;
    if (strcmp(name, "Queue::Dequeue") == 0) return -68;
    if (strcmp(name, "Queue::Peek") == 0) return -69;
    if (strcmp(name, "Queue::Count") == 0) return -70;
    if (strcmp(name, "Queue::Clear") == 0) return -71;
    if (strcmp(name, "Queue::EnsureCapacity") == 0) return -72;
    if (strcmp(name, "Queue::TrimExcess") == 0) return -73;
    if (strcmp(name, "Queue::Capacity") == 0) return -74;
    if (strcmp(name, "Stack::New") == 0) return -75;
    if (strcmp(name, "Stack::Push") == 0) return -76;
    if (strcmp(name, "Stack::Pop") == 0) return -77;
    if (strcmp(name, "Stack::Peek") == 0) return -78;
    if (strcmp(name, "Stack::Count") == 0) return -79;
    if (strcmp(name, "Stack::Clear") == 0) return -80;
    if (strcmp(name, "Stack::EnsureCapacity") == 0) return -81;
    if (strcmp(name, "Stack::TrimExcess") == 0) return -82;
    if (strcmp(name, "Stack::Capacity") == 0) return -83;
    if (strcmp(name, "Dictionary::TryGetValue") == 0) return -84;
    if (strcmp(name, "Queue::TryDequeue") == 0) return -85;
    if (strcmp(name, "Queue::TryPeek") == 0) return -86;
    if (strcmp(name, "Stack::TryPop") == 0) return -87;
    if (strcmp(name, "Stack::TryPeek") == 0) return -88;
    if (strcmp(name, "Console::Write") == 0) return -89;
    if (strcmp(name, "Console::Error::Write") == 0) return -90;
    if (strcmp(name, "StringIndexOfOrdinal") == 0) return -91;
    if (strcmp(name, "BufferAsSlice") == 0) return -95;
    return INT32_MIN;
}

bool runtime_type_supported(const IrType *type) {
    return type->shape == IR_TYPE_UNIT ||
           type->shape == IR_TYPE_NEVER ||
           type->shape == IR_TYPE_BOOL ||
           type->shape == IR_TYPE_SIGNED_INT ||
           type->shape == IR_TYPE_UNSIGNED_INT ||
           type->shape == IR_TYPE_FLOAT ||
           type->shape == IR_TYPE_CHAR ||
           type->shape == IR_TYPE_STRING_VIEW ||
           type->shape == IR_TYPE_FUNCTION ||
           type->shape == IR_TYPE_ARRAY ||
           type->shape == IR_TYPE_STRUCT ||
           type->shape == IR_TYPE_CLASS_REFERENCE ||
           type->shape == IR_TYPE_ENUM ||
           type->shape == IR_TYPE_UNION ||
           type->shape == IR_TYPE_BUILTIN_OBJECT ||
           type->shape == IR_TYPE_ITERATOR ||
           type->shape == IR_TYPE_RAW_POINTER ||
           type->shape == IR_TYPE_SLICE ||
           type->shape == IR_TYPE_ELEMENT_BUILDER;
}

bool runtime_type_may_be_object(const IrType *type) {
    return type->shape == IR_TYPE_ARRAY ||
           type->shape == IR_TYPE_STRUCT ||
           type->shape == IR_TYPE_UNION ||
           type->shape == IR_TYPE_BUILTIN_OBJECT ||
           type->shape == IR_TYPE_ITERATOR ||
           type->shape == IR_TYPE_ELEMENT_BUILDER;
}

int32_t add_symbol_constant(IrBytecodeBuilder *builder,
                                   const char *data, size_t length,
                                   LangSpan span) {
    LangValue unit = {.tag = LANG_VALUE_UNIT};
    size_t constant = add_constant(
        builder, unit, data, length);
    int32_t result = -1;
    (void)as_i32(builder, constant, span, &result);
    return result;
}

int32_t add_struct_metadata(
    IrBytecodeBuilder *builder, const IrInstruction *instruction) {
    const IrType *type =
        &builder->ir->types[instruction->result_type];
    size_t length = instruction->symbol_length + 1U;
    if (type->module_name != NULL)
        length += strlen(type->module_name) + 1U;
    for (size_t i = 0U; i < instruction->label_count; ++i) {
        uint32_t field = instruction->labels[i];
        if (field >= type->field_count) {
            builder->failed = true;
            return -1;
        }
        length += strlen(type->field_names[field]) + 1U;
    }
    char *metadata = ir_bc_resize(NULL, length, 1U);
    size_t used = 0U;
    if (type->module_name != NULL)
        used = (size_t)snprintf(
            metadata, length, "%s#%.*s", type->module_name,
            (int)instruction->symbol_length, instruction->symbol);
    else
        used = (size_t)snprintf(
            metadata, length, "%.*s",
            (int)instruction->symbol_length, instruction->symbol);
    for (size_t i = 0U; i < instruction->label_count; ++i) {
        const char *field =
            type->field_names[instruction->labels[i]];
        size_t field_length = strlen(field);
        metadata[used++] = '|';
        memcpy(metadata + used, field, field_length);
        used += field_length;
        metadata[used] = '\0';
    }
    int32_t result = add_symbol_constant(
        builder, metadata, used, instruction->span);
    free(metadata);
    return result;
}

static size_t enum_family_length(const IrType *type) {
    if (type->error_type != IR_INVALID_ID) return 6U;
    if (type->element_type != IR_INVALID_ID &&
        strncmp(type->name, "Option<", 7U) == 0)
        return 6U;
    const char *generic = strchr(type->name, '<');
    return generic != NULL
         ? (size_t)(generic - type->name) : strlen(type->name);
}

static const char *enum_family_name(const IrType *type) {
    if (type->error_type != IR_INVALID_ID) return "Result";
    if (type->element_type != IR_INVALID_ID &&
        strncmp(type->name, "Option<", 7U) == 0)
        return "Option";
    return type->name;
}

int32_t add_enum_metadata(IrBytecodeBuilder *builder,
                                 const IrType *type,
                                 uint32_t variant,
                                 bool include_module,
                                 LangSpan span) {
    if (variant >= type->variant_count) {
        builder->failed = true;
        return -1;
    }
    const char *family = enum_family_name(type);
    size_t family_length = enum_family_length(type);
    const char *variant_name = type->variant_names[variant];
    size_t variant_length = strlen(variant_name);
    size_t module_length =
        include_module && type->module_name != NULL
        ? strlen(type->module_name) : 0U;
    size_t length = module_length +
        (module_length != 0U ? 1U : 0U) +
        family_length + 2U + variant_length;
    char *metadata = ir_bc_resize(NULL, length + 1U, 1U);
    size_t used = 0U;
    if (module_length != 0U) {
        memcpy(metadata + used, type->module_name, module_length);
        used += module_length;
        metadata[used++] = '#';
    }
    memcpy(metadata + used, family, family_length);
    used += family_length;
    metadata[used++] = ':';
    metadata[used++] = ':';
    memcpy(metadata + used, variant_name, variant_length);
    used += variant_length;
    metadata[used] = '\0';
    int32_t result = add_symbol_constant(
        builder, metadata, used, span);
    free(metadata);
    return result;
}

TypeKind vm_type_kind(const IrType *type) {
    if (type->shape == IR_TYPE_ENUM) return TYPE_U32;
    if (strcmp(type->name, "i8") == 0) return TYPE_I8;
    if (strcmp(type->name, "i16") == 0) return TYPE_I16;
    if (strcmp(type->name, "i32") == 0) return TYPE_I32;
    if (strcmp(type->name, "i64") == 0) return TYPE_I64;
    if (strcmp(type->name, "u8") == 0) return TYPE_U8;
    if (strcmp(type->name, "u16") == 0) return TYPE_U16;
    if (strcmp(type->name, "u32") == 0) return TYPE_U32;
    if (strcmp(type->name, "u64") == 0) return TYPE_U64;
    if (strcmp(type->name, "nint") == 0) return TYPE_ISIZE;
    if (strcmp(type->name, "nuint") == 0) return TYPE_USIZE;
    if (strcmp(type->name, "f32") == 0) return TYPE_F32;
    if (strcmp(type->name, "f64") == 0) return TYPE_F64;
    if (strcmp(type->name, "char") == 0) return TYPE_CHAR;
    return TYPE_ERROR;
}

OpCode arithmetic_opcode(IrOpcode opcode) {
    switch (opcode) {
        case IR_OP_ADD_CHECKED: return OP_ADD_I64;
        case IR_OP_SUB_CHECKED: return OP_SUB_I64;
        case IR_OP_MUL_CHECKED: return OP_MUL_I64;
        case IR_OP_DIV_CHECKED: return OP_DIV_I64;
        case IR_OP_REM_CHECKED: return OP_REM_I64;
        case IR_OP_SHIFT_LEFT_CHECKED: return OP_SHIFT_LEFT;
        case IR_OP_SHIFT_RIGHT_CHECKED: return OP_SHIFT_RIGHT;
        case IR_OP_BIT_AND: return OP_BIT_AND;
        case IR_OP_BIT_OR: return OP_BIT_OR;
        case IR_OP_BIT_XOR: return OP_BIT_XOR;
        case IR_OP_ADD_FLOAT: return OP_ADD_F64;
        case IR_OP_SUB_FLOAT: return OP_SUB_F64;
        case IR_OP_MUL_FLOAT: return OP_MUL_F64;
        case IR_OP_DIV_FLOAT: return OP_DIV_F64;
        case IR_OP_EQUAL: return OP_EQ;
        case IR_OP_NOT_EQUAL: return OP_NEQ;
        case IR_OP_LESS: return OP_LT_I64;
        case IR_OP_LESS_EQUAL: return OP_LE_I64;
        case IR_OP_GREATER: return OP_GT_I64;
        case IR_OP_GREATER_EQUAL: return OP_GE_I64;
        default: return OP_TRAP;
    }
}

void unsupported_instruction(
    IrBytecodeBuilder *builder, const IrInstruction *instruction) {
    lang_diag(builder->diagnostics, instruction->span,
              "IR bytecode backend does not yet support opcode %u",
              (unsigned)instruction->opcode);
    builder->failed = true;
}
