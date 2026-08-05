#include "vm_internal.h"

#include <stdint.h>
#include <stdlib.h>

static bool verify_stack_successor(int32_t *depths, size_t *worklist,
                                   size_t *work_count, size_t code_count,
                                   size_t successor, int32_t depth) {
    if (successor >= code_count) return false;
    if (depths[successor] == -1) {
        depths[successor] = depth;
        worklist[(*work_count)++] = successor;
        return true;
    }
    return depths[successor] == depth;
}

static bool verify_function_stack(const BytecodeFunction *function) {
    if (function->code_count == 0U) return false;
    int32_t *depths = malloc(
        function->code_count * sizeof(*depths));
    size_t *worklist = malloc(
        function->code_count * sizeof(*worklist));
    if (depths == NULL || worklist == NULL) {
        free(depths);
        free(worklist);
        return false;
    }
    for (size_t i = 0U; i < function->code_count; ++i)
        depths[i] = -1;
    size_t work_count = 1U;
    depths[0] = 0;
    worklist[0] = 0U;
    bool valid = true;
    while (work_count != 0U && valid) {
        size_t ip = worklist[--work_count];
        Instruction instruction = function->code[ip];
        int32_t required = 0;
        int32_t popped = 0;
        int32_t pushed = 0;
        bool terminal = false;
        bool unconditional_jump = false;
        bool branch = false;
        switch (instruction.op) {
            case OP_CONSTANT: case OP_UNIT: case OP_TRUE: case OP_FALSE:
            case OP_FUNCTION:
            case OP_LOAD_STATIC:
            case OP_LOAD_LOCAL: case OP_MOVE_LOCAL:
            case OP_REFERENCE_LOCAL:
            case OP_REFERENCE_FIELD_LOCAL:
            case OP_GET_FIELD_LOCAL:
            case OP_GET_FIELD_LOCAL_MOVE:
            case OP_HTML_FRAGMENT: case OP_HTML_BEGIN:
            case OP_HTML_FINISH_LOCAL:
            case OP_ITER_HAS_NEXT_LOCAL:
            case OP_ITER_TAKE_NEXT_LOCAL:
                pushed = 1;
                break;
            case OP_ITER_BORROW_LOCAL:
                pushed = 1;
                break;
            case OP_CONSTANT_LOCAL:
            case OP_INVALIDATE_LOCAL:
            case OP_COPY_LOCAL_TO:
            case OP_MOVE_LOCAL_TO:
            case OP_HTML_FRAGMENT_LOCAL:
            case OP_HTML_BEGIN_LOCAL:
                break;
            case OP_EXCEPTION_PENDING:
            case OP_EXCEPTION_MATCH:
            case OP_EXCEPTION_TAKE:
                pushed = 1;
                break;
            case OP_POP: case OP_STORE_LOCAL: case OP_STORE_STATIC:
            case OP_EXCEPTION_SET:
            case OP_DELETE_CLASS:
                required = 1; popped = 1;
                break;
            case OP_SET_LOCAL: case OP_SET_FIELD_LOCAL:
                required = 1; popped = 1; pushed = 1;
                break;
            case OP_ADD_I64: case OP_SUB_I64: case OP_MUL_I64:
            case OP_DIV_I64: case OP_REM_I64:
            case OP_SHIFT_LEFT: case OP_SHIFT_RIGHT:
            case OP_BIT_AND: case OP_BIT_OR: case OP_BIT_XOR:
            case OP_ADD_F64: case OP_SUB_F64: case OP_MUL_F64:
            case OP_DIV_F64: case OP_EQ: case OP_NEQ:
            case OP_LT_I64: case OP_LE_I64: case OP_GT_I64:
            case OP_GE_I64:
                required = 2; popped = 2; pushed = 1;
                break;
            case OP_BINARY_LOCALS:
            case OP_BINARY_LOCAL_IMMEDIATE:
            case OP_BINARY_LOCALS_IMMEDIATE:
                break;
            case OP_NEG_I64: case OP_NEG_F64: case OP_NOT:
            case OP_BIT_NOT:
            case OP_CAST: case OP_TRY:
            case OP_AWAIT:
            case OP_TASK_WHEN_ALL: case OP_TASK_WHEN_ANY:
            case OP_CANCELLATION_TOKEN_GET:
            case OP_CANCELLATION_CANCEL:
            case OP_CANCELLATION_IS_REQUESTED:
            case OP_CANCELLATION_THROW_IF_REQUESTED:
            case OP_GET_FIELD: case OP_GET_FIELD_BORROW:
            case OP_GET_TAG:
            case OP_TAKE_PAYLOAD: case OP_ITER_INIT:
                required = 1; popped = 1; pushed = 1;
                break;
            case OP_TASK_DELAY:
                required = instruction.a != 0 ? 2 : 1;
                popped = required;
                pushed = 1;
                break;
            case OP_CANCELLATION_SOURCE_NEW:
            case OP_CANCELLATION_TOKEN_NONE:
                pushed = 1;
                break;
            case OP_CLONE:
                required = 1; popped = 1; pushed = 1;
                if (instruction.a != 0 && instruction.a != 1)
                    valid = false;
                break;
            case OP_JUMP:
                unconditional_jump = true;
                break;
            case OP_PROPAGATE_EXCEPTION:
                unconditional_jump = true;
                break;
            case OP_JUMP_IF_FALSE:
                required = 1; popped = 1; branch = true;
                break;
            case OP_COMPARE_BRANCH:
            case OP_COMPARE_LOCAL_CONSTANT_BRANCH:
                branch = true;
                break;
            case OP_CALL: {
                if (instruction.b < 0) {
                    valid = false;
                    break;
                }
                required = instruction.b;
                popped = instruction.b;
                pushed = 1;
                break;
            }
            case OP_CALL_VIRTUAL:
                if (instruction.a < 0 || instruction.b <= 0) {
                    valid = false;
                    break;
                }
                required = instruction.b;
                popped = instruction.b;
                pushed = 1;
                break;
            case OP_BOUND_FUNCTION:
                required = 1;
                popped = 1;
                pushed = 1;
                break;
            case OP_CALL_LOCAL: {
                uint32_t count =
                    (uint32_t)instruction.b &
                    UINT32_C(0x3ff);
                required = (int32_t)count;
                popped = (int32_t)count;
                break;
            }
            case OP_CALL_LOCAL_2_COPY:
            case OP_TEXT_LEN_LOCAL:
            case OP_STRING_SEARCH_LOCAL:
            case OP_STRING_SEARCH_LOCAL_CONSTANT:
                break;
            case OP_CALL_INDIRECT:
                if (instruction.a < 0 ||
                    instruction.a == INT32_MAX) {
                    valid = false;
                    break;
                }
                required = instruction.a + 1;
                popped = instruction.a + 1;
                pushed = 1;
                break;
            case OP_CALL_NATIVE: {
                if (instruction.b < 0) {
                    valid = false;
                    break;
                }
                required = instruction.b;
                popped = instruction.b;
                pushed = 1;
                break;
            }
            case OP_RETURN:
                if (depths[ip] > 1) valid = false;
                terminal = true;
                break;
            case OP_RETURN_LOCAL:
                terminal = true;
                if (depths[ip] != 0) valid = false;
                break;
            case OP_MAKE_ARRAY:
                required = instruction.a;
                popped = instruction.a;
                pushed = 1;
                break;
            case OP_GET_INDEX:
                required = 2; popped = 2; pushed = 1;
                break;
            case OP_GET_INDEX_LOCAL:
                required = 1; popped = 1; pushed = 1;
                break;
            case OP_SET_INDEX_LOCAL:
                required = 2; popped = 2; pushed = 1;
                break;
            case OP_MAKE_STRUCT: case OP_MAKE_CLASS:
                required = instruction.b;
                popped = instruction.b;
                pushed = 1;
                break;
            case OP_HTML_ATTR: case OP_HTML_APPEND:
                required = 2; popped = 1;
                break;
            case OP_HTML_ATTR_LOCAL:
            case OP_HTML_ATTR_APPEND_LOCAL:
            case OP_HTML_CSS_VALUE_LOCAL:
            case OP_HTML_APPEND_LOCAL:
            case OP_HTML_APPEND_FORMATTED_LOCAL:
                required = 1; popped = 1;
                break;
            case OP_HTML_APPEND_CONSTANT_LOCAL:
            case OP_HTML_APPEND_RAW_CONSTANT_LOCAL:
            case OP_HTML_ATTR_CONSTANT_LOCAL:
            case OP_HTML_ATTR_APPEND_CONSTANT_LOCAL:
            case OP_HTML_APPEND_VALUE_LOCAL:
            case OP_HTML_ATTR_APPEND_VALUE_LOCAL:
                break;
            case OP_HTML_RENDER_LOCAL:
                required = 1; popped = 1;
                break;
            case OP_STRING_BUILDER_NEW_LOCAL:
            case OP_STRING_BUILDER_APPEND_CONSTANT_LOCAL:
            case OP_STRING_BUILDER_APPEND_VALUE_LOCAL:
            case OP_STRING_BUILDER_FINISH_LOCAL:
                break;
            case OP_HTML_ATTR_BEGIN_LOCAL:
            case OP_HTML_ATTR_END_LOCAL:
                break;
            case OP_HTML_TEXT:
                break;
            case OP_HTML_END:
                required = 1;
                break;
            case OP_ITER_NEXT:
                required = 1;
                branch = true;
                break;
            case OP_DROP_LOCAL:
                break;
            case OP_TRAP:
                terminal = true;
                break;
        }
        if (!valid || depths[ip] < required) {
            valid = false;
            break;
        }
        int32_t next_depth = depths[ip] - popped + pushed;
        if (next_depth < 0 || next_depth > 1024) {
            valid = false;
            break;
        }
        if (terminal) continue;
        if (unconditional_jump) {
            valid = verify_stack_successor(
                depths, worklist, &work_count, function->code_count,
                (size_t)instruction.a, next_depth);
            continue;
        }
        if (branch) {
            valid = verify_stack_successor(
                depths, worklist, &work_count, function->code_count,
                (size_t)instruction.a, next_depth);
            if (!valid) break;
        }
        valid = verify_stack_successor(
            depths, worklist, &work_count, function->code_count,
            ip + 1U, next_depth);
    }
    free(depths);
    free(worklist);
    return valid;
}

