#include "vm_internal.h"

#include <stdint.h>

static bool execute_fast_affine_wrap_leaf(
    LangVM *vm, const BytecodeFunction *function,
    const LangValue *arguments, size_t argument_count,
    LangValue *out_result, bool *matched) {
    *matched = false;
    if (argument_count != 2U || function->code_count != 8U)
        return true;
    const Instruction *sum = &function->code[0];
    const Instruction *offset = &function->code[1];
    const Instruction *test = &function->code[2];
    const Instruction *copy = &function->code[3];
    const Instruction *constant = &function->code[4];
    const Instruction *subtract = &function->code[5];
    const Instruction *wrapped_return = &function->code[6];
    const Instruction *plain_return = &function->code[7];
    if (sum->op != OP_BINARY_LOCALS ||
        offset->op != OP_BINARY_LOCAL_IMMEDIATE ||
        test->op != OP_COMPARE_LOCAL_CONSTANT_BRANCH ||
        copy->op != OP_COPY_LOCAL_TO ||
        constant->op != OP_CONSTANT_LOCAL ||
        subtract->op != OP_BINARY_LOCALS ||
        wrapped_return->op != OP_RETURN_LOCAL ||
        plain_return->op != OP_RETURN_LOCAL || test->a != 7 ||
        constant->a < 0 ||
        (size_t)constant->a >= vm->module->constant_count)
        return true;
    uint32_t sum_slots = (uint32_t)sum->b;
    size_t sum_left =
        (size_t)(sum_slots & UINT32_C(0x3ff));
    size_t sum_right = (size_t)(
        (sum_slots >> 10U) & UINT32_C(0x3ff));
    size_t sum_destination = (size_t)(
        (sum_slots >> 20U) & UINT32_C(0x3ff));
    uint32_t offset_slots = (uint32_t)offset->b;
    size_t offset_source =
        (size_t)(offset_slots & UINT32_C(0x3ff));
    size_t result_slot = (size_t)(
        (offset_slots >> 10U) & UINT32_C(0x3ff));
    int64_t addend =
        (int64_t)((int32_t)offset_slots >> 20U);
    uint32_t test_packed = (uint32_t)test->b;
    size_t test_local =
        (size_t)(test_packed & UINT32_C(0x3ff));
    size_t test_constant = (size_t)(
        (test_packed >> 10U) & UINT32_C(0xffff));
    uint32_t subtract_slots = (uint32_t)subtract->b;
    if (((OpCode)((uint32_t)sum->a & UINT32_C(0xff))) !=
            OP_ADD_I64 || sum_left != 0U || sum_right != 1U ||
        ((OpCode)((uint32_t)offset->a & UINT32_C(0xff))) !=
            OP_ADD_I64 || offset_source != sum_destination ||
        (OpCode)(test_packed >> 26U) != OP_GT_I64 ||
        test_local != result_slot ||
        test_constant >= vm->module->constant_count ||
        copy->a != (int32_t)result_slot ||
        ((OpCode)((uint32_t)subtract->a & UINT32_C(0xff))) !=
            OP_SUB_I64 ||
        (size_t)(subtract_slots & UINT32_C(0x3ff)) !=
            (size_t)copy->b ||
        (size_t)((subtract_slots >> 10U) & UINT32_C(0x3ff)) !=
            (size_t)constant->b ||
        wrapped_return->a !=
            (int32_t)((subtract_slots >> 20U) & UINT32_C(0x3ff)) ||
        plain_return->a != (int32_t)result_slot)
        return true;
    LangValue test_value =
        vm->module->constants[test_constant].value;
    LangValue subtract_value =
        vm->module->constants[(size_t)constant->a].value;
    if (arguments[0].tag != LANG_VALUE_I64 ||
        arguments[1].tag != LANG_VALUE_I64 ||
        test_value.tag != LANG_VALUE_I64 ||
        subtract_value.tag != LANG_VALUE_I64 ||
        test_value.as.i64 != subtract_value.as.i64)
        return true;
    *matched = true;
    int64_t result;
    if (!vm_checked_add(arguments[0].as.i64,
                     arguments[1].as.i64, &result) ||
        !vm_checked_add(result, addend, &result) ||
        (result > test_value.as.i64 &&
         !vm_checked_sub(result, subtract_value.as.i64, &result)))
        return false;
    *out_result = (LangValue){
        .tag=LANG_VALUE_I64, .as.i64=result
    };
    return true;
}

