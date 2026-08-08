#include "parser_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Decl *parse_function(
    Parser *parser, Token start, bool is_extern,
    const char *return_type, TypeSyntax *return_type_syntax, Token name,
    bool declaration_only);
static Decl *parse_destructor_decl(Parser *parser, Token start);

static void configure_struct_member(
    Parser *parser, Decl *member, const char *owner,
    bool is_static, bool is_readonly,
    bool is_abstract, bool is_virtual, bool is_override,
    bool is_sealed_override,
    bool is_public, bool has_visibility, bool is_class) {
    Function *function = &member->as.function;
    function->owner_type = owner;
    function->is_static_member = is_static;
    function->is_readonly_member = is_readonly;
    function->is_abstract_member = is_abstract;
    function->is_virtual_member = is_virtual || is_abstract;
    function->is_override_member = is_override;
    function->is_sealed_override = is_sealed_override;
    function->name = join_text(parser, owner, "::", function->name);
    member->is_public = is_public;
    member->has_explicit_visibility = has_visibility;
    if (is_static) return;
    Param *parameters = lang_arena_alloc(
        &parser->module->arena,
        (function->param_count + 1U) * sizeof(*parameters));
    parameters[0] = (Param){
        .name="this", .type_name=owner, .borrowed=!is_class,
        .mutable_=!function->is_property_getter && !is_readonly,
        .by_ref=!is_class && !function->is_property_getter && !is_readonly,
        .span=member->span,
        .type_syntax=new_type_syntax(parser, TYPE_SYNTAX_NAMED, member->span)
    };
    parameters[0].type_syntax->as.name = owner;
    if (function->param_count != 0U)
        memcpy(parameters + 1U, function->params,
               function->param_count * sizeof(*parameters));
    function->params = parameters;
    function->param_count += 1U;
}

static bool token_spells(Token token, const char *text) {
    size_t length = strlen(text);
    return token.length == length && strncmp(token.start, text, length) == 0;
}

static Stmt *property_expression_body(
    Parser *parser, Expr *expression, bool returns_value, LangSpan span
) {
    Stmt *statement = new_stmt(
        parser, returns_value ? STMT_RETURN : STMT_EXPR, expression->span);
    if (returns_value)
        statement->as.return_value = expression;
    else {
        statement->as.expression = expression;
        statement->expression_terminated = true;
    }
    statement->span = span;
    Stmt *body = new_stmt(parser, STMT_BLOCK, span);
    body->as.block.items = lang_arena_alloc(
        &parser->module->arena, sizeof(Stmt *));
    body->as.block.items[0] = statement;
    body->as.block.count = 1U;
    return body;
}

static Expr *property_backing_field_expression(
    Parser *parser, const char *owner, const char *backing_field,
    bool is_static, LangSpan span
) {
    Expr *object = parser_new_expr(parser, EXPR_NAME, span);
    object->as.name = is_static ? owner : "this";
    Expr *field = parser_new_expr(parser, EXPR_FIELD, span);
    field->as.field.object = object;
    field->as.field.field = backing_field;
    return field;
}

static Stmt *automatic_property_body(
    Parser *parser, const char *owner, const char *backing_field,
    bool is_static, bool getter, LangSpan span
) {
    Expr *field = property_backing_field_expression(
        parser, owner, backing_field, is_static, span);
    if (getter)
        return property_expression_body(parser, field, true, span);
    Expr *value = parser_new_expr(parser, EXPR_NAME, span);
    value->as.name = "value";
    Expr *assignment = parser_new_expr(parser, EXPR_ASSIGN, span);
    assignment->as.assign.target = field;
    assignment->as.assign.value = value;
    assignment->as.assign.compound_op = TOK_ERROR;
    return property_expression_body(parser, assignment, false, span);
}

static Decl *make_property_accessor(
    Parser *parser, Token property, const char *type_name,
    TypeSyntax *type_syntax, bool getter, Stmt *body,
    bool is_public, bool has_visibility, const char *backing_field
) {
    Decl *member = lang_arena_alloc(
        &parser->module->arena, sizeof(*member));
    member->kind = DECL_FUNCTION;
    member->span = body != NULL ? body->span : property.span;
    member->is_public = is_public;
    member->has_explicit_visibility = has_visibility;
    Function *function = &member->as.function;
    function->name = parser_copy_token(parser, property);
    function->property_name = function->name;
    function->property_backing_field = backing_field;
    function->is_property_getter = getter;
    function->is_property_setter = !getter;
    function->body = body;
    function->span = member->span;
    if (getter) {
        function->return_type = type_name;
        function->return_type_syntax = type_syntax;
    } else {
        TypeSyntax *unit = new_type_syntax(
            parser, TYPE_SYNTAX_NAMED, property.span);
        unit->as.name = "void";
        function->return_type = "void";
        function->return_type_syntax = unit;
        function->params = lang_arena_alloc(
            &parser->module->arena, sizeof(*function->params));
        function->params[0] = (Param){
            .name="value", .type_name=type_name,
            .type_syntax=type_syntax, .mutable_=true,
            .span=property.span
        };
        function->param_count = 1U;
    }
    return member;
}

