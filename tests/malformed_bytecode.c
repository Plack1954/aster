#include "internal.h"

#include <stddef.h>
#include <stdlib.h>

static int rejected(LangVM *vm, Instruction *code, size_t code_count,
                    Constant *constants, size_t constant_count) {
    BytecodeFunction function = {
        .name="main",
        .code=code,
        .code_count=code_count,
        .arity=0U,
        .local_count=0U
    };
    BytecodeModule module = {
        .functions=&function,
        .function_count=1U,
        .constants=constants,
        .constant_count=constant_count
    };
    LangSource source = {
        .text="",
        .length=0U,
        .path="<malformed>"
    };
    return lang_vm_run_module(vm, &module, &source) == 2 ? 0 : 1;
}

int main(void) {
    LangVM *vm = lang_vm_new();
    if (vm == NULL) return 1;
    Constant one_constant[] = {
        {.value={.tag=LANG_VALUE_I64, .as.i64=1}}
    };
    Instruction invalid_constant[] = {
        {.op=OP_CONSTANT, .a=1, .b=0},
        {.op=OP_RETURN, .a=0, .b=0}
    };
    Instruction stack_underflow[] = {
        {.op=OP_POP, .a=0, .b=0},
        {.op=OP_RETURN, .a=0, .b=0}
    };
    Instruction inconsistent_join[] = {
        {.op=OP_TRUE, .a=0, .b=0},
        {.op=OP_JUMP_IF_FALSE, .a=4, .b=0},
        {.op=OP_CONSTANT, .a=0, .b=0},
        {.op=OP_JUMP, .a=4, .b=0},
        {.op=OP_RETURN, .a=0, .b=0}
    };
    Instruction fallthrough[] = {
        {.op=OP_UNIT, .a=0, .b=0}
    };
    Instruction invalid_copy_metadata[] = {
        {.op=OP_CONSTANT, .a=0, .b=0},
        {.op=OP_CLONE, .a=2, .b=0},
        {.op=OP_RETURN, .a=0, .b=0}
    };
    Instruction invalid_function_index[] = {
        {.op=OP_FUNCTION, .a=1, .b=0},
        {.op=OP_RETURN, .a=0, .b=0}
    };
    Instruction indirect_call_underflow[] = {
        {.op=OP_CALL_INDIRECT, .a=0, .b=0},
        {.op=OP_RETURN, .a=0, .b=0}
    };
    int failures = 0;
    failures += rejected(vm, invalid_constant,
                         sizeof(invalid_constant) /
                             sizeof(invalid_constant[0]),
                         NULL, 0U);
    failures += rejected(vm, stack_underflow,
                         sizeof(stack_underflow) /
                             sizeof(stack_underflow[0]),
                         NULL, 0U);
    failures += rejected(vm, inconsistent_join,
                         sizeof(inconsistent_join) /
                             sizeof(inconsistent_join[0]),
                         one_constant, 1U);
    failures += rejected(vm, fallthrough,
                         sizeof(fallthrough) / sizeof(fallthrough[0]),
                         NULL, 0U);
    failures += rejected(
        vm, invalid_copy_metadata,
        sizeof(invalid_copy_metadata) /
            sizeof(invalid_copy_metadata[0]),
        one_constant, 1U);
    failures += rejected(
        vm, invalid_function_index,
        sizeof(invalid_function_index) /
            sizeof(invalid_function_index[0]),
        NULL, 0U);
    failures += rejected(
        vm, indirect_call_underflow,
        sizeof(indirect_call_underflow) /
            sizeof(indirect_call_underflow[0]),
        NULL, 0U);

    size_t overflow_count = 1026U;
    Instruction *overflow = calloc(
        overflow_count, sizeof(*overflow));
    if (overflow == NULL) {
        lang_vm_free(vm);
        return 1;
    }
    for (size_t i = 0U; i + 1U < overflow_count; ++i)
        overflow[i] = (Instruction){
            .op=OP_UNIT, .a=0, .b=0
        };
    overflow[overflow_count - 1U] = (Instruction){
        .op=OP_RETURN, .a=0, .b=0
    };
    failures += rejected(vm, overflow, overflow_count, NULL, 0U);
    free(overflow);
    lang_vm_free(vm);
    return failures == 0 ? 0 : 2;
}