bool vm_execute_fast_scalar_leaf(
    LangVM *vm, size_t function_index,
    const LangValue *arguments, size_t argument_count,
    LangSpan call_span, LangValue *out_result) {
    const BytecodeFunction *function =
        &vm->module->functions[function_index];
    if (function->fast_affine_wrap_leaf && argument_count == 2U &&
        arguments[0].tag == LANG_VALUE_I64 &&
        arguments[1].tag == LANG_VALUE_I64) {
        int64_t result;
        bool ok = vm_checked_add(arguments[0].as.i64,
                              arguments[1].as.i64, &result) &&
            vm_checked_add(result, function->fast_affine_addend, &result);
        if (ok && result > function->fast_affine_limit)
            ok = vm_checked_sub(result, function->fast_affine_limit,
                             &result);
        if (ok) {
            *out_result = (LangValue){
                .tag=LANG_VALUE_I64, .as.i64=result
            };
            if (vm->instruction_count <= UINT64_MAX - 4U)
                vm->instruction_count += 4U;
            else
                vm->instruction_count = UINT64_MAX;
            return true;
        }
        vm->frames[vm->frame_count] = function->name;
        vm->frame_call_sites[vm->frame_count] = call_span;
        ++vm->frame_count;
        vm_runtime_error_at(vm, call_span,
                         "integer overflow in scalar function");
        --vm->frame_count;
        return false;
    }
    bool matched = false;
    bool affine_ok = execute_fast_affine_wrap_leaf(
        vm, function, arguments, argument_count,
        out_result, &matched);
    if (matched) {
        if (affine_ok) {
            if (vm->instruction_count <= UINT64_MAX - 4U)
                vm->instruction_count += 4U;
            else
                vm->instruction_count = UINT64_MAX;
            return true;
        }
        vm->frames[vm->frame_count] = function->name;
        vm->frame_call_sites[vm->frame_count] = call_span;
        ++vm->frame_count;
        vm_runtime_error_at(vm, call_span,
                         "integer overflow in scalar function");
        --vm->frame_count;
        return false;
    }
    LangValue locals[64];
    for (size_t i = 0U; i < argument_count; ++i)
        locals[i] = arguments[i];
    size_t ip = 0U;
    uint64_t executed = 0U;
    while (ip < function->code_count) {
        size_t instruction_index = ip++;
        Instruction instruction =
            function->code[instruction_index];
        ++executed;
        switch (instruction.op) {
            case OP_CONSTANT_LOCAL:
                locals[(size_t)instruction.b] =
                    vm->module->constants[
                        (size_t)instruction.a].value;
                break;
            case OP_COPY_LOCAL_TO:
                locals[(size_t)instruction.b] =
                    locals[(size_t)instruction.a];
                break;
            case OP_BINARY_LOCALS: {
                uint32_t slots = (uint32_t)instruction.b;
                size_t left = (size_t)(slots & UINT32_C(0x3ff));
                size_t right = (size_t)(
                    (slots >> 10U) & UINT32_C(0x3ff));
                size_t destination = (size_t)(
                    (slots >> 20U) & UINT32_C(0x3ff));
                uint32_t packed_operation =
                    (uint32_t)instruction.a;
                OpCode operation = (OpCode)(
                    packed_operation & UINT32_C(0xff));
                TypeKind type = (TypeKind)(packed_operation >> 8U);
                int64_t result = 0;
                bool ok =
                    locals[left].tag == LANG_VALUE_I64 &&
                    locals[right].tag == LANG_VALUE_I64;
                if (ok && operation == OP_ADD_I64)
                    ok = vm_checked_add(
                        locals[left].as.i64,
                        locals[right].as.i64, &result);
                else if (ok && operation == OP_SUB_I64)
                    ok = vm_checked_sub(
                        locals[left].as.i64,
                        locals[right].as.i64, &result);
                else if (ok && operation == OP_MUL_I64)
                    ok = vm_checked_mul(
                        locals[left].as.i64,
                        locals[right].as.i64, &result);
                else
                    ok = false;
                if (!ok || !vm_signed_value_fits_type(result, type))
                    goto invalid_operation;
                locals[destination] = (LangValue){
                    .tag=LANG_VALUE_I64, .as.i64=result
                };
                break;
            }
            case OP_BINARY_LOCAL_IMMEDIATE: {
                uint32_t packed = (uint32_t)instruction.b;
                size_t source =
                    (size_t)(packed & UINT32_C(0x3ff));
                size_t destination = (size_t)(
                    (packed >> 10U) & UINT32_C(0x3ff));
                int64_t immediate =
                    (int64_t)((int32_t)packed >> 20U);
                uint32_t packed_operation =
                    (uint32_t)instruction.a;
                OpCode operation = (OpCode)(
                    packed_operation & UINT32_C(0xff));
                TypeKind type = (TypeKind)(packed_operation >> 8U);
                int64_t result = 0;
                bool ok = locals[source].tag == LANG_VALUE_I64;
                if (ok && operation == OP_ADD_I64)
                    ok = vm_checked_add(
                        locals[source].as.i64, immediate, &result);
                else if (ok && operation == OP_SUB_I64)
                    ok = vm_checked_sub(
                        locals[source].as.i64, immediate, &result);
                else if (ok && operation == OP_MUL_I64)
                    ok = vm_checked_mul(
                        locals[source].as.i64, immediate, &result);
                else
                    ok = false;
                if (!ok || !vm_signed_value_fits_type(result, type))
                    goto invalid_operation;
                locals[destination] = (LangValue){
                    .tag=LANG_VALUE_I64, .as.i64=result
                };
                break;
            }
            case OP_BINARY_LOCALS_IMMEDIATE: {
                uint32_t slots = (uint32_t)instruction.b;
                size_t left = (size_t)(slots & UINT32_C(0x3ff));
                size_t right = (size_t)(
                    (slots >> 10U) & UINT32_C(0x3ff));
                size_t destination = (size_t)(
                    (slots >> 20U) & UINT32_C(0x3ff));
                uint32_t packed = (uint32_t)instruction.a;
                OpCode first =
                    (OpCode)(packed & UINT32_C(0xff));
                TypeKind type = (TypeKind)(
                    (packed >> 8U) & UINT32_C(0xff));
                OpCode second = (OpCode)(
                    (packed >> 16U) & UINT32_C(0xff));
                int64_t immediate =
                    (int64_t)(int8_t)(packed >> 24U);
                int64_t intermediate = 0;
                int64_t result = 0;
                bool ok = locals[left].tag == LANG_VALUE_I64 &&
                    locals[right].tag == LANG_VALUE_I64;
                if (ok && first == OP_ADD_I64)
                    ok = vm_checked_add(locals[left].as.i64,
                                     locals[right].as.i64,
                                     &intermediate);
                else if (ok && first == OP_SUB_I64)
                    ok = vm_checked_sub(locals[left].as.i64,
                                     locals[right].as.i64,
                                     &intermediate);
                else if (ok && first == OP_MUL_I64)
                    ok = vm_checked_mul(locals[left].as.i64,
                                     locals[right].as.i64,
                                     &intermediate);
                else
                    ok = false;
                if (ok && second == OP_ADD_I64)
                    ok = vm_checked_add(intermediate, immediate, &result);
                else if (ok && second == OP_SUB_I64)
                    ok = vm_checked_sub(intermediate, immediate, &result);
                else if (ok && second == OP_MUL_I64)
                    ok = vm_checked_mul(intermediate, immediate, &result);
                else
                    ok = false;
                if (!ok || !vm_signed_value_fits_type(result, type))
                    goto invalid_operation;
                locals[destination] = (LangValue){
                    .tag=LANG_VALUE_I64, .as.i64=result
                };
                break;
            }
            case OP_COMPARE_LOCAL_CONSTANT_BRANCH: {
                uint32_t packed = (uint32_t)instruction.b;
                size_t local =
                    (size_t)(packed & UINT32_C(0x3ff));
                size_t constant = (size_t)(
                    (packed >> 10U) & UINT32_C(0xffff));
                OpCode operation = (OpCode)(packed >> 26U);
                LangValue right =
                    vm->module->constants[constant].value;
                if (locals[local].tag != LANG_VALUE_I64 ||
                    right.tag != LANG_VALUE_I64)
                    goto invalid_operation;
                int64_t left = locals[local].as.i64;
                bool result = operation == OP_EQ
                    ? left == right.as.i64
                    : operation == OP_NEQ
                    ? left != right.as.i64
                    : operation == OP_LT_I64
                    ? left < right.as.i64
                    : operation == OP_LE_I64
                    ? left <= right.as.i64
                    : operation == OP_GT_I64
                    ? left > right.as.i64
                    : left >= right.as.i64;
                if (!result) ip = (size_t)instruction.a;
                break;
            }
            case OP_JUMP:
                ip = (size_t)instruction.a;
                break;
            case OP_RETURN_LOCAL:
                *out_result = locals[(size_t)instruction.a];
                if (UINT64_MAX - vm->instruction_count < executed)
                    vm->instruction_count = UINT64_MAX;
                else
                    vm->instruction_count += executed;
                return true;
            default:
                goto invalid_operation;
        }
        continue;
invalid_operation:
        vm->active_span = function->spans[instruction_index];
        vm->frames[vm->frame_count] = function->name;
        vm->frame_call_sites[vm->frame_count] = call_span;
        ++vm->frame_count;
        vm_runtime_error_at(
            vm, vm->active_span,
            "invalid operation in optimized scalar function");
        if (UINT64_MAX - vm->instruction_count < executed)
            vm->instruction_count = UINT64_MAX;
        else
            vm->instruction_count += executed;
        --vm->frame_count;
        return false;
    }
    return false;
}

