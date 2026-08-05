#ifndef LANG_INTERNAL_H
#define LANG_INTERNAL_H

#include "lang/lang.h"

#include <stdarg.h>
#include <stdio.h>

typedef struct LangArenaBlock LangArenaBlock;
typedef struct LangArena {
    LangArenaBlock *head;
} LangArena;

void lang_arena_init(LangArena *arena);
void *lang_arena_alloc(LangArena *arena, size_t size);
void lang_arena_free(LangArena *arena);
char *lang_arena_strndup(LangArena *arena, const char *text, size_t length);
LangDiagnostic *lang_diag(LangDiagnostics *diagnostics, LangSpan span,
                          const char *format, ...);
void lang_diag_secondary(LangDiagnostic *diagnostic, LangSpan span,
                         const char *label);
void lang_diag_note(LangDiagnostic *diagnostic, const char *note);
void lang_diag_help(LangDiagnostic *diagnostic, const char *help);
const char *lang_source_path_at(const LangSource *source, size_t offset);
void lang_source_line_info(const LangSource *source, size_t offset,
                           size_t *line, size_t *column,
                           size_t *line_start, size_t *line_end);

typedef enum TokenKind {
    TOK_EOF, TOK_ERROR, TOK_IDENT, TOK_INT, TOK_FLOAT, TOK_STRING,
    TOK_DOLLAR,
    TOK_VAR, TOK_NEW, TOK_DELETE, TOK_CONST, TOK_REF, TOK_OUT,
    TOK_STRUCT, TOK_CLASS, TOK_ENUM, TOK_UNION, TOK_IF, TOK_ELSE,
    TOK_WHILE, TOK_FOR, TOK_FOREACH, TOK_IN, TOK_MATCH, TOK_CASE,
    TOK_RETURN, TOK_BREAK,
    TOK_CONTINUE,
    TOK_TRUE, TOK_FALSE, TOK_NULL,
    TOK_UNSAFE, TOK_TRY, TOK_CATCH, TOK_FINALLY, TOK_THROW,
    TOK_ASYNC, TOK_AWAIT,
    TOK_NAMESPACE, TOK_USING, TOK_AS, TOK_PUB, TOK_PRIVATE, TOK_STATIC,
    TOK_READONLY, TOK_EXTERN,
    TOK_TYPE,
    TOK_DELEGATE,
    TOK_ELEMENT,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_COLON, TOK_SEMICOLON, TOK_DOT, TOK_DOT_DOT,
    TOK_DOT_DOT_EQUAL,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_PLUS_PLUS, TOK_MINUS_MINUS,
    TOK_PLUS_EQUAL, TOK_MINUS_EQUAL, TOK_STAR_EQUAL,
    TOK_SLASH_EQUAL, TOK_PERCENT_EQUAL,
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE,
    TOK_AMP_EQUAL, TOK_PIPE_EQUAL, TOK_CARET_EQUAL,
    TOK_SHIFT_LEFT_EQUAL, TOK_SHIFT_RIGHT_EQUAL,
    TOK_AND_AND, TOK_OR_OR,
    TOK_EQUAL, TOK_EQUAL_EQUAL, TOK_BANG, TOK_BANG_EQUAL, TOK_QUESTION,
    TOK_LESS, TOK_LESS_EQUAL, TOK_GREATER, TOK_GREATER_EQUAL,
    TOK_SHIFT_LEFT, TOK_SHIFT_RIGHT,
    TOK_ARROW, TOK_FAT_ARROW
} TokenKind;

typedef struct Token {
    TokenKind kind;
    LangSpan span;
    const char *start; /* Borrowed from source. */
    size_t length;
} Token;

typedef struct Lexer {
    const LangSource *source;
    LangDiagnostics *diagnostics;
    size_t offset;
    bool interpolation_pending;
} Lexer;

void lang_lexer_init(Lexer *lexer, const LangSource *source,
                     LangDiagnostics *diagnostics);
Token lang_lexer_next(Lexer *lexer);
const char *lang_token_name(TokenKind kind);
void lang_dump_tokens(const LangSource *source, LangDiagnostics *diagnostics);

typedef enum TypeKind {
    TYPE_ERROR, TYPE_UNIT, TYPE_NEVER, TYPE_BOOL,
    TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64, TYPE_ISIZE, TYPE_USIZE,
    TYPE_F32, TYPE_F64, TYPE_CHAR,
    TYPE_STR, TYPE_STRING, TYPE_STRING_BUILDER, TYPE_URL,
    TYPE_HTML, TYPE_BUFFER, TYPE_ARENA,
    TYPE_NATIVE_HANDLE, TYPE_CANCELLATION_TOKEN,
    TYPE_CANCELLATION_TOKEN_SOURCE,
    TYPE_RAW_POINTER, TYPE_SLICE, TYPE_READONLY_SPAN, TYPE_VEC,
    TYPE_DICTIONARY, TYPE_HASH_SET, TYPE_QUEUE, TYPE_STACK,
    TYPE_ARRAY, TYPE_OPTION, TYPE_RESULT, TYPE_TASK, TYPE_FUNCTION,
    TYPE_NAMED, TYPE_CLASS
} TypeKind;

