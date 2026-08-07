#!/usr/bin/env python3
"""Generate Aster Web's explicit typed-route overload matrices."""

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ASTER_WEB = ROOT / "packages" / "aster_web" / "src" / "aster" / "web.as"

VARIANTS = (
    ("StringRouteHandler", "String"),
    ("IntRouteHandler", "Int"),
    ("LongRouteHandler", "Long"),
    ("BoolRouteHandler", "Bool"),
    ("RequestStringRouteHandler", "RequestString"),
    ("RequestIntRouteHandler", "RequestInt"),
    ("RequestLongRouteHandler", "RequestLong"),
    ("RequestBoolRouteHandler", "RequestBool"),
    ("AsyncStringRouteHandler", "AsyncString"),
    ("AsyncIntRouteHandler", "AsyncInt"),
    ("AsyncLongRouteHandler", "AsyncLong"),
    ("AsyncBoolRouteHandler", "AsyncBool"),
    ("AsyncRequestStringRouteHandler", "AsyncRequestString"),
    ("AsyncRequestIntRouteHandler", "AsyncRequestInt"),
    ("AsyncRequestLongRouteHandler", "AsyncRequestLong"),
    ("AsyncRequestBoolRouteHandler", "AsyncRequestBool"),
)

VERBS = (
    ("MapPost", "POST"),
    ("MapPut", "PUT"),
    ("MapPatch", "PATCH"),
    ("MapDelete", "DELETE"),
    ("MapHead", "HEAD"),
)


def web_application_overloads():
    blocks = []
    for method, http_method in VERBS:
        for delegate, variant in VARIANTS:
            blocks.append(
                f"""public EndpointBuilder WebApplication.{method}(
    WebApplication self, string path, {delegate} handler
)
{{
    return self.MapTypedMethod(
        \"{http_method}\", path, RouteHandler.{variant}(handler)
    );
}}"""
            )
    for delegate, variant in VARIANTS:
        blocks.append(
            f"""public EndpointBuilder WebApplication.MapMethods(
    WebApplication self,
    string path,
    List<string> methods,
    {delegate} handler
)
{{
    return self.MapTypedMethods(
        path, methods, RouteHandler.{variant}(handler)
    );
}}"""
        )
    return "\n\n".join(blocks)


def route_group_overloads():
    blocks = []
    for method, _ in VERBS:
        for delegate, _ in VARIANTS:
            blocks.append(
                f"""public EndpointBuilder RouteGroup.{method}(
    RouteGroup self, string pattern, {delegate} handler
)
{{
    return self.TrackEndpoint(
        self.Application.{method}(
            GroupPattern(self.Prefix, pattern), handler
        )
    );
}}"""
            )
    for delegate, _ in VARIANTS:
        blocks.append(
            f"""public EndpointBuilder RouteGroup.MapMethods(
    RouteGroup self,
    string pattern,
    List<string> methods,
    {delegate} handler
)
{{
    return self.TrackEndpoint(
        self.Application.MapMethods(
            GroupPattern(self.Prefix, pattern), methods, handler
        )
    );
}}"""
        )
    return "\n\n".join(blocks)


def replace_block(source, name, generated):
    begin = f"// BEGIN GENERATED TYPED ROUTE OVERLOADS: {name}"
    end = f"// END GENERATED TYPED ROUTE OVERLOADS: {name}"
    pattern = re.compile(
        rf"(?<={re.escape(begin)}\n).*?(?={re.escape(end)})",
        re.DOTALL,
    )
    updated, count = pattern.subn(generated + "\n", source)
    if count != 1:
        raise RuntimeError(f"expected one generated block for {name}")
    return updated


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if aster/web.as is not synchronized with this registry",
    )
    arguments = parser.parse_args()

    original = ASTER_WEB.read_text(encoding="utf-8")
    generated = replace_block(
        original, "WEBAPPLICATION", web_application_overloads()
    )
    generated = replace_block(
        generated, "ROUTEGROUP", route_group_overloads()
    )
    if arguments.check:
        if generated != original:
            raise SystemExit("Aster Web typed-route overloads are out of date")
        return
    ASTER_WEB.write_text(generated, encoding="utf-8")


if __name__ == "__main__":
    main()
