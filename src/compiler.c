#include "internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CompileLocal {
    const char *name;
    size_t slot;
    unsigned depth;
    bool owning;
    int32_t destructor;
    size_t binding_id;
} CompileLocal;

typedef struct LoopContext {
    size_t break_jumps[128];
    size_t break_count;
    size_t continue_target;
    size_t local_base;
} LoopContext;

typedef struct Compiler {
    const Module *module;
    LangDiagnostics *diagnostics;
    BytecodeModule *output;
    BytecodeFunction *function;
    const Function *source_function;
    CompileLocal locals[256];
    size_t local_count;
    size_t next_slot;
    unsigned depth;
    int html_depth;
    LoopContext loops[32];
    size_t loop_count;
    const char *current_module;
} Compiler;

#define LANG_MAX_FUNCTION_LOCALS 1024U

static bool compiler_style_name(const char *name) {
    const char *part = name;
    for (const char *cursor = strstr(name, "::"); cursor != NULL;
         cursor = strstr(cursor + 2U, "::"))
        part = cursor + 2U;
    return strcmp(part, "style") == 0;
}

static void *resize(void *pointer, size_t count, size_t size) {
    if (count > SIZE_MAX / size) return NULL;
    void *result = realloc(pointer, count * size);
    if (result == NULL && count != 0U) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    return result;
}

static size_t add_constant(Compiler *compiler, LangValue value, const char *string) {
    BytecodeModule *module = compiler->output;
    if (module->constant_count == module->constant_capacity) {
        size_t next = module->constant_capacity == 0U ? 16U : module->constant_capacity * 2U;
        module->constants = resize(module->constants, next, sizeof(*module->constants));
        memset(module->constants + module->constant_capacity, 0,
               (next - module->constant_capacity) * sizeof(*module->constants));
        module->constant_capacity = next;
    }
    Constant *constant = &module->constants[module->constant_count];
    constant->value = value;
    if (string != NULL) {
        size_t length = strlen(string);
        constant->owned_string = resize(NULL, length + 1U, 1U);
        memcpy(constant->owned_string, string, length + 1U);
        constant->value.tag = LANG_VALUE_STRING_VIEW;
        constant->value.as.string.data = constant->owned_string;
        constant->value.as.string.length = length;
    }
    return module->constant_count++;
}

static size_t add_string_constant(Compiler *compiler, const char *data,
                                  size_t length) {
    LangValue unit = {.tag=LANG_VALUE_UNIT};
    size_t index = add_constant(compiler, unit, NULL);
    Constant *constant = &compiler->output->constants[index];
    constant->owned_string = resize(NULL, length + 1U, 1U);
    if (length != 0U)
        memcpy(constant->owned_string, data, length);
    constant->owned_string[length] = '\0';
    constant->value.tag = LANG_VALUE_STRING_VIEW;
    constant->value.as.string.data = constant->owned_string;
    constant->value.as.string.length = length;
    return index;
}

static size_t emit(Compiler *compiler, OpCode op, int32_t a, int32_t b, LangSpan span) {
    BytecodeFunction *function = compiler->function;
    if (function->code_count == function->code_capacity) {
        size_t next = function->code_capacity == 0U ? 32U : function->code_capacity * 2U;
        function->code = resize(function->code, next, sizeof(*function->code));
        function->spans = resize(function->spans, next, sizeof(*function->spans));
        function->code_capacity = next;
    }
    function->code[function->code_count] = (Instruction){op, a, b};
    function->spans[function->code_count] = span;
    return function->code_count++;
}

static void patch(Compiler *compiler, size_t instruction, size_t target) {
    if (target > (size_t)INT32_MAX) {
        lang_diag(compiler->diagnostics, compiler->function->spans[instruction],
                  "function bytecode is too large");
        return;
    }
    compiler->function->code[instruction].a = (int32_t)target;
}

static CompileLocal *find_local(Compiler *compiler, size_t binding_id) {
    if (binding_id == 0U) return NULL;
    for (size_t i = compiler->local_count; i > 0U; --i)
        if (compiler->locals[i - 1U].binding_id == binding_id)
            return &compiler->locals[i - 1U];
    return NULL;
}

static const char *last_path_separator(const char *path) {
    const char *last = NULL;
    for (const char *cursor = strstr(path, "::");
         cursor != NULL; cursor = strstr(cursor + 2U, "::"))
        last = cursor;
    return last;
}

static int32_t find_function_index(Compiler *compiler, const char *name) {
    if (strcmp(name, "print") == 0) return -1;
    if (strcmp(name, "eprint") == 0) return -2;
    if (strcmp(name, "HtmlRender") == 0) return -3;
    if (strcmp(name, "Buffer::allocate") == 0) return -4;
    if (strcmp(name, "Arena::new") == 0) return -5;
    if (strcmp(name, "ArenaAlloc") == 0) return -6;
    if (strcmp(name, "ArenaReset") == 0) return -7;
    if (strcmp(name, "raw_load_i64") == 0) return -8;
    if (strcmp(name, "raw_store_i64") == 0) return -9;
    if (strcmp(name, "panic") == 0 || strcmp(name, "trap") == 0) return -10;
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
    for (size_t i = 0U;
         i < compiler->output->function_count; ++i)
        if (strcmp(compiler->output->functions[i].name, name) == 0 &&
            compiler->current_module != NULL &&
            compiler->output->functions[i].module_name != NULL &&
            strcmp(compiler->output->functions[i].module_name,
                   compiler->current_module) == 0)
            return (int32_t)i;
    for (size_t i = 0U; i < compiler->output->function_count; ++i)
        if (strcmp(compiler->output->functions[i].name, name) == 0 &&
            compiler->output->functions[i].is_public)
            return (int32_t)i;
    return INT32_MIN;
}

static int32_t find_function_index_in_module(const Compiler *compiler,
                                             const char *name,
                                             const char *module_name) {
    for (size_t i = 0U; i < compiler->output->function_count; ++i)
        if (strcmp(compiler->output->functions[i].name, name) == 0 &&
            module_name != NULL &&
            compiler->output->functions[i].module_name != NULL &&
            strcmp(compiler->output->functions[i].module_name,
                   module_name) == 0)
            return (int32_t)i;
    return INT32_MIN;
}

static int32_t find_function_declaration_index(
    const Compiler *compiler, const Decl *declaration) {
    if (declaration == NULL) return INT32_MIN;
    for (size_t i = 0U; i < compiler->output->function_count; ++i)
        if (compiler->output->functions[i].declaration == declaration)
            return (int32_t)i;
    return INT32_MIN;
}

static int32_t destructor_index_for_type(Compiler *compiler,
                                         const Type *type) {
    if (type == NULL || type->kind != TYPE_NAMED) return -1;
    for (size_t i = 0U; i < compiler->module->count; ++i) {
        const Decl *decl = compiler->module->decls[i];
        if (decl->kind == DECL_FUNCTION && decl->as.function.is_drop &&
            decl->as.function.param_count == 1U &&
            decl->as.function.params[0].checked_type != NULL &&
            decl->as.function.params[0].checked_type->kind == TYPE_NAMED &&
            ((type->declaration != NULL &&
              decl->as.function.params[0].checked_type->declaration ==
                  type->declaration) ||
             (type->declaration == NULL &&
              strcmp(decl->as.function.params[0].checked_type->name,
                     type->name) == 0)))
            return find_function_index_in_module(
                compiler, decl->as.function.name, decl->module_name);
    }
    return -1;
}

static size_t allocate_local_slot(Compiler *compiler, LangSpan span,
                                  int32_t destructor) {
    if (compiler->next_slot >= compiler->function->local_count) {
        lang_diag(compiler->diagnostics, span,
                  "function requires more than %u local slots",
                  (unsigned)compiler->function->local_count);
        return 0U;
    }
    size_t slot = compiler->next_slot++;
    compiler->function->local_destructors[slot] = destructor;
    return slot;
}

static const Function *find_declared_function(const Compiler *compiler,
                                              const char *name) {
    const Function *imported = NULL;
    for (size_t i = 0U; i < compiler->module->count; ++i) {
        const Decl *decl = compiler->module->decls[i];
        if (decl->kind != DECL_FUNCTION ||
            strcmp(decl->as.function.name, name) != 0)
            continue;
        if (compiler->current_module != NULL &&
            decl->module_name != NULL &&
            strcmp(compiler->current_module, decl->module_name) == 0)
            return &decl->as.function;
        if (decl->is_public && imported == NULL)
            imported = &decl->as.function;
    }
    return imported;
}

static void compile_expr(Compiler *compiler, const Expr *expr);
static void compile_stmt(Compiler *compiler, const Stmt *stmt);
static void compile_if_expression(Compiler *compiler, const Expr *expr);
static void compile_match_expression(Compiler *compiler, const Expr *expr);

static bool builtin_borrows_first_place(const char *name) {
    return name != NULL &&
           (strcmp(name, "ArenaAlloc") == 0 ||
            strcmp(name, "ArenaReset") == 0 ||
            strcmp(name, "StringBuilder::Append") == 0 ||
            strcmp(name, "StringBuilder::AppendByte") == 0 ||
            strcmp(name, "StringBuilder::ToString") == 0 ||
            strcmp(name, "StringBuilder::Length") == 0 ||
            strcmp(name, "StringBuilder::Clear") == 0 ||
            strcmp(name, "List::Add") == 0 ||
            strcmp(name, "List::Count") == 0 ||
            strcmp(name, "List::Get") == 0 ||
            strcmp(name, "List::Capacity") == 0 ||
            strcmp(name, "List::Clear") == 0 ||
            strcmp(name, "List::Insert") == 0 ||
            strcmp(name, "List::RemoveAt") == 0 ||
            strcmp(name, "List::Set") == 0 ||
            strcmp(name, "List::Contains") == 0 ||
            strcmp(name, "List::IndexOf") == 0 ||
            strcmp(name, "List::LastIndexOf") == 0 ||
            strcmp(name, "List::Remove") == 0 ||
            strcmp(name, "List::AddRange") == 0 ||
            strcmp(name, "List::InsertRange") == 0 ||
            strcmp(name, "List::RemoveRange") == 0 ||
            strcmp(name, "List::GetRange") == 0 ||
            strcmp(name, "List::Reverse") == 0 ||
            strcmp(name, "List::EnsureCapacity") == 0 ||
            strcmp(name, "List::TrimExcess") == 0 ||
            strcmp(name, "List::SetCapacity") == 0 ||
            strcmp(name, "List::Exists") == 0 ||
            strcmp(name, "List::FindAll") == 0 ||
            strcmp(name, "List::FindIndex") == 0 ||
            strcmp(name, "List::FindLastIndex") == 0 ||
            strcmp(name, "List::RemoveAll") == 0 ||
            strcmp(name, "List::ForEach") == 0 ||
            strcmp(name, "List::TrueForAll") == 0 ||
            strcmp(name, "Dictionary::Add") == 0 ||
            strcmp(name, "Dictionary::Count") == 0 ||
            strcmp(name, "Dictionary::ContainsKey") == 0 ||
            strcmp(name, "Dictionary::Remove") == 0 ||
            strcmp(name, "Dictionary::Clear") == 0 ||
            strcmp(name, "Dictionary::Get") == 0 ||
            strcmp(name, "Dictionary::Set") == 0 ||
            strcmp(name, "Dictionary::TryAdd") == 0 ||
            strcmp(name, "Dictionary::ContainsValue") == 0 ||
            strcmp(name, "Dictionary::EnsureCapacity") == 0 ||
            strcmp(name, "Dictionary::TrimExcess") == 0 ||
            strcmp(name, "Dictionary::Capacity") == 0 ||
            strcmp(name, "BufferAsMutSlice") == 0);
}