static bool execute_affine_wrap_loop(
    LangVM *vm, const BytecodeFunction *function,
    LangValue *locals, bool *initialized,
    size_t loop_start, size_t loop_end, size_t *io_ip,
    uint64_t *executed, bool *matched) {
    *matched = false;
    if (loop_end < loop_start + 10U)
        return true;
    const Instruction *header = &function->code[loop_start];
    const Instruction *sum = &function->code[loop_start + 1U];
    const Instruction *offset = &function->code[loop_start + 2U];
    const Instruction *wrap_test = &function->code[loop_start + 3U];
    const Instruction *to_wrap = &function->code[loop_start + 4U];
    const Instruction *increment = &function->code[loop_end - 1U];
    const Instruction *back = &function->code[loop_end];
    if (header->op != OP_COMPARE_LOCAL_CONSTANT_BRANCH ||
        sum->op != OP_BINARY_LOCALS ||
        offset->op != OP_BINARY_LOCAL_IMMEDIATE ||
        wrap_test->op != OP_COMPARE_LOCAL_CONSTANT_BRANCH ||
        to_wrap->op != OP_JUMP ||
        increment->op != OP_BINARY_LOCAL_IMMEDIATE ||
        back->op != OP_JUMP || back->a != (int32_t)loop_start)
        return true;
    uint32_t header_packed = (uint32_t)header->b;
    size_t iteration =
        (size_t)(header_packed & UINT32_C(0x3ff));
    size_t end_constant = (size_t)(
        (header_packed >> 10U) & UINT32_C(0xffff));
    if ((OpCode)(header_packed >> 26U) != OP_LT_I64 ||
        end_constant >= vm->module->constant_count)
        return true;
    uint32_t sum_slots = (uint32_t)sum->b;
    size_t value = (size_t)(sum_slots & UINT32_C(0x3ff));
    size_t sum_right = (size_t)(
        (sum_slots >> 10U) & UINT32_C(0x3ff));
    size_t temporary = (size_t)(
        (sum_slots >> 20U) & UINT32_C(0x3ff));
    if (((OpCode)((uint32_t)sum->a & UINT32_C(0xff))) !=
            OP_ADD_I64 || sum_right != iteration)
        return true;
    uint32_t offset_slots = (uint32_t)offset->b;
    size_t offset_source =
        (size_t)(offset_slots & UINT32_C(0x3ff));
    size_t offset_destination = (size_t)(
        (offset_slots >> 10U) & UINT32_C(0x3ff));
    int64_t addend =
        (int64_t)((int32_t)offset_slots >> 20U);
    if (((OpCode)((uint32_t)offset->a & UINT32_C(0xff))) !=
            OP_ADD_I64 || offset_source != temporary ||
        offset_destination != value)
        return true;
    uint32_t wrap_packed = (uint32_t)wrap_test->b;
    size_t wrap_local =
        (size_t)(wrap_packed & UINT32_C(0x3ff));
    size_t limit_constant = (size_t)(
        (wrap_packed >> 10U) & UINT32_C(0xffff));
    size_t increment_index = loop_end - 1U;
    if ((OpCode)(wrap_packed >> 26U) != OP_GT_I64 ||
        wrap_local != value ||
        wrap_test->a != (int32_t)increment_index ||
        limit_constant >= vm->module->constant_count)
        return true;
    size_t wrap_start = (size_t)to_wrap->a;
    if (wrap_start + 3U >= loop_end ||
        function->code[wrap_start].op != OP_COPY_LOCAL_TO ||
        function->code[wrap_start + 1U].op != OP_CONSTANT_LOCAL ||
        function->code[wrap_start + 2U].op != OP_BINARY_LOCALS ||
        function->code[wrap_start + 3U].op != OP_JUMP ||
        function->code[wrap_start + 3U].a !=
            (int32_t)increment_index)
        return true;
    const Instruction *wrap_copy = &function->code[wrap_start];
    const Instruction *wrap_constant =
        &function->code[wrap_start + 1U];
    const Instruction *subtract = &function->code[wrap_start + 2U];
    uint32_t subtract_slots = (uint32_t)subtract->b;
    if (wrap_constant->a < 0 ||
        (size_t)wrap_constant->a >= vm->module->constant_count)
        return true;
    if (wrap_copy->a != (int32_t)value ||
        ((OpCode)((uint32_t)subtract->a & UINT32_C(0xff))) !=
            OP_SUB_I64 ||
        (size_t)(subtract_slots & UINT32_C(0x3ff)) !=
            (size_t)wrap_copy->b ||
        (size_t)((subtract_slots >> 10U) & UINT32_C(0x3ff)) !=
            (size_t)wrap_constant->b ||
        (size_t)((subtract_slots >> 20U) & UINT32_C(0x3ff)) !=
            value)
        return true;
    uint32_t increment_slots = (uint32_t)increment->b;
    int64_t step =
        (int64_t)((int32_t)increment_slots >> 20U);
    if (((OpCode)((uint32_t)increment->a & UINT32_C(0xff))) !=
            OP_ADD_I64 ||
        (size_t)(increment_slots & UINT32_C(0x3ff)) != iteration ||
        (size_t)((increment_slots >> 10U) & UINT32_C(0x3ff)) !=
            iteration)
        return true;
    LangValue end_value =
        vm->module->constants[end_constant].value;
    LangValue limit_value =
        vm->module->constants[limit_constant].value;
    LangValue subtract_value =
        vm->module->constants[(size_t)wrap_constant->a].value;
    if (!initialized[value] || !initialized[iteration] ||
        locals[value].tag != LANG_VALUE_I64 ||
        locals[iteration].tag != LANG_VALUE_I64 ||
        end_value.tag != LANG_VALUE_I64 ||
        limit_value.tag != LANG_VALUE_I64 ||
        subtract_value.tag != LANG_VALUE_I64 ||
        subtract_value.as.i64 != limit_value.as.i64)
        return true;
    *matched = true;
    int64_t current = locals[value].as.i64;
    int64_t index = locals[iteration].as.i64;
    while (index < end_value.as.i64) {
        int64_t next;
        if (!vm_checked_add(current, index, &next) ||
            !vm_checked_add(next, addend, &next)) {
            vm->active_span = function->spans[loop_start + 1U];
            vm_runtime_error_at(vm, vm->active_span,
                             "integer overflow in scalar loop");
            return false;
        }
        bool wrapped = next > limit_value.as.i64;
        if (wrapped && !vm_checked_sub(
                next, limit_value.as.i64, &next)) {
            vm->active_span = function->spans[wrap_start + 2U];
            vm_runtime_error_at(vm, vm->active_span,
                             "integer overflow in scalar loop");
            return false;
        }
        if (!vm_checked_add(index, step, &index)) {
            vm->active_span = function->spans[increment_index];
            vm_runtime_error_at(vm, vm->active_span,
                             "integer overflow in scalar loop");
            return false;
        }
        current = next;
        uint64_t cost = wrapped ? 10U : 6U;
        if (UINT64_MAX - *executed < cost)
            *executed = UINT64_MAX;
        else
            *executed += cost;
    }
    locals[value].as.i64 = current;
    locals[iteration].as.i64 = index;
    *io_ip = (size_t)header->a;
    if (*executed != UINT64_MAX)
        ++*executed;
    return true;
}