typedef enum ParameterMode {
    PARAMETER_MODE_VALUE,
    PARAMETER_MODE_IMMUTABLE_REFERENCE,
    PARAMETER_MODE_MUTABLE_REFERENCE,
    PARAMETER_MODE_OUT
} ParameterMode;

typedef enum TypeSyntaxKind {
    TYPE_SYNTAX_NAMED,
    TYPE_SYNTAX_GENERIC,
    TYPE_SYNTAX_FUNCTION,
    TYPE_SYNTAX_POINTER,
    TYPE_SYNTAX_ARRAY,
    TYPE_SYNTAX_TUPLE,
    TYPE_SYNTAX_ERROR
} TypeSyntaxKind;

typedef struct TypeSyntax TypeSyntax;
/* Arena-backed source structure. Semantic resolution must consume this tree;
 * canonical Type.name strings are derived output, not a second type grammar. */
struct TypeSyntax {
    TypeSyntaxKind kind;
    LangSpan span;
    union {
        const char *name;
        struct {
            TypeSyntax *base;
            TypeSyntax **arguments;
            size_t argument_count;
        } generic;
        struct {
            TypeSyntax **parameters;
            ParameterMode *parameter_modes;
            size_t parameter_count;
            TypeSyntax *return_type;
        } function;
        struct {
            bool mutable_;
            TypeSyntax *element;
        } pointer;
        struct {
            TypeSyntax *element;
            size_t count;
        } array;
        struct {
            TypeSyntax **elements;
            size_t element_count;
        } tuple;
    } as;
};

static inline bool parameter_mode_is_reference(ParameterMode mode) {
    return mode != PARAMETER_MODE_VALUE;
}

static inline bool parameter_mode_is_mutable(ParameterMode mode) {
    return mode == PARAMETER_MODE_MUTABLE_REFERENCE ||
           mode == PARAMETER_MODE_OUT;
}

struct Decl;
typedef struct Type {
    TypeKind kind;
    const char *name; /* Arena-backed or static. */
    const struct Decl *declaration; /* Named type identity; otherwise NULL. */
    struct Type *element;
    struct Type *error_type;
    struct Type **arguments; /* Applied named-type arguments. */
    size_t argument_count;
    /* Owned by the arena and parallel to arguments for function types. */
    ParameterMode *parameter_modes;
    size_t array_length;
    bool requires_cleanup;
    /* Copyable value whose copies share automatically managed storage. */
    bool managed;
    bool pointer_mutable; /* Meaningful for TYPE_RAW_POINTER. */
    bool instantiation_resolving;
} Type;

typedef enum ExprKind {
    EXPR_INT, EXPR_FLOAT, EXPR_STRING, EXPR_INTERPOLATION,
    EXPR_BOOL, EXPR_NULL, EXPR_NAME,
    EXPR_BINARY, EXPR_UNARY, EXPR_CALL, EXPR_ASSIGN,
    EXPR_CLONE, EXPR_TRY, EXPR_AWAIT, EXPR_CAST,
    EXPR_ARRAY, EXPR_INDEX, EXPR_FIELD,
    EXPR_STRUCT, EXPR_ELEMENT, EXPR_IF, EXPR_MATCH
} ExprKind;

typedef enum StmtKind {
    STMT_LET, STMT_EXPR, STMT_RETURN, STMT_IF, STMT_WHILE, STMT_FOR,
    STMT_C_FOR, STMT_MATCH, STMT_TRY, STMT_THROW,
    STMT_BREAK, STMT_CONTINUE, STMT_BLOCK, STMT_UNSAFE,
    STMT_DESTRUCTURE, STMT_DELETE
} StmtKind;

typedef struct Expr Expr;
typedef struct Stmt Stmt;

typedef enum CssNodeKind {
    CSS_STYLE_RULE,
    CSS_AT_RULE,
    CSS_DECLARATION
} CssNodeKind;

typedef struct CssNode CssNode;
struct CssNode {
    CssNodeKind kind;
    LangSpan span;
    LangSpan name;
    LangSpan value;
    CssNode *children;
    size_t child_count;
};

typedef struct CssStylesheet {
    LangSpan span;
    CssNode *children;
    size_t child_count;
} CssStylesheet;

bool lang_css_parse(const LangSource *source, size_t start, size_t end,
                    LangArena *arena, LangDiagnostics *diagnostics,
                    CssStylesheet **out_sheet);
bool lang_css_scope(const LangSource *source, const CssStylesheet *sheet,
                    const char *attribute, LangArena *arena,
                    const char **out_text, size_t *out_length);

typedef struct CleanupPlan {
    size_t *binding_ids; /* Arena-backed, in deterministic destruction order. */
    size_t count;
} CleanupPlan;

typedef struct ExprList {
    Expr **items;
    size_t count;
} ExprList;

typedef struct StmtList {
    Stmt **items;
    size_t count;
} StmtList;