static bool builtin_borrows_named_first(const char *name) {
    return name != NULL &&
           (strcmp(name, "print") == 0 ||
            strcmp(name, "eprint") == 0 ||
            strcmp(name, "TextLen") == 0);
}

static void compile_borrowed_place(Compiler *compiler, const Expr *expr) {
    if (expr->kind == EXPR_NAME) {
        CompileLocal *local =
            find_local(compiler, expr->resolved_local_id);
        emit(compiler, OP_LOAD_LOCAL,
             local != NULL ? (int32_t)local->slot : -1,
             0, expr->span);
        return;
    }
    if (expr->kind == EXPR_FIELD) {
        compile_borrowed_place(compiler, expr->as.field.object);
        LangValue unit = {.tag = LANG_VALUE_UNIT};
        size_t field =
            add_constant(compiler, unit, expr->as.field.field);
        emit(compiler, OP_GET_FIELD_BORROW,
             (int32_t)field, 0, expr->span);
        return;
    }
    compile_expr(compiler, expr);
}

static void compile_call(Compiler *compiler, const Expr *expr) {
    const char *name = expr->as.call.callee->as.name;
    const Decl *resolved = expr->resolved_decl;
    const Function *declared =
        resolved != NULL && resolved->kind == DECL_FUNCTION
        ? &resolved->as.function
        : find_declared_function(compiler, name);
    size_t local_base = compiler->local_count;
    size_t *borrowed_temporary_slots = resize(
        NULL, expr->as.call.arguments.count,
        sizeof(*borrowed_temporary_slots));
    size_t borrowed_temporary_count = 0U;
    for (size_t i = 0U; i < expr->as.call.arguments.count; ++i) {
        const Expr *argument = expr->as.call.arguments.items[i];
        const Type *function_type =
            expr->as.call.callee->resolved_local_id != 0U
            ? expr->as.call.callee->type : NULL;
        uint32_t bit = i < 32U
            ? UINT32_C(1) << (unsigned)i : 0U;
        bool indirect_borrow = function_type != NULL &&
            (function_type->borrowed_argument_mask & bit) != 0U;
        bool builtin_borrow =
            i == 0U &&
            (builtin_borrows_first_place(name) ||
             (builtin_borrows_named_first(name) &&
              argument->kind == EXPR_NAME));
        bool borrowed = indirect_borrow ||
            (declared != NULL && i < declared->param_count &&
             declared->params[i].borrowed) ||
            builtin_borrow;
        if (borrowed &&
            (argument->kind == EXPR_NAME ||
             argument->kind == EXPR_FIELD)) {
            compile_borrowed_place(compiler, argument);
        } else if (borrowed) {
            compile_expr(compiler, argument);
            int32_t destructor =
                destructor_index_for_type(compiler, argument->type);
            size_t slot = allocate_local_slot(
                compiler, argument->span, destructor);
            emit(compiler, OP_STORE_LOCAL, (int32_t)slot, 0,
                 argument->span);
            compiler->locals[compiler->local_count++] = (CompileLocal){
                "<borrowed-temporary>", slot, compiler->depth,
                true, destructor, 0U
            };
            borrowed_temporary_slots[borrowed_temporary_count++] = slot;
            emit(compiler, OP_LOAD_LOCAL, (int32_t)slot, 0,
                 argument->span);
        } else {
            compile_expr(compiler, argument);
        }
    }
    if (expr->as.call.callee->resolved_local_id != 0U) {
        compile_expr(compiler, expr->as.call.callee);
        emit(compiler, OP_CALL_INDIRECT,
             (int32_t)expr->as.call.arguments.count, 0,
             expr->span);
        for (size_t i = borrowed_temporary_count; i > 0U; --i)
            emit(compiler, OP_DROP_LOCAL,
                 (int32_t)borrowed_temporary_slots[i - 1U], -1,
                 expr->span);
        compiler->local_count = local_base;
        free(borrowed_temporary_slots);
        return;
    }
    if (declared != NULL && declared->is_extern) {
        LangValue unit = {.tag = LANG_VALUE_UNIT};
        size_t native_name = add_constant(
            compiler, unit, declared->name);
        uint32_t borrowed_mask = 0U;
        for (size_t i = 0U; i < declared->param_count; ++i) {
            if (!declared->params[i].borrowed) continue;
            if (i >= 24U) {
                lang_diag(compiler->diagnostics, expr->span,
                          "native borrowed/`ref` arguments are limited to 24 parameters");
                continue;
            }
            borrowed_mask |= UINT32_C(1) << (unsigned)i;
        }
        uint32_t encoded = (uint32_t)expr->as.call.arguments.count |
                           (borrowed_mask << 8U);
        emit(compiler, OP_CALL_NATIVE, (int32_t)native_name,
             (int32_t)encoded, expr->span);
        for (size_t i = borrowed_temporary_count; i > 0U; --i)
            emit(compiler, OP_DROP_LOCAL,
                 (int32_t)borrowed_temporary_slots[i - 1U], -1,
                 expr->span);
        compiler->local_count = local_base;
        free(borrowed_temporary_slots);
        return;
    }
    if (resolved == NULL && strcmp(name, "StringByteAt") == 0) {
        LangValue unit = {.tag = LANG_VALUE_UNIT};
        size_t native_name = add_constant(
            compiler, unit, name);
        emit(compiler, OP_CALL_NATIVE, (int32_t)native_name,
             (int32_t)expr->as.call.arguments.count, expr->span);
        for (size_t i = borrowed_temporary_count; i > 0U; --i)
            emit(compiler, OP_DROP_LOCAL,
                 (int32_t)borrowed_temporary_slots[i - 1U], -1,
                 expr->span);
        compiler->local_count = local_base;
        free(borrowed_temporary_slots);
        return;
    }
    int32_t function =
        resolved != NULL && resolved->kind == DECL_FUNCTION
        ? find_function_declaration_index(compiler, resolved)
        : find_function_index(compiler, name);
    if (function == INT32_MIN && strstr(name, "::") != NULL) {
        LangValue unit = {.tag = LANG_VALUE_UNIT};
        const char *module_name =
            expr->type != NULL &&
            expr->type->kind == TYPE_NAMED &&
            expr->type->declaration != NULL
            ? expr->type->declaration->module_name : NULL;
        const char *runtime_name = name;
        char *canonical = NULL;
        if (resolved != NULL && resolved->kind == DECL_ENUM) {
            const char *separator = last_path_separator(name);
            if (separator != NULL) {
                const char *variant = separator + 2U;
                size_t length =
                    strlen(resolved->as.enumeration.name) +
                    strlen(variant) + 3U;
                canonical = resize(NULL, length, 1U);
                (void)snprintf(
                    canonical, length, "%s::%s",
                    resolved->as.enumeration.name, variant);
                runtime_name = canonical;
            }
        }
        char *qualified = NULL;
        if (module_name != NULL) {
            size_t length =
                strlen(module_name) + strlen(runtime_name) + 2U;
            qualified = resize(NULL, length, 1U);
            (void)snprintf(qualified, length, "%s#%s",
                           module_name, runtime_name);
        }
        size_t name_constant = add_constant(
            compiler, unit,
            qualified != NULL ? qualified : runtime_name);
        free(qualified);
        free(canonical);
        emit(compiler, OP_MAKE_STRUCT, (int32_t)name_constant,
             (int32_t)expr->as.call.arguments.count, expr->span);
        for (size_t i = borrowed_temporary_count; i > 0U; --i)
            emit(compiler, OP_DROP_LOCAL,
                 (int32_t)borrowed_temporary_slots[i - 1U], -1,
                 expr->span);
        compiler->local_count = local_base;
        free(borrowed_temporary_slots);
        return;
    }
    uint32_t encoded_count =
        (uint32_t)expr->as.call.arguments.count;
    if (function < 0 &&
        expr->as.call.arguments.count >= 1U &&
        (builtin_borrows_first_place(name) ||
         (builtin_borrows_named_first(name) &&
          expr->as.call.arguments.items[0]->kind == EXPR_NAME)))
        encoded_count |= UINT32_C(1) << 8U;
    emit(compiler, OP_CALL, function, (int32_t)encoded_count,
         expr->span);
    for (size_t i = borrowed_temporary_count; i > 0U; --i)
        emit(compiler, OP_DROP_LOCAL,
             (int32_t)borrowed_temporary_slots[i - 1U], -1,
             expr->span);
    compiler->local_count = local_base;
    free(borrowed_temporary_slots);
}

static const ElementProperty *find_component_property(
    const Expr *expr, const char *name) {
    for (size_t i = 0U;
         i < expr->as.element.property_count; ++i) {
        const char *left =
            expr->as.element.properties[i].name;
        const char *right = name;
        while (*left != '\0' && *right != '\0' &&
               (*left == *right ||
                ((*left == '-' || *left == '_') &&
                 (*right == '-' || *right == '_')))) {
            ++left;
            ++right;
        }
        if (*left == '\0' && *right == '\0')
            return &expr->as.element.properties[i];
    }
    return NULL;
}

static void compile_component_property(
    Compiler *compiler, const ElementProperty *property,
    size_t *temporary_slots, size_t *temporary_count) {
    if (!property->borrow_interpolated_string) {
        compile_expr(compiler, property->value);
        return;
    }

    compile_expr(compiler, property->value);
    size_t slot = allocate_local_slot(
        compiler, property->span, -1);
    emit(compiler, OP_STORE_LOCAL, (int32_t)slot, 0,
         property->span);
    compiler->locals[compiler->local_count++] = (CompileLocal){
        "<component-interpolation>", slot, compiler->depth,
        true, -1, 0U
    };
    temporary_slots[(*temporary_count)++] = slot;
    emit(compiler, OP_LOAD_LOCAL, (int32_t)slot, 0,
         property->span);

    LangValue unit = {.tag=LANG_VALUE_UNIT};
    size_t native_name =
        add_constant(compiler, unit, "StringView");
    uint32_t encoded =
        UINT32_C(1) | (UINT32_C(1) << 8U);
    emit(compiler, OP_CALL_NATIVE, (int32_t)native_name,
         (int32_t)encoded, property->span);
}