bool vm_execute_fast_scalar_loop(
    LangVM *vm, const BytecodeFunction *function,
    LangValue *locals, bool *initialized,
    size_t loop_end, size_t *io_ip,
    uint64_t *executed) {
    size_t ip = *io_ip;
    bool matched = false;
    if (!execute_affine_wrap_loop(
            vm, function, locals, initialized, ip, loop_end,
            &ip, executed, &matched))
        return false;
    if (matched) {
        *io_ip = ip;
        return true;
    }
    while (ip <= loop_end) {
        size_t instruction_index = ip++;
        Instruction instruction = function->code[instruction_index];
        switch (instruction.op) {
            case OP_CONSTANT_LOCAL:
                locals[(size_t)instruction.b] =
                    vm->module->constants[
                        (size_t)instruction.a].value;
                initialized[(size_t)instruction.b] = true;
                break;
            case OP_COPY_LOCAL_TO:
                if (!initialized[(size_t)instruction.a])
                    goto unsupported_operation;
                locals[(size_t)instruction.b] =
                    locals[(size_t)instruction.a];
                initialized[(size_t)instruction.b] = true;
                break;
            case OP_BINARY_LOCALS: {
                uint32_t slots = (uint32_t)instruction.b;
                size_t left = (size_t)(slots & UINT32_C(0x3ff));
                size_t right = (size_t)(
                    (slots >> 10U) & UINT32_C(0x3ff));
                size_t destination = (size_t)(
                    (slots >> 20U) & UINT32_C(0x3ff));
                uint32_t packed = (uint32_t)instruction.a;
                OpCode operation =
                    (OpCode)(packed & UINT32_C(0xff));
                TypeKind type = (TypeKind)(packed >> 8U);
                int64_t result = 0;
                if (!initialized[left] || !initialized[right] ||
                    locals[left].tag != LANG_VALUE_I64 ||
                    locals[right].tag != LANG_VALUE_I64 ||
                    operation < OP_ADD_I64 ||
                    operation > OP_MUL_I64)
                    goto unsupported_operation;
                bool ok = true;
                if (ok && operation == OP_ADD_I64)
                    ok = vm_checked_add(locals[left].as.i64,
                                     locals[right].as.i64, &result);
                else if (ok && operation == OP_SUB_I64)
                    ok = vm_checked_sub(locals[left].as.i64,
                                     locals[right].as.i64, &result);
                else if (ok && operation == OP_MUL_I64)
                    ok = vm_checked_mul(locals[left].as.i64,
                                     locals[right].as.i64, &result);
                else
                    ok = false;
                if (!ok || !vm_signed_value_fits_type(result, type))
                    goto invalid_operation;
                locals[destination] = (LangValue){
                    .tag=LANG_VALUE_I64, .as.i64=result
                };
                initialized[destination] = true;
                break;
            }
            case OP_BINARY_LOCAL_IMMEDIATE: {
                uint32_t slots = (uint32_t)instruction.b;
                size_t source =
                    (size_t)(slots & UINT32_C(0x3ff));
                size_t destination = (size_t)(
                    (slots >> 10U) & UINT32_C(0x3ff));
                int64_t immediate =
                    (int64_t)((int32_t)slots >> 20U);
                uint32_t packed = (uint32_t)instruction.a;
                OpCode operation =
                    (OpCode)(packed & UINT32_C(0xff));
                TypeKind type = (TypeKind)(packed >> 8U);
                int64_t result = 0;
                if (!initialized[source] ||
                    locals[source].tag != LANG_VALUE_I64 ||
                    operation < OP_ADD_I64 ||
                    operation > OP_MUL_I64)
                    goto unsupported_operation;
                bool ok = true;
                if (ok && operation == OP_ADD_I64)
                    ok = vm_checked_add(
                        locals[source].as.i64, immediate, &result);
                else if (ok && operation == OP_SUB_I64)
                    ok = vm_checked_sub(
                        locals[source].as.i64, immediate, &result);
                else if (ok && operation == OP_MUL_I64)
                    ok = vm_checked_mul(
                        locals[source].as.i64, immediate, &result);
                else
                    ok = false;
                if (!ok || !vm_signed_value_fits_type(result, type))
                    goto invalid_operation;
                locals[destination] = (LangValue){
                    .tag=LANG_VALUE_I64, .as.i64=result
                };
                initialized[destination] = true;
                break;
            }
            case OP_BINARY_LOCALS_IMMEDIATE: {
                uint32_t slots = (uint32_t)instruction.b;
                size_t left = (size_t)(slots & UINT32_C(0x3ff));
                size_t right = (size_t)(
                    (slots >> 10U) & UINT32_C(0x3ff));
                size_t destination = (size_t)(
                    (slots >> 20U) & UINT32_C(0x3ff));
                uint32_t packed = (uint32_t)instruction.a;
                OpCode first =
                    (OpCode)(packed & UINT32_C(0xff));
                TypeKind type = (TypeKind)(
                    (packed >> 8U) & UINT32_C(0xff));
                OpCode second = (OpCode)(
                    (packed >> 16U) & UINT32_C(0xff));
                int64_t immediate =
                    (int64_t)(int8_t)(packed >> 24U);
                int64_t intermediate = 0;
                int64_t result = 0;
                if (!initialized[left] || !initialized[right] ||
                    locals[left].tag != LANG_VALUE_I64 ||
                    locals[right].tag != LANG_VALUE_I64 ||
                    first < OP_ADD_I64 || first > OP_MUL_I64 ||
                    second < OP_ADD_I64 || second > OP_MUL_I64)
                    goto unsupported_operation;
                bool ok = true;
                if (ok && first == OP_ADD_I64)
                    ok = vm_checked_add(locals[left].as.i64,
                                     locals[right].as.i64,
                                     &intermediate);
                else if (ok && first == OP_SUB_I64)
                    ok = vm_checked_sub(locals[left].as.i64,
                                     locals[right].as.i64,
                                     &intermediate);
                else if (ok && first == OP_MUL_I64)
                    ok = vm_checked_mul(locals[left].as.i64,
                                     locals[right].as.i64,
                                     &intermediate);
                else
                    ok = false;
                if (ok && second == OP_ADD_I64)
                    ok = vm_checked_add(intermediate, immediate, &result);
                else if (ok && second == OP_SUB_I64)
                    ok = vm_checked_sub(intermediate, immediate, &result);
                else if (ok && second == OP_MUL_I64)
                    ok = vm_checked_mul(intermediate, immediate, &result);
                else
                    ok = false;
                if (!ok || !vm_signed_value_fits_type(result, type))
                    goto invalid_operation;
                locals[destination] = (LangValue){
                    .tag=LANG_VALUE_I64, .as.i64=result
                };
                initialized[destination] = true;
                break;
            }
            case OP_COMPARE_LOCAL_CONSTANT_BRANCH: {
                uint32_t packed = (uint32_t)instruction.b;
                size_t local =
                    (size_t)(packed & UINT32_C(0x3ff));
                size_t constant = (size_t)(
                    (packed >> 10U) & UINT32_C(0xffff));
                OpCode operation = (OpCode)(packed >> 26U);
                LangValue right =
                    vm->module->constants[constant].value;
                if (!initialized[local] ||
                    locals[local].tag != LANG_VALUE_I64 ||
                    right.tag != LANG_VALUE_I64)
                    goto unsupported_operation;
                int64_t left = locals[local].as.i64;
                bool result = operation == OP_EQ
                    ? left == right.as.i64
                    : operation == OP_NEQ
                    ? left != right.as.i64
                    : operation == OP_LT_I64
                    ? left < right.as.i64
                    : operation == OP_LE_I64
                    ? left <= right.as.i64
                    : operation == OP_GT_I64
                    ? left > right.as.i64
                    : left >= right.as.i64;
                if (!result)
                    ip = (size_t)instruction.a;
                break;
            }
            case OP_JUMP:
                ip = (size_t)instruction.a;
                break;
            default:
unsupported_operation:
                *io_ip = instruction_index;
                return true;
        }
        ++*executed;
    }
    *io_ip = ip;
    return true;

invalid_operation:
    vm->active_span = function->spans[ip - 1U];
    vm_runtime_error_at(
        vm, vm->active_span,
        "invalid operation in optimized scalar loop");
    return false;
}
