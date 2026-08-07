#ifndef ASTER_IR_BYTECODE_INTERNAL_H
#define ASTER_IR_BYTECODE_INTERNAL_H

#include "internal.h"

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

void *ir_bc_resize(void *pointer, size_t count, size_t size);
size_t emit_instruction(IrBytecodeBuilder *builder, OpCode opcode,
                        int32_t a, int32_t b, LangSpan span);
size_t add_constant(IrBytecodeBuilder *builder, LangValue value,
                    const char *data, size_t length);
bool as_i32(IrBytecodeBuilder *builder, size_t value,
            LangSpan span, int32_t *result);
size_t value_slot(const IrBytecodeBuilder *builder, IrValueId value);
void move_value(IrBytecodeBuilder *builder, IrValueId value,
                LangSpan span);
void store_result(IrBytecodeBuilder *builder,
                  const IrInstruction *instruction);
void add_patch(IrBytecodeBuilder *builder, size_t instruction,
               IrBlockId target);
int32_t builtin_index(const char *name);
bool runtime_type_supported(const IrType *type);
bool runtime_type_may_be_object(const IrType *type);
int32_t add_symbol_constant(IrBytecodeBuilder *builder,
                            const char *data, size_t length,
                            LangSpan span);
int32_t add_struct_metadata(IrBytecodeBuilder *builder,
                            const IrInstruction *instruction);
int32_t add_enum_metadata(IrBytecodeBuilder *builder,
                          const IrType *type, uint32_t variant,
                          bool include_module, LangSpan span);
TypeKind vm_type_kind(const IrType *type);
OpCode arithmetic_opcode(IrOpcode opcode);
void unsupported_instruction(
    IrBytecodeBuilder *builder, const IrInstruction *instruction);
void lower_instruction(IrBytecodeBuilder *builder,
                       const IrInstruction *instruction);

#endif