typedef struct MatchArm {
    const char *variant;
    const char *binding;
    const char *binding_type_name;
    TypeSyntax *binding_type_syntax;
    size_t binding_id; /* Checker-assigned local identity; zero means none. */
    Type *binding_type;
    Stmt *body;
    LangSpan span;
} MatchArm;

typedef struct ElementProperty {
    const char *name;
    Expr *value;
    LangSpan span;
    bool borrow_interpolated_string;
    bool css_custom_property;
    const char *event_binding; /* Generated `event|handler|result|type:name...`. */
} ElementProperty;

typedef struct ElementBodyItem {
    bool is_statement;
    bool is_static_text;
    union { Stmt *statement; Expr *expression; } as;
} ElementBodyItem;

typedef struct InterpolationPart {
    const char *text;
    size_t text_length;
    Expr *expression; /* NULL for a literal text segment. */
    bool borrow_owned_string;
    LangSpan span;
} InterpolationPart;

struct Expr {
    ExprKind kind;
    LangSpan span;
    Type *type; /* Set by checker. */
    const struct Decl *resolved_decl; /* Typed-AST symbol target, if any. */
    size_t resolved_local_id; /* Checker-assigned binding; zero for non-locals. */
    bool borrow_html_string; /* Render an owned String place without consuming it. */
    CleanupPlan error_cleanup; /* Cleanup when this expression propagates. */
    union {
        uint64_t integer;
        double floating;
        bool boolean;
        struct { const char *data; size_t length; } string;
        struct {
            InterpolationPart *parts;
            size_t part_count;
        } interpolation;
        const char *name;
        struct { TokenKind op; Expr *left; Expr *right; } binary;
        struct { TokenKind op; Expr *operand; } unary;
        struct {
            Expr *callee;
            ExprList arguments;
            /* Arena-owned and parallel to arguments. */
            ParameterMode *argument_modes;
            bool implicit_receiver;
            bool implicit_enum_value;
        } call;
        struct {
            Expr *target;
            Expr *value;
            TokenKind compound_op; /* TOK_ERROR for plain `=`. */
        } assign;
        struct { Expr *value; } clone;
        struct { Expr *value; } try_;
        struct {
            Expr *value;
            const char *type_name;
            TypeSyntax *type_syntax;
        } cast;
        ExprList array;
        struct {
            Expr *object;
            Expr *index;
            bool unchecked; /* Written in unsafe context; VM still traps. */
        } index;
        struct { Expr *object; const char *field; } field;
        struct {
            const char *name;
            TypeSyntax *type_syntax;
            ElementProperty *fields;
            size_t field_count;
        } structure;
        struct { Expr *condition; Stmt *then_branch; Stmt *else_branch; } if_;
        struct { Expr *value; MatchArm *arms; size_t arm_count; } match_;
        struct {
            const char *name;
            ElementProperty *properties;
            size_t property_count;
            ElementBodyItem *body;
            size_t body_count;
            bool self_closing;
            bool css_scoped;
            const char *css_style_attribute;
            LangSpan open_span;
            LangSpan close_span;
            CssStylesheet *css;
        } element;
    } as;
};

struct Stmt {
    StmtKind kind;
    LangSpan span;
    bool expression_terminated;
    CleanupPlan exit_cleanup; /* Explicit cleanup for this statement's exit. */
    union {
        struct {
            const char *name;
            const char *type_name;
            TypeSyntax *type_syntax;
            Type *checked_type;
            bool mutable_;
            size_t binding_id; /* Checker-assigned local identity. */
            Expr *value;
        } let;
        struct {
            const char **names;
            const char **type_names;
            TypeSyntax **type_syntaxes;
            Type **checked_types;
            size_t *binding_ids;
            size_t count;
            Expr *value;
        } destructure;
        Expr *expression;
        Expr *return_value;
        struct { Expr *condition; Stmt *then_branch; Stmt *else_branch; } if_;
        struct { Expr *condition; Stmt *body; } while_;
        struct {
            const char *name;
            const char *type_name; /* Explicit only for `foreach`. */
            TypeSyntax *type_syntax;
            size_t binding_id; /* Checker-assigned iteration-local identity. */
            bool borrowed; /* Non-consuming iteration over a stable local. */
            bool foreach;
            Expr *iterable;
            Expr *range_end; /* Non-NULL for a half-open integer range. */
            Type *element_type; /* Checker-resolved iteration value type. */
            Stmt *body;
        } for_;
        struct {
            Stmt *initializer;
            Expr *condition;
            Expr *increment;
            Stmt *body;
        } c_for;
        struct { Expr *value; MatchArm *arms; size_t arm_count; } match_;
        struct {
            Stmt *body;
            const char *catch_type_name;
            TypeSyntax *catch_type_syntax;
            const char *catch_name;
            Type *catch_type;
            size_t catch_binding_id;
            Stmt *catch_body;
            Stmt *finally_body;
        } try_;
        Expr *throw_value;
        Expr *delete_value;
        Stmt *unsafe_body;
        StmtList block;
    } as;
};

