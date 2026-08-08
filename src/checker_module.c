#include "checker_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool function_parameter_is_receiver(
    const Function *function, size_t parameter, const Type *type
) {
    if (parameter != 0U || function->is_static_member ||
        function->is_constructor || function->is_drop || type == NULL)
        return false;
    if (type->kind == TYPE_CLASS) return false;
    if (function->owner_type != NULL) return true;
    const char *separator = strstr(function->name, "::");
    if (separator == NULL) return false;
    size_t owner_length = (size_t)(separator - function->name);
    const char *declared_name = function->params[parameter].type_name;
    return declared_name != NULL && strlen(declared_name) == owner_length &&
           strncmp(function->name, declared_name, owner_length) == 0;
}

static bool type_has_c_abi(Checker *checker, Type *type,
                           const Decl **seen, size_t seen_count) {
    if (type == NULL) return false;
    switch (type->kind) {
        case TYPE_BOOL:
        case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
        case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
        case TYPE_ISIZE: case TYPE_USIZE:
        case TYPE_F32: case TYPE_F64: case TYPE_CHAR:
        case TYPE_RAW_POINTER:
            return true;
        case TYPE_ARRAY:
            return type_has_c_abi(
                checker, type->element, seen, seen_count);
        case TYPE_NAMED: {
            const Decl *decl = type->declaration;
            if (decl == NULL) return false;
            for (size_t i = 0U; i < seen_count; ++i)
                if (seen[i] == decl) return false;
            if (seen_count >= 64U) return false;
            const Decl *next_seen[64];
            for (size_t i = 0U; i < seen_count; ++i)
                next_seen[i] = seen[i];
            next_seen[seen_count++] = decl;
            const char *previous_module = checker->current_module;
            checker->current_module = decl->module_name;
            if (decl->kind == DECL_ALIAS) {
                Type *target = resolve_declared_type(
                    checker, decl->as.alias.target_syntax,
                    decl->as.alias.target, decl->span);
                bool safe = type_has_c_abi(
                    checker, target, next_seen, seen_count);
                checker->current_module = previous_module;
                return safe;
            }
            if (decl->kind == DECL_ENUM &&
                !decl->as.enumeration.is_union) {
                checker->current_module = previous_module;
                return true;
            }
            if (decl->kind != DECL_STRUCT ||
                !decl->as.structure.is_extern) {
                checker->current_module = previous_module;
                return false;
            }
            for (size_t field = 0U;
                 field < decl->as.structure.field_count; ++field) {
                FieldDecl *field_decl =
                    &decl->as.structure.fields[field];
                Type *field_type = resolve_declared_type(
                    checker, field_decl->type_syntax,
                    field_decl->type_name, field_decl->span);
                if (!type_has_c_abi(
                        checker, field_type,
                        next_seen, seen_count)) {
                    checker->current_module = previous_module;
                    return false;
                }
            }
            checker->current_module = previous_module;
            return true;
        }
        case TYPE_ERROR:
        case TYPE_UNIT: case TYPE_NEVER:
        case TYPE_STR: case TYPE_STRING: case TYPE_STRING_BUILDER:
        case TYPE_URL: case TYPE_HTML: case TYPE_BUFFER: case TYPE_ARENA:
        case TYPE_NATIVE_HANDLE:
        case TYPE_CANCELLATION_TOKEN:
        case TYPE_CANCELLATION_TOKEN_SOURCE:
        case TYPE_SLICE: case TYPE_READONLY_SPAN: case TYPE_VEC:
        case TYPE_DICTIONARY: case TYPE_HASH_SET: case TYPE_QUEUE:
        case TYPE_STACK:
        case TYPE_OPTION: case TYPE_RESULT: case TYPE_TASK:
        case TYPE_FUNCTION: case TYPE_CLASS:
            return false;
    }
    return false;
}

static bool interface_reaches(
    const Decl *current, const Decl *target, size_t depth, size_t limit
) {
    if (current == target) return true;
    if (current == NULL || depth >= limit) return false;
    for (size_t i = 0U;
         i < current->as.structure.interface_count; ++i)
        if (interface_reaches(
                current->as.structure.interfaces[i], target,
                depth + 1U, limit))
            return true;
    return false;
}

