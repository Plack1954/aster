#include "internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IR_BC_MAX_LOCALS 1024U

typedef struct IrBytecodePatch {
    size_t instruction;
    IrBlockId target;
} IrBytecodePatch;

typedef struct IrBytecodeBuilder {
    const IrModule *ir;
    LangDiagnostics *diagnostics;
    BytecodeModule *module;
    const IrFunction *source;
    BytecodeFunction *function;
    size_t value_base;
    size_t block_start;
    IrBlockId current_block;
    size_t *block_offsets;
    IrBytecodePatch *patches;
    size_t patch_count;
    size_t patch_capacity;
    uint32_t *value_source_locals;
    int32_t *value_source_fields;
    bool failed;
} IrBytecodeBuilder;

static void *ir_bc_resize(void *pointer, size_t count, size_t size) {
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

static size_t emit_instruction(IrBytecodeBuilder *builder, OpCode opcode,
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

static size_t add_constant(IrBytecodeBuilder *builder, LangValue value,
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

static bool as_i32(IrBytecodeBuilder *builder, size_t value,
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

static size_t value_slot(const IrBytecodeBuilder *builder,
                         IrValueId value) {
    return builder->value_base + (size_t)value;
}

static void move_value(IrBytecodeBuilder *builder, IrValueId value,
                       LangSpan span) {
    int32_t slot;
    if (as_i32(builder, value_slot(builder, value), span, &slot))
        (void)emit_instruction(
            builder, OP_MOVE_LOCAL, slot, 0, span);
}

static void store_result(IrBytecodeBuilder *builder,
                         const IrInstruction *instruction) {
    if (instruction->result == IR_INVALID_ID) return;
    int32_t slot;
    if (as_i32(builder,
               value_slot(builder, instruction->result),
               instruction->span, &slot))
        (void)emit_instruction(
            builder, OP_STORE_LOCAL, slot, 0, instruction->span);
}

static void add_patch(IrBytecodeBuilder *builder, size_t instruction,
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

static int32_t builtin_index(const char *name) {
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
    return INT32_MIN;
}

static bool runtime_type_supported(const IrType *type) {
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

static bool runtime_type_may_be_object(const IrType *type) {
    return type->shape == IR_TYPE_ARRAY ||
           type->shape == IR_TYPE_STRUCT ||
           type->shape == IR_TYPE_UNION ||
           type->shape == IR_TYPE_BUILTIN_OBJECT ||
           type->shape == IR_TYPE_ITERATOR ||
           type->shape == IR_TYPE_ELEMENT_BUILDER;
}

static int32_t add_symbol_constant(IrBytecodeBuilder *builder,
                                   const char *data, size_t length,
                                   LangSpan span) {
    LangValue unit = {.tag = LANG_VALUE_UNIT};
    size_t constant = add_constant(
        builder, unit, data, length);
    int32_t result = -1;
    (void)as_i32(builder, constant, span, &result);
    return result;
}

static int32_t add_struct_metadata(
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

static int32_t add_enum_metadata(IrBytecodeBuilder *builder,
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

static TypeKind vm_type_kind(const IrType *type) {
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

static OpCode arithmetic_opcode(IrOpcode opcode) {
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

static void unsupported_instruction(
    IrBytecodeBuilder *builder, const IrInstruction *instruction) {
    lang_diag(builder->diagnostics, instruction->span,
              "IR bytecode backend does not yet support opcode %u",
              (unsigned)instruction->opcode);
    builder->failed = true;
}

static void lower_call(IrBytecodeBuilder *builder,
                       const IrInstruction *instruction) {
    if (instruction->opcode == IR_OP_CALL_INDIRECT) {
        for (size_t i = 1U; i < instruction->operand_count; ++i) {
            IrValueId value = instruction->operands[i];
            uint32_t local = value < builder->source->value_count
                ? builder->value_source_locals[value] : UINT32_MAX;
            if (i - 1U < instruction->argument_mode_count &&
                parameter_mode_is_reference(
                    instruction->argument_modes[i - 1U]) &&
                local != UINT32_MAX) {
                int32_t field = builder->value_source_fields[value];
                if (field >= 0) {
                    (void)emit_instruction(
                        builder, OP_REFERENCE_FIELD_LOCAL,
                        (int32_t)local, field, instruction->span);
                    (void)emit_instruction(
                        builder, OP_INVALIDATE_LOCAL,
                        (int32_t)value_slot(builder, value), 0,
                        instruction->span);
                } else {
                    (void)emit_instruction(
                        builder, OP_REFERENCE_LOCAL, (int32_t)local,
                        (int32_t)value_slot(builder, value),
                        instruction->span);
                }
            }
            else
                move_value(builder, value, instruction->span);
        }
        move_value(
            builder, instruction->operands[0], instruction->span);
        int32_t count;
        if (as_i32(builder, instruction->operand_count - 1U,
                   instruction->span, &count))
            (void)emit_instruction(
                builder, OP_CALL_INDIRECT, count, 0,
                instruction->span);
        store_result(builder, instruction);
        return;
    }
    for (size_t i = 0U; i < instruction->operand_count; ++i) {
        IrValueId value = instruction->operands[i];
        uint32_t local = value < builder->source->value_count
            ? builder->value_source_locals[value] : UINT32_MAX;
        ParameterMode mode = i < instruction->argument_mode_count
            ? instruction->argument_modes[i]
            : PARAMETER_MODE_VALUE;
        bool reference_argument =
            instruction->opcode == IR_OP_CALL_DIRECT
                ? parameter_mode_is_reference(mode)
                : instruction->opcode == IR_OP_CALL_NATIVE
                ? parameter_mode_is_mutable(mode)
                : false;
        if (reference_argument && local != UINT32_MAX) {
            int32_t field = builder->value_source_fields[value];
            if (field >= 0) {
                (void)emit_instruction(
                    builder, OP_REFERENCE_FIELD_LOCAL,
                    (int32_t)local, field, instruction->span);
                (void)emit_instruction(
                    builder, OP_INVALIDATE_LOCAL,
                    (int32_t)value_slot(builder, value), 0,
                    instruction->span);
            } else {
                (void)emit_instruction(
                    builder, OP_REFERENCE_LOCAL, (int32_t)local,
                    (int32_t)value_slot(builder, value),
                    instruction->span);
            }
        }
        else
            move_value(builder, value, instruction->span);
    }
    int32_t count;
    if (!as_i32(builder, instruction->operand_count,
                instruction->span, &count))
        return;
    if (instruction->opcode == IR_OP_CALL_DIRECT) {
        int32_t target;
        if (!as_i32(builder, instruction->index,
                    instruction->span, &target))
            return;
        (void)emit_instruction(
            builder, OP_CALL, target, count, instruction->span);
    } else {
        if (instruction->symbol != NULL &&
            strcmp(instruction->symbol, "Task::Delay") == 0 &&
            (instruction->operand_count == 1U ||
             instruction->operand_count == 2U)) {
            (void)emit_instruction(
                builder, OP_TASK_DELAY,
                instruction->operand_count == 2U ? 1 : 0,
                0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        if (instruction->symbol != NULL &&
            (strcmp(instruction->symbol, "Task::WhenAll") == 0 ||
             strcmp(instruction->symbol, "Task::WhenAny") == 0) &&
            instruction->operand_count == 1U) {
            (void)emit_instruction(
                builder,
                strcmp(instruction->symbol, "Task::WhenAll") == 0
                    ? OP_TASK_WHEN_ALL : OP_TASK_WHEN_ANY,
                strcmp(instruction->symbol, "Task::WhenAll") == 0 &&
                builder->ir->types[instruction->result_type].element_type !=
                    IR_INVALID_ID &&
                builder->ir->types[builder->ir->types[
                    instruction->result_type].element_type].shape !=
                    IR_TYPE_UNIT,
                0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        if (instruction->symbol != NULL) {
            OpCode cancellation_op = OP_TRAP;
            if (strcmp(instruction->symbol,
                       "CancellationTokenSource::New") == 0)
                cancellation_op = OP_CANCELLATION_SOURCE_NEW;
            else if (strcmp(instruction->symbol,
                            "CancellationToken::None") == 0)
                cancellation_op = OP_CANCELLATION_TOKEN_NONE;
            else if (strcmp(instruction->symbol,
                            "CancellationTokenSource::Token") == 0)
                cancellation_op = OP_CANCELLATION_TOKEN_GET;
            else if (strcmp(instruction->symbol,
                            "CancellationTokenSource::Cancel") == 0)
                cancellation_op = OP_CANCELLATION_CANCEL;
            else if (strcmp(instruction->symbol,
                            "CancellationToken::IsCancellationRequested") == 0)
                cancellation_op = OP_CANCELLATION_IS_REQUESTED;
            else if (strcmp(instruction->symbol,
                            "CancellationToken::ThrowIfCancellationRequested") == 0)
                cancellation_op = OP_CANCELLATION_THROW_IF_REQUESTED;
            if (cancellation_op != OP_TRAP) {
                (void)emit_instruction(
                    builder, cancellation_op, 0, 0, instruction->span);
                store_result(builder, instruction);
                return;
            }
        }
        int32_t builtin = builtin_index(instruction->symbol);
        if (builtin == -13 && instruction->operand_count == 2U &&
            builder->ir->types[builder->source->value_types[
                instruction->operands[1]]].shape == IR_TYPE_CHAR)
            builtin = -89;
        size_t call_index;
        if (builtin != INT32_MIN) {
            call_index = emit_instruction(
                builder, OP_CALL, builtin, count,
                instruction->span);
        } else {
            LangValue unit = {.tag = LANG_VALUE_UNIT};
            size_t constant = add_constant(
                builder, unit, instruction->symbol,
                instruction->symbol_length);
            int32_t name_index;
            if (!as_i32(builder, constant,
                        instruction->span, &name_index))
                return;
            call_index = emit_instruction(
                builder, OP_CALL_NATIVE, name_index,
                count, instruction->span);
        }
        BytecodeCallSite *call_site =
            &builder->function->call_sites[call_index];
        call_site->argument_count = instruction->argument_mode_count;
        if (call_site->argument_count != 0U) {
            call_site->argument_modes = ir_bc_resize(
                NULL, call_site->argument_count,
                sizeof(*call_site->argument_modes));
            memcpy(call_site->argument_modes, instruction->argument_modes,
                   call_site->argument_count *
                       sizeof(*call_site->argument_modes));
        }
    }
    store_result(builder, instruction);
}

static void lower_instruction(IrBytecodeBuilder *builder,
                              const IrInstruction *instruction) {
    int32_t index;
    switch (instruction->opcode) {
        case IR_OP_PARAMETER:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            {
            bool borrowed =
                instruction->index < builder->source->parameter_count &&
                parameter_mode_is_reference(
                    builder->source->parameters[instruction->index].mode);
            /*
             * VM parameter slots are already the function's first locals.
             * A borrowed parameter must remain an alias to that slot: copying
             * it through a temporary would create false ownership.
             */
            if (borrowed) return;
            (void)emit_instruction(
                builder, OP_MOVE_LOCAL, index, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
            }
        case IR_OP_UNIT:
            (void)emit_instruction(
                builder, OP_UNIT, 0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_CONST_BOOL:
            (void)emit_instruction(
                builder, instruction->integer != 0U
                    ? OP_TRUE : OP_FALSE,
                0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_CONST_INT: {
            const IrType *type =
                &builder->ir->types[instruction->result_type];
            LangValue value;
            if (type->shape == IR_TYPE_UNSIGNED_INT ||
                type->shape == IR_TYPE_CHAR) {
                value.tag = LANG_VALUE_U64;
                value.as.u64 = instruction->integer;
            } else {
                value.tag = LANG_VALUE_I64;
                value.as.i64 = (int64_t)instruction->integer;
            }
            size_t constant = add_constant(
                builder, value, NULL, 0U);
            if (!as_i32(builder, constant,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_CONSTANT, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_CONST_FLOAT: {
            LangValue value = {
                .tag = LANG_VALUE_F64,
                .as.f64 = instruction->floating
            };
            size_t constant = add_constant(
                builder, value, NULL, 0U);
            if (!as_i32(builder, constant,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_CONSTANT, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_CONST_STRING: {
            LangValue unit = {.tag = LANG_VALUE_UNIT};
            size_t constant = add_constant(
                builder, unit, instruction->symbol,
                instruction->symbol_length);
            if (!as_i32(builder, constant,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_CONSTANT, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_CONST_NULL: {
            LangValue value = {
                .tag = LANG_VALUE_RAW_POINTER,
                .as.pointer = NULL
            };
            size_t constant = add_constant(
                builder, value, NULL, 0U);
            if (!as_i32(builder, constant,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_CONSTANT, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_LOAD:
        case IR_OP_LOCAL_MOVE:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder,
                instruction->opcode == IR_OP_LOCAL_LOAD
                    ? OP_LOAD_LOCAL : OP_MOVE_LOCAL,
                index, 0, instruction->span);
            if (instruction->opcode == IR_OP_LOCAL_LOAD &&
                instruction->result != IR_INVALID_ID)
                builder->value_source_locals[instruction->result] =
                    instruction->index;
            store_result(builder, instruction);
            return;
        case IR_OP_LOCAL_STORE:
            {
            if (instruction->index < builder->source->parameter_count &&
                parameter_mode_is_reference(
                    builder->source->parameters[instruction->index].mode) &&
                instruction->operand_count == 1U &&
                instruction->operands[0] == instruction->index)
                return;
            }
            move_value(
                builder, instruction->operands[0], instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_SET_LOCAL, index, 0, instruction->span);
            (void)emit_instruction(
                builder, OP_POP, 0, 0, instruction->span);
            return;
        case IR_OP_LOCAL_DROP:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_DROP_LOCAL, index, -1,
                instruction->span);
            return;
        case IR_OP_VALUE_CLONE:
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_CLONE,
                (int32_t)instruction->auxiliary, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_VALUE_DISCARD:
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_POP, 0, 0, instruction->span);
            return;
        case IR_OP_ADD_CHECKED: case IR_OP_SUB_CHECKED:
        case IR_OP_MUL_CHECKED: case IR_OP_DIV_CHECKED:
        case IR_OP_REM_CHECKED: case IR_OP_SHIFT_LEFT_CHECKED:
        case IR_OP_SHIFT_RIGHT_CHECKED:
        case IR_OP_BIT_AND: case IR_OP_BIT_OR: case IR_OP_BIT_XOR:
        case IR_OP_ADD_FLOAT: case IR_OP_SUB_FLOAT:
        case IR_OP_MUL_FLOAT: case IR_OP_DIV_FLOAT:
        case IR_OP_EQUAL: case IR_OP_NOT_EQUAL:
        case IR_OP_LESS: case IR_OP_LESS_EQUAL:
        case IR_OP_GREATER: case IR_OP_GREATER_EQUAL:
            {
            IrTypeId operand_type = builder->source->value_types[
                instruction->operands[0]];
            move_value(
                builder, instruction->operands[0], instruction->span);
            move_value(
                builder, instruction->operands[1], instruction->span);
            (void)emit_instruction(
                builder, arithmetic_opcode(instruction->opcode),
                (int32_t)vm_type_kind(
                    &builder->ir->types[operand_type]),
                0, instruction->span);
            store_result(builder, instruction);
            return;
            }
        case IR_OP_NEGATE: {
            IrTypeId operand_type = builder->source->value_types[
                instruction->operands[0]];
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder,
                builder->ir->types[operand_type].shape ==
                    IR_TYPE_FLOAT ? OP_NEG_F64 : OP_NEG_I64,
                (int32_t)vm_type_kind(
                    &builder->ir->types[operand_type]),
                0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_NOT:
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_NOT, 0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_BIT_NOT: {
            IrTypeId operand_type = builder->source->value_types[
                instruction->operands[0]];
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_BIT_NOT,
                (int32_t)vm_type_kind(
                    &builder->ir->types[operand_type]),
                0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_CAST: {
            IrTypeId source_type = builder->source->value_types[
                instruction->operands[0]];
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_CAST,
                (int32_t)vm_type_kind(
                    &builder->ir->types[source_type]),
                (int32_t)vm_type_kind(
                    &builder->ir->types[instruction->result_type]),
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_FUNCTION_REF:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_FUNCTION, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_CALL_DIRECT:
        case IR_OP_CALL_INDIRECT:
        case IR_OP_CALL_NATIVE:
            lower_call(builder, instruction);
            return;
        case IR_OP_AWAIT:
            move_value(builder, instruction->operands[0],
                       instruction->span);
            (void)emit_instruction(builder, OP_AWAIT, 0, 0,
                                   instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_EXCEPTION_SET:
            move_value(builder, instruction->operands[0],
                       instruction->span);
            (void)emit_instruction(builder, OP_EXCEPTION_SET, 0, 0,
                                   instruction->span);
            return;
        case IR_OP_EXCEPTION_PENDING:
            (void)emit_instruction(builder, OP_EXCEPTION_PENDING, 0, 0,
                                   instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_EXCEPTION_MATCH:
            index = add_symbol_constant(
                builder, instruction->symbol,
                strlen(instruction->symbol), instruction->span);
            (void)emit_instruction(
                builder, OP_EXCEPTION_MATCH, index, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_EXCEPTION_TAKE:
            (void)emit_instruction(builder, OP_EXCEPTION_TAKE, 0, 0,
                                   instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_AGGREGATE_MAKE: {
            const IrType *type =
                &builder->ir->types[instruction->result_type];
            for (size_t i = 0U;
                 i < instruction->operand_count; ++i)
                move_value(
                    builder, instruction->operands[i],
                    instruction->span);
            int32_t count;
            if (!as_i32(builder, instruction->operand_count,
                        instruction->span, &count))
                return;
            if (type->shape == IR_TYPE_ARRAY) {
                (void)emit_instruction(
                    builder, OP_MAKE_ARRAY, count, 0,
                    instruction->span);
            } else if (type->shape == IR_TYPE_STRUCT ||
                       type->shape == IR_TYPE_CLASS_REFERENCE) {
                int32_t metadata =
                    add_struct_metadata(builder, instruction);
                if (builder->failed) return;
                (void)emit_instruction(
                    builder,
                    type->shape == IR_TYPE_CLASS_REFERENCE
                        ? OP_MAKE_CLASS : OP_MAKE_STRUCT,
                    metadata, count,
                    instruction->span);
            } else if (type->shape == IR_TYPE_ENUM) {
                LangValue value = {
                    .tag = LANG_VALUE_U64,
                    .as.u64 = instruction->index
                };
                size_t constant = add_constant(
                    builder, value, NULL, 0U);
                if (!as_i32(builder, constant,
                            instruction->span, &index))
                    return;
                (void)emit_instruction(
                    builder, OP_CONSTANT, index, 0,
                    instruction->span);
            } else if (type->shape == IR_TYPE_UNION) {
                int32_t metadata = add_enum_metadata(
                    builder, type, instruction->index,
                    true, instruction->span);
                if (builder->failed) return;
                (void)emit_instruction(
                    builder, OP_MAKE_STRUCT, metadata, count,
                    instruction->span);
            } else {
                unsupported_instruction(builder, instruction);
                return;
            }
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_ENUM_IS: {
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            const IrType *type = &builder->ir->types[
                builder->source->locals[instruction->index].type];
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, local, 0,
                instruction->span);
            if (type->shape == IR_TYPE_ENUM) {
                LangValue value = {
                    .tag = LANG_VALUE_U64,
                    .as.u64 = instruction->auxiliary
                };
                size_t constant = add_constant(
                    builder, value, NULL, 0U);
                if (!as_i32(builder, constant,
                            instruction->span, &index))
                    return;
                (void)emit_instruction(
                    builder, OP_CONSTANT, index, 0,
                    instruction->span);
                (void)emit_instruction(
                    builder, OP_EQ, (int32_t)TYPE_U32, 0,
                    instruction->span);
                store_result(builder, instruction);
                return;
            }
            (void)emit_instruction(
                builder, OP_GET_TAG, 0, 0, instruction->span);
            int32_t tag = add_enum_metadata(
                builder, type, instruction->auxiliary,
                false, instruction->span);
            if (builder->failed) return;
            (void)emit_instruction(
                builder, OP_CONSTANT, tag, 0, instruction->span);
            (void)emit_instruction(
                builder, OP_EQ, 0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_ENUM_PAYLOAD_MOVE:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_MOVE_LOCAL, index, 0,
                instruction->span);
            (void)emit_instruction(
                builder, OP_TAKE_PAYLOAD, 0, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_ITERATOR_BEGIN:
            move_value(
                builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_ITER_INIT, 0, 0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_BORROWED_ITERATOR_BEGIN:
            if (instruction->operand_count == 1U) {
                move_value(
                    builder, instruction->operands[0],
                    instruction->span);
                (void)emit_instruction(
                    builder, OP_ITER_INIT, 0, 1,
                    instruction->span);
                store_result(builder, instruction);
                return;
            }
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_ITER_BORROW_LOCAL, index, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_LOCAL_ITERATOR_HAS_NEXT:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_ITER_HAS_NEXT_LOCAL, index, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_LOCAL_ITERATOR_NEXT:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_ITER_TAKE_NEXT_LOCAL, index, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_CLASS_DELETE:
            move_value(builder, instruction->operands[0], instruction->span);
            (void)emit_instruction(
                builder, OP_DELETE_CLASS, 0, 0, instruction->span);
            return;
        case IR_OP_RAW_ALLOC:
        case IR_OP_RAW_STORE:
            move_value(
                builder, instruction->operands[0], instruction->span);
            move_value(
                builder, instruction->operands[1], instruction->span);
            {
            size_t call_index = emit_instruction(
                builder, OP_CALL,
                instruction->opcode == IR_OP_RAW_ALLOC ? -6 : -9,
                2, instruction->span);
            BytecodeCallSite *call_site =
                &builder->function->call_sites[call_index];
            call_site->argument_count = 2U;
            call_site->argument_modes = ir_bc_resize(
                NULL, 2U, sizeof(*call_site->argument_modes));
            call_site->argument_modes[0] = PARAMETER_MODE_VALUE;
            call_site->argument_modes[1] = PARAMETER_MODE_VALUE;
            }
            store_result(builder, instruction);
            return;
        case IR_OP_RAW_LOAD:
            move_value(
                builder, instruction->operands[0], instruction->span);
            {
            size_t call_index = emit_instruction(
                builder, OP_CALL, -8, 1, instruction->span);
            BytecodeCallSite *call_site =
                &builder->function->call_sites[call_index];
            call_site->argument_count = 1U;
            call_site->argument_modes = ir_bc_resize(
                NULL, 1U, sizeof(*call_site->argument_modes));
            call_site->argument_modes[0] = PARAMETER_MODE_VALUE;
            }
            store_result(builder, instruction);
            return;
        case IR_OP_ELEMENT_BEGIN:
            {
            int32_t parent = 0;
            if (instruction->index != IR_INVALID_ID) {
                if (!as_i32(
                        builder, instruction->index,
                        instruction->span, &parent))
                    return;
                ++parent;
            }
            if (instruction->symbol_length == 9U &&
                memcmp(
                    instruction->symbol, "#fragment", 9U) == 0) {
                (void)emit_instruction(
                    builder, OP_HTML_FRAGMENT, 0, parent,
                    instruction->span);
            } else {
                int32_t tag = add_symbol_constant(
                    builder, instruction->symbol,
                    instruction->symbol_length, instruction->span);
                (void)emit_instruction(
                    builder, OP_HTML_BEGIN, tag, parent,
                    instruction->span);
            }
            store_result(builder, instruction);
            return;
            }
        case IR_OP_LOCAL_ELEMENT_PROPERTY: {
            move_value(
                builder, instruction->operands[0], instruction->span);
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            int32_t property = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_HTML_ATTR_LOCAL, local, property,
                instruction->span);
            return;
        }
        case IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN: {
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            int32_t property = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_HTML_ATTR_BEGIN_LOCAL,
                local, property, instruction->span);
            return;
        }
        case IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND:
            move_value(
                builder, instruction->operands[0],
                instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_ATTR_APPEND_LOCAL,
                index, 0, instruction->span);
            return;
        case IR_OP_LOCAL_ELEMENT_CSS_VALUE:
            move_value(
                builder, instruction->operands[0],
                instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_CSS_VALUE_LOCAL,
                index, 0, instruction->span);
            return;
        case IR_OP_LOCAL_ELEMENT_PROPERTY_END:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_ATTR_END_LOCAL,
                index, 0, instruction->span);
            return;
        case IR_OP_LOCAL_ELEMENT_APPEND:
            move_value(
                builder, instruction->operands[0], instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_APPEND_LOCAL, index, 0,
                instruction->span);
            return;
        case IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT: {
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            int32_t text = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_HTML_APPEND_CONSTANT_LOCAL,
                index, text, instruction->span);
            return;
        }
        case IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED:
            move_value(
                builder, instruction->operands[0],
                instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_APPEND_FORMATTED_LOCAL,
                index, 0, instruction->span);
            return;
        case IR_OP_LOCAL_ELEMENT_FINISH:
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_HTML_FINISH_LOCAL, index, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_FIELD_GET: {
            move_value(
                builder, instruction->operands[0], instruction->span);
            int32_t field = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_GET_FIELD, field, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_FIELD_GET: {
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            int32_t field = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_GET_FIELD_LOCAL, local, field,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_FIELD_MOVE: {
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            int32_t field = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_GET_FIELD_LOCAL_MOVE, local, field,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_FIELD_BORROW: {
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            (void)emit_instruction(
                builder, OP_LOAD_LOCAL, local, 0,
                instruction->span);
            int32_t field = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            if (instruction->result != IR_INVALID_ID) {
                builder->value_source_locals[instruction->result] =
                    instruction->index;
                builder->value_source_fields[instruction->result] = field;
            }
            (void)emit_instruction(
                builder, OP_GET_FIELD_BORROW, field, 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        }
        case IR_OP_LOCAL_FIELD_SET: {
            move_value(
                builder, instruction->operands[0], instruction->span);
            int32_t local;
            if (!as_i32(builder, instruction->index,
                        instruction->span, &local))
                return;
            int32_t field = add_symbol_constant(
                builder, instruction->symbol,
                instruction->symbol_length, instruction->span);
            (void)emit_instruction(
                builder, OP_SET_FIELD_LOCAL, local, field,
                instruction->span);
            (void)emit_instruction(
                builder, OP_POP, 0, 0, instruction->span);
            return;
        }
        case IR_OP_INDEX_GET:
            move_value(
                builder, instruction->operands[0], instruction->span);
            move_value(
                builder, instruction->operands[1], instruction->span);
            (void)emit_instruction(
                builder, OP_GET_INDEX,
                instruction->auxiliary == 1U ? 1 : 0,
                0, instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_LOCAL_INDEX_GET:
            move_value(
                builder, instruction->operands[0], instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_GET_INDEX_LOCAL, index,
                instruction->auxiliary == 1U ? 1 : 0,
                instruction->span);
            store_result(builder, instruction);
            return;
        case IR_OP_LOCAL_INDEX_SET:
            move_value(
                builder, instruction->operands[0], instruction->span);
            move_value(
                builder, instruction->operands[1], instruction->span);
            if (!as_i32(builder, instruction->index,
                        instruction->span, &index))
                return;
            (void)emit_instruction(
                builder, OP_SET_INDEX_LOCAL, index,
                instruction->auxiliary == 1U ? 1 : 0,
                instruction->span);
            (void)emit_instruction(
                builder, OP_POP, 0, 0, instruction->span);
            return;
        default:
            unsupported_instruction(builder, instruction);
            return;
    }
}

static void lower_terminator(IrBytecodeBuilder *builder,
                             const IrTerminator *terminator) {
    switch (terminator->kind) {
        case IR_TERM_JUMP: {
            if (terminator->target == builder->current_block + 1U)
                break;
            size_t jump = emit_instruction(
                builder, OP_JUMP, 0, 0, terminator->span);
            add_patch(builder, jump, terminator->target);
            break;
        }
        case IR_TERM_BRANCH: {
            move_value(builder, terminator->value, terminator->span);
            size_t false_jump = emit_instruction(
                builder, OP_JUMP_IF_FALSE, 0, 0, terminator->span);
            add_patch(builder, false_jump, terminator->alternate);
            if (terminator->target != builder->current_block + 1U) {
                size_t true_jump = emit_instruction(
                    builder, OP_JUMP, 0, 0, terminator->span);
                add_patch(builder, true_jump, terminator->target);
            }
            break;
        }
        case IR_TERM_RETURN:
            move_value(builder, terminator->value, terminator->span);
            (void)emit_instruction(
                builder, OP_RETURN, 0, 0, terminator->span);
            break;
        case IR_TERM_PROPAGATE_EXCEPTION:
            (void)emit_instruction(
                builder, OP_PROPAGATE_EXCEPTION, 0, 0,
                terminator->span);
            break;
        case IR_TERM_TRAP:
            (void)emit_instruction(
                builder, OP_TRAP, 0, 0, terminator->span);
            break;
        case IR_TERM_NONE:
            lang_diag(builder->diagnostics, terminator->span,
                      "IR bytecode backend received an unterminated block");
            builder->failed = true;
            break;
    }
}

static bool lower_function(IrBytecodeBuilder *builder) {
    const IrFunction *source = builder->source;
    BytecodeFunction *function = builder->function;
    size_t local_count = source->local_count + source->value_count;
    if (local_count > IR_BC_MAX_LOCALS) {
        lang_diag(builder->diagnostics, source->span,
                  "IR bytecode function requires %zu locals; limit is %u",
                  local_count, (unsigned)IR_BC_MAX_LOCALS);
        return false;
    }
    for (size_t l = 0U; l < source->local_count; ++l)
        if (!runtime_type_supported(
                &builder->ir->types[source->locals[l].type])) {
            lang_diag(builder->diagnostics, source->span,
                      "IR bytecode backend does not yet support this local type");
            return false;
        }
    function->object_local_mask_valid = local_count <= 64U;
    for (size_t l = 0U; l < source->local_count; ++l)
        if (runtime_type_may_be_object(
                &builder->ir->types[source->locals[l].type])) {
            function->may_have_object_locals = true;
            if (l < 64U)
                function->object_local_mask |=
                    UINT64_C(1) << (unsigned)l;
        }
    for (size_t value = 0U; value < source->value_count; ++value)
        if (runtime_type_may_be_object(
                &builder->ir->types[source->value_types[value]])) {
            function->may_have_object_locals = true;
            size_t slot = builder->value_base + value;
            if (slot < 64U)
                function->object_local_mask |=
                    UINT64_C(1) << (unsigned)slot;
        }
    function->local_count = local_count;
    function->local_destructors = ir_bc_resize(
        NULL, local_count, sizeof(*function->local_destructors));
    for (size_t i = 0U; i < local_count; ++i)
        function->local_destructors[i] = -1;
    builder->value_base = source->local_count;
    builder->value_source_locals = ir_bc_resize(
        NULL, source->value_count, sizeof(*builder->value_source_locals));
    for (size_t i = 0U; i < source->value_count; ++i)
        builder->value_source_locals[i] = UINT32_MAX;
    builder->value_source_fields = ir_bc_resize(
        NULL, source->value_count, sizeof(*builder->value_source_fields));
    for (size_t i = 0U; i < source->value_count; ++i)
        builder->value_source_fields[i] = -1;
    builder->block_offsets = ir_bc_resize(
        NULL, source->block_count, sizeof(*builder->block_offsets));
    for (size_t b = 0U; b < source->block_count; ++b) {
        builder->block_offsets[b] = function->code_count;
        builder->block_start = function->code_count;
        builder->current_block = (IrBlockId)b;
        const IrBlock *block = &source->blocks[b];
        for (size_t i = 0U; i < block->instruction_count; ++i) {
            lower_instruction(builder, &block->instructions[i]);
            if (builder->failed) return false;
        }
        lower_terminator(builder, &block->terminator);
        if (builder->failed) return false;
    }
    for (size_t p = 0U; p < builder->patch_count; ++p) {
        IrBytecodePatch patch = builder->patches[p];
        if (patch.target >= source->block_count) return false;
        int32_t target;
        if (!as_i32(builder, builder->block_offsets[patch.target],
                    source->span, &target))
            return false;
        function->code[patch.instruction].a = target;
    }
    function->fast_scalar_loop_start = SIZE_MAX;
    function->fast_scalar_loop_end = SIZE_MAX;
    if (!function->may_have_object_locals &&
        function->local_count <= 64U) {
        for (size_t i = 0U; i < function->code_count; ++i) {
            Instruction instruction = function->code[i];
            if (instruction.op == OP_JUMP && instruction.a >= 0 &&
                (size_t)instruction.a < i) {
                function->fast_scalar_loop_start =
                    (size_t)instruction.a;
                function->fast_scalar_loop_end = i;
                break;
            }
        }
    }
    bool fast_signed_scalar_types = true;
    for (size_t l = 0U;
         l < source->local_count && fast_signed_scalar_types; ++l) {
        const IrType *type = &builder->ir->types[source->locals[l].type];
        fast_signed_scalar_types =
            type->shape == IR_TYPE_SIGNED_INT ||
            type->shape == IR_TYPE_BOOL || type->shape == IR_TYPE_UNIT ||
            type->shape == IR_TYPE_NEVER;
    }
    for (size_t value = 0U;
         value < source->value_count && fast_signed_scalar_types;
         ++value) {
        const IrType *type =
            &builder->ir->types[source->value_types[value]];
        fast_signed_scalar_types =
            type->shape == IR_TYPE_SIGNED_INT ||
            type->shape == IR_TYPE_BOOL || type->shape == IR_TYPE_UNIT ||
            type->shape == IR_TYPE_NEVER;
    }
    bool no_reference_parameters = true;
    for (size_t parameter = 0U; parameter < function->arity; ++parameter)
        if (parameter_mode_is_reference(
                function->parameter_modes[parameter]))
            no_reference_parameters = false;
    function->fast_scalar_leaf =
        !function->is_async && !function->may_have_object_locals &&
        no_reference_parameters &&
        function->local_count <= 64U &&
        fast_signed_scalar_types;
    for (size_t i = 0U;
         i < function->code_count && function->fast_scalar_leaf; ++i) {
        Instruction instruction = function->code[i];
        switch (instruction.op) {
            case OP_CONSTANT_LOCAL:
            case OP_COPY_LOCAL_TO:
            case OP_BINARY_LOCALS:
            case OP_BINARY_LOCAL_IMMEDIATE:
            case OP_BINARY_LOCALS_IMMEDIATE:
            case OP_RETURN_LOCAL:
                break;
            case OP_COMPARE_LOCAL_CONSTANT_BRANCH:
            case OP_JUMP:
                if (instruction.a <= (int32_t)i)
                    function->fast_scalar_leaf = false;
                break;
            default:
                function->fast_scalar_leaf = false;
                break;
        }
    }
    if (function->fast_scalar_leaf && function->arity == 2U &&
        function->code_count == 8U) {
        const Instruction *sum = &function->code[0];
        const Instruction *offset = &function->code[1];
        const Instruction *test = &function->code[2];
        const Instruction *copy = &function->code[3];
        const Instruction *constant = &function->code[4];
        const Instruction *subtract = &function->code[5];
        const Instruction *wrapped_return = &function->code[6];
        const Instruction *plain_return = &function->code[7];
        uint32_t sum_slots = (uint32_t)sum->b;
        uint32_t offset_slots = (uint32_t)offset->b;
        uint32_t test_packed = (uint32_t)test->b;
        uint32_t subtract_slots = (uint32_t)subtract->b;
        size_t sum_destination = (size_t)(
            (sum_slots >> 20U) & UINT32_C(0x3ff));
        size_t result_slot = (size_t)(
            (offset_slots >> 10U) & UINT32_C(0x3ff));
        size_t test_constant = (size_t)(
            (test_packed >> 10U) & UINT32_C(0xffff));
        bool shape =
            sum->op == OP_BINARY_LOCALS &&
            offset->op == OP_BINARY_LOCAL_IMMEDIATE &&
            test->op == OP_COMPARE_LOCAL_CONSTANT_BRANCH &&
            copy->op == OP_COPY_LOCAL_TO &&
            constant->op == OP_CONSTANT_LOCAL &&
            subtract->op == OP_BINARY_LOCALS &&
            wrapped_return->op == OP_RETURN_LOCAL &&
            plain_return->op == OP_RETURN_LOCAL &&
            ((OpCode)((uint32_t)sum->a & UINT32_C(0xff))) ==
                OP_ADD_I64 &&
            (sum_slots & UINT32_C(0x3ff)) == 0U &&
            ((sum_slots >> 10U) & UINT32_C(0x3ff)) == 1U &&
            ((OpCode)((uint32_t)offset->a & UINT32_C(0xff))) ==
                OP_ADD_I64 &&
            (offset_slots & UINT32_C(0x3ff)) == sum_destination &&
            (OpCode)(test_packed >> 26U) == OP_GT_I64 &&
            (test_packed & UINT32_C(0x3ff)) == result_slot &&
            test->a == 7 && copy->a == (int32_t)result_slot &&
            ((OpCode)((uint32_t)subtract->a & UINT32_C(0xff))) ==
                OP_SUB_I64 &&
            (subtract_slots & UINT32_C(0x3ff)) ==
                (uint32_t)copy->b &&
            ((subtract_slots >> 10U) & UINT32_C(0x3ff)) ==
                (uint32_t)constant->b &&
            wrapped_return->a == (int32_t)(
                (subtract_slots >> 20U) & UINT32_C(0x3ff)) &&
            plain_return->a == (int32_t)result_slot &&
            constant->a >= 0 && test_constant <
                builder->module->constant_count &&
            (size_t)constant->a < builder->module->constant_count;
        if (shape) {
            LangValue test_value = builder->module->constants[
                test_constant].value;
            LangValue subtract_value = builder->module->constants[
                (size_t)constant->a].value;
            if (test_value.tag == LANG_VALUE_I64 &&
                subtract_value.tag == LANG_VALUE_I64 &&
                test_value.as.i64 == subtract_value.as.i64) {
                function->fast_affine_wrap_leaf = true;
                function->fast_affine_addend =
                    (int64_t)((int32_t)offset_slots >> 20U);
                function->fast_affine_limit = test_value.as.i64;
            }
        }
    }
    return true;
}

bool lang_ir_compile_bytecode(const IrModule *ir,
                              LangDiagnostics *diagnostics,
                              BytecodeModule *bytecode) {
    memset(bytecode, 0, sizeof(*bytecode));
    bytecode->function_count = ir->function_count;
    bytecode->functions = ir_bc_resize(
        NULL, ir->function_count, sizeof(*bytecode->functions));
    memset(bytecode->functions, 0,
           ir->function_count * sizeof(*bytecode->functions));
    for (size_t f = 0U; f < ir->function_count; ++f) {
        const IrFunction *source = &ir->functions[f];
        BytecodeFunction *output = &bytecode->functions[f];
        output->name = source->name;
        output->module_name = source->module_name;
        output->arity = source->parameter_count;
        if (output->arity != 0U) {
            output->parameter_modes = ir_bc_resize(
                NULL, output->arity, sizeof(*output->parameter_modes));
            for (size_t parameter = 0U;
                 parameter < output->arity; ++parameter)
                output->parameter_modes[parameter] =
                    source->parameters[parameter].mode;
        }
        output->is_entry = source->is_entry;
        output->is_async = source->is_async;
        output->is_public = source->is_public;
    }
    for (size_t f = 0U; f < ir->function_count; ++f) {
        IrBytecodeBuilder builder;
        memset(&builder, 0, sizeof(builder));
        builder.ir = ir;
        builder.diagnostics = diagnostics;
        builder.module = bytecode;
        builder.source = &ir->functions[f];
        builder.function = &bytecode->functions[f];
        bool ok = lower_function(&builder);
        free(builder.block_offsets);
        free(builder.patches);
        free(builder.value_source_locals);
        free(builder.value_source_fields);
        if (!ok || builder.failed) {
            lang_bytecode_free(bytecode);
            return false;
        }
    }
    return diagnostics->count == 0U;
}
