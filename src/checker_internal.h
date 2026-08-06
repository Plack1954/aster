#ifndef ASTER_CHECKER_INTERNAL_H
#define ASTER_CHECKER_INTERNAL_H

#include "internal.h"

typedef struct Local {
    const char *name;
    Type *type;
    bool mutable_;
    bool borrowed;
    unsigned depth;
    LangSpan declaration;
    size_t id;
    bool is_out_parameter;
    bool definitely_assigned;
} Local;

typedef struct Checker {
    Module *module;
    LangDiagnostics *diagnostics;
    Function *function;
    const char *current_module;
    Local locals[256];
    size_t local_count;
    size_t next_local_id;
    unsigned depth;
    unsigned loop_depth;
    size_t loop_local_bases[32];
    size_t exception_local_bases[32];
    unsigned exception_depth;
    unsigned finally_depth;
    const char *catch_names[32];
    unsigned catch_depth;
    unsigned unsafe_depth;
    Type *expected_type;
    const char *resolving_aliases[64];
    size_t resolving_alias_count;
    const Decl *substitution_decl;
    Type **substitution_arguments;
    size_t substitution_argument_count;
    size_t generic_instantiation_depth;
    bool html_interpolation_destination;
    const Expr *allowed_unassigned_out_place;
} Checker;

extern Type type_error;
extern Type type_unit;
extern Type type_never;
extern Type type_bool;
extern Type type_i8;
extern Type type_i16;
extern Type type_i32;
extern Type type_i64;
extern Type type_u8;
extern Type type_u16;
extern Type type_u32;
extern Type type_u64;
extern Type type_isize;
extern Type type_usize;
extern Type type_f32;
extern Type type_f64;
extern Type type_char;
extern Type type_string;
extern Type type_exception;
extern Type type_format_exception;
extern Type type_overflow_exception;
extern Type type_argument_exception;
extern Type type_invalid_operation_exception;
extern Type type_io_exception;
extern Type type_json_exception;
extern Type type_sqlite_exception;
extern Type type_operation_canceled_exception;
extern Type type_task_canceled_exception;
extern Type type_string_builder;
extern Type type_url;
extern Type type_html;
extern Type type_buffer;
extern Type type_arena;
extern Type type_native_handle;
extern Type type_cancellation_token;
extern Type type_cancellation_token_source;
extern Type type_raw_pointer;
extern Type type_u8_slice;

bool is_signed_integer(const Type *type);
bool is_unsigned_integer(const Type *type);
bool is_integer(const Type *type);
bool is_float(const Type *type);
bool is_numeric(const Type *type);
bool is_exception_type(const Type *type);
const char *type_declaration_name(const Decl *decl);
const char *last_path_separator(const char *path);
bool imported_declaration_matches(const Checker *checker,
                                  const char *use_name,
                                  const char *declaration_name,
                                  const char *declaration_module);
bool visible_declaration_path_matches(const Checker *checker,
                                      const char *use_name,
                                      const char *declaration_name,
                                      const char *declaration_module);
Decl *find_type_declaration(Checker *checker, const char *name,
                            LangSpan use_span);
bool split_generic_application(Checker *checker, const char *name,
                               char **out_base, char ***out_arguments,
                               size_t *out_count);
Type *resolve_type_in_applied_declaration(Checker *checker,
                                         const Type *applied,
                                         const char *name, LangSpan span);
Type *resolve_type_syntax_in_applied_declaration(
    Checker *checker, const Type *applied, const TypeSyntax *syntax,
    const char *fallback_name, LangSpan span);
Type *resolve_type(Checker *checker, const char *name, LangSpan span);
Type *resolve_type_syntax(Checker *checker, const TypeSyntax *syntax);
Type *resolve_declared_type(Checker *checker, const TypeSyntax *syntax,
                            const char *fallback_name, LangSpan span);
bool same_type(const Type *a, const Type *b);
bool type_assignable(const Type *expected, const Type *actual);
const char *type_display_name(Checker *checker, const Type *type);
bool coerce_literal(Checker *checker, Expr *expr, Type *expected);
bool type_is_copyable(Checker *checker, Type *type);
const Decl *type_copy_constructor(const Type *type);

Local *find_local(Checker *checker, const char *name);
Function *find_function(Checker *checker, const char *name,
                        LangSpan use_span);
const Decl *function_declaration(const Checker *checker,
                                 const Function *function);
const char *function_module_name(const Checker *checker,
                                 const Function *function);
Type *resolve_type_in_module(Checker *checker, const char *name,
                             LangSpan span, const char *module_name);
Type *resolve_declared_type_in_module(
    Checker *checker, const TypeSyntax *syntax, const char *fallback_name,
    LangSpan span, const char *module_name);
Type *check_place(Checker *checker, Expr *expr);
Type *checker_check_name(Checker *checker, Expr *expr);
FieldDecl *checker_static_field_from_path(
    Checker *checker, const char *path, const Decl **out_owner);
void checker_rewrite_unqualified_static_field(
    Checker *checker, Expr *expr);
const char *checker_static_call_path(Checker *checker, Expr *expr);
Type *checker_check_call(Checker *checker, Expr *expr);
Type *check_expr(Checker *checker, Expr *expr);
bool check_stmt(Checker *checker, Stmt *stmt);
Type *check_element(Checker *checker, Expr *expr);
Type *check_generic_call(Checker *checker, Expr *expr,
                         const Decl *template_decl);

#endif