static Decl *parse_struct_decl(
    Parser *parser, Token start, bool enumeration, bool is_union,
    bool is_class, bool is_interface) {
    Token name = parser_expect(
        parser, TOK_IDENT,
        enumeration
            ? (is_union ? "expected union name" : "expected enum name")
            : (is_interface ? "expected interface name"
                            : is_class ? "expected class name"
                                       : "expected struct name"));
    Decl *decl = lang_arena_alloc(&parser->module->arena, sizeof(*decl));
    decl->kind = enumeration ? DECL_ENUM
                             : (is_class || is_interface)
                                 ? DECL_CLASS : DECL_STRUCT;
    decl->span = start.span;
    const char **decl_name = enumeration ? &decl->as.enumeration.name : &decl->as.structure.name;
    *decl_name = parser_copy_token(parser, name);
    if (!enumeration)
        decl->as.structure.is_interface = is_interface;
    if (enumeration) decl->as.enumeration.is_union = is_union;
    parse_type_parameters(parser, decl);
    if (is_class && decl->type_param_count != 0U)
        lang_diag(parser->diagnostics, name.span,
                  "generic classes are not implemented yet");
    if (decl->type_param_count > 16U)
        lang_diag(parser->diagnostics, name.span,
                  "generic declarations are limited to 16 type parameters");
    if ((is_class || is_interface) && parser_accept(parser, TOK_COLON)) {
        ParserArrayBuilder heritage_names =
            parser_array_builder(sizeof(const char *));
        ParserArrayBuilder heritage_syntaxes =
            parser_array_builder(sizeof(TypeSyntax *));
        do {
            TypeSyntax *syntax = NULL;
            const char *heritage = parse_type(parser, &syntax);
            parser_array_push(&heritage_names, &heritage);
            parser_array_push(&heritage_syntaxes, &syntax);
        } while (parser_accept(parser, TOK_COMMA));
        decl->as.structure.heritage_type_count = heritage_names.count;
        decl->as.structure.heritage_type_names =
            parser_array_freeze(parser, &heritage_names);
        decl->as.structure.heritage_type_syntaxes =
            parser_array_freeze(parser, &heritage_syntaxes);
    }
    parser_expect(parser, TOK_LBRACE, "expected `{` after type name");
    ParserArrayBuilder fields = parser_array_builder(sizeof(FieldDecl));
    ParserArrayBuilder static_fields = parser_array_builder(sizeof(FieldDecl));
    ParserArrayBuilder members = parser_array_builder(sizeof(Decl *));
    while (parser->current.kind != TOK_RBRACE && parser->current.kind != TOK_EOF) {
        Token member_start = parser->current;
        bool explicit_public = parser_accept(parser, TOK_PUB);
        bool member_private = !explicit_public &&
            parser_accept(parser, TOK_PRIVATE);
        bool member_public = is_interface || explicit_public;
        bool member_visibility = is_interface ||
            explicit_public || member_private;
        if (is_interface && member_private)
            lang_diag(parser->diagnostics, member_start.span,
                      "interface members are public");
        bool member_async = parser_accept(parser, TOK_ASYNC);
        bool member_static = parser_accept(parser, TOK_STATIC);
        bool member_abstract = parser_accept(parser, TOK_ABSTRACT);
        member_abstract = member_abstract || is_interface;
        bool member_virtual = parser_accept(parser, TOK_VIRTUAL);
        bool member_override = parser_accept(parser, TOK_OVERRIDE);
        bool member_sealed = parser_accept(parser, TOK_SEALED);
        if (member_sealed && !member_override)
            member_override = parser_accept(parser, TOK_OVERRIDE);
        bool member_readonly = parser_accept(parser, TOK_READONLY);
        if (!is_class && !is_interface &&
            (member_abstract || member_virtual ||
                          member_override || member_sealed))
            lang_diag(parser->diagnostics, member_start.span,
                      "abstract, virtual, override, and sealed members require a class");
        if ((is_class || is_interface) && parser_accept(parser, TOK_TILDE)) {
            Decl *member = parse_destructor_decl(parser, member_start);
            member->as.function.owner_type = *decl_name;
            member->as.function.params[0].name = "this";
            member->has_explicit_visibility = true;
            if (is_interface)
                lang_diag(parser->diagnostics, member_start.span,
                          "interfaces cannot declare destructors");
            if (member_visibility || member_static || member_readonly ||
                member_abstract || member_virtual || member_override ||
                member_sealed || member_async)
                lang_diag(parser->diagnostics, member_start.span,
                          "class destructors do not declare modifiers");
            if (strcmp(member->as.function.params[0].type_name,
                       *decl_name) != 0)
                lang_diag(parser->diagnostics, member_start.span,
                          "class destructor name must match `%s`",
                          *decl_name);
            parser_array_push(&members, &member);
            continue;
        }
        Token field;
        const char *type_name = "Unit";
        TypeSyntax *type_syntax = new_type_syntax(
            parser, TYPE_SYNTAX_NAMED, parser->current.span);
        type_syntax->as.name = "Unit";
        if (enumeration) {
            field = parser_expect(parser, TOK_IDENT, "expected variant name");
        } else {
            type_name = parse_type(parser, &type_syntax);
            if (parser->current.kind == TOK_LPAREN &&
                strcmp(type_name, *decl_name) == 0) {
                Decl *member = parse_function(
                    parser, member_start, false,
                    type_name, type_syntax, member_start, false);
                member->as.function.is_constructor = true;
                member->as.function.owner_type = *decl_name;
                member->as.function.name = join_text(
                    parser, *decl_name, "::", "new");
                Function *constructor = &member->as.function;
                constructor->is_copy_constructor =
                    constructor->param_count == 1U &&
                    constructor->params[0].by_const_ref &&
                    strcmp(constructor->params[0].type_name,
                           *decl_name) == 0;
                member->is_public = member_public;
                member->has_explicit_visibility =
                    member_visibility || is_class;
                if (member_static)
                    lang_diag(parser->diagnostics, member_start.span,
                              "constructors cannot be static");
                if (member_async)
                    lang_diag(parser->diagnostics, member_start.span,
                              "constructors cannot be async");
                if (is_interface)
                    lang_diag(parser->diagnostics, member_start.span,
                              "interfaces cannot declare constructors");
                if (member_abstract || member_virtual || member_override ||
                    member_sealed || member_readonly)
                    lang_diag(parser->diagnostics, member_start.span,
                              "constructors cannot be abstract, virtual, override, sealed, or readonly");
                parser_array_push(&members, &member);
                continue;
            }
            field = parser_expect(parser, TOK_IDENT,
                           "expected field name after type");
            parse_declarator_suffix(parser, &type_syntax, &type_name);
            if (parser->current.kind == TOK_LPAREN) {
                Decl *member = parse_function(
                    parser, field, false, type_name, type_syntax, field,
                    member_abstract);
                configure_struct_member(
                    parser, member, *decl_name, member_static,
                    member_readonly, member_abstract, member_virtual,
                    member_override, member_sealed,
                    member_public,
                    member_visibility || is_class || is_interface,
                    is_class || is_interface);
                member->as.function.is_async = member_async;
                parser_array_push(&members, &member);
                continue;
            }
            if (member_async)
                lang_diag(parser->diagnostics, member_start.span,
                          "async member must be a method");
            if (parser_accept(parser, TOK_FAT_ARROW)) {
                Expr *value = parser_parse_expression(parser);
                Token end = parser_expect(
                    parser, TOK_SEMICOLON,
                    "expected `;` after property expression");
                Stmt *return_stmt = new_stmt(parser, STMT_RETURN, value->span);
                return_stmt->as.return_value = value;
                return_stmt->span.end = end.span.end;
                Stmt *body = new_stmt(parser, STMT_BLOCK, field.span);
                body->as.block.items = lang_arena_alloc(
                    &parser->module->arena, sizeof(Stmt *));
                body->as.block.items[0] = return_stmt;
                body->as.block.count = 1U;
                body->span.end = end.span.end;
                Decl *member = lang_arena_alloc(
                    &parser->module->arena, sizeof(*member));
                member->kind = DECL_FUNCTION;
                member->span = (LangSpan){field.span.file, field.span.start, end.span.end};
                member->as.function.name = parser_copy_token(parser, field);
                member->as.function.return_type = type_name;
                member->as.function.return_type_syntax = type_syntax;
                member->as.function.body = body;
                member->as.function.span = member->span;
                member->as.function.is_property_getter = true;
                member->as.function.property_name =
                    member->as.function.name;
                configure_struct_member(
                    parser, member, *decl_name, member_static,
                    member_readonly, member_abstract, member_virtual,
                    member_override, member_sealed,
                    member_public,
                    member_visibility || is_class || is_interface,
                    is_class || is_interface);
                parser_array_push(&members, &member);
                continue;
            }
            if (parser_accept(parser, TOK_LBRACE)) {
                ParserArrayBuilder accessors = parser_array_builder(
                    sizeof(Decl *));
                bool has_getter = false;
                bool has_setter = false;
                bool any_automatic = false;
                bool any_custom = false;
                const char *backing_field = join_text(
                    parser, "<", parser_copy_token(parser, field),
                    ">k__BackingField");
                while (parser->current.kind != TOK_RBRACE &&
                       parser->current.kind != TOK_EOF) {
                    Token accessor_start = parser->current;
                    bool accessor_public = parser_accept(parser, TOK_PUB);
                    bool accessor_private = !accessor_public &&
                        parser_accept(parser, TOK_PRIVATE);
                    bool accessor_visibility =
                        accessor_public || accessor_private;
                    if (is_interface && accessor_private)
                        lang_diag(parser->diagnostics, accessor_start.span,
                                  "interface accessors are public");
                    Token accessor = parser_expect(
                        parser, TOK_IDENT,
                        "expected `get` or `set` property accessor");
                    bool getter = token_spells(accessor, "get");
                    bool setter = token_spells(accessor, "set");
                    if (!getter && !setter)
                        lang_diag(parser->diagnostics, accessor.span,
                                  "property accessor must be `get` or `set`");
                    if ((getter && has_getter) || (setter && has_setter))
                        lang_diag(parser->diagnostics, accessor.span,
                                  "duplicate `%s` property accessor",
                                  getter ? "get" : "set");
                    has_getter = has_getter || getter;
                    has_setter = has_setter || setter;
                    if (accessor_public && !member_public)
                        lang_diag(parser->diagnostics, accessor_start.span,
                                  "an accessor cannot be more public than its property");
                    bool automatic = parser_accept(parser, TOK_SEMICOLON);
                    Stmt *body = NULL;
                    if (automatic) {
                        any_automatic = true;
                        if (!member_abstract)
                            body = automatic_property_body(
                                parser, *decl_name, backing_field,
                                member_static, getter,
                                (LangSpan){field.span.file, field.span.start,
                                           parser->previous.span.end});
                    } else if (parser_accept(parser, TOK_FAT_ARROW)) {
                        any_custom = true;
                        Expr *value = parser_parse_expression(parser);
                        Token end = parser_expect(
                            parser, TOK_SEMICOLON,
                            "expected `;` after property accessor expression");
                        body = property_expression_body(
                            parser, value, getter,
                            (LangSpan){field.span.file, field.span.start,
                                       end.span.end});
                    } else {
                        any_custom = true;
                        body = parse_block(parser);
                    }
                    bool visible = is_interface ? true
                        : accessor_visibility
                            ? accessor_public : member_public;
                    Decl *member = make_property_accessor(
                        parser, field, type_name, type_syntax, getter,
                        body, visible,
                        accessor_visibility || member_visibility || is_class,
                        automatic && !member_abstract
                            ? backing_field : NULL);
                    configure_struct_member(
                        parser, member, *decl_name, member_static,
                        member_readonly, member_abstract, member_virtual,
                        member_override, member_sealed, visible,
                        accessor_visibility || member_visibility || is_class,
                        is_class || is_interface);
                    parser_array_push(&accessors, &member);
                }
                Token close = parser_expect(
                    parser, TOK_RBRACE,
                    "expected `}` after property accessors");
                if (!has_getter && !has_setter)
                    lang_diag(parser->diagnostics, field.span,
                              "property must declare a `get` or `set` accessor");
                if (any_automatic && any_custom)
                    lang_diag(parser->diagnostics, field.span,
                              "automatic and custom accessors cannot be mixed");
                if (member_readonly && has_setter)
                    lang_diag(parser->diagnostics, field.span,
                              "a readonly property cannot declare a setter");
                if (member_abstract && any_custom)
                    lang_diag(parser->diagnostics, field.span,
                              "abstract property accessors do not have bodies");
                if (any_automatic && !member_abstract) {
                    FieldDecl backing = {
                        .name=backing_field, .type_name=type_name,
                        .span=field.span, .type_syntax=type_syntax,
                        .is_public=false, .has_explicit_visibility=true,
                        .is_readonly=member_readonly
                    };
                    parser_array_push(
                        member_static ? &static_fields : &fields,
                        &backing);
                }
                Decl **property_accessors = accessors.items;
                for (size_t accessor = 0U;
                     accessor < accessors.count; ++accessor) {
                    property_accessors[accessor]->span.end = close.span.end;
                    property_accessors[accessor]
                        ->as.function.span.end = close.span.end;
                    parser_array_push(
                        &members, &property_accessors[accessor]);
                }
                free(accessors.items);
                continue;
            }
            if (member_visibility && !is_class && !is_interface)
                lang_diag(parser->diagnostics, field.span,
                          "field visibility is not supported yet");
        }
        if (enumeration && parser_accept(parser, TOK_LPAREN)) {
            if (!is_union)
                lang_diag(
                    parser->diagnostics, field.span,
                    "enum member `%s` cannot carry a payload; declare `%s` as a union",
                    parser_copy_token(parser, field), *decl_name);
            type_name = parse_type(parser, &type_syntax);
            if (parser->current.kind != TOK_RPAREN) {
                lang_diag(
                    parser->diagnostics,
                    parser->current.span,
                    parser->current.kind == TOK_COMMA
                        ? "enum variants parser_accept one payload type"
                        : "expected `)` after variant payload");
                while (parser->current.kind != TOK_RPAREN &&
                       parser->current.kind != TOK_RBRACE &&
                       parser->current.kind != TOK_EOF)
                    parser_next(parser);
            }
            if (parser->current.kind == TOK_RPAREN)
                parser_next(parser);
        }
        if (parser->panic) {
            while (parser->current.kind != TOK_COMMA &&
                   parser->current.kind != TOK_SEMICOLON &&
                   parser->current.kind != TOK_RBRACE &&
                   parser->current.kind != TOK_EOF)
                parser_next(parser);
            if (!parser_accept(parser, TOK_COMMA))
                (void)parser_accept(parser, TOK_SEMICOLON);
            parser->panic = false;
            continue;
        }
        Expr *field_initializer = NULL;
        if (!enumeration && (member_abstract || member_virtual ||
                             member_override || member_sealed))
            lang_diag(parser->diagnostics, field.span,
                      "fields cannot be abstract, virtual, override, or sealed");
        if (is_interface)
            lang_diag(parser->diagnostics, field.span,
                      "interfaces cannot declare instance fields");
        if (!enumeration && parser_accept(parser, TOK_EQUAL)) {
            field_initializer = parser_parse_expression(parser);
            if (!member_static)
                lang_diag(parser->diagnostics, field.span,
                          "instance field initializers are not implemented yet");
        }
        FieldDecl item = {
            .name=parser_copy_token(parser, field),
            .type_name=type_name,
            .span=field.span,
            .type_syntax=type_syntax,
            .is_public=member_public,
            .has_explicit_visibility=member_visibility,
            .is_readonly=member_readonly,
            .initializer=field_initializer
        };
        parser_array_push(member_static ? &static_fields : &fields, &item);
        if (!parser_accept(parser, TOK_COMMA))
            (void)parser_accept(parser, TOK_SEMICOLON);
    }
    if (enumeration) {
        decl->as.enumeration.variant_count = fields.count;
        decl->as.enumeration.variants = parser_array_freeze(parser, &fields);
    } else {
        decl->as.structure.field_count = fields.count;
        decl->as.structure.fields = parser_array_freeze(parser, &fields);
        decl->as.structure.static_field_count = static_fields.count;
        decl->as.structure.static_fields =
            parser_array_freeze(parser, &static_fields);
        decl->as.structure.member_count = members.count;
        decl->as.structure.members = parser_array_freeze(parser, &members);
    }
    decl->span.end = parser_expect(parser, TOK_RBRACE, "expected `}` after declaration").span.end;
    return decl;
}