typedef struct Param {
    const char *name;
    const char *type_name;
    TypeSyntax *type_syntax;
    bool borrowed;
    bool mutable_;
    bool by_ref;
    bool by_out;
    LangSpan span;
    Type *checked_type;
    size_t binding_id; /* Checker-assigned parameter-local identity. */
} Param;

static inline ParameterMode parameter_mode_from_param(const Param *param) {
    if (param->by_out) return PARAMETER_MODE_OUT;
    if (param->by_ref) return PARAMETER_MODE_MUTABLE_REFERENCE;
    if (param->borrowed) return PARAMETER_MODE_IMMUTABLE_REFERENCE;
    return PARAMETER_MODE_VALUE;
}

typedef struct Function {
    const char *name;
    Param *params;
    size_t param_count;
    const char *return_type;
    TypeSyntax *return_type_syntax;
    Stmt *body;
    LangSpan span;
    size_t local_count;
    Type *checked_return_type;
    bool is_extern;
    bool is_drop;
    bool is_async;
    bool is_static_member;
    bool is_readonly_member;
    bool is_property_getter;
    bool is_property_setter;
    const char *property_name;
    const char *property_backing_field;
    bool is_constructor;
    const char *owner_type;
    size_t *constructor_field_binding_ids;
    Type **constructor_field_types;
    size_t constructor_field_count;
    bool is_web_handler;
    const char *css_scope_attribute;
} Function;

typedef enum DeclKind {
    DECL_FUNCTION, DECL_STRUCT, DECL_CLASS, DECL_ENUM, DECL_ALIAS, DECL_ELEMENT
} DeclKind;
typedef struct FieldDecl {
    const char *name;
    const char *type_name;
    LangSpan span;
    TypeSyntax *type_syntax;
    bool is_public;
    bool has_explicit_visibility;
} FieldDecl;
typedef struct Decl {
    DeclKind kind;
    LangSpan span;
    const char *module_name; /* Arena-backed; declaration's source module. */
    bool is_public;
    bool has_explicit_visibility;
    const char **type_params; /* Generic parameters declared by this item. */
    size_t type_param_count;
    const struct Decl *generic_origin; /* Template for a function instance. */
    Type **generic_arguments; /* Concrete function type arguments. */
    size_t generic_argument_count;
    union {
        Function function;
        struct {
            const char *name;
            FieldDecl *fields;
            size_t field_count;
            struct Decl **members;
            size_t member_count;
            bool is_extern;
        } structure;
        struct {
            const char *name;
            FieldDecl *variants;
            size_t variant_count;
            bool is_union;
        } enumeration;
        struct {
            const char *name;
            const char *target;
            TypeSyntax *target_syntax;
        } alias;
        struct {
            const char *name;
            const char *result_type;
            TypeSyntax *result_type_syntax;
            FieldDecl *properties;
            size_t property_count;
        } element;
    } as;
} Decl;

typedef struct ImportItem {
    const char *name;
    const char *alias; /* Arena-backed; NULL keeps `name`. */
    LangSpan span;
} ImportItem;

typedef struct ImportDecl {
    const char *owner_module;
    const char *module_path;
    const char *alias; /* Module alias; NULL for an unaliased import. */
    ImportItem *items;
    size_t item_count; /* Zero imports the complete public module surface. */
    LangSpan span;
} ImportDecl;

typedef struct Module {
    Decl **decls;
    size_t count;
    ImportDecl *imports;
    size_t import_count;
    Type **type_instantiations; /* Canonical applied named types. */
    size_t type_instantiation_count;
    LangArena arena;
    const LangSource *source;
    const char *entry_module; /* Arena-backed root module name. */
    bool require_entrypoint;
    bool strict_imports; /* Project mode requires exact module-to-file identity. */
} Module;

bool lang_parse_module(const LangSource *source, LangDiagnostics *diagnostics,
                       Module *module);
void lang_module_free(Module *module);
void lang_dump_ast(const Module *module);
bool lang_check_module(Module *module, LangDiagnostics *diagnostics);
void lang_dump_types(const Module *module);
void lang_dump_layout(Module *module, const LangTargetInfo *target);

typedef uint32_t IrTypeId;
typedef uint32_t IrValueId;
typedef uint32_t IrBlockId;
typedef uint32_t IrFunctionId;

#define IR_INVALID_ID UINT32_MAX

typedef enum IrTypeShape {
    IR_TYPE_ERROR,
    IR_TYPE_UNIT,
    IR_TYPE_NEVER,
    IR_TYPE_BOOL,
    IR_TYPE_SIGNED_INT,
    IR_TYPE_UNSIGNED_INT,
    IR_TYPE_FLOAT,
    IR_TYPE_CHAR,
    IR_TYPE_STRING_VIEW,
    IR_TYPE_BUILTIN_OBJECT,
    IR_TYPE_ARRAY,
    IR_TYPE_RAW_POINTER,
    IR_TYPE_SLICE,
    IR_TYPE_ITERATOR,
    IR_TYPE_ELEMENT_BUILDER,
    IR_TYPE_FUNCTION,
    IR_TYPE_STRUCT,
    IR_TYPE_CLASS_REFERENCE,
    IR_TYPE_ENUM,
    IR_TYPE_UNION
} IrTypeShape;

