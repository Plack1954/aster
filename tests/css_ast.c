#include "internal.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const Expr *find_style_expr(const Stmt *statement) {
    if (statement == NULL) return NULL;
    if (statement->kind == STMT_LET && statement->as.let.value != NULL &&
        statement->as.let.value->kind == EXPR_ELEMENT &&
        strcmp(statement->as.let.value->as.element.name, "style") == 0)
        return statement->as.let.value;
    if (statement->kind == STMT_BLOCK)
        for (size_t i = 0U; i < statement->as.block.count; ++i) {
            const Expr *found = find_style_expr(statement->as.block.items[i]);
            if (found != NULL) return found;
        }
    return NULL;
}

static int check_css_files(int count, char **paths) {
    for (int i = 0; i < count; ++i) {
        LangSource source;
        if (!lang_source_load(paths[i], &source)) return 10;
        LangDiagnostics diagnostics;
        lang_diagnostics_init(&diagnostics);
        LangArena arena;
        lang_arena_init(&arena);
        CssStylesheet *sheet = NULL;
        bool parsed = lang_css_parse(
            &source, 0U, source.length, &arena, &diagnostics, &sheet);
        if (!parsed) lang_diagnostics_print(&source, &diagnostics, stderr);
        bool valid = parsed && sheet != NULL &&
            sheet->span.start == 0U && sheet->span.end == source.length;
        lang_arena_free(&arena);
        lang_diagnostics_free(&diagnostics);
        lang_source_free(&source);
        if (!valid) return 11;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1) return check_css_files(argc - 1, argv + 1);
    LangSource source;
    if (!lang_source_load("tests/native_css.as", &source)) return 1;
    LangDiagnostics diagnostics;
    lang_diagnostics_init(&diagnostics);
    Module module;
    bool parsed = lang_parse_module(&source, &diagnostics, &module);
    const Expr *style = NULL;
    if (parsed)
        for (size_t i = 0U; i < module.count && style == NULL; ++i)
            if (module.decls[i]->kind == DECL_FUNCTION)
                style = find_style_expr(module.decls[i]->as.function.body);
    bool valid = style != NULL && style->as.element.css != NULL &&
        style->as.element.css->child_count == 8U &&
        style->as.element.css->children[0].kind == CSS_STYLE_RULE &&
        style->as.element.css->children[0].child_count == 2U &&
        style->as.element.css->children[2].kind == CSS_AT_RULE &&
        style->as.element.css->children[3].kind == CSS_AT_RULE;
    lang_module_free(&module);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&source);
    if (!parsed || !valid) return 2;

    static char compatibility_text[] =
        "<!-- @future top-level(value); -->\n"
        "@media (width > 1px) { @future nested(value) }\n"
        "@final-at-rule without-a-semicolon";
    static char compatibility_path[] = "<css-compatibility>";
    LangSource compatibility = {
        .text=compatibility_text,
        .length=sizeof(compatibility_text) - 1U,
        .path=compatibility_path
    };
    LangArena compatibility_arena;
    lang_arena_init(&compatibility_arena);
    lang_diagnostics_init(&diagnostics);
    CssStylesheet *compatibility_sheet = NULL;
    bool compatibility_parsed = lang_css_parse(
        &compatibility, 0U, compatibility.length,
        &compatibility_arena, &diagnostics, &compatibility_sheet);
    bool compatibility_valid = compatibility_parsed &&
        compatibility_sheet != NULL &&
        compatibility_sheet->child_count == 3U &&
        compatibility_sheet->children[0].kind == CSS_AT_RULE &&
        compatibility_sheet->children[1].child_count == 1U &&
        compatibility_sheet->children[2].kind == CSS_AT_RULE;
    lang_arena_free(&compatibility_arena);
    lang_diagnostics_free(&diagnostics);
    if (!compatibility_valid) return 3;

    static const char prefix[] = ".stress{";
    static const char declaration[] =
        "future-property: unknown(value);";
    static const char suffix[] = "}";
    const size_t declaration_count = 8192U;
    const size_t text_length = sizeof(prefix) - 1U +
        declaration_count * (sizeof(declaration) - 1U) +
        sizeof(suffix) - 1U;
    char *text = malloc(text_length + 1U);
    char *path = malloc(sizeof("<css-stress>"));
    if (text == NULL || path == NULL) {
        free(text);
        free(path);
        return 4;
    }
    size_t offset = 0U;
    memcpy(text + offset, prefix, sizeof(prefix) - 1U);
    offset += sizeof(prefix) - 1U;
    for (size_t i = 0U; i < declaration_count; ++i) {
        memcpy(text + offset, declaration, sizeof(declaration) - 1U);
        offset += sizeof(declaration) - 1U;
    }
    memcpy(text + offset, suffix, sizeof(suffix));
    memcpy(path, "<css-stress>", sizeof("<css-stress>"));
    LangSource stress = {
        .text=text, .length=text_length, .path=path
    };
    LangArena arena;
    lang_arena_init(&arena);
    lang_diagnostics_init(&diagnostics);
    CssStylesheet *sheet = NULL;
    bool stress_parsed = lang_css_parse(
        &stress, 0U, stress.length, &arena, &diagnostics, &sheet);
    bool stress_valid = stress_parsed && sheet != NULL &&
        sheet->child_count == 1U &&
        sheet->children[0].child_count == declaration_count;
    lang_arena_free(&arena);
    lang_diagnostics_free(&diagnostics);
    lang_source_free(&stress);
    return stress_valid ? 0 : 5;
}