static Decl *parse_function(
    Parser *parser, Token start, bool is_extern,
    const char *return_type, TypeSyntax *return_type_syntax, Token name,
    bool declaration_only
) {
    Decl *decl = lang_arena_alloc(&parser->module->arena, sizeof(*decl));
    decl->kind = DECL_FUNCTION;
    Function *fn = &decl->as.function;
    fn->name = parser_copy_token(parser, name);
    while (parser->current.kind == TOK_DOT) {
        parser_next(parser);
        Token part = parser_expect(
            parser, TOK_IDENT,
            "expected function name after `::`");
        fn->name = join_text(
            parser, fn->name, "::", parser_copy_token(parser, part));
    }
    fn->is_extern = is_extern;
    parse_type_parameters(parser, decl);
    if (decl->type_param_count > 16U)
        lang_diag(parser->diagnostics, name.span,
                  "generic functions are limited to 16 type parameters");
    if (is_extern && decl->type_param_count != 0U)
        lang_diag(parser->diagnostics, name.span,
                  "extern functions cannot be generic");
    parser_expect(parser, TOK_LPAREN, "expected `(` after function name");
    ParserArrayBuilder parameters = parser_array_builder(sizeof(Param));
    while (parser->current.kind != TOK_RPAREN && parser->current.kind != TOK_EOF) {
        bool immutable_ref = false;
        if (parser->current.kind == TOK_CONST) {
            Parser probe = *parser;
            parser_next(&probe);
            if (probe.current.kind == TOK_REF) {
                parser_next(parser);
                parser_next(parser);
                immutable_ref = true;
            }
        }
        bool by_ref = !immutable_ref && parser_accept(parser, TOK_REF);
        bool by_out = !by_ref && parser_accept(parser, TOK_OUT);
        TypeSyntax *type_syntax = NULL;
        const char *type_name = parse_type(parser, &type_syntax);
        Token param_name = parser_expect(parser, TOK_IDENT,
                                  "expected parameter name after type");
        parse_declarator_suffix(parser, &type_syntax, &type_name);
        const char *parameter_name = parser_copy_token(parser, param_name);
        Param param = {
            .name=parameter_name,
            .type_name=type_name,
            .type_syntax=type_syntax,
            .borrowed=immutable_ref || by_ref || by_out,
            .mutable_=!immutable_ref,
            .by_const_ref=immutable_ref,
            .by_ref=by_ref || by_out,
            .by_out=by_out,
            .span=param_name.span,
            .checked_type=NULL
        };
        parser_array_push(&parameters, &param);
        if (!parser_accept(parser, TOK_COMMA)) break;
    }
    parser_expect(parser, TOK_RPAREN, "expected `)` after parameters");
    fn->param_count = parameters.count;
    fn->params = parser_array_freeze(parser, &parameters);
    fn->return_type = return_type;
    fn->return_type_syntax = return_type_syntax;
    if (parser_accept(parser, TOK_EQUAL)) {
        Token deleted = parser_expect(
            parser, TOK_DELETE, "expected `delete` after `=`");
        Token end = parser_expect(
            parser, TOK_SEMICOLON, "expected `;` after `= delete`");
        fn->is_deleted = deleted.kind == TOK_DELETE;
        fn->span = (LangSpan){start.span.file, start.span.start, end.span.end};
    } else if (is_extern || declaration_only) {
        Token end = parser_expect(parser, TOK_SEMICOLON,
                           "expected `;` after function declaration");
        fn->span = (LangSpan){start.span.file, start.span.start, end.span.end};
    } else {
        Function *previous_function = parser->current_function;
        parser->current_function = fn;
        fn->body = parse_block(parser);
        parser->current_function = previous_function;
        fn->span = (LangSpan){start.span.file, start.span.start, fn->body->span.end};
    }
    decl->span = fn->span;
    return decl;
}