static void compile_element(Compiler *compiler, const Expr *expr) {
    const Decl *resolved = expr->resolved_decl;
    const Function *component =
        resolved != NULL && resolved->kind == DECL_FUNCTION
        ? &resolved->as.function
        : NULL;
    if (component != NULL && !component->is_extern &&
        strcmp(component->return_type, "Html") == 0) {
        size_t *temporary_slots = resize(
            NULL, component->param_count,
            sizeof(*temporary_slots));
        size_t temporary_count = 0U;
        size_t local_base = compiler->local_count;
        for (size_t p = 0U; p < component->param_count; ++p) {
            if (strcmp(component->params[p].name, "children") == 0) {
                emit(compiler, OP_HTML_FRAGMENT, 0, 0, expr->span);
                ++compiler->html_depth;
                for (size_t i = 0U; i < expr->as.element.body_count; ++i) {
                    const ElementBodyItem *item = &expr->as.element.body[i];
                    if (item->is_statement) {
                        compile_stmt(compiler, item->as.statement);
                    } else {
                        compile_expr(compiler, item->as.expression);
                        emit(compiler, OP_HTML_APPEND, 0, 0,
                             item->as.expression->span);
                    }
                }
                --compiler->html_depth;
            } else {
                const ElementProperty *property =
                    find_component_property(
                        expr, component->params[p].name);
                const Type *parameter_type =
                    component->params[p].checked_type;
                if (property != NULL) {
                    if (component->params[p].borrowed)
                        compile_borrowed_place(
                            compiler, property->value);
                    else
                        compile_component_property(
                            compiler, property,
                            temporary_slots, &temporary_count);
                }
                if (parameter_type != NULL &&
                    parameter_type->kind == TYPE_OPTION &&
                    (property == NULL ||
                     property->value->type->kind != TYPE_OPTION)) {
                    LangValue unit = {
                        .tag=LANG_VALUE_UNIT
                    };
                    const char *variant =
                        property == NULL
                        ? "Option::None" : "Option::Some";
                    size_t metadata = add_constant(
                        compiler, unit, variant);
                    emit(
                        compiler, OP_MAKE_STRUCT,
                        (int32_t)metadata,
                        property == NULL ? 0 : 1,
                        property != NULL
                            ? property->span : expr->span);
                }
            }
        }
        int32_t function =
            find_function_declaration_index(compiler, resolved);
        emit(compiler, OP_CALL, function, (int32_t)component->param_count,
             expr->span);
        for (size_t i = temporary_count; i > 0U; --i)
            emit(compiler, OP_DROP_LOCAL,
                 (int32_t)temporary_slots[i - 1U],
                 -1, expr->span);
        compiler->local_count = local_base;
        free(temporary_slots);
        return;
    }
    LangValue unit = {.tag = LANG_VALUE_UNIT};
    const char *rendered_name =
        resolved != NULL && resolved->kind == DECL_ELEMENT
        ? resolved->as.element.name : expr->as.element.name;
    if (strcmp(rendered_name, "#fragment") == 0) {
        emit(
            compiler, OP_HTML_FRAGMENT, 0, 0,
            expr->as.element.open_span);
    } else {
        size_t name =
            add_constant(compiler, unit, rendered_name);
        emit(
            compiler, OP_HTML_BEGIN, (int32_t)name, 0,
            expr->as.element.open_span);
    }
    for (size_t i = 0U; i < expr->as.element.property_count; ++i) {
        const ElementProperty *property = &expr->as.element.properties[i];
        if (property->css_custom_property) continue;
        compile_expr(compiler, property->value);
        size_t property_name = add_constant(compiler, unit, property->name);
        emit(compiler, OP_HTML_ATTR, (int32_t)property_name, 0, property->span);
    }
    bool custom_property_started = false;
    size_t custom_slot = 0U;
    for (size_t i = 0U; i < expr->as.element.property_count; ++i) {
        const ElementProperty *property = &expr->as.element.properties[i];
        if (!property->css_custom_property) continue;
        bool first = !custom_property_started;
        if (first) {
            custom_slot = allocate_local_slot(compiler, property->span, -1);
            emit(compiler, OP_STORE_LOCAL, (int32_t)custom_slot, 0,
                 property->span);
            size_t style = add_constant(compiler, unit, "style");
            emit(compiler, OP_HTML_ATTR_BEGIN_LOCAL,
                 (int32_t)custom_slot, (int32_t)style, property->span);
            custom_property_started = true;
        }
        size_t name_length = strlen(property->name);
        size_t prefix_length = name_length + (first ? 2U : 4U);
        char *prefix = resize(NULL, prefix_length + 1U, 1U);
        (void)snprintf(prefix, prefix_length + 1U, "%s%s: ",
                       first ? "" : "; ", property->name);
        emit(
            compiler, OP_CONSTANT,
            (int32_t)add_string_constant(
                compiler, prefix, prefix_length),
            0, property->span);
        free(prefix);
        emit(compiler, OP_HTML_ATTR_APPEND_LOCAL,
             (int32_t)custom_slot, 0, property->span);
        compile_expr(compiler, property->value);
        emit(compiler, OP_HTML_CSS_VALUE_LOCAL,
             (int32_t)custom_slot, 0, property->span);
    }
    if (custom_property_started) {
        emit(compiler, OP_HTML_ATTR_END_LOCAL,
             (int32_t)custom_slot, 0, expr->as.element.open_span);
        emit(compiler, OP_MOVE_LOCAL,
             (int32_t)custom_slot, 0, expr->as.element.open_span);
    }
    if (expr->as.element.css_style_attribute != NULL) {
        emit(compiler, OP_TRUE, 0, 0, expr->as.element.open_span);
        size_t property_name = add_constant(
            compiler, unit, expr->as.element.css_style_attribute);
        emit(compiler, OP_HTML_ATTR, (int32_t)property_name, 0,
             expr->as.element.open_span);
    }
    if (compiler->source_function != NULL &&
        compiler->source_function->css_scope_attribute != NULL &&
        strcmp(rendered_name, "#fragment") != 0 &&
        !compiler_style_name(rendered_name)) {
        emit(compiler, OP_TRUE, 0, 0, expr->as.element.open_span);
        size_t property_name = add_constant(
            compiler, unit,
            compiler->source_function->css_scope_attribute);
        emit(compiler, OP_HTML_ATTR, (int32_t)property_name, 0,
             expr->as.element.open_span);
    }
    ++compiler->html_depth;
    for (size_t i = 0U; i < expr->as.element.body_count; ++i) {
        const ElementBodyItem *item = &expr->as.element.body[i];
        if (item->is_statement) {
            compile_stmt(compiler, item->as.statement);
        } else if (item->is_static_text) {
            size_t text = add_string_constant(
                compiler, item->as.expression->as.string.data,
                item->as.expression->as.string.length);
            emit(compiler, OP_HTML_TEXT, 0, (int32_t)text,
                 item->as.expression->span);
        } else {
            compile_expr(compiler, item->as.expression);
            emit(compiler, OP_HTML_APPEND, 0, 0, item->as.expression->span);
        }
    }
    --compiler->html_depth;
    emit(compiler, OP_HTML_END, 0, 0, expr->span);
}

static void emit_compound_operator(
    Compiler *compiler, TokenKind op, const Type *type, LangSpan span
) {
    bool floating = type != NULL &&
        (type->kind == TYPE_F32 || type->kind == TYPE_F64);
    int32_t kind = type != NULL ? (int32_t)type->kind : 0;
    switch (op) {
        case TOK_PLUS:
            emit(compiler, floating ? OP_ADD_F64 : OP_ADD_I64,
                 kind, 0, span);
            break;
        case TOK_MINUS:
            emit(compiler, floating ? OP_SUB_F64 : OP_SUB_I64,
                 kind, 0, span);
            break;
        case TOK_STAR:
            emit(compiler, floating ? OP_MUL_F64 : OP_MUL_I64,
                 kind, 0, span);
            break;
        case TOK_SLASH:
            emit(compiler, floating ? OP_DIV_F64 : OP_DIV_I64,
                 kind, 0, span);
            break;
        case TOK_PERCENT: emit(compiler, OP_REM_I64, kind, 0, span); break;
        case TOK_SHIFT_LEFT: emit(compiler, OP_SHIFT_LEFT, kind, 0, span); break;
        case TOK_SHIFT_RIGHT: emit(compiler, OP_SHIFT_RIGHT, kind, 0, span); break;
        case TOK_AMP: emit(compiler, OP_BIT_AND, kind, 0, span); break;
        case TOK_PIPE: emit(compiler, OP_BIT_OR, kind, 0, span); break;
        case TOK_CARET: emit(compiler, OP_BIT_XOR, kind, 0, span); break;
        default: emit(compiler, OP_TRAP, 0, 0, span); break;
    }
}