typedef enum IrCopyPolicy {
    IR_COPY_TRIVIAL,
    IR_COPY_DEEP,
    IR_COPY_SHARED_RETAIN,
    IR_COPY_NONCOPYABLE,
    IR_COPY_CUSTOM
} IrCopyPolicy;

typedef enum IrDropPolicy {
    IR_DROP_TRIVIAL,
    IR_DROP_RECURSIVE,
    IR_DROP_CUSTOM
} IrDropPolicy;

typedef enum IrCallingConvention {
    IR_CALLING_CONVENTION_ASTER,
    IR_CALLING_CONVENTION_NATIVE
} IrCallingConvention;

typedef struct IrParameter {
    const char *name;
    IrTypeId type;
    ParameterMode mode;
    LangSpan span;
} IrParameter;

typedef struct IrFunctionAbi {
    IrCallingConvention calling_convention;
    bool may_propagate_exception;
    bool returns_async_task;
} IrFunctionAbi;

typedef struct IrStaticCss {
    const char *scope_attribute;
    const char *text;
    size_t text_length;
    LangSpan span;
} IrStaticCss;

typedef struct IrNativeCallDescriptor {
    const char *name;
    IrTypeId return_type;
    IrTypeId *parameter_types;
    ParameterMode *parameter_modes;
    size_t parameter_count;
    IrCallingConvention calling_convention;
    bool may_propagate_exception;
    bool compiler_generated;
} IrNativeCallDescriptor;

typedef enum IrOpcode {
    IR_OP_PARAMETER,
    IR_OP_UNIT,
    IR_OP_CONST_BOOL,
    IR_OP_CONST_INT,
    IR_OP_CONST_FLOAT,
    IR_OP_CONST_STRING,
    IR_OP_CONST_NULL,
    IR_OP_LOCAL_LOAD,
    IR_OP_LOCAL_MOVE,
    IR_OP_LOCAL_STORE,
    IR_OP_LOCAL_DROP,
    IR_OP_VALUE_CLONE,
    IR_OP_VALUE_DISCARD,
    IR_OP_ADD_CHECKED,
    IR_OP_SUB_CHECKED,
    IR_OP_MUL_CHECKED,
    IR_OP_DIV_CHECKED,
    IR_OP_REM_CHECKED,
    IR_OP_SHIFT_LEFT_CHECKED,
    IR_OP_SHIFT_RIGHT_CHECKED,
    IR_OP_BIT_AND,
    IR_OP_BIT_OR,
    IR_OP_BIT_XOR,
    IR_OP_BIT_NOT,
    IR_OP_ADD_FLOAT,
    IR_OP_SUB_FLOAT,
    IR_OP_MUL_FLOAT,
    IR_OP_DIV_FLOAT,
    IR_OP_NEGATE,
    IR_OP_NOT,
    IR_OP_EQUAL,
    IR_OP_NOT_EQUAL,
    IR_OP_LESS,
    IR_OP_LESS_EQUAL,
    IR_OP_GREATER,
    IR_OP_GREATER_EQUAL,
    IR_OP_CAST,
    IR_OP_FUNCTION_REF,
    IR_OP_CALL_DIRECT,
    IR_OP_CALL_INDIRECT,
    IR_OP_CALL_NATIVE,
    IR_OP_AWAIT,
    IR_OP_EXCEPTION_SET,
    IR_OP_EXCEPTION_PENDING,
    IR_OP_EXCEPTION_MATCH,
    IR_OP_EXCEPTION_TAKE,
    IR_OP_AGGREGATE_MAKE,
    IR_OP_FIELD_GET,
    IR_OP_FIELD_SET,
    IR_OP_LOCAL_FIELD_GET,
    IR_OP_LOCAL_FIELD_MOVE,
    IR_OP_LOCAL_FIELD_BORROW,
    IR_OP_LOCAL_FIELD_SET,
    IR_OP_INDEX_GET,
    IR_OP_INDEX_SET,
    IR_OP_LOCAL_INDEX_GET,
    IR_OP_LOCAL_INDEX_SET,
    IR_OP_LOCAL_ENUM_IS,
    IR_OP_LOCAL_ENUM_PAYLOAD_MOVE,
    IR_OP_ITERATOR_BEGIN,
    IR_OP_BORROWED_ITERATOR_BEGIN,
    IR_OP_LOCAL_ITERATOR_HAS_NEXT,
    IR_OP_LOCAL_ITERATOR_NEXT,
    IR_OP_RAW_ALLOC,
    IR_OP_RAW_LOAD,
    IR_OP_RAW_STORE,
    IR_OP_CLASS_DELETE,
    IR_OP_ELEMENT_BEGIN,
    IR_OP_LOCAL_ELEMENT_PROPERTY,
    IR_OP_LOCAL_ELEMENT_PROPERTY_BEGIN,
    IR_OP_LOCAL_ELEMENT_PROPERTY_APPEND,
    IR_OP_LOCAL_ELEMENT_CSS_VALUE,
    IR_OP_LOCAL_ELEMENT_PROPERTY_END,
    IR_OP_LOCAL_ELEMENT_APPEND,
    IR_OP_LOCAL_ELEMENT_APPEND_STATIC_TEXT,
    IR_OP_LOCAL_ELEMENT_APPEND_FORMATTED,
    IR_OP_LOCAL_ELEMENT_FINISH,
    IR_OP_COUNT
} IrOpcode;