static Decl *parse_destructor_decl(Parser *parser, Token start) {
    Token type_name = parser_expect(
        parser, TOK_IDENT, "expected type name after `~`");
    parser_expect(parser, TOK_LPAREN,
           "expected `(` after destructor type");
    parser_expect(parser, TOK_RPAREN,
           "destructors do not declare parameters; `self` is implicit");

    Decl *decl = lang_arena_alloc(
        &parser->module->arena, sizeof(*decl));
    decl->kind = DECL_FUNCTION;
    Function *function = &decl->as.function;
    const char *type = parser_copy_token(parser, type_name);
    size_t function_name_length = strlen(type) + sizeof("::drop");
    char *function_name = lang_arena_alloc(
        &parser->module->arena, function_name_length);
    (void)snprintf(
        function_name, function_name_length, "%s::drop", type);
    function->name = function_name;
    function->return_type = "void";
    function->return_type_syntax = new_type_syntax(
        parser, TYPE_SYNTAX_NAMED, type_name.span);
    function->return_type_syntax->as.name = "void";
    function->is_drop = true;
    function->params = lang_arena_alloc(
        &parser->module->arena, sizeof(*function->params));
    function->params[0] = (Param){
        .name="self",
        .type_name=type,
        .type_syntax=new_type_syntax(
            parser, TYPE_SYNTAX_NAMED, type_name.span),
        .borrowed=false,
        .mutable_=true,
        .by_ref=false,
        .by_out=false,
        .span=type_name.span,
        .checked_type=NULL
    };
    function->params[0].type_syntax->as.name = type;
    function->param_count = 1U;
    function->body = parse_block(parser);
    function->span = (LangSpan){
        start.span.file, start.span.start, function->body->span.end
    };
    decl->span = function->span;
    return decl;
}

