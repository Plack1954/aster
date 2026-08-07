            case OP_ADD_I64: case OP_SUB_I64: case OP_MUL_I64:
            case OP_DIV_I64: case OP_REM_I64: {
                LangValue right = POP(), left = POP();
                if (left.tag == LANG_VALUE_U64 &&
                    right.tag == LANG_VALUE_U64) {
                    uint64_t result = 0U;
                    bool ok = true;
                    if (scalar_operation == OP_ADD_I64) {
                        ok = left.as.u64 <= UINT64_MAX - right.as.u64;
                        if (ok) result = left.as.u64 + right.as.u64;
                    } else if (scalar_operation == OP_SUB_I64) {
                        ok = left.as.u64 >= right.as.u64;
                        if (ok) result = left.as.u64 - right.as.u64;
                    } else if (scalar_operation == OP_MUL_I64) {
                        ok = right.as.u64 == 0U ||
                             left.as.u64 <= UINT64_MAX / right.as.u64;
                        if (ok) result = left.as.u64 * right.as.u64;
                    } else {
                        if (right.as.u64 == 0U) {
                            runtime_error(vm, instruction,
                                          "division by zero");
                            goto fail;
                        }
                        result = scalar_operation == OP_DIV_I64
                               ? left.as.u64 / right.as.u64
                               : left.as.u64 % right.as.u64;
                    }
                    ok = ok && vm_unsigned_value_fits_type(
                        result, (TypeKind)scalar_type);
                    if (!ok) {
                        runtime_error(vm, instruction,
                                      "integer overflow or invalid operands");
                        goto fail;
                    }
                    STORE_SCALAR_RESULT(((LangValue){
                        .tag=LANG_VALUE_U64, .as.u64=result
                    }));
                    break;
                }
                LangValue result = {.tag=LANG_VALUE_I64};
                bool ok = left.tag == LANG_VALUE_I64 && right.tag == LANG_VALUE_I64;
                if (ok && scalar_operation == OP_ADD_I64) ok = vm_checked_add(left.as.i64, right.as.i64, &result.as.i64);
                else if (ok && scalar_operation == OP_SUB_I64) ok = vm_checked_sub(left.as.i64, right.as.i64, &result.as.i64);
                else if (ok && scalar_operation == OP_MUL_I64) ok = vm_checked_mul(left.as.i64, right.as.i64, &result.as.i64);
                else if (ok && scalar_operation == OP_DIV_I64) {
                    if (right.as.i64 == 0) { runtime_error(vm, instruction, "division by zero"); goto fail; }
                    if (left.as.i64 == INT64_MIN && right.as.i64 == -1) ok = false;
                    else result.as.i64 = left.as.i64 / right.as.i64;
                } else if (ok) {
                    if (right.as.i64 == 0) { runtime_error(vm, instruction, "division by zero"); goto fail; }
                    result.as.i64 = left.as.i64 % right.as.i64;
                }
                if (ok)
                    ok = vm_signed_value_fits_type(
                        result.as.i64, (TypeKind)scalar_type);
                if (!ok) { runtime_error(vm, instruction, "integer overflow or invalid operands"); goto fail; }
                STORE_SCALAR_RESULT(result); break;
            }
            VM_LABEL(shift)
            case OP_SHIFT_LEFT: case OP_SHIFT_RIGHT: {
                LangValue right = POP();
                LangValue left = POP();
                uint64_t count;
                if (right.tag == LANG_VALUE_I64) {
                    if (right.as.i64 < 0) {
                        runtime_error(vm, instruction,
                                      "invalid shift count");
                        goto fail;
                    }
                    count = (uint64_t)right.as.i64;
                } else if (right.tag == LANG_VALUE_U64) {
                    count = right.as.u64;
                } else {
                    runtime_error(vm, instruction,
                                  "invalid shift operands");
                    goto fail;
                }
                unsigned width = vm_runtime_integer_width(
                    (TypeKind)instruction.a);
                if (width == 0U || count >= (uint64_t)width) {
                    runtime_error(vm, instruction,
                                  "invalid shift count");
                    goto fail;
                }
                if (left.tag == LANG_VALUE_U64) {
                    uint64_t result;
                    if (instruction.op == OP_SHIFT_RIGHT) {
                        result = left.as.u64 >> (unsigned)count;
                    } else {
                        result = left.as.u64 << (unsigned)count;
                        if ((count != 0U &&
                             (result >> (unsigned)count) != left.as.u64) ||
                            !vm_unsigned_value_fits_type(
                                result, (TypeKind)instruction.a)) {
                            runtime_error(vm, instruction,
                                          "integer shift overflow");
                            goto fail;
                        }
                    }
                    PUSH(((LangValue){
                        .tag=LANG_VALUE_U64, .as.u64=result
                    }));
                    break;
                }
                if (left.tag != LANG_VALUE_I64) {
                    runtime_error(vm, instruction,
                                  "invalid shift operands");
                    goto fail;
                }
                int64_t result;
                if (instruction.op == OP_SHIFT_RIGHT) {
                    if (count == 63U) {
                        result = left.as.i64 < 0 ? -1 : 0;
                    } else {
                        int64_t divisor =
                            INT64_C(1) << (unsigned)count;
                        result = left.as.i64 / divisor;
                        if (left.as.i64 < 0 &&
                            left.as.i64 % divisor != 0)
                            --result;
                    }
                } else if (count == 63U) {
                    if (left.as.i64 != 0) {
                        runtime_error(vm, instruction,
                                      "integer shift overflow");
                        goto fail;
                    }
                    result = 0;
                } else {
                    int64_t factor =
                        INT64_C(1) << (unsigned)count;
                    if (!vm_checked_mul(left.as.i64, factor, &result) ||
                        !vm_signed_value_fits_type(
                            result, (TypeKind)instruction.a)) {
                        runtime_error(vm, instruction,
                                      "integer shift overflow");
                        goto fail;
                    }
                }
                PUSH(((LangValue){
                    .tag=LANG_VALUE_I64, .as.i64=result
                }));
                break;
            }
            VM_LABEL(bitwise)
            case OP_BIT_AND: case OP_BIT_OR: case OP_BIT_XOR: {
                LangValue right = POP();
                LangValue left = POP();
                TypeKind kind = (TypeKind)instruction.a;
                unsigned width = vm_runtime_integer_width(kind);
                bool unsigned_kind =
                    kind == TYPE_U8 || kind == TYPE_U16 ||
                    kind == TYPE_U32 || kind == TYPE_U64 ||
                    kind == TYPE_USIZE;
                if (width == 0U ||
                    (unsigned_kind &&
                     (left.tag != LANG_VALUE_U64 ||
                      right.tag != LANG_VALUE_U64)) ||
                    (!unsigned_kind &&
                     (left.tag != LANG_VALUE_I64 ||
                      right.tag != LANG_VALUE_I64))) {
                    runtime_error(
                        vm, instruction, "invalid bitwise operands");
                    goto fail;
                }
                uint64_t left_bits = unsigned_kind
                    ? left.as.u64 : (uint64_t)left.as.i64;
                uint64_t right_bits = unsigned_kind
                    ? right.as.u64 : (uint64_t)right.as.i64;
                uint64_t bits =
                    instruction.op == OP_BIT_AND
                        ? left_bits & right_bits :
                    instruction.op == OP_BIT_OR
                        ? left_bits | right_bits :
                        left_bits ^ right_bits;
                STORE_SCALAR_RESULT(
                    vm_runtime_value_from_integer_bits(bits, kind));
                break;
            }
            VM_LABEL(bitwise_not)
            case OP_BIT_NOT: {
                LangValue operand = POP();
                TypeKind kind = (TypeKind)instruction.a;
                unsigned width = vm_runtime_integer_width(kind);
                bool unsigned_kind =
                    kind == TYPE_U8 || kind == TYPE_U16 ||
                    kind == TYPE_U32 || kind == TYPE_U64 ||
                    kind == TYPE_USIZE;
                if (width == 0U ||
                    (unsigned_kind &&
                     operand.tag != LANG_VALUE_U64) ||
                    (!unsigned_kind &&
                     operand.tag != LANG_VALUE_I64)) {
                    runtime_error(
                        vm, instruction, "invalid bitwise operand");
                    goto fail;
                }
                uint64_t bits = unsigned_kind
                    ? operand.as.u64 : (uint64_t)operand.as.i64;
                STORE_SCALAR_RESULT(
                    vm_runtime_value_from_integer_bits(~bits, kind));
                break;
            }
            VM_LABEL(float_binary)
            case OP_ADD_F64: case OP_SUB_F64:
            case OP_MUL_F64: case OP_DIV_F64: {
                LangValue right = POP(), left = POP();
                if (left.tag != LANG_VALUE_F64 ||
                    right.tag != LANG_VALUE_F64) {
                    runtime_error(vm, instruction,
                                  "invalid floating-point operands");
                    goto fail;
                }
                if (instruction.op == OP_DIV_F64 && right.as.f64 == 0.0) {
                    runtime_error(vm, instruction, "division by zero");
                    goto fail;
                }
                double result;
                if (scalar_operation == OP_ADD_F64)
                    result = left.as.f64 + right.as.f64;
                else if (scalar_operation == OP_SUB_F64)
                    result = left.as.f64 - right.as.f64;
                else if (scalar_operation == OP_MUL_F64)
                    result = left.as.f64 * right.as.f64;
                else
                    result = left.as.f64 / right.as.f64;
                double maximum =
                    (TypeKind)scalar_type == TYPE_F32
                    ? (double)FLT_MAX : DBL_MAX;
                if (result > maximum || result < -maximum ||
                    result != result) {
                    runtime_error(vm, instruction,
                                  "floating-point overflow or invalid result");
                    goto fail;
                }
                if ((TypeKind)scalar_type == TYPE_F32)
                    result = (double)(float)result;
                PUSH(((LangValue){
                    .tag=LANG_VALUE_F64, .as.f64=result
                }));
                break;
            }
            VM_LABEL(neg_i64)
            case OP_NEG_I64: {
                LangValue value = POP();
                if (value.tag != LANG_VALUE_I64 || value.as.i64 == INT64_MIN) {
                    runtime_error(vm, instruction, "integer negation overflow"); goto fail;
                }
                value.as.i64 = -value.as.i64;
                if (!vm_signed_value_fits_type(
                        value.as.i64, (TypeKind)instruction.a)) {
                    runtime_error(vm, instruction,
                                  "integer negation overflow");
                    goto fail;
                }
                PUSH(value); break;
            }
            VM_LABEL(neg_f64)
            case OP_NEG_F64: {
                LangValue value = POP();
                if (value.tag != LANG_VALUE_F64) {
                    runtime_error(vm, instruction,
                                  "invalid floating-point operand");
                    goto fail;
                }
                value.as.f64 = -value.as.f64;
                PUSH(value);
                break;
            }
            VM_LABEL(not)
            case OP_NOT: {
                LangValue value = POP();
                PUSH(((LangValue){.tag=LANG_VALUE_BOOL,.as.boolean=!value.as.boolean})); break;
            }
            VM_LABEL(cast)
            case OP_CAST: {
                LangValue input = POP();
                LangValue output;
                if (!vm_cast_numeric_value(
                        input, (TypeKind)instruction.b, &output)) {
                    runtime_error(vm, instruction,
                                  "numeric cast is out of range");
                    goto fail;
                }
                PUSH(output);
                break;
            }
            VM_LABEL(compare)
            case OP_EQ: case OP_NEQ: case OP_LT_I64: case OP_LE_I64:
            case OP_GT_I64: case OP_GE_I64: {
                LangValue right = POP(), left = POP();
                bool result = false;
                if (left.tag == LANG_VALUE_I64 && right.tag == LANG_VALUE_I64) {
                    if (scalar_operation == OP_EQ) result = left.as.i64 == right.as.i64;
                    else if (scalar_operation == OP_NEQ) result = left.as.i64 != right.as.i64;
                    else if (scalar_operation == OP_LT_I64) result = left.as.i64 < right.as.i64;
                    else if (scalar_operation == OP_LE_I64) result = left.as.i64 <= right.as.i64;
                    else if (scalar_operation == OP_GT_I64) result = left.as.i64 > right.as.i64;
                    else result = left.as.i64 >= right.as.i64;
                } else if (left.tag == LANG_VALUE_U64 &&
                           right.tag == LANG_VALUE_U64) {
                    if (scalar_operation == OP_EQ)
                        result = left.as.u64 == right.as.u64;
                    else if (scalar_operation == OP_NEQ)
                        result = left.as.u64 != right.as.u64;
                    else if (scalar_operation == OP_LT_I64)
                        result = left.as.u64 < right.as.u64;
                    else if (scalar_operation == OP_LE_I64)
                        result = left.as.u64 <= right.as.u64;
                    else if (scalar_operation == OP_GT_I64)
                        result = left.as.u64 > right.as.u64;
                    else
                        result = left.as.u64 >= right.as.u64;
                } else if (left.tag == LANG_VALUE_F64 &&
                           right.tag == LANG_VALUE_F64) {
                    if (scalar_operation == OP_EQ)
                        result = left.as.f64 == right.as.f64;
                    else if (scalar_operation == OP_NEQ)
                        result = left.as.f64 != right.as.f64;
                    else if (scalar_operation == OP_LT_I64)
                        result = left.as.f64 < right.as.f64;
                    else if (scalar_operation == OP_LE_I64)
                        result = left.as.f64 <= right.as.f64;
                    else if (scalar_operation == OP_GT_I64)
                        result = left.as.f64 > right.as.f64;
                    else
                        result = left.as.f64 >= right.as.f64;
                } else if (left.tag == LANG_VALUE_BOOL && right.tag == LANG_VALUE_BOOL) {
                    result = scalar_operation == OP_EQ ? left.as.boolean == right.as.boolean
                                                    : left.as.boolean != right.as.boolean;
                } else if (left.tag == LANG_VALUE_RAW_POINTER &&
                           right.tag == LANG_VALUE_RAW_POINTER &&
                           (scalar_operation == OP_EQ ||
                            scalar_operation == OP_NEQ)) {
                    bool equal = left.as.pointer == right.as.pointer;
                    result = scalar_operation == OP_EQ ? equal : !equal;
                } else if ((scalar_operation == OP_EQ ||
                            scalar_operation == OP_NEQ) &&
                           (vm_is_payloadless_variant(left) ||
                            vm_is_payloadless_variant(right))) {
                    bool equal = vm_payloadless_variants_equal(left, right);
                    result = scalar_operation == OP_EQ ? equal : !equal;
                } else if (scalar_operation == OP_EQ ||
                           scalar_operation == OP_NEQ) {
                    LangStringView left_string;
                    LangStringView right_string;
                    bool strings =
                        lang_value_string_view(&left, &left_string) &&
                        lang_value_string_view(&right, &right_string);
                    bool equal = strings &&
                        left_string.length == right_string.length &&
                        (left_string.length == 0U ||
                         memcmp(left_string.data, right_string.data,
                                left_string.length) == 0);
                    result = scalar_operation == OP_EQ ? equal : !equal;
                }
                vm_value_drop_owned(vm, left);
                vm_value_drop_owned(vm, right);
                STORE_SCALAR_RESULT(((LangValue){
                    .tag=LANG_VALUE_BOOL,.as.boolean=result
                }));
                break;
            }
            VM_LABEL(jump)
            case OP_JUMP: ip = (size_t)instruction.a; break;
            VM_LABEL(jump_if_false)
            case OP_JUMP_IF_FALSE: {
                LangValue condition = POP();
                if (!condition.as.boolean) ip = (size_t)instruction.a;
                break;
            }
            VM_LABEL(compare_branch)
            case OP_COMPARE_BRANCH: {
                uint32_t packed = (uint32_t)instruction.b;
                size_t left_slot =
                    (size_t)(packed & UINT32_C(0x3ff));
                size_t right_slot = (size_t)(
                    (packed >> 10U) & UINT32_C(0x3ff));
                OpCode operation = (OpCode)(
                    (packed >> 20U) & UINT32_C(0x3f));
                if (!initialized[left_slot] ||
                    !initialized[right_slot]) {
                    runtime_error(
                        vm, instruction,
                        "comparison used an unavailable local");
                    goto fail;
                }
                LangValue left = LOCAL(left_slot);
                LangValue right = LOCAL(right_slot);
                initialized[left_slot] = false;
                initialized[right_slot] = false;
                bool result;
                if (left.tag == LANG_VALUE_I64 &&
                    right.tag == LANG_VALUE_I64) {
                    if (operation == OP_EQ)
                        result = left.as.i64 == right.as.i64;
                    else if (operation == OP_NEQ)
                        result = left.as.i64 != right.as.i64;
                    else if (operation == OP_LT_I64)
                        result = left.as.i64 < right.as.i64;
                    else if (operation == OP_LE_I64)
                        result = left.as.i64 <= right.as.i64;
                    else if (operation == OP_GT_I64)
                        result = left.as.i64 > right.as.i64;
                    else
                        result = left.as.i64 >= right.as.i64;
                } else if (left.tag == LANG_VALUE_U64 &&
                           right.tag == LANG_VALUE_U64) {
                    if (operation == OP_EQ)
                        result = left.as.u64 == right.as.u64;
                    else if (operation == OP_NEQ)
                        result = left.as.u64 != right.as.u64;
                    else if (operation == OP_LT_I64)
                        result = left.as.u64 < right.as.u64;
                    else if (operation == OP_LE_I64)
                        result = left.as.u64 <= right.as.u64;
                    else if (operation == OP_GT_I64)
                        result = left.as.u64 > right.as.u64;
                    else
                        result = left.as.u64 >= right.as.u64;
                } else {
                    runtime_error(
                        vm, instruction,
                        "invalid integer comparison operands");
                    goto fail;
                }
                if (!result)
                    ip = (size_t)instruction.a;
                break;
            }
            VM_LABEL(compare_local_constant_branch)
            case OP_COMPARE_LOCAL_CONSTANT_BRANCH: {
                uint32_t packed = (uint32_t)instruction.b;
                size_t local_slot =
                    (size_t)(packed & UINT32_C(0x3ff));
                size_t constant_index = (size_t)(
                    (packed >> 10U) & UINT32_C(0xffff));
                OpCode operation = (OpCode)(packed >> 26U);
                if (!initialized[local_slot]) {
                    runtime_error(
                        vm, instruction,
                        "comparison used an unavailable local");
                    goto fail;
                }
                LangValue left = LOCAL(local_slot);
                LangValue right =
                    vm->module->constants[constant_index].value;
                bool result;
                if (left.tag == LANG_VALUE_I64 &&
                    right.tag == LANG_VALUE_I64) {
                    if (operation == OP_EQ)
                        result = left.as.i64 == right.as.i64;
                    else if (operation == OP_NEQ)
                        result = left.as.i64 != right.as.i64;
                    else if (operation == OP_LT_I64)
                        result = left.as.i64 < right.as.i64;
                    else if (operation == OP_LE_I64)
                        result = left.as.i64 <= right.as.i64;
                    else if (operation == OP_GT_I64)
                        result = left.as.i64 > right.as.i64;
                    else
                        result = left.as.i64 >= right.as.i64;
                } else if (left.tag == LANG_VALUE_U64 &&
                           right.tag == LANG_VALUE_U64) {
                    if (operation == OP_EQ)
                        result = left.as.u64 == right.as.u64;
                    else if (operation == OP_NEQ)
                        result = left.as.u64 != right.as.u64;
                    else if (operation == OP_LT_I64)
                        result = left.as.u64 < right.as.u64;
                    else if (operation == OP_LE_I64)
                        result = left.as.u64 <= right.as.u64;
                    else if (operation == OP_GT_I64)
                        result = left.as.u64 > right.as.u64;
                    else
                        result = left.as.u64 >= right.as.u64;
                } else {
                    runtime_error(
                        vm, instruction,
                        "invalid integer comparison operands");
                    goto fail;
                }
                if (!result)
                    ip = (size_t)instruction.a;
                break;
            }
