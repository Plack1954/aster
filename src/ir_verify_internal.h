#ifndef ASTER_IR_VERIFY_INTERNAL_H
#define ASTER_IR_VERIFY_INTERNAL_H

#include "ir_internal.h"

bool ir_verify_opcode_valid(IrOpcode opcode);
bool ir_verify_type(const IrModule *ir, IrTypeId type);
bool ir_verify_parameter_mode(ParameterMode mode);
bool ir_verify_copy_policy(IrCopyPolicy policy);
bool ir_verify_drop_policy(IrDropPolicy policy);
bool ir_verify_type_shape(IrTypeShape shape);
bool ir_verify_value(const IrFunction *function, IrValueId value);
bool ir_verify_type_assignable(
    const IrModule *ir, IrTypeId expected, IrTypeId actual);
bool ir_verify_operand_count(const IrInstruction *instruction);
bool ir_verify_result_type(
    const IrModule *ir, const IrFunction *function,
    const IrInstruction *instruction);
bool ir_verify_value_is_type(
    const IrFunction *function, IrValueId value, IrTypeId expected);
bool ir_verify_value_type(
    const IrModule *ir, const IrFunction *function,
    IrValueId value, IrTypeId *type);
bool ir_verify_instruction_signature(
    const IrModule *ir, const IrFunction *function,
    const IrInstruction *instruction);

#endif