static Decl *parse_delegate_decl(Parser *parser, Token start) {
    TypeSyntax *return_type_syntax = NULL;
    (void)parse_type(parser, &return_type_syntax);
    Token name = parser_expect(parser, TOK_IDENT,
                        "expected delegate name after return type");
    Decl *decl = lang_arena_alloc(
        &parser->module->arena, sizeof(*decl));
    decl->kind = DECL_ALIAS;
    decl->as.alias.name = parser_copy_token(parser, name);
    parse_type_parameters(parser, decl);
    if (decl->type_param_count > 16U)
        lang_diag(parser->diagnostics, name.span,
                  "generic delegates are limited to 16 type parameters");
    parser_expect(parser, TOK_LPAREN,
           "expected `(` after delegate name");
    ParserArrayBuilder parameters = parser_array_builder(
        sizeof(TypeSyntax *));
    ParserArrayBuilder modes = parser_array_builder(sizeof(ParameterMode));
    while (parser->current.kind != TOK_RPAREN &&
           parser->current.kind != TOK_EOF) {
        bool immutable_ref = false;
        if (parser->current.kind == TOK_CONST) {
            Parser probe = *parser;
            parser_next(&probe);
            if (probe.current.kind == TOK_REF) {
                parser_next(parser);
                parser_next(parser);
                immutable_ref = true;
            }
        }
        bool by_ref = !immutable_ref && parser_accept(parser, TOK_REF);
        bool by_out = !by_ref && parser_accept(parser, TOK_OUT);
        TypeSyntax *parameter_syntax = NULL;
        const char *parameter_type = parse_type(
            parser, &parameter_syntax);
        Token parameter = parser_expect(
            parser, TOK_IDENT,
            "expected parameter name after delegate parameter type");
        parse_declarator_suffix(
            parser, &parameter_syntax, &parameter_type);
        ParameterMode mode = by_out ? PARAMETER_MODE_OUT
            : by_ref ? PARAMETER_MODE_MUTABLE_REFERENCE
            : immutable_ref ? PARAMETER_MODE_IMMUTABLE_REFERENCE
            : PARAMETER_MODE_VALUE;
        parser_array_push(&parameters, &parameter_syntax);
        parser_array_push(&modes, &mode);
        (void)parameter;
        if (!parser_accept(parser, TOK_COMMA)) break;
    }
    parser_expect(parser, TOK_RPAREN,
           "expected `)` after delegate parameters");
    Token end = parser_expect(parser, TOK_SEMICOLON,
                       "expected `;` after delegate declaration");
    TypeSyntax *function_syntax = new_type_syntax(
        parser, TYPE_SYNTAX_FUNCTION,
        (LangSpan){start.span.file, start.span.start, end.span.end});
    function_syntax->as.function.parameter_count = parameters.count;
    function_syntax->as.function.parameters =
        parser_array_freeze(parser, &parameters);
    function_syntax->as.function.parameter_modes =
        parser_array_freeze(parser, &modes);
    function_syntax->as.function.return_type = return_type_syntax;
    const char *function_type = format_type_syntax(parser, function_syntax);

    decl->as.alias.target = function_type;
    decl->as.alias.target_syntax = function_syntax;
    decl->as.alias.is_delegate = true;
    decl->span = (LangSpan){
        start.span.file, start.span.start, end.span.end
    };
    return decl;
}