static void compile_expr(Compiler *compiler, const Expr *expr) {
    switch (expr->kind) {
        case EXPR_INT: {
            bool unsigned_integer = expr->type != NULL &&
                (expr->type->kind == TYPE_U8 ||
                 expr->type->kind == TYPE_U16 ||
                 expr->type->kind == TYPE_U32 ||
                 expr->type->kind == TYPE_U64 ||
                 expr->type->kind == TYPE_USIZE);
            LangValue value = {
                .tag = unsigned_integer ? LANG_VALUE_U64 : LANG_VALUE_I64
            };
            if (unsigned_integer)
                value.as.u64 = expr->as.integer;
            else
                value.as.i64 = (int64_t)expr->as.integer;
            emit(compiler, OP_CONSTANT, (int32_t)add_constant(compiler, value, NULL), 0, expr->span);
            break;
        }
        case EXPR_FLOAT: {
            LangValue value = {.tag = LANG_VALUE_F64};
            value.as.f64 = expr->type != NULL &&
                           expr->type->kind == TYPE_F32
                         ? (double)(float)expr->as.floating
                         : expr->as.floating;
            emit(compiler, OP_CONSTANT, (int32_t)add_constant(compiler, value, NULL), 0, expr->span);
            break;
        }
        case EXPR_STRING: {
            emit(compiler, OP_CONSTANT,
                 (int32_t)add_string_constant(
                     compiler, expr->as.string.data,
                     expr->as.string.length),
                 0, expr->span);
            if (expr->type != NULL && expr->type->kind == TYPE_STRING)
                emit(compiler, OP_CALL, -11, 1, expr->span);
            break;
        }
        case EXPR_INTERPOLATION: {
            uint32_t borrowed_mask = 0U;
            for (size_t i = 0U;
                 i < expr->as.interpolation.part_count; ++i) {
                const InterpolationPart *part =
                    &expr->as.interpolation.parts[i];
                if (part->expression != NULL) {
                    if (part->borrow_owned_string) {
                        compile_borrowed_place(
                            compiler, part->expression);
                        if (i < 24U)
                            borrowed_mask |=
                                UINT32_C(1) <<
                                (unsigned)i;
                    } else {
                        compile_expr(
                            compiler, part->expression);
                    }
                } else {
                    emit(
                        compiler, OP_CONSTANT,
                        (int32_t)add_string_constant(
                            compiler, part->text,
                            part->text_length),
                        0, part->span);
                }
            }
            uint32_t encoded_count =
                (uint32_t)
                    expr->as.interpolation.part_count |
                (borrowed_mask << 8U);
            emit(
                compiler, OP_CALL, -24,
                (int32_t)encoded_count,
                expr->span);
            break;
        }
        case EXPR_BOOL: emit(compiler, expr->as.boolean ? OP_TRUE : OP_FALSE, 0, 0, expr->span); break;
        case EXPR_NULL: {
            LangValue value = {
                .tag=LANG_VALUE_RAW_POINTER,
                .as.pointer=NULL
            };
            emit(compiler, OP_CONSTANT,
                 (int32_t)add_constant(compiler, value, NULL), 0,
                 expr->span);
            break;
        }
        case EXPR_NAME: {
            CompileLocal *local =
                find_local(compiler, expr->resolved_local_id);
            if (local == NULL && expr->resolved_decl != NULL &&
                expr->resolved_decl->kind == DECL_FUNCTION) {
                int32_t function = find_function_declaration_index(
                    compiler, expr->resolved_decl);
                emit(compiler, OP_FUNCTION, function, 0,
                     expr->span);
                break;
            }
            emit(compiler, OP_LOAD_LOCAL,
                 local != NULL ? (int32_t)local->slot : -1,
                 0, expr->span);
            if (expr->type != NULL &&
                (expr->type->managed || expr->type->kind == TYPE_ARRAY ||
                 expr->type->kind == TYPE_OPTION ||
                 expr->type->kind == TYPE_RESULT ||
                 expr->type->kind == TYPE_NAMED))
                emit(compiler, OP_CLONE, 0, 0, expr->span);
            break;
        }
        case EXPR_BINARY: {
            if (expr->as.binary.op == TOK_AND_AND ||
                expr->as.binary.op == TOK_OR_OR) {
                bool conjunction =
                    expr->as.binary.op == TOK_AND_AND;
                compile_expr(compiler, expr->as.binary.left);
                size_t left_false = emit(
                    compiler, OP_JUMP_IF_FALSE, 0, 0, expr->span);
                if (!conjunction)
                    emit(compiler, OP_TRUE, 0, 0, expr->span);
                if (!conjunction) {
                    size_t end = emit(
                        compiler, OP_JUMP, 0, 0, expr->span);
                    patch(
                        compiler, left_false,
                        compiler->function->code_count);
                    compile_expr(
                        compiler, expr->as.binary.right);
                    patch(
                        compiler, end,
                        compiler->function->code_count);
                } else {
                    compile_expr(
                        compiler, expr->as.binary.right);
                    size_t end = emit(
                        compiler, OP_JUMP, 0, 0, expr->span);
                    patch(
                        compiler, left_false,
                        compiler->function->code_count);
                    emit(compiler, OP_FALSE, 0, 0, expr->span);
                    patch(
                        compiler, end,
                        compiler->function->code_count);
                }
                break;
            }
            const Expr *nullable = NULL;
            const Expr *left = expr->as.binary.left;
            const Expr *right = expr->as.binary.right;
            bool nullable_comparison = left->type != NULL &&
                left->type->kind == TYPE_OPTION &&
                (expr->as.binary.op == TOK_EQUAL_EQUAL ||
                 expr->as.binary.op == TOK_BANG_EQUAL);
            bool left_null = nullable_comparison &&
                left->kind == EXPR_CALL &&
                left->as.call.callee->kind == EXPR_NAME &&
                strcmp(left->as.call.callee->as.name,
                       "Option::None") == 0;
            bool right_null = nullable_comparison &&
                right->kind == EXPR_CALL &&
                right->as.call.callee->kind == EXPR_NAME &&
                strcmp(right->as.call.callee->as.name,
                       "Option::None") == 0;
            nullable = left_null ? right : (right_null ? left : NULL);
            if (nullable != NULL && nullable->kind == EXPR_NAME) {
                CompileLocal *local = find_local(
                    compiler, nullable->resolved_local_id);
                emit(compiler, OP_LOAD_LOCAL,
                     local != NULL ? (int32_t)local->slot : -1,
                     0, nullable->span);
                emit(compiler, OP_GET_TAG, 0, 0, expr->span);
                LangValue unit = {.tag=LANG_VALUE_UNIT};
                size_t tag = add_constant(
                    compiler, unit, "Option::None");
                emit(compiler, OP_CONSTANT, (int32_t)tag, 0,
                     expr->span);
                emit(compiler,
                     expr->as.binary.op == TOK_EQUAL_EQUAL
                         ? OP_EQ : OP_NEQ,
                     0, 0, expr->span);
                break;
            }
            compile_expr(compiler, expr->as.binary.left);
            compile_expr(compiler, expr->as.binary.right);
            bool floating = expr->as.binary.left->type != NULL &&
                (expr->as.binary.left->type->kind == TYPE_F32 ||
                 expr->as.binary.left->type->kind == TYPE_F64);
            switch (expr->as.binary.op) {
                case TOK_PLUS:
                    emit(compiler, floating ? OP_ADD_F64 : OP_ADD_I64,
                         (int32_t)expr->as.binary.left->type->kind, 0,
                         expr->span);
                    break;
                case TOK_MINUS:
                    emit(compiler, floating ? OP_SUB_F64 : OP_SUB_I64,
                         (int32_t)expr->as.binary.left->type->kind, 0,
                         expr->span);
                    break;
                case TOK_STAR:
                    emit(compiler, floating ? OP_MUL_F64 : OP_MUL_I64,
                         (int32_t)expr->as.binary.left->type->kind, 0,
                         expr->span);
                    break;
                case TOK_SLASH:
                    emit(compiler, floating ? OP_DIV_F64 : OP_DIV_I64,
                         (int32_t)expr->as.binary.left->type->kind, 0,
                         expr->span);
                    break;
                case TOK_PERCENT:
                    emit(compiler, OP_REM_I64,
                         (int32_t)expr->as.binary.left->type->kind, 0,
                         expr->span);
                    break;
                case TOK_SHIFT_LEFT:
                    emit(compiler, OP_SHIFT_LEFT,
                         (int32_t)expr->as.binary.left->type->kind, 0,
                         expr->span);
                    break;
                case TOK_SHIFT_RIGHT:
                    emit(compiler, OP_SHIFT_RIGHT,
                         (int32_t)expr->as.binary.left->type->kind, 0,
                         expr->span);
                    break;
                case TOK_AMP:
                    emit(compiler, OP_BIT_AND,
                         (int32_t)expr->as.binary.left->type->kind, 0,
                         expr->span);
                    break;
                case TOK_PIPE:
                    emit(compiler, OP_BIT_OR,
                         (int32_t)expr->as.binary.left->type->kind, 0,
                         expr->span);
                    break;
                case TOK_CARET:
                    emit(compiler, OP_BIT_XOR,
                         (int32_t)expr->as.binary.left->type->kind, 0,
                         expr->span);
                    break;
                case TOK_EQUAL_EQUAL: emit(compiler, OP_EQ, 0, 0, expr->span); break;
                case TOK_BANG_EQUAL: emit(compiler, OP_NEQ, 0, 0, expr->span); break;
                case TOK_LESS: emit(compiler, OP_LT_I64, 0, 0, expr->span); break;
                case TOK_LESS_EQUAL: emit(compiler, OP_LE_I64, 0, 0, expr->span); break;
                case TOK_GREATER: emit(compiler, OP_GT_I64, 0, 0, expr->span); break;
                case TOK_GREATER_EQUAL: emit(compiler, OP_GE_I64, 0, 0, expr->span); break;
                default: emit(compiler, OP_TRAP, 0, 0, expr->span); break;
            }
            break;
        }
        case EXPR_UNARY:
            if (expr->as.unary.op == TOK_MINUS &&
                expr->as.unary.operand->kind == EXPR_INT &&
                expr->type != NULL &&
                (expr->type->kind == TYPE_I64 ||
                 expr->type->kind == TYPE_ISIZE) &&
                expr->as.unary.operand->as.integer ==
                    (UINT64_C(1) << 63U)) {
                LangValue minimum = {
                    .tag=LANG_VALUE_I64, .as.i64=INT64_MIN
                };
                emit(compiler, OP_CONSTANT,
                     (int32_t)add_constant(compiler, minimum, NULL),
                     0, expr->span);
                break;
            }
            compile_expr(compiler, expr->as.unary.operand);
            if (expr->as.unary.op == TOK_STAR) {
                emit(compiler, OP_CALL, -8, 1, expr->span);
            } else if (expr->as.unary.op == TOK_MINUS) {
                bool floating = expr->type != NULL &&
                    (expr->type->kind == TYPE_F32 ||
                     expr->type->kind == TYPE_F64);
                emit(compiler, floating ? OP_NEG_F64 : OP_NEG_I64,
                     expr->type != NULL ? (int32_t)expr->type->kind : 0,
                     0, expr->span);
            } else if (expr->as.unary.op == TOK_TILDE) {
                emit(compiler, OP_BIT_NOT,
                     expr->type != NULL
                         ? (int32_t)expr->type->kind : 0,
                     0, expr->span);
            } else {
                emit(compiler, OP_NOT, 0, 0, expr->span);
            }
            break;
        case EXPR_CALL: compile_call(compiler, expr); break;
        case EXPR_ASSIGN: {
            const Expr *target = expr->as.assign.target;
            TokenKind compound = expr->as.assign.compound_op;
            if (target->kind == EXPR_UNARY &&
                target->as.unary.op == TOK_STAR) {
                if (compound == TOK_ERROR) {
                    compile_expr(
                        compiler, target->as.unary.operand);
                    compile_expr(
                        compiler, expr->as.assign.value);
                } else {
                    size_t pointer_slot = allocate_local_slot(
                        compiler, target->span, -1);
                    size_t value_slot = allocate_local_slot(
                        compiler, expr->span, -1);
                    compile_expr(
                        compiler, target->as.unary.operand);
                    emit(compiler, OP_STORE_LOCAL,
                         (int32_t)pointer_slot, 0, target->span);
                    emit(compiler, OP_LOAD_LOCAL,
                         (int32_t)pointer_slot, 0, target->span);
                    emit(compiler, OP_CALL, -8, 1, target->span);
                    compile_expr(
                        compiler, expr->as.assign.value);
                    emit_compound_operator(
                        compiler, compound, target->type, expr->span);
                    emit(compiler, OP_STORE_LOCAL,
                         (int32_t)value_slot, 0, expr->span);
                    emit(compiler, OP_LOAD_LOCAL,
                         (int32_t)pointer_slot, 0, target->span);
                    emit(compiler, OP_LOAD_LOCAL,
                         (int32_t)value_slot, 0, expr->span);
                }
                emit(compiler, OP_CALL, -9, 2, expr->span);
            } else if (target->kind == EXPR_NAME) {
                CompileLocal *local =
                    find_local(compiler, target->resolved_local_id);
                if (compound != TOK_ERROR) {
                    emit(compiler, OP_LOAD_LOCAL,
                         local != NULL ? (int32_t)local->slot : -1,
                         0, target->span);
                }
                compile_expr(compiler, expr->as.assign.value);
                if (compound != TOK_ERROR)
                    emit_compound_operator(
                        compiler, compound, target->type, expr->span);
                emit(compiler, OP_SET_LOCAL,
                     local != NULL ? (int32_t)local->slot : -1,
                     local != NULL ? local->destructor : -1,
                     expr->span);
            } else if (target->kind == EXPR_FIELD) {
                CompileLocal *local = find_local(
                    compiler,
                    target->as.field.object->resolved_local_id);
                LangValue unit = {.tag = LANG_VALUE_UNIT};
                size_t field = add_constant(
                    compiler, unit, target->as.field.field);
                if (compound != TOK_ERROR)
                    emit(compiler, OP_GET_FIELD_LOCAL,
                         local != NULL ? (int32_t)local->slot : -1,
                         (int32_t)field, target->span);
                compile_expr(compiler, expr->as.assign.value);
                if (compound != TOK_ERROR)
                    emit_compound_operator(
                        compiler, compound, target->type, expr->span);
                emit(compiler, OP_SET_FIELD_LOCAL,
                     local != NULL ? (int32_t)local->slot : -1,
                     (int32_t)field, expr->span);
            } else {
                CompileLocal *local = find_local(
                    compiler,
                    target->as.index.object->resolved_local_id);
                if (compound != TOK_ERROR) {
                    size_t index_slot = allocate_local_slot(
                        compiler, target->span, -1);
                    size_t value_slot = allocate_local_slot(
                        compiler, target->span, -1);
                    compile_expr(
                        compiler, target->as.index.index);
                    emit(compiler, OP_STORE_LOCAL,
                         (int32_t)index_slot, 0, target->span);
                    emit(compiler, OP_LOAD_LOCAL,
                         (int32_t)index_slot, 0, target->span);
                    emit(compiler, OP_GET_INDEX_LOCAL,
                         local != NULL ? (int32_t)local->slot : -1,
                         target->as.index.unchecked ? 1 : 0,
                         target->span);
                    compile_expr(
                        compiler, expr->as.assign.value);
                    emit_compound_operator(
                        compiler, compound, target->type, expr->span);
                    emit(compiler, OP_STORE_LOCAL,
                         (int32_t)value_slot, 0, expr->span);
                    emit(compiler, OP_LOAD_LOCAL,
                         (int32_t)index_slot, 0, target->span);
                    emit(compiler, OP_LOAD_LOCAL,
                         (int32_t)value_slot, 0, expr->span);
                } else {
                    compile_expr(
                        compiler, target->as.index.index);
                    compile_expr(
                        compiler, expr->as.assign.value);
                }
                emit(compiler, OP_SET_INDEX_LOCAL,
                     local != NULL ? (int32_t)local->slot : -1,
                     target->as.index.unchecked ? 1 : 0, expr->span);
            }
            break;
        }
        case EXPR_CLONE:
            /*
             * A local load is borrowed for the purpose of explicit cloning.
             * Compiling it through EXPR_NAME would first perform the ordinary
             * implicit copy of a copyable aggregate, then clone that copy.
             */
            if (expr->as.clone.value->kind == EXPR_NAME) {
                CompileLocal *local = find_local(
                    compiler,
                    expr->as.clone.value->resolved_local_id);
                emit(compiler, OP_LOAD_LOCAL,
                     local != NULL ? (int32_t)local->slot : -1,
                     0, expr->as.clone.value->span);
                emit(compiler, OP_CLONE, 0, 0, expr->span);
            } else {
                compile_expr(compiler, expr->as.clone.value);
                /* Operand is an owned temporary and must be consumed. */
                emit(compiler, OP_CLONE, 1, 0, expr->span);
            }
            break;
        case EXPR_TRY:
            compile_expr(compiler, expr->as.try_.value);
            emit(compiler, OP_TRY, 0, 0, expr->span);
            break;
        case EXPR_AWAIT:
            lang_diag(compiler->diagnostics, expr->span,
                      "async code generation is not implemented yet");
            break;
        case EXPR_CAST:
            compile_expr(compiler, expr->as.cast.value);
            emit(compiler, OP_CAST,
                 expr->as.cast.value->type != NULL
                     ? (int32_t)expr->as.cast.value->type->kind : -1,
                 expr->type != NULL ? (int32_t)expr->type->kind : -1,
                 expr->span);
            break;
        case EXPR_ARRAY:
            for (size_t i = 0U; i < expr->as.array.count; ++i)
                compile_expr(compiler, expr->as.array.items[i]);
            emit(compiler, OP_MAKE_ARRAY, (int32_t)expr->as.array.count, 0, expr->span);
            break;
        case EXPR_INDEX:
            if (expr->as.index.object->kind == EXPR_NAME) {
                compile_expr(compiler, expr->as.index.index);
                CompileLocal *local = find_local(
                    compiler, expr->as.index.object->resolved_local_id);
                emit(compiler, OP_GET_INDEX_LOCAL,
                     local != NULL ? (int32_t)local->slot : -1,
                     expr->as.index.unchecked ? 1 : 0, expr->span);
            } else {
                compile_expr(compiler, expr->as.index.object);
                compile_expr(compiler, expr->as.index.index);
                emit(compiler, OP_GET_INDEX,
                     expr->as.index.unchecked ? 1 : 0, 0, expr->span);
            }
            break;
        case EXPR_FIELD: {
            if (expr->as.field.object->type != NULL &&
                expr->as.field.object->type->kind == TYPE_OPTION &&
                strcmp(expr->as.field.field, "Value") == 0) {
                size_t before = compiler->local_count;
                int32_t destructor = destructor_index_for_type(
                    compiler, expr->as.field.object->type);
                size_t slot = allocate_local_slot(
                    compiler, expr->span, destructor);
                compile_expr(compiler, expr->as.field.object);
                emit(compiler, OP_STORE_LOCAL, (int32_t)slot, 0,
                     expr->span);
                compiler->locals[compiler->local_count++] = (CompileLocal){
                    "<nullable-value>", slot, compiler->depth, true,
                    destructor, 0U
                };
                emit(compiler, OP_LOAD_LOCAL, (int32_t)slot, 0,
                     expr->span);
                emit(compiler, OP_GET_TAG, 0, 0, expr->span);
                LangValue unit = {.tag=LANG_VALUE_UNIT};
                size_t tag = add_constant(
                    compiler, unit, "Option::Some");
                emit(compiler, OP_CONSTANT, (int32_t)tag, 0,
                     expr->span);
                emit(compiler, OP_EQ, 0, 0, expr->span);
                size_t absent = emit(
                    compiler, OP_JUMP_IF_FALSE, 0, 0, expr->span);
                emit(compiler, OP_MOVE_LOCAL, (int32_t)slot, 0,
                     expr->span);
                emit(compiler, OP_TAKE_PAYLOAD, 0, 0, expr->span);
                size_t done = emit(
                    compiler, OP_JUMP, 0, 0, expr->span);
                patch(compiler, absent, compiler->function->code_count);
                emit(compiler, OP_DROP_LOCAL, (int32_t)slot,
                     destructor, expr->span);
                emit(compiler, OP_TRAP, 0, 0, expr->span);
                patch(compiler, done, compiler->function->code_count);
                compiler->local_count = before;
                break;
            }
            LangValue unit = {.tag = LANG_VALUE_UNIT};
            size_t field = add_constant(compiler, unit, expr->as.field.field);
            if (expr->as.field.object->kind == EXPR_NAME) {
                CompileLocal *local = find_local(
                    compiler, expr->as.field.object->resolved_local_id);
                emit(compiler, OP_GET_FIELD_LOCAL,
                     local != NULL ? (int32_t)local->slot : -1,
                     (int32_t)field, expr->span);
            } else {
                compile_expr(compiler, expr->as.field.object);
                emit(compiler, OP_GET_FIELD, (int32_t)field, 0,
                     expr->span);
            }
            break;
        }
        case EXPR_STRUCT: {
            for (size_t i = 0U; i < expr->as.structure.field_count; ++i)
                compile_expr(compiler, expr->as.structure.fields[i].value);
            LangValue unit = {.tag = LANG_VALUE_UNIT};
            const char *structure_name =
                expr->resolved_decl != NULL &&
                expr->resolved_decl->kind == DECL_STRUCT
                ? expr->resolved_decl->as.structure.name
                : expr->as.structure.name;
            const char *module_name =
                expr->type != NULL &&
                expr->type->kind == TYPE_NAMED &&
                expr->type->declaration != NULL
                ? expr->type->declaration->module_name : NULL;
            size_t metadata_length = strlen(structure_name) + 1U;
            if (module_name != NULL)
                metadata_length += strlen(module_name) + 1U;
            for (size_t i = 0U; i < expr->as.structure.field_count; ++i)
                metadata_length += strlen(expr->as.structure.fields[i].name) + 1U;
            char *metadata = resize(NULL, metadata_length, 1U);
            if (module_name != NULL)
                (void)snprintf(metadata, metadata_length, "%s#%s",
                               module_name, structure_name);
            else
                (void)snprintf(metadata, metadata_length, "%s",
                               structure_name);
            for (size_t i = 0U; i < expr->as.structure.field_count; ++i) {
                strcat(metadata, "|");
                strcat(metadata, expr->as.structure.fields[i].name);
            }
            size_t name = add_constant(compiler, unit, metadata);
            free(metadata);
            emit(compiler, OP_MAKE_STRUCT, (int32_t)name,
                 (int32_t)expr->as.structure.field_count, expr->span);
            break;
        }
        case EXPR_ELEMENT: compile_element(compiler, expr); break;
        case EXPR_IF:
            compile_if_expression(compiler, expr);
            break;
        case EXPR_MATCH:
            compile_match_expression(compiler, expr);
            break;
    }
}