typedef enum IrTerminatorKind {
    IR_TERM_NONE,
    IR_TERM_JUMP,
    IR_TERM_BRANCH,
    IR_TERM_RETURN,
    IR_TERM_PROPAGATE_EXCEPTION,
    IR_TERM_TRAP
} IrTerminatorKind;

typedef struct IrType {
    const char *name; /* Borrowed from checked module or static storage. */
    /* Lowering-only identity link; cleared before the IR reaches a backend. */
    const Type *checked_type;
    const char *module_name; /* Borrowed; NULL for structural/builtin types. */
    IrTypeShape shape;
    IrTypeId element_type; /* Arrays, pointers, and slices; else invalid. */
    IrTypeId error_type; /* Result error type; else invalid. */
    IrTypeId *argument_types; /* Owned function/generic arguments. */
    ParameterMode *parameter_modes; /* Owned for function arguments. */
    size_t argument_count;
    const char **field_names; /* Owned array; names borrowed from declarations. */
    IrTypeId *field_types; /* Owned array; resolved concrete field types. */
    LangSpan *field_spans; /* Owned array; declaration order. */
    size_t *field_offsets; /* Owned target offsets; valid when member_layout_known. */
    size_t field_count;
    const char **variant_names; /* Owned array; declaration order. */
    /*
     * Owned array parallel to variant_names. IR_INVALID_ID denotes a
     * payloadless variant.
     */
    IrTypeId *variant_payload_types;
    LangSpan *variant_spans; /* Owned array; declaration order. */
    uint32_t *variant_discriminants; /* Owned, stable declaration order. */
    size_t *variant_payload_offsets; /* Owned; ignored for payloadless variants. */
    size_t variant_count;
    IrCopyPolicy copy_policy;
    IrDropPolicy drop_policy;
    /* Future user-defined copy hook; invalid for built-in/deep policies. */
    IrFunctionId copy_function;
    /* Concrete Aster destructor function, or IR_INVALID_ID. */
    IrFunctionId destructor_function;
    size_t target_size;
    size_t target_alignment;
    bool target_layout_known;
    /* Class values are pointers; these describe their heap object body. */
    size_t object_size;
    size_t object_alignment;
    bool object_layout_known;
    bool member_layout_known;
    size_t array_length;
    uint8_t bit_width;
    bool pointer_mutable;
    bool requires_cleanup;
    bool managed;
    bool element_child_collection;
} IrType;

typedef struct IrInstruction {
    IrOpcode opcode;
    IrValueId result; /* IR_INVALID_ID for effect-only instructions. */
    IrTypeId result_type; /* IR_INVALID_ID when there is no result. */
    IrValueId *operands; /* Owned by instruction. */
    size_t operand_count;
    uint32_t *labels; /* Owned aggregate operand-to-field/variant mapping. */
    size_t label_count;
    ParameterMode *argument_modes; /* Owned for call operands. */
    size_t argument_mode_count;
    /* Present only for IR_OP_CALL_NATIVE; owns its parallel arrays. */
    IrNativeCallDescriptor *native_call;
    uint64_t integer;
    double floating;
    uint32_t index;
    uint32_t auxiliary;
    uint32_t render_destination;
    const char *symbol; /* Borrowed resolved symbol/field/string data. */
    size_t symbol_length;
    LangSpan span;
} IrInstruction;

typedef struct IrTerminator {
    IrTerminatorKind kind;
    IrValueId value; /* Condition, return value, or IR_INVALID_ID. */
    IrBlockId target;
    IrBlockId alternate;
    LangSpan span;
} IrTerminator;

typedef struct IrBlock {
    IrInstruction *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
    IrTerminator terminator;
} IrBlock;

typedef struct IrLocal {
    const char *name; /* Borrowed from typed AST. */
    size_t binding_id;
    IrTypeId type;
    bool mutable_;
    bool borrowed;
} IrLocal;

typedef struct IrFunction {
    const char *name;
    const char *module_name;
    /* Lowering-only source link; cleared before verification/backend use. */
    const Decl *declaration;
    LangSpan span;
    IrTypeId return_type;
    /* Completion value produced by an async function; otherwise return_type. */
    IrTypeId async_result_type;
    IrParameter *parameters;
    size_t parameter_count;
    IrFunctionAbi abi;
    IrLocal *locals;
    size_t local_count;
    size_t local_capacity;
    IrTypeId *value_types;
    size_t value_count;
    size_t value_capacity;
    IrBlock *blocks;
    size_t block_count;
    size_t block_capacity;
    IrBlockId entry_block;
    LangSpan render_root_span;
    const char *css_scope_attribute;
    IrStaticCss *static_css;
    size_t static_css_count;
    size_t static_css_capacity;
    size_t async_suspension_count;
    bool has_render_root;
    bool is_entry;
    bool is_public;
    bool is_destructor;
    bool is_web_export;
    bool is_async;
} IrFunction;

