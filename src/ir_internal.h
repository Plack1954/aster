#ifndef ASTER_IR_INTERNAL_H
#define ASTER_IR_INTERNAL_H

#include "internal.h"

typedef struct IrLoop {
    IrBlockId break_target;
    IrBlockId continue_target;
    size_t finalizer_count;
} IrLoop;

typedef struct IrElementContext {
    uint32_t local;
    size_t loop_depth;
} IrElementContext;

typedef struct IrExceptionContext {
    IrBlockId handler;
} IrExceptionContext;

typedef struct IrFinalizerContext {
    const Stmt *body;
} IrFinalizerContext;

typedef struct IrBuilder {
    const Module *source;
    LangDiagnostics *diagnostics;
    IrModule *module;
    IrFunction *function;
    IrBlockId current;
    IrLoop loops[32];
    size_t loop_count;
    IrElementContext elements[32];
    size_t element_count;
    IrExceptionContext exceptions[32];
    size_t exception_count;
    IrFinalizerContext finalizers[32];
    size_t finalizer_count;
    bool failed;
} IrBuilder;

extern const Type ir_bool_type;
extern const Type ir_str_type;
extern const Type ir_string_type;

void *ir_resize(void *pointer, size_t count, size_t size);
const char *ir_opcode_name(IrOpcode opcode);
bool ir_style_name(const char *name);
IrTypeId ir_intern_type(IrModule *module, const Type *type);
IrTypeId ir_intern_element_builder_type(IrModule *module,
                                        IrTypeId result_type);
IrTypeId ir_intern_iterator_type(IrModule *module,
                                 IrTypeId source_type,
                                 IrTypeId element_type);
IrBlockId ir_add_block(IrFunction *function);
void ir_set_terminator(IrBuilder *builder, IrTerminatorKind kind,
                       IrValueId condition, IrBlockId first,
                       IrBlockId second, LangSpan span);
uint32_t ir_add_local(IrBuilder *builder, const char *name,
                      size_t binding_id, const Type *type,
                      bool mutable_);
IrInstruction *ir_append_instruction(
    IrBuilder *builder, IrOpcode opcode, IrTypeId result_type,
    const IrValueId *operands, size_t operand_count, LangSpan span);
bool ir_current_terminated(const IrBuilder *builder);
uint32_t ir_add_synthetic_local(IrBuilder *builder,
                                const char *name, IrTypeId type);
uint32_t ir_find_local(IrBuilder *builder, size_t binding_id,
                       LangSpan span);
uint32_t ir_find_function(const IrModule *ir, const Decl *decl);
uint32_t ir_field_index(const Type *object_type, const char *name);
IrValueId ir_emit_unit(IrBuilder *builder, LangSpan span,
                       const Type *type);
void ir_emit_cleanup(IrBuilder *builder, const CleanupPlan *plan,
                     LangSpan span);
bool ir_type_needs_cleanup(const IrModule *module, IrTypeId id);
void ir_emit_element_exit_cleanup(IrBuilder *builder,
                                  size_t target_depth,
                                  LangSpan span);
void ir_emit_function_cleanup(IrBuilder *builder, LangSpan span);
void ir_emit_function_cleanup_except(
    IrBuilder *builder, LangSpan span, uint32_t excluded_local);
IrInstruction *ir_emit_local_enum_operation(
    IrBuilder *builder, IrOpcode opcode, IrTypeId result_type,
    uint32_t local, const Type *type, const char *variant,
    LangSpan span);
IrValueId ir_lower_expr(IrBuilder *builder, const Expr *expr);
void ir_lower_stmt(IrBuilder *builder, const Stmt *stmt);
IrValueId ir_emit_synthetic_native_call(
    IrBuilder *builder, const char *name, const Type *result_type,
    const IrValueId *operands, size_t operand_count,
    bool borrow_first, LangSpan span);
bool ir_type_produces_element_child(const Type *type);
IrValueId ir_lower_element(IrBuilder *builder, const Expr *expr);
void ir_append_element_child(IrBuilder *builder, IrValueId child,
                             const Type *type, LangSpan span);
IrValueId ir_lower_element_with_parent(
    IrBuilder *builder, const Expr *expr, uint32_t parent_local);

#endif
