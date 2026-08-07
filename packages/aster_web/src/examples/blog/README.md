# Aster Web routing and SSG fixture

This is a compiler and framework fixture, not a real blog application or a
Aster Web showcase. It exists to verify that one route graph can be dispatched by
the HTTP server and materialized by the static builder.

The content is deliberately insufficient for evaluating blog authoring,
publishing, application structure, or Aster Web development speed. In particular,
the single post is compiled into the program, and there is no content loader,
asset workflow, archive, search, pagination, drafts, or editing workflow.

Set the title, description, and real public URL in `blog_new()`. Each post has
one native `Html` body function and one `Post` value in that function. The post
collection renders the indexes and derives all concrete `/blog/{slug}/` build
pages, so there is no separate SSG route list.

Build static files:

```sh
./build/lang project build-site packages/aster_web/aster.toml public blog_fixture
```

Check the generated-C server target:

```sh
./build/lang project check packages/aster_web/aster.toml blog_fixture_server
./build/lang project emit-c packages/aster_web/aster.toml blog_fixture_server > blog.c
```

The fixture covers a home route, post index, one parameterized post route,
About, RSS, robots.txt, native CSS, a 404 response, and XML escaping. Passing
these checks demonstrates only those mechanisms. It does not demonstrate that
Aster Web is ready for a real blog or competitive with vanilla PHP development.
