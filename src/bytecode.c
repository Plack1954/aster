#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lang_bytecode_free(BytecodeModule *bytecode) {
    for (size_t i = 0U; i < bytecode->function_count; ++i)
        free(bytecode->functions[i].local_destructors);
    for (size_t i = 0U; i < bytecode->function_count; ++i)
        free(bytecode->functions[i].parameter_modes);
    for (size_t i = 0U; i < bytecode->function_count; ++i)
        free(bytecode->functions[i].spans);
    for (size_t i = 0U; i < bytecode->function_count; ++i) {
        for (size_t instruction = 0U;
             instruction < bytecode->functions[i].code_capacity;
             ++instruction)
            free(bytecode->functions[i].call_sites[instruction]
                     .argument_modes);
        free(bytecode->functions[i].call_sites);
    }
    for (size_t i = 0U; i < bytecode->function_count; ++i)
        free(bytecode->functions[i].code);
    for (size_t i = 0U; i < bytecode->constant_count; ++i)
        free(bytecode->constants[i].owned_string);
    free(bytecode->functions);
    free(bytecode->constants);
    free(bytecode->static_defaults);
    free(bytecode->virtual_entries);
    free(bytecode->class_destructors);
    memset(bytecode, 0, sizeof(*bytecode));
}

static const char *op_name(OpCode op) {
    static const char *names[] = {
        "CONSTANT","UNIT","TRUE","FALSE","POP","LOAD_LOCAL","STORE_LOCAL",
        "MOVE_LOCAL","REFERENCE_LOCAL","REFERENCE_FIELD_LOCAL","INVALIDATE_LOCAL",
        "LOAD_STATIC","STORE_STATIC",
        "ADD_I64","SUB_I64","MUL_I64","DIV_I64","REM_I64",
        "SHIFT_LEFT","SHIFT_RIGHT",
        "BIT_AND","BIT_OR","BIT_XOR","BIT_NOT",
        "ADD_F64","SUB_F64","MUL_F64","DIV_F64",
        "NEG_I64","NEG_F64","NOT","CAST",
        "EQ","NEQ","LT_I64","LE_I64","GT_I64","GE_I64",
        "JUMP","JUMP_IF_FALSE","FUNCTION","BOUND_FUNCTION","CALL","CALL_VIRTUAL","CALL_INDIRECT",
        "CALL_NATIVE","AWAIT","TASK_DELAY","TASK_WHEN_ALL","TASK_WHEN_ANY",
        "RETURN","CANCELLATION_SOURCE_NEW","CANCELLATION_TOKEN_NONE",
        "CANCELLATION_TOKEN_GET","CANCELLATION_CANCEL",
        "CANCELLATION_IS_REQUESTED","CANCELLATION_THROW_IF_REQUESTED",
        "MAKE_ARRAY","GET_INDEX",
        "GET_INDEX_LOCAL","SET_INDEX_LOCAL","MAKE_STRUCT","MAKE_CLASS",
        "DELETE_CLASS","GET_FIELD",
        "GET_FIELD_LOCAL","GET_FIELD_LOCAL_MOVE",
        "GET_FIELD_BORROW","SET_FIELD_LOCAL",
        "GET_TAG","TAKE_PAYLOAD","SET_LOCAL",
        "HTML_FRAGMENT","HTML_BEGIN",
        "HTML_FRAGMENT_LOCAL","HTML_BEGIN_LOCAL","HTML_ATTR",
        "HTML_TEXT","HTML_APPEND","HTML_END",
        "HTML_ATTR_LOCAL",
        "HTML_ATTR_BEGIN_LOCAL","HTML_ATTR_APPEND_LOCAL",
        "HTML_CSS_VALUE_LOCAL",
        "HTML_ATTR_END_LOCAL",
        "HTML_APPEND_LOCAL","HTML_APPEND_FORMATTED_LOCAL",
        "HTML_APPEND_CONSTANT_LOCAL","HTML_APPEND_RAW_CONSTANT_LOCAL",
        "HTML_ATTR_CONSTANT_LOCAL",
        "HTML_ATTR_APPEND_CONSTANT_LOCAL",
        "HTML_APPEND_VALUE_LOCAL","HTML_ATTR_APPEND_VALUE_LOCAL",
        "HTML_FINISH_LOCAL","HTML_RENDER_LOCAL",
        "STRING_BUILDER_NEW_LOCAL",
        "STRING_BUILDER_APPEND_CONSTANT_LOCAL",
        "STRING_BUILDER_APPEND_VALUE_LOCAL",
        "STRING_BUILDER_FINISH_LOCAL",
        "ITER_INIT","ITER_BORROW_LOCAL","ITER_NEXT",
        "ITER_HAS_NEXT_LOCAL","ITER_TAKE_NEXT_LOCAL",
        "DROP_LOCAL","CLONE","TRY",
        "CONSTANT_LOCAL","COPY_LOCAL_TO","MOVE_LOCAL_TO",
        "BINARY_LOCALS","BINARY_LOCAL_IMMEDIATE",
        "BINARY_LOCALS_IMMEDIATE","COMPARE_BRANCH",
        "COMPARE_LOCAL_CONSTANT_BRANCH","CALL_LOCAL",
        "CALL_LOCAL_2_COPY","RETURN_LOCAL","TEXT_LEN_LOCAL",
        "STRING_SEARCH_LOCAL","STRING_SEARCH_LOCAL_CONSTANT",
        "EXCEPTION_SET","EXCEPTION_PENDING","EXCEPTION_MATCH","EXCEPTION_TAKE",
        "PROPAGATE_EXCEPTION","TRAP"
    };
    return (size_t)op < sizeof(names) / sizeof(names[0])
        ? names[(size_t)op] : "?";
}

void lang_dump_bytecode(const BytecodeModule *bytecode) {
    for (size_t f = 0U; f < bytecode->function_count; ++f) {
        const BytecodeFunction *function = &bytecode->functions[f];
        printf("%sfunction %s:\n",
               function->is_entry ? "" :
                   function->is_public ? "public " : "private ",
               function->name);
        for (size_t i = 0U; i < function->code_count; ++i) {
            const Instruction *instruction = &function->code[i];
            if (instruction->op == OP_CALL_NATIVE) {
                printf("%04zu %-18s %d argc=%d\n", i,
                       op_name(instruction->op), instruction->a,
                       instruction->b);
            } else if (instruction->op == OP_BINARY_LOCALS ||
                       instruction->op == OP_BINARY_LOCAL_IMMEDIATE) {
                OpCode operation = (OpCode)(
                    (uint32_t)instruction->a & UINT32_C(0xff));
                printf("%04zu %-18s %s %d\n", i,
                       op_name(instruction->op), op_name(operation),
                       instruction->b);
            } else {
                printf("%04zu %-18s %d %d\n", i,
                       op_name(instruction->op), instruction->a,
                       instruction->b);
            }
        }
    }
}