static void emit_scope_drops(Compiler *compiler, size_t begin, LangSpan span) {
    for (size_t i = compiler->local_count; i > begin; --i)
        if (compiler->locals[i - 1U].owning)
            emit(compiler, OP_DROP_LOCAL,
                 (int32_t)compiler->locals[i - 1U].slot,
                 compiler->locals[i - 1U].destructor, span);
}

static void emit_cleanup_plan(Compiler *compiler, const CleanupPlan *plan,
                              LangSpan span) {
    for (size_t i = 0U; i < plan->count; ++i) {
        CompileLocal *local =
            find_local(compiler, plan->binding_ids[i]);
        if (local != NULL)
            emit(compiler, OP_DROP_LOCAL, (int32_t)local->slot,
                 local->destructor, span);
    }
}

static void emit_cleanup_plan_except(
    Compiler *compiler, const CleanupPlan *plan,
    size_t excluded_binding_id, LangSpan span) {
    for (size_t i = 0U; i < plan->count; ++i) {
        if (plan->binding_ids[i] == excluded_binding_id) continue;
        CompileLocal *local =
            find_local(compiler, plan->binding_ids[i]);
        if (local != NULL)
            emit(compiler, OP_DROP_LOCAL, (int32_t)local->slot,
                 local->destructor, span);
    }
}