static const char *member_short_name(const Function *function) {
    const char *separator = strrchr(function->name, ':');
    return separator != NULL ? separator + 1U : function->name;
}

static bool member_signatures_match(
    const Function *implementation, const Function *contract
) {
    if (strcmp(member_short_name(implementation),
               member_short_name(contract)) != 0 ||
        implementation->param_count != contract->param_count ||
        !same_type(implementation->checked_return_type,
                   contract->checked_return_type))
        return false;
    for (size_t parameter = 1U;
         parameter < implementation->param_count; ++parameter)
        if (!same_type(implementation->params[parameter].checked_type,
                       contract->params[parameter].checked_type) ||
            parameter_mode_from_param(
                &implementation->params[parameter]) !=
            parameter_mode_from_param(&contract->params[parameter]))
            return false;
    return true;
}

bool lang_check_module(Module *module, LangDiagnostics *diagnostics) {
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *first = module->decls[i];
        const char *first_name =
            (first->kind == DECL_STRUCT || first->kind == DECL_CLASS)
                ? first->as.structure.name :
            first->kind == DECL_ENUM ? first->as.enumeration.name :
            first->kind == DECL_ALIAS ? first->as.alias.name : NULL;
        if (first_name == NULL) continue;
        for (size_t j = i + 1U; j < module->count; ++j) {
            Decl *second = module->decls[j];
            const char *second_name =
                (second->kind == DECL_STRUCT || second->kind == DECL_CLASS)
                    ? second->as.structure.name :
                second->kind == DECL_ENUM ? second->as.enumeration.name :
                second->kind == DECL_ALIAS ? second->as.alias.name : NULL;
            if (second_name != NULL &&
                first->module_name != NULL &&
                second->module_name != NULL &&
                strcmp(first->module_name,
                       second->module_name) == 0 &&
                strcmp(first_name, second_name) == 0)
                lang_diag(diagnostics, second->span,
                          "duplicate type `%s`", second_name);
        }
    }
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *decl = module->decls[i];
        if (decl->kind != DECL_CLASS ||
            decl->as.structure.heritage_type_count == 0U)
            continue;
        Checker checker;
        memset(&checker, 0, sizeof(checker));
        checker.module = module;
        checker.diagnostics = diagnostics;
        checker.current_module = decl->module_name;
        decl->as.structure.interfaces = lang_arena_alloc(
            &module->arena,
            decl->as.structure.heritage_type_count *
                sizeof(*decl->as.structure.interfaces));
        for (size_t heritage = 0U;
             heritage < decl->as.structure.heritage_type_count;
             ++heritage) {
            Type *base_type = resolve_declared_type(
                &checker,
                decl->as.structure.heritage_type_syntaxes[heritage],
                decl->as.structure.heritage_type_names[heritage],
                decl->span);
            if (base_type->kind != TYPE_CLASS ||
                base_type->declaration == NULL) {
                lang_diag(diagnostics, decl->span,
                          "base type `%s` must be a class or interface",
                          decl->as.structure.heritage_type_names[heritage]);
                continue;
            }
            Decl *base = (Decl *)base_type->declaration;
            if (base->as.structure.is_interface) {
                bool duplicate = false;
                for (size_t previous = 0U;
                     previous < decl->as.structure.interface_count;
                     ++previous)
                    duplicate = duplicate ||
                        decl->as.structure.interfaces[previous] == base;
                if (duplicate)
                    lang_diag(diagnostics, decl->span,
                              "duplicate interface `%s`",
                              base->as.structure.name);
                else
                    decl->as.structure.interfaces[
                        decl->as.structure.interface_count++] = base;
                continue;
            }
            if (decl->as.structure.is_interface) {
                lang_diag(diagnostics, decl->span,
                          "interface `%s` cannot inherit class `%s`",
                          decl->as.structure.name,
                          base->as.structure.name);
                continue;
            }
            if (decl->as.structure.base_class != NULL) {
                lang_diag(diagnostics, decl->span,
                          "class `%s` cannot have more than one base class",
                          decl->as.structure.name);
                continue;
            }
            decl->as.structure.base_class = base;
            decl->as.structure.base_type_name =
                decl->as.structure.heritage_type_names[heritage];
            decl->as.structure.base_type_syntax =
                decl->as.structure.heritage_type_syntaxes[heritage];
            if (base->as.structure.is_sealed)
                lang_diag(diagnostics, decl->span,
                          "class `%s` cannot derive from sealed class `%s`",
                          decl->as.structure.name,
                          base->as.structure.name);
            if (base->as.structure.field_count != 0U)
                lang_diag(
                    diagnostics, decl->span,
                    "base classes with instance fields require base-constructor lowering, which is not implemented yet");
        }
    }
    /* All direct bases are resolved now, so validate complete chains. */
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *decl = module->decls[i];
        if (decl->kind != DECL_CLASS) continue;
        const Decl *cursor = decl->as.structure.base_class;
        for (size_t depth = 0U; cursor != NULL; ++depth) {
            if (cursor == decl || depth >= module->count) {
                lang_diag(diagnostics, decl->span,
                          "class inheritance cycle involving `%s`",
                          decl->as.structure.name);
                break;
            }
            cursor = cursor->as.structure.base_class;
        }
        if (decl->as.structure.is_interface)
            for (size_t interface = 0U;
                 interface < decl->as.structure.interface_count;
                 ++interface)
                if (interface_reaches(
                        decl->as.structure.interfaces[interface], decl,
                        0U, module->count)) {
                    lang_diag(
                        diagnostics, decl->span,
                        "interface inheritance cycle involving `%s`",
                        decl->as.structure.name);
                    break;
                }
    }
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *owner = module->decls[i];
        if (owner->kind != DECL_STRUCT && owner->kind != DECL_CLASS)
            continue;
        Checker checker;
        memset(&checker, 0, sizeof(checker));
        checker.module = module;
        checker.diagnostics = diagnostics;
        checker.current_module = owner->module_name;
        for (size_t field = 0U;
             field < owner->as.structure.static_field_count; ++field) {
            FieldDecl *declaration =
                &owner->as.structure.static_fields[field];
            declaration->checked_type = resolve_declared_type(
                &checker, declaration->type_syntax,
                declaration->type_name, declaration->span);
            Type *type = declaration->checked_type;
            bool supported = type != NULL &&
                (is_numeric(type) || type->kind == TYPE_BOOL ||
                 type->kind == TYPE_CHAR || type->kind == TYPE_CLASS ||
                 type->kind == TYPE_RAW_POINTER ||
                 (type->kind == TYPE_NAMED &&
                  type->declaration != NULL &&
                  type->declaration->kind == DECL_ENUM &&
                  !type->declaration->as.enumeration.is_union));
            if (!supported)
                lang_diag(
                    diagnostics, declaration->span,
                    "static field `%s` currently requires a scalar, enum, pointer, or class-reference type",
                    declaration->name);
            if (declaration->initializer != NULL) {
                Expr *initializer = declaration->initializer;
                bool constant = initializer->kind == EXPR_INT ||
                    initializer->kind == EXPR_FLOAT ||
                    initializer->kind == EXPR_BOOL ||
                    initializer->kind == EXPR_NULL ||
                    (initializer->kind == EXPR_UNARY &&
                     initializer->as.unary.op == TOK_MINUS &&
                     (initializer->as.unary.operand->kind == EXPR_INT ||
                      initializer->as.unary.operand->kind == EXPR_FLOAT));
                if (!constant) {
                    lang_diag(
                        diagnostics, initializer->span,
                        "static field initializers currently require a scalar constant or null");
                } else {
                    Type *previous_expected = checker.expected_type;
                    checker.expected_type = type;
                    Type *actual = check_expr(&checker, initializer);
                    checker.expected_type = previous_expected;
                    if (coerce_literal(&checker, initializer, type))
                        actual = type;
                    if (!type_assignable(type, actual))
                        lang_diag(
                            diagnostics, initializer->span,
                            "static field initializer expects `%s`, found `%s`",
                            type_display_name(&checker, type),
                            type_display_name(&checker, actual));
                }
            }
            for (size_t previous = 0U; previous < field; ++previous)
                if (strcmp(
                        owner->as.structure.static_fields[previous].name,
                        declaration->name) == 0)
                    lang_diag(
                        diagnostics, declaration->span,
                        "duplicate static field `%s`",
                        declaration->name);
        }
    }
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *decl = module->decls[i];
        if (decl->kind != DECL_STRUCT ||
            !decl->as.structure.is_extern)
            continue;
        Checker checker;
        memset(&checker, 0, sizeof(checker));
        checker.module = module;
        checker.diagnostics = diagnostics;
        checker.current_module = decl->module_name;
        if (decl->type_param_count != 0U) {
            lang_diag(
                diagnostics, decl->span,
                "extern struct `%s` cannot be generic",
                decl->as.structure.name);
            continue;
        }
        for (size_t field = 0U;
             field < decl->as.structure.field_count; ++field) {
            FieldDecl *field_decl =
                &decl->as.structure.fields[field];
            Type *field_type = resolve_declared_type(
                &checker, field_decl->type_syntax,
                field_decl->type_name, field_decl->span);
            if (!type_has_c_abi(&checker, field_type, NULL, 0U))
                lang_diag(
                    diagnostics, field_decl->span,
                    "field `%s` of extern struct `%s` has non-C-ABI type `%s`",
                    field_decl->name, decl->as.structure.name,
                    field_decl->type_name);
        }
    }
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *decl = module->decls[i];
        if (decl->has_explicit_visibility) continue;
        const char *kind = NULL;
        const char *name = NULL;
        if (decl->kind == DECL_CLASS) {
            kind = decl->as.structure.is_interface ? "interface" : "class";
            name = decl->as.structure.name;
        } else if (decl->kind == DECL_STRUCT) {
            kind = "struct";
            name = decl->as.structure.name;
        } else if (decl->kind == DECL_ENUM) {
            kind = decl->as.enumeration.is_union ? "union" : "enum";
            name = decl->as.enumeration.name;
        } else if (decl->kind == DECL_ELEMENT) {
            kind = "element";
            name = decl->as.element.name;
        } else if (decl->kind == DECL_ALIAS && decl->as.alias.is_delegate) {
            kind = "delegate";
            name = decl->as.alias.name;
        }
        if (kind != NULL) {
            LangDiagnostic *diagnostic = lang_diag(
                diagnostics, decl->span,
                "%s `%s` must begin with `public` or `private`",
                kind, name);
            lang_diag_help(
                diagnostic,
                "use `private` for a module-local declaration or `public` for an exported declaration");
        }
    }
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *decl = module->decls[i];
        if (decl->kind != DECL_FUNCTION ||
            decl->generic_origin != NULL ||
            decl->as.function.is_drop)
            continue;
        bool is_main = decl->type_param_count == 0U &&
            !decl->as.function.is_extern &&
            strcmp(decl->as.function.name, "main") == 0;
        if (is_main && decl->has_explicit_visibility) {
            LangDiagnostic *diagnostic = lang_diag(
                diagnostics, decl->span,
                "`main` must not declare `public` or `private` visibility");
            lang_diag_help(diagnostic, "write the entry point as `int main()`");
        } else if (!is_main && !decl->has_explicit_visibility) {
            LangDiagnostic *diagnostic = lang_diag(
                diagnostics, decl->span,
                "function `%s` must begin with `public` or `private`",
                decl->as.function.name);
            lang_diag_help(
                diagnostic,
                "use `private` for module-internal functions or `public` for exported functions");
        }
    }
    /* Resolve callable ownership before checking any body. */
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *decl = module->decls[i];
        if (decl->kind != DECL_FUNCTION ||
            decl->type_param_count != 0U)
            continue;
        Checker checker;
        memset(&checker, 0, sizeof(checker));
        checker.module = module;
        checker.diagnostics = diagnostics;
        checker.current_module = decl->module_name;
        Function *function = &decl->as.function;
        function->checked_return_type = resolve_declared_type(
            &checker, function->return_type_syntax,
            function->return_type, function->span);
        for (size_t p = 0U; p < function->param_count; ++p) {
            Param *parameter = &function->params[p];
            Type *type = resolve_declared_type(
                &checker, parameter->type_syntax,
                parameter->type_name, parameter->span);
            parameter->checked_type = type;
        }
    }
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *member_decl = module->decls[i];
        if (member_decl->kind != DECL_FUNCTION) continue;
        Function *member = &member_decl->as.function;
        if (member->owner_type == NULL ||
            (!member->is_abstract_member &&
             !member->is_virtual_member &&
             !member->is_override_member &&
             !member->is_sealed_override))
            continue;
        Decl *owner = NULL;
        for (size_t type = 0U; type < module->count; ++type) {
            Decl *candidate = module->decls[type];
            if (candidate->kind == DECL_CLASS &&
                candidate->module_name != NULL &&
                member_decl->module_name != NULL &&
                strcmp(candidate->module_name,
                       member_decl->module_name) == 0 &&
                strcmp(candidate->as.structure.name,
                       member->owner_type) == 0) {
                owner = candidate;
                break;
            }
        }
        if (owner == NULL) continue;
        if (member->is_static_member)
            lang_diag(diagnostics, member_decl->span,
                      "static methods cannot be abstract, virtual, or override");
        if (member->is_constructor || member->is_drop)
            lang_diag(diagnostics, member_decl->span,
                      "constructors and destructors cannot be abstract, virtual, or override");
        if (member->is_sealed_override && !member->is_override_member)
            lang_diag(diagnostics, member_decl->span,
                      "`sealed` on a method requires `override`");
        if (!member_decl->is_public)
            lang_diag(diagnostics, member_decl->span,
                      "virtual methods must be public");
        if (member->is_abstract_member &&
            !owner->as.structure.is_abstract)
            lang_diag(diagnostics, member_decl->span,
                      "abstract method `%s` requires an abstract class",
                      member->name);
        if (member->is_abstract_member && member->body != NULL)
            lang_diag(diagnostics, member_decl->span,
                      "abstract methods do not have bodies");
        if (member->is_virtual_member && member->is_override_member &&
            !member->is_abstract_member)
            lang_diag(diagnostics, member_decl->span,
                      "an override method does not also declare `virtual`");
        if (member->is_abstract_member && member->is_sealed_override)
            lang_diag(diagnostics, member_decl->span,
                      "an abstract override cannot be sealed");
        if (!member->is_override_member) {
            if (member->is_virtual_member)
                member->virtual_root_decl = member_decl;
            continue;
        }
        const Decl *matched = NULL;
        for (Decl *base = owner->as.structure.base_class;
             base != NULL && matched == NULL;
             base = base->as.structure.base_class) {
            for (size_t m = 0U; m < base->as.structure.member_count; ++m) {
                Decl *candidate_decl = base->as.structure.members[m];
                Function *candidate = &candidate_decl->as.function;
                const char *member_short = strrchr(member->name, ':');
                const char *candidate_short = strrchr(candidate->name, ':');
                member_short = member_short != NULL
                    ? member_short + 1U : member->name;
                candidate_short = candidate_short != NULL
                    ? candidate_short + 1U : candidate->name;
                if ((!candidate->is_virtual_member &&
                     !candidate->is_override_member) ||
                    strcmp(member_short, candidate_short) != 0 ||
                    member->param_count != candidate->param_count ||
                    !same_type(member->checked_return_type,
                               candidate->checked_return_type))
                    continue;
                bool signature = true;
                for (size_t p = 1U; p < member->param_count; ++p)
                    if (!same_type(member->params[p].checked_type,
                                   candidate->params[p].checked_type) ||
                        parameter_mode_from_param(&member->params[p]) !=
                            parameter_mode_from_param(
                                &candidate->params[p])) {
                        signature = false;
                        break;
                    }
                if (signature) matched = candidate_decl;
            }
        }
        if (matched == NULL) {
            lang_diag(diagnostics, member_decl->span,
                      "override `%s` has no matching virtual base method",
                      member->name);
        } else {
            if (matched->as.function.is_sealed_override)
                lang_diag(diagnostics, member_decl->span,
                          "cannot override sealed method `%s`",
                          matched->as.function.name);
            member->overridden_decl = matched;
            member->virtual_root_decl =
                matched->as.function.virtual_root_decl != NULL
                    ? matched->as.function.virtual_root_decl : matched;
        }
    }
    size_t interface_contract_capacity = 0U;
    size_t interface_stack_capacity = module->count;
    for (size_t i = 0U; i < module->count; ++i)
        if (module->decls[i]->kind == DECL_CLASS) {
            interface_stack_capacity +=
                module->decls[i]->as.structure.interface_count;
            if (module->decls[i]->as.structure.is_interface)
                interface_contract_capacity +=
                    module->decls[i]->as.structure.member_count;
        }
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *owner = module->decls[i];
        if (owner->kind != DECL_CLASS ||
            owner->as.structure.is_interface)
            continue;
        owner->as.structure.interface_members = lang_arena_alloc(
            &module->arena, interface_contract_capacity *
                sizeof(*owner->as.structure.interface_members));
        owner->as.structure.interface_implementations = lang_arena_alloc(
            &module->arena, interface_contract_capacity *
                sizeof(*owner->as.structure.interface_implementations));
        Decl **stack = calloc(interface_stack_capacity, sizeof(*stack));
        Decl **seen = calloc(module->count, sizeof(*seen));
        if ((module->count != 0U && stack == NULL) ||
            (module->count != 0U && seen == NULL)) {
            free(stack);
            free(seen);
            fputs("fatal: out of memory\n", stderr);
            exit(2);
        }
        size_t stack_count = 0U;
        for (Decl *cursor = owner; cursor != NULL;
             cursor = cursor->as.structure.base_class)
            for (size_t interface = 0U;
                 interface < cursor->as.structure.interface_count;
                 ++interface)
                if (stack_count < interface_stack_capacity)
                    stack[stack_count++] =
                        cursor->as.structure.interfaces[interface];
        size_t seen_count = 0U;
        while (stack_count != 0U) {
            Decl *contract_owner = stack[--stack_count];
            bool already_seen = false;
            for (size_t seen_index = 0U;
                 seen_index < seen_count; ++seen_index)
                already_seen = already_seen ||
                    seen[seen_index] == contract_owner;
            if (already_seen) continue;
            if (seen_count < module->count)
                seen[seen_count++] = contract_owner;
            for (size_t parent = 0U;
                 parent < contract_owner->as.structure.interface_count;
                 ++parent)
                if (stack_count < interface_stack_capacity)
                    stack[stack_count++] =
                        contract_owner->as.structure.interfaces[parent];
            for (size_t member = 0U;
                 member < contract_owner->as.structure.member_count;
                 ++member) {
                Decl *contract_decl =
                    contract_owner->as.structure.members[member];
                Function *contract = &contract_decl->as.function;
                Decl *implementation_decl = NULL;
                for (Decl *cursor = owner;
                     cursor != NULL && implementation_decl == NULL;
                     cursor = cursor->as.structure.base_class)
                    for (size_t candidate = 0U;
                         candidate < cursor->as.structure.member_count;
                         ++candidate) {
                        Decl *candidate_decl =
                            cursor->as.structure.members[candidate];
                        Function *implementation =
                            &candidate_decl->as.function;
                        if (implementation->is_static_member ||
                            implementation->is_abstract_member ||
                            !candidate_decl->is_public ||
                            !member_signatures_match(
                                implementation, contract))
                            continue;
                        implementation_decl = candidate_decl;
                        break;
                    }
                if (implementation_decl == NULL) {
                    if (!owner->as.structure.is_abstract)
                        lang_diag(
                            diagnostics, owner->span,
                            "class `%s` does not implement interface member `%s.%s`",
                            owner->as.structure.name,
                            contract_owner->as.structure.name,
                            member_short_name(contract));
                    continue;
                }
                size_t output = owner->as.structure
                    .interface_implementation_count++;
                owner->as.structure.interface_members[output] =
                    contract_decl;
                owner->as.structure.interface_implementations[output] =
                    implementation_decl;
            }
        }
        free(stack);
        free(seen);
    }
    for (size_t i = 0U; i < module->count; ++i) {
        Decl *owner = module->decls[i];
        if (owner->kind != DECL_CLASS ||
            owner->as.structure.is_abstract)
            continue;
        for (Decl *base = owner->as.structure.base_class;
             base != NULL; base = base->as.structure.base_class) {
            for (size_t m = 0U; m < base->as.structure.member_count; ++m) {
                Decl *abstract_decl = base->as.structure.members[m];
                Function *abstract_member = &abstract_decl->as.function;
                if (!abstract_member->is_abstract_member) continue;
                const Decl *root = abstract_member->virtual_root_decl != NULL
                    ? abstract_member->virtual_root_decl : abstract_decl;
                bool implemented = false;
                for (Decl *cursor = owner;
                     cursor != NULL && !implemented;
                     cursor = cursor->as.structure.base_class)
                    for (size_t candidate = 0U;
                         candidate < cursor->as.structure.member_count;
                         ++candidate) {
                        Function *method = &cursor->as.structure
                            .members[candidate]->as.function;
                        const Decl *method_root =
                            method->virtual_root_decl != NULL
                                ? method->virtual_root_decl
                                : cursor->as.structure.members[candidate];
                        if (method_root == root &&
                            !method->is_abstract_member) {
                            implemented = true;
                            break;
                        }
                    }
                if (!implemented)
                    lang_diag(
                        diagnostics, owner->span,
                        "non-abstract class `%s` does not implement abstract member `%s`",
                        owner->as.structure.name, abstract_member->name);
            }
        }
    }
    for (size_t i = 0U; i < module->count; ++i) {
        if (module->decls[i]->kind != DECL_FUNCTION) continue;
        if (module->decls[i]->type_param_count != 0U) continue;
        Function *function = &module->decls[i]->as.function;
        for (size_t j = i + 1U; j < module->count; ++j) {
            Decl *candidate_decl = module->decls[j];
            if (candidate_decl->kind != DECL_FUNCTION ||
                candidate_decl->type_param_count != 0U ||
                function->span.file == NULL ||
                candidate_decl->as.function.span.file == NULL ||
                module->decls[i]->module_name == NULL ||
                candidate_decl->module_name == NULL ||
                strcmp(module->decls[i]->module_name,
                       candidate_decl->module_name) != 0 ||
                strcmp(function->name,
                       candidate_decl->as.function.name) != 0)
                continue;
            Function *candidate = &candidate_decl->as.function;
            if (function->param_count != candidate->param_count)
                continue;
            bool same_signature = true;
            for (size_t p = 0U; p < function->param_count; ++p) {
                if (parameter_mode_from_param(&function->params[p]) !=
                        parameter_mode_from_param(&candidate->params[p]) ||
                    !same_type(function->params[p].checked_type,
                               candidate->params[p].checked_type)) {
                    same_signature = false;
                    break;
                }
            }
            if (same_signature) {
                LangDiagnostic *diagnostic = lang_diag(
                    diagnostics, candidate_decl->span,
                    "duplicate overload signature for `%s`",
                    function->name);
                lang_diag_secondary(
                    diagnostic, module->decls[i]->span,
                    "first declaration with this signature");
            }
        }
        Checker checker;
        memset(&checker, 0, sizeof(checker));
        checker.module = module;
        checker.diagnostics = diagnostics;
        checker.function = function;
        checker.current_module = module->decls[i]->module_name;
        if (module->decls[i]->generic_origin != NULL) {
            checker.substitution_decl =
                module->decls[i]->generic_origin;
            checker.substitution_arguments =
                module->decls[i]->generic_arguments;
            checker.substitution_argument_count =
                module->decls[i]->generic_argument_count;
        }
        function->checked_return_type = resolve_declared_type(
            &checker, function->return_type_syntax,
            function->return_type, function->span);
        if (function->is_constructor) {
            const Decl *structure =
                function->checked_return_type->declaration;
            size_t field_count = structure != NULL &&
                (structure->kind == DECL_STRUCT ||
                 structure->kind == DECL_CLASS)
                ? structure->as.structure.field_count : 0U;
            function->constructor_field_count = field_count;
            function->constructor_field_binding_ids = lang_arena_alloc(
                &module->arena, field_count * sizeof(size_t));
            function->constructor_field_types = lang_arena_alloc(
                &module->arena, field_count * sizeof(Type *));
            for (size_t field = 0U; field < field_count; ++field) {
                FieldDecl *declaration =
                    &structure->as.structure.fields[field];
                Type *field_type = resolve_type_syntax_in_applied_declaration(
                    &checker, function->checked_return_type,
                    declaration->type_syntax, declaration->type_name,
                    declaration->span);
                size_t binding = ++checker.next_local_id;
                function->constructor_field_binding_ids[field] = binding;
                function->constructor_field_types[field] = field_type;
                checker.locals[checker.local_count++] = (Local){
                    declaration->name, field_type,
                    true, false, 0U, declaration->span,
                    binding, true, false, false, {0}
                };
            }
        }
        if (function->is_deleted && !function->is_copy_constructor)
            lang_diag(diagnostics, function->span,
                      "only a copy constructor may be declared `= delete`");
        if (function->is_copy_constructor && !function->is_deleted &&
            !module->decls[i]->is_public)
            lang_diag(diagnostics, function->span,
                      "a custom copy constructor must be public");
        if (function->is_async && function->is_extern)
            lang_diag(diagnostics, function->span,
                      "extern functions cannot be declared async");
        if (function->is_async &&
            function->checked_return_type->kind != TYPE_TASK)
            lang_diag(diagnostics, function->span,
                      "async function `%s` must return Task or Task<T>",
                      function->name);
        if (function->is_async)
            for (size_t parameter = 0U;
                 parameter < function->param_count; ++parameter)
                if (function->params[parameter].by_ref ||
                    function->params[parameter].by_const_ref ||
                    function->params[parameter].by_out)
                    lang_diag(
                        diagnostics,
                        function->params[parameter].span,
                        "async functions cannot have ref, const ref, or out parameters");
        for (size_t j = 0U; j < function->param_count; ++j) {
            Type *type = resolve_declared_type(
                &checker, function->params[j].type_syntax,
                function->params[j].type_name,
                function->params[j].span);
            function->params[j].checked_type = type;
            /* Instance receivers for value types are borrowed. Class
             * receivers remain ordinary pointer values in the ABI. An
             * async value receiver is consumed into the task frame because
             * a borrow cannot safely outlive the initiating call. */
            bool implicit_receiver = function_parameter_is_receiver(
                function, j, type);
            function->params[j].borrowed =
                function->params[j].by_ref ||
                function->params[j].by_const_ref ||
                (implicit_receiver && !function->is_async);
            function->params[j].binding_id = ++checker.next_local_id;
            checker.locals[checker.local_count++] = (Local){
                function->params[j].name, type,
                function->params[j].mutable_,
                function->params[j].borrowed, 0U,
                function->params[j].span,
                function->params[j].binding_id,
                function->params[j].by_out,
                !function->params[j].by_out,
                !function->params[j].by_out, {0}
            };
        }
        if (function->is_extern || function->is_abstract_member ||
            function->is_deleted) {
            function->local_count = function->param_count;
            continue;
        }
        if (function->is_drop &&
            (function->param_count != 1U ||
             (function->params[0].checked_type->kind != TYPE_NAMED &&
              function->params[0].checked_type->kind != TYPE_CLASS)))
            lang_diag(diagnostics, function->span,
                      "a destructor must target one user-defined struct, class, or enum");
        bool falls_through = check_stmt(&checker, function->body);
        function->local_count = checker.local_count + 64U;
        Type *logical_return =
            function->is_async &&
            function->checked_return_type->kind == TYPE_TASK
                ? function->checked_return_type->element
                : function->checked_return_type;
        if (logical_return->kind != TYPE_UNIT && falls_through &&
            !function->is_constructor)
            lang_diag(diagnostics, function->span,
                      "function `%s` may exit without returning `%s`",
                      function->name, logical_return->name);
        if (falls_through)
            require_assigned_out_parameters(&checker, function->span);
    }
    bool found_main = false;
    for (size_t i = 0U; i < module->count; ++i)
        if (module->decls[i]->kind == DECL_FUNCTION &&
            module->decls[i]->type_param_count == 0U &&
            !module->decls[i]->as.function.is_extern &&
            strcmp(module->decls[i]->as.function.name, "main") == 0 &&
            module->entry_module != NULL &&
            module->decls[i]->module_name != NULL &&
            strcmp(module->entry_module,
                   module->decls[i]->module_name) == 0)
            found_main = true;
    if (module->require_entrypoint && !found_main) {
        LangSpan span = {module->source->path, 0U, 0U};
        lang_diag(diagnostics, span, "module has no `main` function");
    }
    return diagnostics->count == 0U;
}
