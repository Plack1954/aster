#include "internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

int main(void) {
    LangSource source;
    if (!lang_source_load("tests/generic_identity_test.as", &source))
        return 1;
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool ok = lang_parse_module(&source, &diagnostics, &module);
    if (ok) ok = lang_check_module(&module, &diagnostics);

    const Type *first = NULL;
    const Type *second = NULL;
    const Type *plain_first = NULL;
    const Type *plain_second = NULL;
    const Decl *function_first = NULL;
    const Decl *function_second = NULL;
    const Decl *function_template = NULL;
    size_t function_instance_count = 0U;
    if (ok) {
        for (size_t i = 0U; i < module.count; ++i) {
            Decl *decl = module.decls[i];
            if (decl->kind != DECL_FUNCTION)
                continue;
            if (strcmp(decl->as.function.name, "identity") == 0)
                function_template = decl;
            if (decl->generic_origin != NULL)
                ++function_instance_count;
            if (decl->as.function.param_count == 1U &&
                strcmp(decl->as.function.name, "AcceptFirst") == 0)
                first = decl->as.function.params[0].checked_type;
            if (decl->as.function.param_count == 1U &&
                strcmp(decl->as.function.name, "AcceptSecond") == 0)
                second = decl->as.function.params[0].checked_type;
            if (decl->as.function.param_count == 1U &&
                strcmp(decl->as.function.name, "AcceptPlainFirst") == 0)
                plain_first = decl->as.function.params[0].checked_type;
            if (decl->as.function.param_count == 1U &&
                strcmp(decl->as.function.name, "AcceptPlainSecond") == 0)
                plain_second = decl->as.function.params[0].checked_type;
            if ((strcmp(decl->as.function.name, "CallFirst") == 0 ||
                 strcmp(decl->as.function.name, "CallSecond") == 0) &&
                decl->as.function.body != NULL &&
                decl->as.function.body->kind == STMT_BLOCK &&
                decl->as.function.body->as.block.count == 1U) {
                Stmt *return_ =
                    decl->as.function.body->as.block.items[0];
                const Decl *resolved =
                    return_->kind == STMT_RETURN &&
                    return_->as.return_value != NULL
                    ? return_->as.return_value->resolved_decl : NULL;
                if (strcmp(
                        decl->as.function.name,
                        "CallFirst") == 0)
                    function_first = resolved;
                else
                    function_second = resolved;
            }
        }
    }
    bool canonical =
        first != NULL && first == second &&
        plain_first != NULL && plain_first == plain_second &&
        plain_first->declaration != NULL &&
        plain_first->declaration->resolved_type == plain_first &&
        module.type_instantiation_count == 1U &&
        module.type_instantiations[0] == first &&
        function_template != NULL &&
        function_first != NULL &&
        function_first == function_second &&
        function_first->generic_origin == function_template &&
        function_instance_count == 1U;

    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    return ok && canonical ? 0 : 2;
}
