#include "internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static bool initialize_source(LangSource *source, const char *path,
                              const char *text) {
    size_t path_length = strlen(path);
    size_t text_length = strlen(text);
    source->path = malloc(path_length + 1U);
    source->text = malloc(text_length + 1U);
    if (source->path == NULL || source->text == NULL) {
        free(source->path);
        free(source->text);
        memset(source, 0, sizeof(*source));
        return false;
    }
    memcpy(source->path, path, path_length + 1U);
    memcpy(source->text, text, text_length + 1U);
    source->length = text_length;
    return true;
}

static const Decl *find_alias(const Module *module, const char *name) {
    for (size_t i = 0U; i < module->count; ++i)
        if (module->decls[i]->kind == DECL_ALIAS &&
            strcmp(module->decls[i]->as.alias.name, name) == 0)
            return module->decls[i];
    return NULL;
}

static bool valid_shapes(const Module *module) {
    const Decl *nested = find_alias(module, "Nested");
    const Decl *generic_function = find_alias(module, "GenericFunction");
    const Decl *array_function = find_alias(module, "ArrayFunction");
    const Decl *array_generic = find_alias(module, "ArrayGeneric");
    const Decl *nested_function = find_alias(module, "NestedFunction");
    const Decl *shift_boundary = find_alias(module, "ShiftBoundary");
    if (nested == NULL || generic_function == NULL ||
        array_function == NULL || array_generic == NULL ||
        nested_function == NULL ||
        shift_boundary == NULL)
        return false;
    const TypeSyntax *nested_syntax = nested->as.alias.target_syntax;
    const TypeSyntax *generic_function_syntax =
        generic_function->as.alias.target_syntax;
    const TypeSyntax *array_syntax = array_function->as.alias.target_syntax;
    const TypeSyntax *array_generic_syntax =
        array_generic->as.alias.target_syntax;
    const TypeSyntax *function_syntax = nested_function->as.alias.target_syntax;
    return nested_syntax != NULL &&
        nested_syntax->kind == TYPE_SYNTAX_GENERIC &&
        nested_syntax->as.generic.argument_count == 1U &&
        nested_syntax->as.generic.arguments[0]->kind == TYPE_SYNTAX_GENERIC &&
        generic_function_syntax != NULL &&
        generic_function_syntax->kind == TYPE_SYNTAX_GENERIC &&
        generic_function_syntax->as.generic.arguments[0]->kind ==
            TYPE_SYNTAX_FUNCTION &&
        array_syntax != NULL && array_syntax->kind == TYPE_SYNTAX_ARRAY &&
        array_syntax->as.array.count == 32U &&
        array_syntax->as.array.element->kind == TYPE_SYNTAX_FUNCTION &&
        array_generic_syntax != NULL &&
        array_generic_syntax->kind == TYPE_SYNTAX_ARRAY &&
        array_generic_syntax->as.array.element->kind ==
            TYPE_SYNTAX_GENERIC &&
        function_syntax != NULL &&
        function_syntax->kind == TYPE_SYNTAX_FUNCTION &&
        function_syntax->as.function.parameters[0]->kind ==
            TYPE_SYNTAX_FUNCTION &&
        function_syntax->as.function.return_type->kind ==
            TYPE_SYNTAX_FUNCTION &&
        shift_boundary->as.alias.target_syntax->span.end >
            shift_boundary->as.alias.target_syntax->span.start;
}

int main(void) {
    static const char valid_text[] =
        "type Nested = Option<Result<int,string>>;\n"
        "type GenericFunction = Option<fn(ref int,out string)->bool>;\n"
        "type ArrayFunction = [fn(int)->bool;32];\n"
        "type ArrayGeneric = [Option<Result<int,string>>;8];\n"
        "type NestedFunction = fn(fn(int)->bool)->fn(string)->bool;\n"
        "type ShiftBoundary = Option<Result<int,Option<string>>>;\n"
        "private Nested KeepNested(Nested value) { return value; }\n"
        "private GenericFunction KeepGenericFunction(GenericFunction value) { return value; }\n"
        "private ArrayFunction KeepArrayFunction(ArrayFunction value) { return value; }\n"
        "private ArrayGeneric KeepArrayGeneric(ArrayGeneric value) { return value; }\n"
        "private NestedFunction KeepNestedFunction(NestedFunction value) { return value; }\n"
        "private ShiftBoundary KeepShiftBoundary(ShiftBoundary value) { return value; }\n";
    LangSource source = {0};
    if (!initialize_source(&source, "<type-syntax>", valid_text)) return 1;
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool parsed = lang_parse_module(&source, &diagnostics, &module);
    module.require_entrypoint = false;
    bool checked = parsed && lang_check_module(&module, &diagnostics);
    bool valid = checked && diagnostics.count == 0U && valid_shapes(&module);
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    if (!valid) return 2;

    static const char tuple_text[] =
        "type TupleSyntax = (int,string);\n";
    if (!initialize_source(&source, "<tuple-type-syntax>", tuple_text))
        return 3;
    lang_diagnostics_init(&diagnostics);
    parsed = lang_parse_module(&source, &diagnostics, &module);
    const Decl *tuple = find_alias(&module, "TupleSyntax");
    bool tuple_valid = parsed && diagnostics.count == 0U && tuple != NULL &&
        tuple->as.alias.target_syntax != NULL &&
        tuple->as.alias.target_syntax->kind == TYPE_SYNTAX_TUPLE &&
        tuple->as.alias.target_syntax->as.tuple.element_count == 2U;
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    if (!tuple_valid) return 4;

    static const char malformed_text[] =
        "type Broken = Option<Result<int,string>;\n"
        "type Recovered = int;\n";
    if (!initialize_source(
            &source, "<type-syntax-recovery>", malformed_text))
        return 5;
    lang_diagnostics_init(&diagnostics);
    parsed = lang_parse_module(&source, &diagnostics, &module);
    bool recovered = !parsed && diagnostics.count != 0U &&
        find_alias(&module, "Recovered") != NULL;
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    return recovered ? 0 : 6;
}