static bool type_produces_html_child(const Type *type) {
    if (type == NULL) return false;
    if (type->kind == TYPE_HTML || type->kind == TYPE_STR ||
        type->kind == TYPE_STRING)
        return true;
    if (type->kind == TYPE_OPTION || type->kind == TYPE_VEC ||
        type->kind == TYPE_ARRAY)
        return type_produces_html_child(type->element);
    return false;
}

static void compile_value_block(Compiler *compiler, const Stmt *block) {
    size_t begin = compiler->local_count;
    ++compiler->depth;
    size_t count = block->as.block.count;
    for (size_t i = 0U; i + 1U < count; ++i)
        compile_stmt(compiler, block->as.block.items[i]);
    if (count != 0U) {
        const Stmt *tail = block->as.block.items[count - 1U];
        if (tail->kind == STMT_EXPR)
            compile_expr(compiler, tail->as.expression);
        else
            emit(compiler, OP_UNIT, 0, 0, tail->span);
    } else {
        emit(compiler, OP_UNIT, 0, 0, block->span);
    }
    emit_cleanup_plan(compiler, &block->exit_cleanup, block->span);
    compiler->local_count = begin;
    --compiler->depth;
}

static void compile_if_expression(Compiler *compiler, const Expr *expr) {
    compile_expr(compiler, expr->as.if_.condition);
    size_t false_jump = emit(
        compiler, OP_JUMP_IF_FALSE, 0, 0, expr->span);
    compile_value_block(compiler, expr->as.if_.then_branch);
    size_t end_jump = emit(compiler, OP_JUMP, 0, 0, expr->span);
    patch(compiler, false_jump, compiler->function->code_count);
    compile_value_block(compiler, expr->as.if_.else_branch);
    patch(compiler, end_jump, compiler->function->code_count);
}

static void compile_match_expression(Compiler *compiler, const Expr *expr) {
    size_t before = compiler->local_count;
    int32_t matched_destructor = destructor_index_for_type(
        compiler, expr->as.match_.value->type);
    size_t matched_slot = allocate_local_slot(
        compiler, expr->as.match_.value->span, matched_destructor);
    compile_expr(compiler, expr->as.match_.value);
    emit(compiler, OP_STORE_LOCAL, (int32_t)matched_slot, 0,
         expr->as.match_.value->span);
    compiler->locals[compiler->local_count++] = (CompileLocal){
        "<match>", matched_slot, compiler->depth, true,
        matched_destructor, 0U
    };
    size_t end_jumps[256];
    size_t end_count = 0U;
    for (size_t a = 0U; a < expr->as.match_.arm_count; ++a) {
        const MatchArm *arm = &expr->as.match_.arms[a];
        emit(compiler, OP_LOAD_LOCAL, (int32_t)matched_slot, 0,
             arm->span);
        emit(compiler, OP_GET_TAG, 0, 0, arm->span);
        LangValue unit = {.tag=LANG_VALUE_UNIT};
        size_t tag = add_constant(compiler, unit, arm->variant);
        emit(compiler, OP_CONSTANT, (int32_t)tag, 0, arm->span);
        emit(compiler, OP_EQ, 0, 0, arm->span);
        size_t next_arm =
            emit(compiler, OP_JUMP_IF_FALSE, 0, 0, arm->span);
        size_t arm_locals = compiler->local_count;
        if (arm->binding != NULL && arm->binding_type != NULL) {
            emit(compiler, OP_MOVE_LOCAL, (int32_t)matched_slot, 0,
                 arm->span);
            emit(compiler, OP_TAKE_PAYLOAD, 0, 0, arm->span);
            int32_t binding_destructor =
                destructor_index_for_type(compiler, arm->binding_type);
            size_t binding_slot = allocate_local_slot(
                compiler, arm->span, binding_destructor);
            emit(compiler, OP_STORE_LOCAL, (int32_t)binding_slot, 0,
                 arm->span);
            compiler->locals[compiler->local_count++] = (CompileLocal){
                arm->binding, binding_slot, compiler->depth + 1U,
                arm->binding_type->requires_cleanup ||
                arm->binding_type->kind == TYPE_ARRAY ||
                arm->binding_type->kind == TYPE_OPTION ||
                arm->binding_type->kind == TYPE_RESULT ||
                arm->binding_type->kind == TYPE_NAMED,
                binding_destructor, arm->binding_id
            };
        } else {
            emit(compiler, OP_DROP_LOCAL, (int32_t)matched_slot,
                 matched_destructor, arm->span);
        }
        compile_value_block(compiler, arm->body);
        compiler->local_count = arm_locals;
        if (end_count < 256U)
            end_jumps[end_count++] =
                emit(compiler, OP_JUMP, 0, 0, arm->span);
        patch(compiler, next_arm, compiler->function->code_count);
    }
    emit(compiler, OP_DROP_LOCAL, (int32_t)matched_slot,
         matched_destructor, expr->span);
    emit(compiler, OP_TRAP, 0, 0, expr->span);
    size_t end = compiler->function->code_count;
    for (size_t i = 0U; i < end_count; ++i)
        patch(compiler, end_jumps[i], end);
    compiler->local_count = before;
}

