namespace Docs.Model;

public struct Document<Metadata> {
    Metadata metadata;
    string title;
    string body;
}

public Document<Metadata> document<Metadata>(
    Metadata metadata,
    string title,
    string body
) {
    return new Document {
        metadata = metadata,
        title = title,
        body = body,
    };
}