static Decl *parse_element_decl(Parser *parser, Token start) {
    Token name;
    const char *result_type;
    TypeSyntax *result_type_syntax = NULL;
    result_type = parse_type(parser, &result_type_syntax);
    name = parser_expect_element_word(
        parser, "expected element name after result type");
    parser_expect(parser, TOK_LBRACE,
           "expected `{` before element properties");
    Decl *decl =
        lang_arena_alloc(&parser->module->arena, sizeof(*decl));
    decl->kind = DECL_ELEMENT;
    decl->as.element.name = parser_copy_token(parser, name);
    decl->as.element.result_type = result_type;
    decl->as.element.result_type_syntax = result_type_syntax;
    ParserArrayBuilder properties = parser_array_builder(
        sizeof(FieldDecl));
    while (parser->current.kind != TOK_RBRACE &&
           parser->current.kind != TOK_EOF) {
        LangSpan property_span = parser->current.span;
        char *property_name;
        const char *type_name;
        TypeSyntax *type_syntax = NULL;
        type_name = parse_type(parser, &type_syntax);
        Token property = parser->current;
        property_span = property.span;
        if (!parser_element_property_word(property.kind)) {
            lang_diag(parser->diagnostics, property.span,
                      "expected element property name after type");
        } else {
            parser_next(parser);
        }
        property_name = parser_parse_element_property_name(parser, property);
        parse_declarator_suffix(parser, &type_syntax, &type_name);
        if (parser->panic) {
            while (parser->current.kind != TOK_COMMA &&
                   parser->current.kind != TOK_SEMICOLON &&
                   parser->current.kind != TOK_RBRACE &&
                   parser->current.kind != TOK_EOF)
                parser_next(parser);
            if (!parser_accept(parser, TOK_COMMA))
                (void)parser_accept(parser, TOK_SEMICOLON);
            parser->panic = false;
            continue;
        }
        FieldDecl item = {
            .name=property_name,
            .type_name=type_name,
            .span=property_span,
            .type_syntax=type_syntax
        };
        parser_array_push(&properties, &item);
        if (!parser_accept(parser, TOK_COMMA))
            (void)parser_accept(parser, TOK_SEMICOLON);
    }
    decl->as.element.property_count = properties.count;
    decl->as.element.properties =
        parser_array_freeze(parser, &properties);
    decl->span = (LangSpan){
        start.span.file, start.span.start,
        parser_expect(parser, TOK_RBRACE,
               "expected `}` after element declaration").span.end
    };
    return decl;
}