static void compile_stmt(Compiler *compiler, const Stmt *stmt) {
    switch (stmt->kind) {
        case STMT_LET: {
            compile_expr(compiler, stmt->as.let.value);
            if (strcmp(stmt->as.let.name, "_") == 0) {
                emit(compiler, OP_POP, 0, 0, stmt->span);
                break;
            }
            int32_t destructor = destructor_index_for_type(
                compiler, stmt->as.let.value->type);
            size_t slot =
                allocate_local_slot(compiler, stmt->span, destructor);
            emit(compiler, OP_STORE_LOCAL, (int32_t)slot, 0, stmt->span);
            compiler->locals[compiler->local_count++] = (CompileLocal){
                stmt->as.let.name, slot, compiler->depth,
                stmt->as.let.value->type != NULL &&
                (stmt->as.let.value->type->requires_cleanup ||
                 stmt->as.let.value->type->kind == TYPE_ARRAY ||
                 stmt->as.let.value->type->kind == TYPE_OPTION ||
                 stmt->as.let.value->type->kind == TYPE_RESULT ||
                 stmt->as.let.value->type->kind == TYPE_NAMED),
                destructor, stmt->as.let.binding_id
            };
            break;
        }
        case STMT_DESTRUCTURE: {
            const Expr *source_expr = stmt->as.destructure.value;
            CompileLocal *source = find_local(
                compiler, source_expr->resolved_local_id);
            const Decl *structure = source_expr->type->declaration;
            for (size_t field = 0U;
                 field < stmt->as.destructure.count; ++field) {
                LangValue unit = {.tag=LANG_VALUE_UNIT};
                size_t field_name = add_constant(
                    compiler, unit,
                    structure->as.structure.fields[field].name);
                emit(compiler, OP_GET_FIELD_LOCAL,
                     source != NULL ? (int32_t)source->slot : -1,
                     (int32_t)field_name, stmt->span);
                Type *field_type =
                    stmt->as.destructure.checked_types[field];
                int32_t destructor = destructor_index_for_type(
                    compiler, field_type);
                size_t slot = allocate_local_slot(
                    compiler, stmt->span, destructor);
                emit(compiler, OP_STORE_LOCAL,
                     (int32_t)slot, 0, stmt->span);
                compiler->locals[compiler->local_count++] =
                    (CompileLocal){
                        stmt->as.destructure.names[field], slot,
                        compiler->depth,
                        field_type->requires_cleanup ||
                        field_type->kind == TYPE_ARRAY ||
                        field_type->kind == TYPE_OPTION ||
                        field_type->kind == TYPE_RESULT ||
                        field_type->kind == TYPE_NAMED,
                        destructor,
                        stmt->as.destructure.binding_ids[field]
                    };
            }
            break;
        }
        case STMT_EXPR:
            compile_expr(compiler, stmt->as.expression);
            if (compiler->html_depth > 0 &&
                type_produces_html_child(stmt->as.expression->type))
                emit(compiler, OP_HTML_APPEND, 0, 0, stmt->span);
            else emit(compiler, OP_POP, 0, 0, stmt->span);
            break;
        case STMT_RETURN: {
            const Expr *return_expr = stmt->as.return_value;
            const Expr *local_expr = return_expr;
            if (local_expr != NULL && local_expr->kind == EXPR_CLONE &&
                local_expr->as.clone.value->kind == EXPR_NAME)
                local_expr = local_expr->as.clone.value;
            CompileLocal *returned = NULL;
            if (local_expr != NULL && local_expr->kind == EXPR_NAME &&
                local_expr->resolved_local_id != 0U &&
                local_expr->type != NULL &&
                (local_expr->type->requires_cleanup ||
                 local_expr->type->managed ||
                 local_expr->type->kind == TYPE_ARRAY ||
                 local_expr->type->kind == TYPE_OPTION ||
                 local_expr->type->kind == TYPE_RESULT ||
                 local_expr->type->kind == TYPE_NAMED)) {
                returned = find_local(
                    compiler, local_expr->resolved_local_id);
                emit(compiler, OP_MOVE_LOCAL,
                     returned != NULL ? (int32_t)returned->slot : -1,
                     0, local_expr->span);
            } else if (return_expr != NULL) {
                compile_expr(compiler, return_expr);
            } else {
                emit(compiler, OP_UNIT, 0, 0, stmt->span);
            }
            emit_cleanup_plan_except(
                compiler, &stmt->exit_cleanup,
                returned != NULL ? returned->binding_id : 0U,
                stmt->span);
            emit(compiler, OP_RETURN, 0, 0, stmt->span);
            break;
        }
        case STMT_IF: {
            compile_expr(compiler, stmt->as.if_.condition);
            size_t false_jump = emit(compiler, OP_JUMP_IF_FALSE, 0, 0, stmt->span);
            compile_stmt(compiler, stmt->as.if_.then_branch);
            size_t end_jump = emit(compiler, OP_JUMP, 0, 0, stmt->span);
            patch(compiler, false_jump, compiler->function->code_count);
            if (stmt->as.if_.else_branch != NULL)
                compile_stmt(compiler, stmt->as.if_.else_branch);
            patch(compiler, end_jump, compiler->function->code_count);
            break;
        }
        case STMT_WHILE: {
            size_t loop = compiler->function->code_count;
            compile_expr(compiler, stmt->as.while_.condition);
            size_t exit_jump = emit(compiler, OP_JUMP_IF_FALSE, 0, 0, stmt->span);
            LoopContext *context = &compiler->loops[compiler->loop_count++];
            memset(context, 0, sizeof(*context));
            context->continue_target = loop;
            context->local_base = compiler->local_count;
            compile_stmt(compiler, stmt->as.while_.body);
            --compiler->loop_count;
            emit(compiler, OP_JUMP, (int32_t)loop, 0, stmt->span);
            size_t exit = compiler->function->code_count;
            patch(compiler, exit_jump, exit);
            for (size_t i = 0U; i < context->break_count; ++i)
                patch(compiler, context->break_jumps[i], exit);
            break;
        }
        case STMT_FOR: {
            if (stmt->as.for_.range_end != NULL) {
                Type *element_type = stmt->as.for_.iterable->type;
                size_t counter_slot =
                    allocate_local_slot(compiler, stmt->span, -1);
                size_t end_slot =
                    allocate_local_slot(compiler, stmt->span, -1);
                compile_expr(compiler, stmt->as.for_.iterable);
                emit(compiler, OP_STORE_LOCAL,
                     (int32_t)counter_slot, 0, stmt->span);
                compile_expr(compiler, stmt->as.for_.range_end);
                emit(compiler, OP_STORE_LOCAL,
                     (int32_t)end_slot, 0, stmt->span);

                size_t loop = compiler->function->code_count;
                emit(compiler, OP_LOAD_LOCAL,
                     (int32_t)counter_slot, 0, stmt->span);
                emit(compiler, OP_LOAD_LOCAL,
                     (int32_t)end_slot, 0, stmt->span);
                emit(compiler, OP_LT_I64, 0, 0, stmt->span);
                size_t exit_jump = emit(
                    compiler, OP_JUMP_IF_FALSE, 0, 0, stmt->span);

                size_t item_slot =
                    allocate_local_slot(compiler, stmt->span, -1);
                emit(compiler, OP_LOAD_LOCAL,
                     (int32_t)counter_slot, 0, stmt->span);
                emit(compiler, OP_STORE_LOCAL,
                     (int32_t)item_slot, 0, stmt->span);
                emit(compiler, OP_LOAD_LOCAL,
                     (int32_t)counter_slot, 0, stmt->span);
                Expr one;
                memset(&one, 0, sizeof(one));
                one.kind = EXPR_INT;
                one.span = stmt->span;
                one.type = element_type;
                one.as.integer = 1U;
                compile_expr(compiler, &one);
                emit(compiler, OP_ADD_I64,
                     element_type != NULL
                         ? (int32_t)element_type->kind
                         : (int32_t)TYPE_I64,
                     0, stmt->span);
                emit(compiler, OP_STORE_LOCAL,
                     (int32_t)counter_slot, 0, stmt->span);

                LoopContext *context =
                    &compiler->loops[compiler->loop_count++];
                memset(context, 0, sizeof(*context));
                context->continue_target = loop;
                context->local_base = compiler->local_count;
                compiler->locals[compiler->local_count++] =
                    (CompileLocal){
                        stmt->as.for_.name, item_slot,
                        compiler->depth + 1U, false, -1,
                        stmt->as.for_.binding_id
                    };
                compile_stmt(compiler, stmt->as.for_.body);
                --compiler->local_count;
                --compiler->loop_count;
                emit(compiler, OP_JUMP, (int32_t)loop, 0,
                     stmt->span);
                size_t exit = compiler->function->code_count;
                patch(compiler, exit_jump, exit);
                for (size_t i = 0U; i < context->break_count; ++i)
                    patch(compiler, context->break_jumps[i], exit);
                break;
            }
            if (stmt->as.for_.borrowed) {
                const Expr *iterable = stmt->as.for_.iterable;
                if (iterable->kind == EXPR_FIELD) {
                    CompileLocal *source = find_local(
                        compiler,
                        iterable->as.field.object->resolved_local_id);
                    emit(compiler, OP_LOAD_LOCAL,
                         source != NULL ? (int32_t)source->slot : -1,
                         0, iterable->span);
                    LangValue unit = {.tag=LANG_VALUE_UNIT};
                    size_t field = add_constant(
                        compiler, unit, iterable->as.field.field);
                    emit(compiler, OP_GET_FIELD_BORROW,
                         (int32_t)field, 0, iterable->span);
                    emit(compiler, OP_ITER_INIT, 0, 1, stmt->span);
                } else {
                    CompileLocal *source = find_local(
                        compiler, iterable->resolved_local_id);
                    emit(compiler, OP_ITER_BORROW_LOCAL,
                         source != NULL ? (int32_t)source->slot : 0,
                         0, stmt->span);
                }
            } else {
                compile_expr(compiler, stmt->as.for_.iterable);
                emit(compiler, OP_ITER_INIT, 0, 0, stmt->span);
            }
            size_t loop = compiler->function->code_count;
            Type *element_type = stmt->as.for_.iterable->type != NULL
                               ? stmt->as.for_.iterable->type->element : NULL;
            int32_t destructor =
                destructor_index_for_type(compiler, element_type);
            size_t slot =
                allocate_local_slot(compiler, stmt->span, destructor);
            size_t next = emit(compiler, OP_ITER_NEXT, 0, (int32_t)slot, stmt->span);
            LoopContext *context = &compiler->loops[compiler->loop_count++];
            memset(context, 0, sizeof(*context));
            context->continue_target = loop;
            context->local_base = compiler->local_count;
            compiler->locals[compiler->local_count++] = (CompileLocal){
                stmt->as.for_.name, slot, compiler->depth + 1U,
                element_type != NULL &&
                    (element_type->requires_cleanup ||
                     element_type->kind == TYPE_STRING ||
                     element_type->kind == TYPE_ARRAY ||
                     element_type->kind == TYPE_OPTION ||
                     element_type->kind == TYPE_RESULT ||
                     element_type->kind == TYPE_NAMED),
                destructor,
                stmt->as.for_.binding_id
            };
            compile_stmt(compiler, stmt->as.for_.body);
            if (compiler->locals[compiler->local_count - 1U].owning)
                emit(compiler, OP_DROP_LOCAL, (int32_t)slot,
                     destructor, stmt->span);
            --compiler->local_count;
            --compiler->loop_count;
            emit(compiler, OP_JUMP, (int32_t)loop, 0, stmt->span);
            size_t cleanup = compiler->function->code_count;
            patch(compiler, next, cleanup);
            for (size_t i = 0U; i < context->break_count; ++i)
                patch(compiler, context->break_jumps[i], cleanup);
            emit(compiler, OP_POP, 0, 0, stmt->span);
            break;
        }
        case STMT_C_FOR: {
            size_t local_base = compiler->local_count;
            ++compiler->depth;
            if (stmt->as.c_for.initializer != NULL)
                compile_stmt(
                    compiler, stmt->as.c_for.initializer);
            size_t condition = compiler->function->code_count;
            if (stmt->as.c_for.condition != NULL)
                compile_expr(
                    compiler, stmt->as.c_for.condition);
            else
                emit(compiler, OP_TRUE, 0, 0, stmt->span);
            size_t exit_jump = emit(
                compiler, OP_JUMP_IF_FALSE, 0, 0, stmt->span);
            size_t body_jump = emit(
                compiler, OP_JUMP, 0, 0, stmt->span);

            size_t increment = compiler->function->code_count;
            if (stmt->as.c_for.increment != NULL) {
                compile_expr(compiler, stmt->as.c_for.increment);
                emit(compiler, OP_POP, 0, 0, stmt->span);
            }
            emit(compiler, OP_JUMP, (int32_t)condition, 0,
                 stmt->span);

            size_t body = compiler->function->code_count;
            patch(compiler, body_jump, body);
            LoopContext *context =
                &compiler->loops[compiler->loop_count++];
            memset(context, 0, sizeof(*context));
            context->continue_target = increment;
            context->local_base = compiler->local_count;
            compile_stmt(compiler, stmt->as.c_for.body);
            --compiler->loop_count;
            emit(compiler, OP_JUMP, (int32_t)increment, 0,
                 stmt->span);

            size_t cleanup = compiler->function->code_count;
            patch(compiler, exit_jump, cleanup);
            for (size_t i = 0U; i < context->break_count; ++i)
                patch(compiler, context->break_jumps[i], cleanup);
            emit_cleanup_plan(
                compiler, &stmt->exit_cleanup, stmt->span);
            compiler->local_count = local_base;
            --compiler->depth;
            break;
        }
        case STMT_MATCH: {
            size_t before = compiler->local_count;
            int32_t matched_destructor = destructor_index_for_type(
                compiler, stmt->as.match_.value->type);
            size_t matched_slot = allocate_local_slot(
                compiler, stmt->as.match_.value->span, matched_destructor);
            compile_expr(compiler, stmt->as.match_.value);
            emit(compiler, OP_STORE_LOCAL, (int32_t)matched_slot, 0,
                 stmt->as.match_.value->span);
            compiler->locals[compiler->local_count++] = (CompileLocal){
                "<match>", matched_slot, compiler->depth, true,
                matched_destructor, 0U
            };
            size_t end_jumps[256];
            size_t end_count = 0U;
            for (size_t a = 0U; a < stmt->as.match_.arm_count; ++a) {
                const MatchArm *arm = &stmt->as.match_.arms[a];
                emit(compiler, OP_LOAD_LOCAL, (int32_t)matched_slot, 0,
                     arm->span);
                emit(compiler, OP_GET_TAG, 0, 0, arm->span);
                LangValue unit = {.tag=LANG_VALUE_UNIT};
                size_t tag = add_constant(compiler, unit, arm->variant);
                emit(compiler, OP_CONSTANT, (int32_t)tag, 0, arm->span);
                emit(compiler, OP_EQ, 0, 0, arm->span);
                size_t next_arm =
                    emit(compiler, OP_JUMP_IF_FALSE, 0, 0, arm->span);
                size_t arm_locals = compiler->local_count;
                if (arm->binding != NULL && arm->binding_type != NULL) {
                    emit(compiler, OP_MOVE_LOCAL, (int32_t)matched_slot, 0,
                         arm->span);
                    emit(compiler, OP_TAKE_PAYLOAD, 0, 0, arm->span);
                    int32_t binding_destructor =
                        destructor_index_for_type(compiler, arm->binding_type);
                    size_t binding_slot = allocate_local_slot(
                        compiler, arm->span, binding_destructor);
                    emit(compiler, OP_STORE_LOCAL, (int32_t)binding_slot, 0,
                         arm->span);
                    compiler->locals[compiler->local_count++] = (CompileLocal){
                        arm->binding, binding_slot, compiler->depth + 1U,
                        arm->binding_type->requires_cleanup ||
                        arm->binding_type->kind == TYPE_ARRAY ||
                        arm->binding_type->kind == TYPE_OPTION ||
                        arm->binding_type->kind == TYPE_RESULT ||
                        arm->binding_type->kind == TYPE_NAMED,
                        binding_destructor, arm->binding_id
                    };
                } else {
                    emit(compiler, OP_DROP_LOCAL, (int32_t)matched_slot,
                         matched_destructor,
                         arm->span);
                }
                compile_stmt(compiler, arm->body);
                emit_scope_drops(compiler, arm_locals, arm->span);
                compiler->local_count = arm_locals;
                if (end_count < 256U)
                    end_jumps[end_count++] =
                        emit(compiler, OP_JUMP, 0, 0, arm->span);
                patch(compiler, next_arm, compiler->function->code_count);
            }
            emit(compiler, OP_DROP_LOCAL, (int32_t)matched_slot,
                 matched_destructor, stmt->span);
            emit(compiler, OP_TRAP, 0, 0, stmt->span);
            size_t end = compiler->function->code_count;
            for (size_t i = 0U; i < end_count; ++i)
                patch(compiler, end_jumps[i], end);
            compiler->local_count = before;
            break;
        }
        case STMT_THROW:
        case STMT_TRY:
            lang_diag(compiler->diagnostics, stmt->span,
                      "legacy direct bytecode does not support exceptions; use the typed IR path");
            emit(compiler, OP_TRAP, 0, 0, stmt->span);
            break;
        case STMT_BREAK: {
            if (compiler->loop_count == 0U) {
                emit(compiler, OP_TRAP, 0, 0, stmt->span);
                break;
            }
            LoopContext *context = &compiler->loops[compiler->loop_count - 1U];
            emit_cleanup_plan(compiler, &stmt->exit_cleanup, stmt->span);
            size_t jump = emit(compiler, OP_JUMP, 0, 0, stmt->span);
            if (context->break_count < 128U)
                context->break_jumps[context->break_count++] = jump;
            else
                lang_diag(compiler->diagnostics, stmt->span,
                          "too many break statements in one loop");
            break;
        }
        case STMT_CONTINUE: {
            if (compiler->loop_count == 0U) {
                emit(compiler, OP_TRAP, 0, 0, stmt->span);
                break;
            }
            LoopContext *context = &compiler->loops[compiler->loop_count - 1U];
            emit_cleanup_plan(compiler, &stmt->exit_cleanup, stmt->span);
            emit(compiler, OP_JUMP, (int32_t)context->continue_target, 0,
                 stmt->span);
            break;
        }
        case STMT_BLOCK: {
            size_t begin = compiler->local_count;
            ++compiler->depth;
            for (size_t i = 0U; i < stmt->as.block.count; ++i)
                compile_stmt(compiler, stmt->as.block.items[i]);
            emit_cleanup_plan(compiler, &stmt->exit_cleanup, stmt->span);
            compiler->local_count = begin;
            --compiler->depth;
            break;
        }
        case STMT_UNSAFE:
            compile_stmt(compiler, stmt->as.unsafe_body);
            break;
    }
}