bool vm_verify_bytecode_module(const BytecodeModule *module) {
    if (module == NULL ||
        (module->function_count != 0U &&
         module->functions == NULL) ||
        (module->constant_count != 0U &&
         module->constants == NULL) ||
        (module->static_count != 0U &&
         module->static_defaults == NULL) ||
        (module->virtual_entry_count != 0U &&
         module->virtual_entries == NULL) ||
        (module->class_destructor_count != 0U &&
         module->class_destructors == NULL))
        return false;
    for (size_t entry = 0U;
         entry < module->virtual_entry_count; ++entry) {
        const BytecodeVirtualEntry *virtual_entry =
            &module->virtual_entries[entry];
        if (virtual_entry->runtime_type == NULL ||
            virtual_entry->runtime_type_length == 0U ||
            virtual_entry->root_function >= module->function_count ||
            virtual_entry->target_function >= module->function_count ||
            (virtual_entry->runtime_module_length != 0U &&
             virtual_entry->runtime_module == NULL))
            return false;
    }
    for (size_t entry = 0U;
         entry < module->class_destructor_count; ++entry) {
        const BytecodeClassDestructor *destructor =
            &module->class_destructors[entry];
        if (destructor->runtime_type == NULL ||
            destructor->runtime_type_length == 0U ||
            destructor->destructor_function >= module->function_count ||
            (destructor->runtime_module_length != 0U &&
             destructor->runtime_module == NULL))
            return false;
    }
    for (size_t f = 0U; f < module->function_count; ++f) {
        const BytecodeFunction *function =
            &module->functions[f];
        if (function->name == NULL ||
            (function->code_count != 0U &&
             (function->code == NULL || function->call_sites == NULL)) ||
            (function->arity != 0U &&
             function->parameter_modes == NULL))
            return false;
        for (size_t parameter = 0U; parameter < function->arity; ++parameter)
            if (function->parameter_modes[parameter] <
                    PARAMETER_MODE_VALUE ||
                function->parameter_modes[parameter] > PARAMETER_MODE_OUT)
                return false;
        for (size_t ip = 0U; ip < function->code_count; ++ip) {
            Instruction instruction = function->code[ip];
            if ((unsigned)instruction.op > (unsigned)OP_TRAP)
                return false;
            const BytecodeCallSite *call_site = &function->call_sites[ip];
            if (call_site->argument_count != 0U &&
                call_site->argument_modes == NULL)
                return false;
            for (size_t argument = 0U;
                 argument < call_site->argument_count; ++argument)
                if (call_site->argument_modes[argument] <
                        PARAMETER_MODE_VALUE ||
                    call_site->argument_modes[argument] >
                        PARAMETER_MODE_OUT)
                    return false;
            switch (instruction.op) {
                case OP_LOAD_STATIC:
                case OP_STORE_STATIC:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >= module->static_count)
                        return false;
                    break;
                case OP_HTML_FRAGMENT_LOCAL:
                case OP_HTML_BEGIN_LOCAL: {
                    uint32_t packed = (uint32_t)instruction.b;
                    size_t slot = (size_t)(
                        packed & UINT32_C(0x3ff));
                    size_t parent = (size_t)(packed >> 10U);
                    if (slot >= function->local_count ||
                        parent > function->local_count ||
                        (instruction.op == OP_HTML_BEGIN_LOCAL &&
                         (instruction.a < 0 ||
                          (size_t)instruction.a >=
                            module->constant_count)))
                        return false;
                    break;
                }
                case OP_HTML_FRAGMENT:
                    if (instruction.b < 0 ||
                        (instruction.b > 0 &&
                         (size_t)instruction.b >
                             function->local_count))
                        return false;
                    break;
                case OP_CONSTANT:
                case OP_HTML_BEGIN:
                case OP_HTML_ATTR:
                case OP_CALL_NATIVE:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            module->constant_count)
                        return false;
                    if (instruction.op == OP_HTML_BEGIN &&
                        (instruction.b < 0 ||
                         (instruction.b > 0 &&
                          (size_t)instruction.b >
                              function->local_count)))
                        return false;
                    if (instruction.op == OP_CALL_NATIVE &&
                        (instruction.b < 0 ||
                         (size_t)instruction.b !=
                             function->call_sites[ip].argument_count ||
                         (instruction.b != 0 &&
                          function->call_sites[ip].argument_modes == NULL)))
                        return false;
                    break;
                case OP_HTML_TEXT:
                    if (instruction.b < 0 ||
                        (size_t)instruction.b >=
                            module->constant_count ||
                        module->constants[
                            (size_t)instruction.b].value.tag !=
                            LANG_VALUE_STRING_VIEW)
                        return false;
                    break;
                case OP_HTML_ATTR_LOCAL:
                case OP_HTML_ATTR_BEGIN_LOCAL:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            function->local_count ||
                        instruction.b < 0 ||
                        (size_t)instruction.b >=
                            module->constant_count)
                        return false;
                    break;
                case OP_GET_FIELD:
                case OP_GET_FIELD_BORROW:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            module->constant_count)
                        return false;
                    break;
                case OP_GET_FIELD_LOCAL:
                case OP_GET_FIELD_LOCAL_MOVE:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            function->local_count ||
                        instruction.b < 0 ||
                        (size_t)instruction.b >=
                            module->constant_count)
                        return false;
                    break;
                case OP_SET_FIELD_LOCAL:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            function->local_count ||
                        instruction.b < 0 ||
                        (size_t)instruction.b >=
                            module->constant_count)
                        return false;
                    break;
                case OP_MAKE_STRUCT:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            module->constant_count ||
                        instruction.b < 0)
                        return false;
                    break;
                case OP_LOAD_LOCAL:
                case OP_STORE_LOCAL:
                case OP_MOVE_LOCAL:
                case OP_SET_LOCAL:
                case OP_GET_INDEX_LOCAL:
                case OP_SET_INDEX_LOCAL:
                case OP_DROP_LOCAL:
                case OP_ITER_HAS_NEXT_LOCAL:
                case OP_ITER_TAKE_NEXT_LOCAL:
                case OP_ITER_BORROW_LOCAL:
                case OP_HTML_ATTR_APPEND_LOCAL:
                case OP_HTML_CSS_VALUE_LOCAL:
                case OP_HTML_ATTR_END_LOCAL:
                case OP_HTML_APPEND_LOCAL:
                case OP_HTML_APPEND_FORMATTED_LOCAL:
                case OP_HTML_FINISH_LOCAL:
                case OP_HTML_RENDER_LOCAL:
                case OP_STRING_BUILDER_NEW_LOCAL:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            function->local_count)
                        return false;
                    break;
                case OP_REFERENCE_LOCAL:
                    if (instruction.a < 0 || instruction.b < 0 ||
                        (size_t)instruction.a >= function->local_count ||
                        (size_t)instruction.b >= function->local_count)
                        return false;
                    break;
                case OP_REFERENCE_FIELD_LOCAL:
                    if (instruction.a < 0 || instruction.b < 0 ||
                        (size_t)instruction.a >= function->local_count ||
                        (size_t)instruction.b >= module->constant_count ||
                        module->constants[(size_t)instruction.b]
                            .value.tag != LANG_VALUE_STRING_VIEW)
                        return false;
                    break;
                case OP_INVALIDATE_LOCAL:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >= function->local_count)
                        return false;
                    break;
                case OP_STRING_BUILDER_APPEND_CONSTANT_LOCAL:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            function->local_count ||
                        instruction.b < 0 ||
                        (size_t)instruction.b >=
                            module->constant_count ||
                        module->constants[
                            (size_t)instruction.b].value.tag !=
                            LANG_VALUE_STRING_VIEW)
                        return false;
                    break;
                case OP_STRING_BUILDER_APPEND_VALUE_LOCAL:
                case OP_STRING_BUILDER_FINISH_LOCAL:
                    if (instruction.a < 0 || instruction.b < 0 ||
                        (size_t)instruction.a >=
                            function->local_count ||
                        (size_t)instruction.b >=
                            function->local_count)
                        return false;
                    break;
                case OP_HTML_APPEND_CONSTANT_LOCAL:
                case OP_HTML_APPEND_RAW_CONSTANT_LOCAL:
                case OP_HTML_ATTR_APPEND_CONSTANT_LOCAL:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            function->local_count ||
                        instruction.b < 0 ||
                        (size_t)instruction.b >=
                            module->constant_count)
                        return false;
                    break;
                case OP_HTML_ATTR_CONSTANT_LOCAL: {
                    uint32_t packed = (uint32_t)instruction.b;
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            function->local_count ||
                        (size_t)(packed & UINT32_C(0xffff)) >=
                            module->constant_count ||
                        (size_t)(packed >> 16U) >=
                            module->constant_count)
                        return false;
                    break;
                }
                case OP_HTML_APPEND_VALUE_LOCAL:
                case OP_HTML_ATTR_APPEND_VALUE_LOCAL:
                    if (instruction.a < 0 || instruction.b < 0 ||
                        (size_t)instruction.a >=
                            function->local_count ||
                        (size_t)instruction.b >=
                            function->local_count)
                        return false;
                    break;
                case OP_BINARY_LOCALS: {
                    uint32_t packed_operation =
                        (uint32_t)instruction.a;
                    OpCode operation = (OpCode)(
                        packed_operation & UINT32_C(0xff));
                    uint32_t packed_slots =
                        (uint32_t)instruction.b;
                    size_t left_slot = (size_t)(
                        packed_slots & UINT32_C(0x3ff));
                    size_t right_slot = (size_t)(
                        (packed_slots >> 10U) &
                        UINT32_C(0x3ff));
                    size_t destination = (size_t)(
                        (packed_slots >> 20U) &
                        UINT32_C(0x3ff));
                    bool binary =
                        operation >= OP_ADD_I64 &&
                        operation <= OP_REM_I64;
                    if (!binary ||
                        left_slot >= function->local_count ||
                        right_slot >= function->local_count ||
                        destination >= function->local_count)
                        return false;
                    break;
                }
                case OP_BINARY_LOCAL_IMMEDIATE: {
                    uint32_t packed_operation =
                        (uint32_t)instruction.a;
                    OpCode operation = (OpCode)(
                        packed_operation & UINT32_C(0xff));
                    uint32_t packed =
                        (uint32_t)instruction.b;
                    size_t source = (size_t)(
                        packed & UINT32_C(0x3ff));
                    size_t destination = (size_t)(
                        (packed >> 10U) &
                        UINT32_C(0x3ff));
                    bool binary =
                        operation >= OP_ADD_I64 &&
                        operation <= OP_REM_I64;
                    if (!binary ||
                        source >= function->local_count ||
                        destination >= function->local_count)
                        return false;
                    break;
                }
                case OP_BINARY_LOCALS_IMMEDIATE: {
                    uint32_t packed_operation =
                        (uint32_t)instruction.a;
                    OpCode first = (OpCode)(
                        packed_operation & UINT32_C(0xff));
                    OpCode second = (OpCode)(
                        (packed_operation >> 16U) & UINT32_C(0xff));
                    uint32_t packed_slots =
                        (uint32_t)instruction.b;
                    size_t left = (size_t)(
                        packed_slots & UINT32_C(0x3ff));
                    size_t right = (size_t)(
                        (packed_slots >> 10U) & UINT32_C(0x3ff));
                    size_t destination = (size_t)(
                        (packed_slots >> 20U) & UINT32_C(0x3ff));
                    bool valid_operations =
                        first >= OP_ADD_I64 &&
                        first <= OP_MUL_I64 &&
                        second >= OP_ADD_I64 &&
                        second <= OP_MUL_I64;
                    if (!valid_operations ||
                        left >= function->local_count ||
                        right >= function->local_count ||
                        destination >= function->local_count)
                        return false;
                    break;
                }
                case OP_CONSTANT_LOCAL:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            module->constant_count ||
                        instruction.b < 0 ||
                        (size_t)instruction.b >=
                            function->local_count)
                        return false;
                    break;
                case OP_COPY_LOCAL_TO:
                case OP_MOVE_LOCAL_TO:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            function->local_count ||
                        instruction.b < 0 ||
                        (size_t)instruction.b >=
                            function->local_count)
                        return false;
                    break;
                case OP_JUMP:
                case OP_JUMP_IF_FALSE:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >
                            function->code_count)
                        return false;
                    break;
                case OP_COMPARE_BRANCH: {
                    uint32_t packed = (uint32_t)instruction.b;
                    size_t left_slot = (size_t)(
                        packed & UINT32_C(0x3ff));
                    size_t right_slot = (size_t)(
                        (packed >> 10U) & UINT32_C(0x3ff));
                    OpCode operation =
                        (OpCode)(
                            (packed >> 20U) &
                            UINT32_C(0x3f));
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >
                            function->code_count ||
                        left_slot >= function->local_count ||
                        right_slot >= function->local_count ||
                        operation < OP_EQ ||
                        operation > OP_GE_I64)
                        return false;
                    break;
                }
                case OP_COMPARE_LOCAL_CONSTANT_BRANCH: {
                    uint32_t packed = (uint32_t)instruction.b;
                    size_t local_slot = (size_t)(
                        packed & UINT32_C(0x3ff));
                    size_t constant_index = (size_t)(
                        (packed >> 10U) &
                        UINT32_C(0xffff));
                    OpCode operation =
                        (OpCode)(packed >> 26U);
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >
                            function->code_count ||
                        local_slot >= function->local_count ||
                        constant_index >= module->constant_count ||
                        operation < OP_EQ ||
                        operation > OP_GE_I64)
                        return false;
                    break;
                }
                case OP_ITER_NEXT:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >
                            function->code_count ||
                        instruction.b < 0 ||
                        (size_t)instruction.b >=
                            function->local_count)
                        return false;
                    break;
                case OP_CALL:
                    if (instruction.a >= 0 &&
                        (size_t)instruction.a >=
                            module->function_count)
                        return false;
                    if (instruction.b < 0)
                        return false;
                    if (instruction.a < 0 &&
                        ((size_t)instruction.b !=
                             function->call_sites[ip].argument_count ||
                         (instruction.b != 0 &&
                          function->call_sites[ip].argument_modes == NULL)))
                        return false;
                    break;
                case OP_CALL_VIRTUAL:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >= module->function_count ||
                        instruction.b <= 0)
                        return false;
                    break;
                case OP_CALL_LOCAL: {
                    uint32_t encoded =
                        (uint32_t)instruction.b;
                    size_t destination = (size_t)(
                        (encoded >> 10U) &
                        UINT32_C(0x3ff));
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            module->function_count ||
                        destination >= function->local_count)
                        return false;
                    break;
                }
                case OP_CALL_LOCAL_2_COPY: {
                    uint32_t packed =
                        (uint32_t)instruction.b;
                    size_t first_slot = (size_t)(
                        packed & UINT32_C(0x3ff));
                    size_t second_slot = (size_t)(
                        (packed >> 10U) &
                        UINT32_C(0x3ff));
                    size_t destination = (size_t)(
                        (packed >> 20U) &
                        UINT32_C(0x3ff));
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            module->function_count ||
                        first_slot >= function->local_count ||
                        second_slot >= function->local_count ||
                        destination >= function->local_count)
                        return false;
                    break;
                }
                case OP_TEXT_LEN_LOCAL:
                    if (instruction.a < 0 || instruction.b < 0 ||
                        (size_t)instruction.a >=
                            function->local_count ||
                        (size_t)instruction.b >=
                            function->local_count)
                        return false;
                    break;
                case OP_STRING_SEARCH_LOCAL: {
                    uint32_t packed = (uint32_t)instruction.a;
                    uint32_t options = (uint32_t)instruction.b;
                    size_t value_slot = (size_t)(
                        packed & UINT32_C(0x3ff));
                    size_t needle_slot = (size_t)(
                        (packed >> 10U) & UINT32_C(0x3ff));
                    size_t destination = (size_t)(
                        (packed >> 20U) & UINT32_C(0x3ff));
                    size_t start_slot = (size_t)(
                        options & UINT32_C(0x3ff));
                    unsigned kind = options >> 10U;
                    if (kind > 4U ||
                        value_slot >= function->local_count ||
                        needle_slot >= function->local_count ||
                        destination >= function->local_count ||
                        (kind == 1U &&
                         start_slot >= function->local_count) ||
                        value_slot == needle_slot ||
                        destination == value_slot ||
                        destination == needle_slot ||
                        (kind == 1U &&
                         (start_slot == value_slot ||
                          start_slot == needle_slot ||
                          start_slot == destination)))
                        return false;
                    break;
                }
                case OP_STRING_SEARCH_LOCAL_CONSTANT: {
                    uint32_t packed = (uint32_t)instruction.a;
                    size_t value_slot = (size_t)(
                        packed & UINT32_C(0x3ff));
                    size_t destination = (size_t)(
                        (packed >> 10U) & UINT32_C(0x3ff));
                    unsigned kind =
                        (packed >> 20U) & UINT32_C(0x7);
                    if ((kind != 0U && kind != 2U &&
                         kind != 3U && kind != 4U) ||
                        (packed >> 23U) != 0U ||
                        value_slot >= function->local_count ||
                        destination >= function->local_count ||
                        value_slot == destination ||
                        instruction.b < 0 ||
                        (size_t)instruction.b >=
                            module->constant_count ||
                        module->constants[(size_t)instruction.b]
                            .value.tag != LANG_VALUE_STRING_VIEW)
                        return false;
                    break;
                }
                case OP_RETURN_LOCAL:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >=
                            function->local_count ||
                        (instruction.b != 0 && instruction.b != 1))
                        return false;
                    break;
                case OP_FUNCTION:
                case OP_BOUND_FUNCTION:
                    if (instruction.a < 0 ||
                        (size_t)instruction.a >= module->function_count ||
                        (instruction.op == OP_BOUND_FUNCTION &&
                         instruction.b != 0 && instruction.b != 1))
                        return false;
                    break;
                case OP_CALL_INDIRECT:
                    if (instruction.a < 0)
                        return false;
                    break;
                case OP_MAKE_ARRAY:
                    if (instruction.a < 0) return false;
                    break;
                default:
                    break;
            }
        }
        if (!verify_function_stack(function)) return false;
    }
    return true;
}