static Decl *parse_using_decl(Parser *parser, Token start,
                              ParserArrayBuilder *imports) {
    ImportDecl import_decl;
    memset(&import_decl, 0, sizeof(import_decl));
    import_decl.owner_module = parser->current_module;
    import_decl.span = start.span;
    Token first = parser_expect(parser, TOK_IDENT, "expected namespace name");
    if (parser_accept(parser, TOK_EQUAL)) {
        TypeSyntax *target_syntax = NULL;
        const char *target = parse_type(parser, &target_syntax);
        Token end = parser_expect(parser, TOK_SEMICOLON,
                           "expected `;` after using alias");
        /* A qualified target is a namespace alias. Unqualified aliases are
         * type aliases; this keeps declaration-side aliases deterministic
         * without requiring filesystem knowledge in the parser. */
        if (strstr(target, "::") == NULL) {
            Decl *decl = lang_arena_alloc(
                &parser->module->arena, sizeof(*decl));
            decl->kind = DECL_ALIAS;
            decl->as.alias.name = parser_copy_token(parser, first);
            decl->as.alias.target = target;
            decl->as.alias.target_syntax = target_syntax;
            decl->span = (LangSpan){
                start.span.file, start.span.start, end.span.end
            };
            return decl;
        }
        import_decl.alias = parser_copy_token(parser, first);
        import_decl.module_path = target;
        import_decl.span.end = end.span.end;
        parser_array_push(imports, &import_decl);
        return NULL;
    }
    const char *path = parser_copy_token(parser, first);
    while (parser->current.kind == TOK_DOT) {
        parser_next(parser);
        Token part = parser_expect(
            parser, TOK_IDENT, "expected namespace name after `.`");
        path = join_text(
            parser, path, "::", parser_copy_token(parser, part));
    }
    parser_expect(parser, TOK_SEMICOLON, "expected `;` after using declaration");
    import_decl.module_path = path;
    import_decl.span.end = parser->previous.span.end;
    parser_array_push(imports, &import_decl);
    return NULL;
}