typedef struct IrModule {
    IrType *types;
    size_t type_count;
    size_t type_capacity;
    IrFunction *functions;
    size_t function_count;
    LangTargetInfo target;
    /* Non-owning lowering context; cleared before lang_ir_lower_module returns. */
    Module *lowering_module;
    LangDiagnostics *lowering_diagnostics;
} IrModule;

/*
 * Resolve one member of a concrete checked aggregate. The returned Type is
 * arena-backed by module; NULL denotes a payloadless enum variant.
 */
Type *lang_checker_resolve_aggregate_member(
    Module *module, LangDiagnostics *diagnostics,
    const Type *aggregate, size_t member_index);
bool lang_checker_resolve_type_layout(
    Module *module, LangDiagnostics *diagnostics,
    const Type *type, const LangTargetInfo *target,
    size_t *out_size, size_t *out_alignment);

bool lang_ir_lower_module(Module *module,
                          const LangTargetInfo *target,
                          LangDiagnostics *diagnostics,
                          IrModule *ir);
bool lang_ir_verify_module(const IrModule *ir,
                           LangDiagnostics *diagnostics);
void lang_ir_dump_module(const IrModule *ir);
void lang_ir_free_module(IrModule *ir);

typedef enum OpCode {
    OP_CONSTANT, OP_UNIT, OP_TRUE, OP_FALSE, OP_POP,
    OP_LOAD_LOCAL, OP_STORE_LOCAL, OP_MOVE_LOCAL, OP_REFERENCE_LOCAL,
    OP_REFERENCE_FIELD_LOCAL, OP_INVALIDATE_LOCAL,
    OP_ADD_I64, OP_SUB_I64, OP_MUL_I64, OP_DIV_I64, OP_REM_I64,
    OP_SHIFT_LEFT, OP_SHIFT_RIGHT,
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_NOT,
    OP_ADD_F64, OP_SUB_F64, OP_MUL_F64, OP_DIV_F64,
    OP_NEG_I64, OP_NEG_F64, OP_NOT, OP_CAST,
    OP_EQ, OP_NEQ, OP_LT_I64, OP_LE_I64, OP_GT_I64, OP_GE_I64,
    OP_JUMP, OP_JUMP_IF_FALSE, OP_FUNCTION, OP_CALL,
    OP_CALL_INDIRECT, OP_CALL_NATIVE, OP_AWAIT, OP_TASK_DELAY,
    OP_TASK_WHEN_ALL, OP_TASK_WHEN_ANY, OP_RETURN,
    OP_CANCELLATION_SOURCE_NEW, OP_CANCELLATION_TOKEN_NONE,
    OP_CANCELLATION_TOKEN_GET, OP_CANCELLATION_CANCEL,
    OP_CANCELLATION_IS_REQUESTED,
    OP_CANCELLATION_THROW_IF_REQUESTED,
    OP_MAKE_ARRAY, OP_GET_INDEX, OP_GET_INDEX_LOCAL, OP_SET_INDEX_LOCAL,
    OP_MAKE_STRUCT, OP_MAKE_CLASS, OP_DELETE_CLASS,
    OP_GET_FIELD, OP_GET_FIELD_LOCAL,
    OP_GET_FIELD_LOCAL_MOVE, OP_GET_FIELD_BORROW,
    OP_SET_FIELD_LOCAL,
    OP_GET_TAG, OP_TAKE_PAYLOAD, OP_SET_LOCAL,
    OP_HTML_FRAGMENT, OP_HTML_BEGIN,
    OP_HTML_FRAGMENT_LOCAL, OP_HTML_BEGIN_LOCAL,
    OP_HTML_ATTR, OP_HTML_TEXT,
    OP_HTML_APPEND, OP_HTML_END,
    OP_HTML_ATTR_LOCAL,
    OP_HTML_ATTR_BEGIN_LOCAL,
    OP_HTML_ATTR_APPEND_LOCAL,
    OP_HTML_CSS_VALUE_LOCAL,
    OP_HTML_ATTR_END_LOCAL,
    OP_HTML_APPEND_LOCAL,
    OP_HTML_APPEND_FORMATTED_LOCAL,
    OP_HTML_APPEND_CONSTANT_LOCAL,
    OP_HTML_APPEND_RAW_CONSTANT_LOCAL,
    OP_HTML_ATTR_CONSTANT_LOCAL,
    OP_HTML_ATTR_APPEND_CONSTANT_LOCAL,
    OP_HTML_APPEND_VALUE_LOCAL,
    OP_HTML_ATTR_APPEND_VALUE_LOCAL,
    OP_HTML_FINISH_LOCAL,
    OP_HTML_RENDER_LOCAL,
    OP_STRING_BUILDER_NEW_LOCAL,
    OP_STRING_BUILDER_APPEND_CONSTANT_LOCAL,
    OP_STRING_BUILDER_APPEND_VALUE_LOCAL,
    OP_STRING_BUILDER_FINISH_LOCAL,
    OP_ITER_INIT, OP_ITER_BORROW_LOCAL, OP_ITER_NEXT,
    OP_ITER_HAS_NEXT_LOCAL, OP_ITER_TAKE_NEXT_LOCAL,
    OP_DROP_LOCAL, OP_CLONE, OP_TRY,
    OP_CONSTANT_LOCAL, OP_COPY_LOCAL_TO, OP_MOVE_LOCAL_TO,
    OP_BINARY_LOCALS, OP_BINARY_LOCAL_IMMEDIATE,
    OP_BINARY_LOCALS_IMMEDIATE, OP_COMPARE_BRANCH,
    OP_COMPARE_LOCAL_CONSTANT_BRANCH,
    OP_CALL_LOCAL, OP_CALL_LOCAL_2_COPY, OP_RETURN_LOCAL,
    OP_TEXT_LEN_LOCAL, OP_STRING_SEARCH_LOCAL,
    OP_STRING_SEARCH_LOCAL_CONSTANT,
    OP_EXCEPTION_SET, OP_EXCEPTION_PENDING, OP_EXCEPTION_MATCH,
    OP_EXCEPTION_TAKE,
    OP_PROPAGATE_EXCEPTION,
    OP_TRAP
} OpCode;

