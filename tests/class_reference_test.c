#include "internal.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool rejects_private_field_access(void) {
    static const char text[] =
        "class Secret { private long Value; "
        "public Secret(long value) { Value = value; } }\n"
        "int main() { Secret value = new Secret(1); "
        "return value.Value; }\n";
    LangSource source = {
        .text=(char *)text,
        .length=sizeof(text) - 1U,
        .path="<private-class-field-test>"
    };
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool parsed = lang_parse_module(&source, &diagnostics, &module);
    bool checked = parsed && lang_check_module(&module, &diagnostics);
    bool rejected = parsed && !checked && diagnostics.count != 0U;
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    return rejected;
}

int main(void) {
    static const char text[] =
        "private class Node {\n"
        "    private long Value;\n"
        "    public Node Next;\n"
        "    public Node(long value) { this.Value = value; this.Next = null; }\n"
        "    public long Increment() { Value = Value + 1; return Value; }\n"
        "    ~Node() { Value = 0; }\n"
        "}\n"
        "public Node Identity(Node value) { return value; }\n"
        "int main() { Node value = new Node(40); Node alias = value; "
        "long result = alias.Increment(); delete value; "
        "return result == 41 ? 0 : 1; }\n";
    LangSource source = {
        .text=(char *)text,
        .length=sizeof(text) - 1U,
        .path="<class-reference-test>"
    };
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool ok = lang_parse_module(&source, &diagnostics, &module);
    if (!ok) fputs("class test failed while parsing\n", stderr);
    if (ok) ok = lang_check_module(&module, &diagnostics);
    if (!ok && diagnostics.count == 0U)
        fputs("class test failed while checking\n", stderr);

    const Decl *class_decl = NULL;
    const Type *parameter_type = NULL;
    for (size_t i = 0U; ok && i < module.count; ++i) {
        const Decl *decl = module.decls[i];
        if (decl->kind == DECL_CLASS)
            class_decl = decl;
        else if (decl->kind == DECL_FUNCTION &&
                 strcmp(decl->as.function.name, "Identity") == 0)
            parameter_type = decl->as.function.params[0].checked_type;
    }
    ok = ok && class_decl != NULL &&
         class_decl->as.structure.field_count == 2U &&
         parameter_type != NULL && parameter_type->kind == TYPE_CLASS &&
         parameter_type->declaration == class_decl &&
         !parameter_type->managed && !parameter_type->requires_cleanup;
    if (!ok && diagnostics.count == 0U)
        fprintf(stderr, "class AST: decl=%p fields=%zu parameter=%p kind=%d\n",
                (void *)class_decl,
                class_decl != NULL ? class_decl->as.structure.field_count : 0U,
                (void *)parameter_type,
                parameter_type != NULL ? (int)parameter_type->kind : -1);

    LangTargetInfo target;
    lang_target_host(&target);
    IrModule ir = {0};
    if (ok) ok = lang_ir_lower_module(
        &module, &target, &diagnostics, &ir);
    if (!ok && diagnostics.count == 0U) {
        fputs("class test failed while lowering\n", stderr);
        lang_ir_dump_module(&ir);
    }
    if (ok) ok = lang_ir_verify_module(&ir, &diagnostics);
    if (!ok && diagnostics.count == 0U)
        fputs("class test failed while verifying\n", stderr);

    IrTypeId class_type = IR_INVALID_ID;
    for (size_t i = 0U; ok && i < ir.type_count; ++i) {
        const IrType *type = &ir.types[i];
        if (type->shape == IR_TYPE_CLASS_REFERENCE &&
            strcmp(type->name, "Node") == 0) {
            class_type = (IrTypeId)i;
            ok = type->copy_policy == IR_COPY_TRIVIAL &&
                 type->drop_policy == IR_DROP_TRIVIAL &&
                 type->target_layout_known &&
                 type->target_size == target.pointer_size &&
                 type->target_alignment == target.pointer_alignment &&
                 type->object_layout_known &&
                 type->object_size >= target.enum_tag_size +
                     target.pointer_size + sizeof(int64_t) &&
                 type->field_count == 2U &&
                 type->field_offsets[0] >= target.enum_tag_size &&
                 type->field_offsets[1] >=
                     type->field_offsets[0] + sizeof(int64_t) &&
                 type->field_types[1] == (IrTypeId)i &&
                 type->destructor_function != IR_INVALID_ID &&
                 type->destructor_function < ir.function_count &&
                 ir.functions[type->destructor_function].is_destructor;
            if (!ok)
                fprintf(stderr,
                        "class IR: fields=%zu size=%zu destructor=%u functions=%zu\n",
                        type->field_count, type->object_size,
                        type->destructor_function, ir.function_count);
            break;
        }
    }
    ok = ok && class_type != IR_INVALID_ID;

    BytecodeModule bytecode = {0};
    if (ok) ok = lang_ir_compile_bytecode(&ir, &diagnostics, &bytecode);
    if (ok) {
        LangVM *vm = lang_vm_new();
        int vm_status = vm != NULL
            ? lang_vm_run_module(vm, &bytecode, &source) : -1;
        if (vm_status != 0)
            fprintf(stderr, "class VM status: %d\n", vm_status);
        ok = vm != NULL && vm_status == 0;
        lang_vm_free(vm);
    }
    lang_bytecode_free(&bytecode);

    FILE *generated = tmpfile();
    if (ok && generated != NULL)
        ok = lang_c_emit_module(&ir, &diagnostics, generated);
    if (ok && generated != NULL) {
        char forward[128];
        char pointer[128];
        char definition[128];
        (void)snprintf(forward, sizeof(forward),
                       "typedef struct aster_type_%u aster_type_%u;",
                       class_type, class_type);
        (void)snprintf(pointer, sizeof(pointer),
                       "aster_type_%u *", class_type);
        (void)snprintf(definition, sizeof(definition),
                       "struct aster_type_%u {", class_type);
        bool has_forward = false;
        bool has_pointer = false;
        bool has_definition = false;
        char line[1024];
        rewind(generated);
        while (fgets(line, sizeof(line), generated) != NULL) {
            has_forward = has_forward || strstr(line, forward) != NULL;
            has_pointer = has_pointer || strstr(line, pointer) != NULL;
            has_definition =
                has_definition || strstr(line, definition) != NULL;
        }
        ok = has_forward && has_pointer && has_definition;
    } else if (generated == NULL) {
        ok = false;
    }

    if (!ok)
        lang_diagnostics_print(&source, &diagnostics, stderr);
    if (generated != NULL) fclose(generated);
    lang_ir_free_module(&ir);
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    return ok && rejects_private_field_access() ? 0 : 1;
}