bool lang_parse_module(const LangSource *source, LangDiagnostics *diagnostics,
                       Module *module) {
    memset(module, 0, sizeof(*module));
    module->source = source;
    module->require_entrypoint = true;
    lang_arena_init(&module->arena);
    Parser parser;
    memset(&parser, 0, sizeof(parser));
    parser.diagnostics = diagnostics;
    parser.module = module;
    parser.current_module = source->path;
    lang_lexer_init(&parser.lexer, source, diagnostics);
    parser_next(&parser);
    ParserArrayBuilder imports = parser_array_builder(sizeof(ImportDecl));
    ParserArrayBuilder declarations = parser_array_builder(sizeof(Decl *));
    while (parser.current.kind != TOK_EOF) {
        Token start = parser.current;
        Token visibility = parser.current;
        bool is_public = parser_accept(&parser, TOK_PUB);
        bool is_private = !is_public &&
            parser_accept(&parser, TOK_PRIVATE);
        bool has_explicit_visibility = is_public || is_private;
        if (has_explicit_visibility &&
            (parser.current.kind == TOK_PUB ||
             parser.current.kind == TOK_PRIVATE)) {
            lang_diag(diagnostics, parser.current.span,
                      "a declaration can have only one visibility modifier");
            parser_next(&parser);
        }
        bool is_async = parser_accept(&parser, TOK_ASYNC);
        bool is_abstract_type = parser_accept(&parser, TOK_ABSTRACT);
        bool is_sealed_type = parser_accept(&parser, TOK_SEALED);
        if (!has_explicit_visibility && !is_async &&
            !is_abstract_type && !is_sealed_type)
            start = parser.current;
        Decl *decl = NULL;
        bool is_extern = false;
        if (parser_accept(&parser, TOK_TILDE)) {
            decl = parse_destructor_decl(&parser, start);
        } else if (parser_accept(&parser, TOK_DELEGATE)) {
            decl = parse_delegate_decl(&parser, start);
        } else if (parser_accept(&parser, TOK_ELEMENT)) {
            decl = parse_element_decl(&parser, start);
        } else {
            is_extern = parser_accept(&parser, TOK_EXTERN);
        }
        if (decl != NULL) {
            /* Already parsed above. */
        } else if (parser_accept(&parser, TOK_STRUCT)) {
            decl = parse_struct_decl(
                &parser, start, false, false, false, false);
            decl->as.structure.is_extern = is_extern;
        } else if (parser_accept(&parser, TOK_CLASS)) {
            decl = parse_struct_decl(
                &parser, start, false, false, true, false);
            decl->as.structure.is_abstract = is_abstract_type;
            decl->as.structure.is_sealed = is_sealed_type;
            if (is_abstract_type && is_sealed_type)
                lang_diag(diagnostics, start.span,
                          "a class cannot be both abstract and sealed");
            if (is_extern)
                lang_diag(diagnostics, start.span,
                          "`extern class` is not supported");
        } else if (parser_accept(&parser, TOK_INTERFACE)) {
            decl = parse_struct_decl(
                &parser, start, false, false, false, true);
            decl->as.structure.is_abstract = true;
            if (is_abstract_type || is_sealed_type)
                lang_diag(diagnostics, start.span,
                          "interfaces do not declare `abstract` or `sealed`");
            if (is_extern)
                lang_diag(diagnostics, start.span,
                          "`extern interface` is not supported");
        } else if (parser_accept(&parser, TOK_ENUM)) {
            decl = parse_struct_decl(
                &parser, start, true, false, false, false);
            if (is_extern)
                lang_diag(diagnostics, start.span,
                          "`extern enum` is not supported; use an integer-backed C ABI wrapper");
        } else if (parser_accept(&parser, TOK_UNION)) {
            decl = parse_struct_decl(
                &parser, start, true, true, false, false);
            if (is_extern)
                lang_diag(diagnostics, start.span,
                          "`extern union` is not supported");
        }
        else if (parser_accept(&parser, TOK_NAMESPACE)) {
            if (has_explicit_visibility)
                lang_diag(diagnostics, start.span,
                          "visibility cannot be applied to a namespace declaration");
            Token first = parser_expect(&parser, TOK_IDENT,
                                 "expected namespace name");
            const char *name = parser_copy_token(&parser, first);
            while (parser.current.kind == TOK_DOT) {
                parser_next(&parser);
                Token part = parser_expect(&parser, TOK_IDENT,
                                    "expected namespace name after `.`");
                name = join_text(&parser, name, "::",
                                 parser_copy_token(&parser, part));
            }
            parser_expect(&parser, TOK_SEMICOLON,
                   "expected `;` after namespace declaration");
            parser.current_module = name;
            continue;
        } else if (parser_accept(&parser, TOK_USING)) {
            decl = parse_using_decl(&parser, start, &imports);
            if (decl == NULL) {
                if (has_explicit_visibility)
                    lang_diag(diagnostics, start.span,
                              "visibility cannot be applied to a namespace using declaration");
                continue;
            }
        } else if (parser.current.kind == TOK_IDENT ||
                   parser.current.kind == TOK_CONST ||
                   parser.current.kind == TOK_STAR ||
                   parser.current.kind == TOK_LBRACKET ||
                   parser.current.kind == TOK_LPAREN) {
            TypeSyntax *return_type_syntax = NULL;
            const char *return_type = parse_type(
                &parser, &return_type_syntax);
            Token name = parser_expect(
                &parser, TOK_IDENT,
                "expected function name after return type");
            decl = parse_function(
                &parser, start, is_extern, return_type,
                return_type_syntax, name, false);
            decl->as.function.is_async = is_async;
        } else {
            lang_diag(diagnostics, parser.current.span,
                      "expected a function, struct, class, enum, union, element, using, delegate, or destructor declaration");
            parser_next(&parser);
            continue;
        }
        if (is_async && (decl == NULL || decl->kind != DECL_FUNCTION))
            lang_diag(diagnostics, start.span,
                      "`async` can only be applied to a function");
        if ((is_abstract_type || is_sealed_type) &&
            (decl == NULL || decl->kind != DECL_CLASS))
            lang_diag(diagnostics, start.span,
                      "`abstract` and `sealed` type modifiers require a class");
        if (is_public && visibility.length == 3U &&
            decl != NULL && decl->kind == DECL_FUNCTION)
            lang_diag(diagnostics, visibility.span,
                      "`pub` is not valid function visibility; use `public` or `private`");
        decl->module_name = parser.current_module;
        decl->is_public = is_public;
        decl->has_explicit_visibility = has_explicit_visibility;
        parser_array_push(&declarations, &decl);
        if (decl->kind == DECL_STRUCT || decl->kind == DECL_CLASS) {
            for (size_t member = 0U;
                 member < decl->as.structure.member_count; ++member) {
                Decl *nested = decl->as.structure.members[member];
                nested->module_name = parser.current_module;
                parser_array_push(&declarations, &nested);
            }
        }
    }
    module->import_count = imports.count;
    module->imports = parser_array_freeze(&parser, &imports);
    module->count = declarations.count;
    module->decls = parser_array_freeze(&parser, &declarations);
    module->entry_module = parser.current_module;
    return diagnostics->count == 0U;
}