typedef struct Instruction {
    OpCode op;
    int32_t a;
    int32_t b;
} Instruction;

typedef struct Constant {
    LangValue value;
    char *owned_string;
} Constant;

typedef struct BytecodeCallSite {
    ParameterMode *argument_modes;
    size_t argument_count;
} BytecodeCallSite;

typedef struct BytecodeFunction {
    const char *name;
    const char *module_name;
    bool is_public;
    bool is_entry;
    bool is_async;
    Instruction *code;
    LangSpan *spans;
    BytecodeCallSite *call_sites; /* Parallel to code. */
    size_t code_count;
    size_t code_capacity;
    size_t arity;
    ParameterMode *parameter_modes;
    bool may_have_object_locals;
    bool object_local_mask_valid;
    uint64_t object_local_mask;
    bool fast_scalar_leaf;
    bool fast_affine_wrap_leaf;
    int64_t fast_affine_addend;
    int64_t fast_affine_limit;
    size_t fast_scalar_loop_start;
    size_t fast_scalar_loop_end;
    size_t local_count;
    int32_t *local_destructors;
} BytecodeFunction;

typedef struct BytecodeModule {
    BytecodeFunction *functions;
    size_t function_count;
    Constant *constants;
    size_t constant_count;
    size_t constant_capacity;
} BytecodeModule;

bool lang_ir_compile_bytecode(const IrModule *ir,
                              LangDiagnostics *diagnostics,
                              BytecodeModule *bytecode);
bool lang_c_emit_module(const IrModule *ir,
                        LangDiagnostics *diagnostics,
                        FILE *output);
bool lang_c_emit_site(const IrModule *ir, LangDiagnostics *diagnostics,
                      FILE *output, const char *css_directory);
bool lang_c_emit_runtime(FILE *output);
void lang_bytecode_free(BytecodeModule *bytecode);
void lang_dump_bytecode(const BytecodeModule *bytecode);
int lang_vm_run_module(LangVM *vm, const BytecodeModule *module,
                       const LangSource *source);
uint64_t lang_vm_instruction_count(const LangVM *vm);
void lang_vm_register_builtins(LangVM *vm);
void lang_vm_set_process_arguments(
    LangVM *vm, size_t argument_count, const char *const *arguments);

int lang_run_file_with_roots(const char *path, const char *source_root,
                             const char *const *dependency_roots,
                             size_t dependency_root_count,
                             const char *project_root,
                             const char *stdlib_root, bool check_only,
                             const char *dump_kind, bool require_entrypoint);
int lang_run_file_with_roots_args(
    const char *path, const char *source_root,
    const char *const *dependency_roots, size_t dependency_root_count,
    const char *project_root, const char *stdlib_root,
    bool check_only, const char *dump_kind, bool require_entrypoint,
    size_t argument_count, const char *const *arguments);
int lang_emit_c_site_with_roots(const char *path, const char *source_root,
                                const char *const *dependency_roots,
                                size_t dependency_root_count,
                                const char *project_root,
                                const char *stdlib_root,
                                const char *css_directory,
                                bool require_entrypoint);
int lang_emit_c_with_roots_to_file(
    const char *path,
    const char *source_root,
    const char *const *dependency_roots,
    size_t dependency_root_count,
    const char *project_root,
    const char *stdlib_root,
    bool require_entrypoint,
    FILE *output
);
#endif
