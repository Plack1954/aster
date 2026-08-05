namespace Docs.Pages;

using Docs.Model;
using Aster.Html;
using Aster.Core;

private Html RenderDocument(
    Document<Pair<string, string>> page
) {
    return <section>
        <h2>{page.title}</h2>
        <p>{page.body}</p>
    </section>;
}

public Html home(string path) {
    Pair<string, string> metadata =
        pair("section", "home");
    Document<Pair<string, string>> page = document(
        metadata,
        "Aster language",
        "Typed systems programming with deterministic ownership.",
    );
    return RenderDocument(page);
}

public Html guide(string path) {
    Pair<string, string> metadata =
        pair("section", "guide");
    Document<Pair<string, string>> page = document(
        metadata,
        "Language guide",
        "Generics, callbacks, HTML, and HTTP are ordinary Aster.",
    );
    return RenderDocument(page);
}

public Html missing(string path) {
    return <section>
        <h2>Document not found</h2>
        <p>{path}</p>
    </section>;
}