bool lang_compile_module(const Module *module, LangDiagnostics *diagnostics,
                         BytecodeModule *bytecode) {
    memset(bytecode, 0, sizeof(*bytecode));
    for (size_t i = 0U; i < module->count; ++i) {
        const Decl *decl = module->decls[i];
        if (decl->kind == DECL_FUNCTION &&
            decl->as.function.is_async) {
            lang_diag(diagnostics, decl->span,
                      "async code generation is not implemented yet");
            return false;
        }
    }
    for (size_t i = 0U; i < module->count; ++i)
        if (module->decls[i]->kind == DECL_FUNCTION &&
            module->decls[i]->type_param_count == 0U &&
            !module->decls[i]->as.function.is_extern)
            ++bytecode->function_count;
    bytecode->functions = resize(NULL, bytecode->function_count, sizeof(*bytecode->functions));
    memset(bytecode->functions, 0, bytecode->function_count * sizeof(*bytecode->functions));
    size_t index = 0U;
    for (size_t i = 0U; i < module->count; ++i) {
        if (module->decls[i]->kind != DECL_FUNCTION ||
            module->decls[i]->type_param_count != 0U ||
            module->decls[i]->as.function.is_extern) continue;
        const Function *function = &module->decls[i]->as.function;
        BytecodeFunction *output = &bytecode->functions[index++];
        output->name = function->name;
        output->module_name = module->decls[i]->module_name;
        output->is_public = module->decls[i]->is_public;
        output->is_entry =
            strcmp(function->name, "main") == 0 &&
            module->entry_module != NULL &&
            output->module_name != NULL &&
            strcmp(module->entry_module,
                   output->module_name) == 0;
        output->declaration = module->decls[i];
        output->arity = function->param_count;
        output->may_have_object_locals = true;
        for (size_t parameter = 0U;
             parameter < function->param_count && parameter < 32U;
             ++parameter)
            if (function->params[parameter].borrowed)
                output->borrowed_parameter_mask |=
                    UINT32_C(1) << (unsigned)parameter;
        output->local_count = LANG_MAX_FUNCTION_LOCALS;
        output->local_destructors = resize(
            NULL, output->local_count, sizeof(*output->local_destructors));
        for (size_t local = 0U; local < output->local_count; ++local)
            output->local_destructors[local] = -1;
    }
    index = 0U;
    for (size_t i = 0U; i < module->count; ++i) {
        if (module->decls[i]->kind != DECL_FUNCTION ||
            module->decls[i]->type_param_count != 0U ||
            module->decls[i]->as.function.is_extern) continue;
        const Function *function = &module->decls[i]->as.function;
        Compiler compiler;
        memset(&compiler, 0, sizeof(compiler));
        compiler.module = module; compiler.diagnostics = diagnostics;
        compiler.output = bytecode; compiler.function = &bytecode->functions[index++];
        compiler.source_function = function;
        compiler.current_module = module->decls[i]->module_name;
        compiler.next_slot = function->param_count;
        for (size_t p = 0U; p < function->param_count; ++p) {
            Type *parameter_type = function->params[p].checked_type;
            int32_t destructor =
                (function->is_drop && p == 0U) ||
                function->params[p].borrowed
                ? -1
                : destructor_index_for_type(&compiler, parameter_type);
            compiler.function->local_destructors[p] = destructor;
            compiler.locals[compiler.local_count++] = (CompileLocal){
                function->params[p].name, p, 0U,
                !function->params[p].borrowed &&
                parameter_type != NULL &&
                (parameter_type->requires_cleanup ||
                 parameter_type->kind == TYPE_ARRAY ||
                 parameter_type->kind == TYPE_OPTION ||
                 parameter_type->kind == TYPE_RESULT ||
                 parameter_type->kind == TYPE_NAMED),
                destructor, function->params[p].binding_id
            };
        }
        compile_stmt(&compiler, function->body);
        if (compiler.function->code_count == 0U ||
            compiler.function->code[compiler.function->code_count - 1U].op != OP_RETURN) {
            emit(&compiler, OP_UNIT, 0, 0, function->span);
            emit_scope_drops(&compiler, 0U, function->span);
            emit(&compiler, OP_RETURN, 0, 0, function->span);
        }
    }
    return diagnostics->count == 0U;
}

void lang_bytecode_free(BytecodeModule *bytecode) {
    for (size_t i = 0U; i < bytecode->function_count; ++i)
        free(bytecode->functions[i].local_destructors);
    for (size_t i = 0U; i < bytecode->function_count; ++i)
        free(bytecode->functions[i].spans);
    for (size_t i = 0U; i < bytecode->function_count; ++i)
        free(bytecode->functions[i].code);
    for (size_t i = 0U; i < bytecode->constant_count; ++i)
        free(bytecode->constants[i].owned_string);
    free(bytecode->functions);
    free(bytecode->constants);
    memset(bytecode, 0, sizeof(*bytecode));
}

static const char *op_name(OpCode op) {
    static const char *names[] = {
        "CONSTANT","UNIT","TRUE","FALSE","POP","LOAD_LOCAL","STORE_LOCAL",
        "MOVE_LOCAL","REFERENCE_LOCAL","ADD_I64","SUB_I64","MUL_I64","DIV_I64","REM_I64",
        "SHIFT_LEFT","SHIFT_RIGHT",
        "BIT_AND","BIT_OR","BIT_XOR","BIT_NOT",
        "ADD_F64","SUB_F64","MUL_F64","DIV_F64",
        "NEG_I64","NEG_F64","NOT","CAST",
        "EQ","NEQ","LT_I64","LE_I64","GT_I64","GE_I64",
        "JUMP","JUMP_IF_FALSE","FUNCTION","CALL","CALL_INDIRECT",
        "CALL_NATIVE","AWAIT","TASK_DELAY","TASK_WHEN_ALL","TASK_WHEN_ANY",
        "RETURN","CANCELLATION_SOURCE_NEW","CANCELLATION_TOKEN_NONE",
        "CANCELLATION_TOKEN_GET","CANCELLATION_CANCEL",
        "CANCELLATION_IS_REQUESTED","CANCELLATION_THROW_IF_REQUESTED",
        "MAKE_ARRAY","GET_INDEX",
        "GET_INDEX_LOCAL","SET_INDEX_LOCAL","MAKE_STRUCT","GET_FIELD",
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
        "EXCEPTION_SET","EXCEPTION_PENDING","EXCEPTION_MATCH","EXCEPTION_TAKE",
        "PROPAGATE_EXCEPTION","TRAP"
    };
    return (size_t)op < sizeof(names) / sizeof(names[0]) ? names[(size_t)op] : "?";
}

void lang_dump_bytecode(const BytecodeModule *bytecode) {
    for (size_t f = 0U; f < bytecode->function_count; ++f) {
        const BytecodeFunction *function = &bytecode->functions[f];
        printf("function %s:\n", function->name);
        for (size_t i = 0U; i < function->code_count; ++i) {
            const Instruction *instruction = &function->code[i];
            if (instruction->op == OP_CALL_NATIVE) {
                uint32_t encoded = (uint32_t)instruction->b;
                printf("%04zu %-18s %d argc=%u borrowed=0x%x\n", i,
                       op_name(instruction->op), instruction->a,
                       encoded & UINT32_C(0xff), encoded >> 8U);
            } else if (instruction->op == OP_BINARY_LOCALS ||
                       instruction->op == OP_BINARY_LOCAL_IMMEDIATE) {
                OpCode operation = (OpCode)(
                    (uint32_t)instruction->a & UINT32_C(0xff));
                printf("%04zu %-18s %s %d\n", i,
                       op_name(instruction->op), op_name(operation),
                       instruction->b);
            } else {
                printf("%04zu %-18s %d %d\n", i, op_name(instruction->op),
                       instruction->a, instruction->b);
            }
        }
    }
}
