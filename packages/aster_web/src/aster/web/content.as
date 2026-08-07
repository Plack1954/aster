namespace Aster.Web.Content;

public delegate Result<T, string> ContentDecoder<T>(
    ContentDocument document
);
public delegate T ContentValueDecoder<T>(ContentDocument document);

using Aster.Content;
using System.IO;
using System.Text;

public struct ContentField
{
    string name;
    string value;
}

public struct ContentDocument
{
    string path;
    List<ContentField> fields;
    string body;
}

private bool ContentSpace(byte value)
{
    return value == 32 || value == 9 || value == 13;
}

private string ContentTrim(string value)
{
    nuint start = 0;
    nuint end = value.Length;
    while (start < end && ContentSpace(StringByteAt(value, start)))
    {
        start += 1;
    }
    while (end > start && ContentSpace(StringByteAt(value, end - 1)))
    {
        end -= 1;
    }
    return StringSlice(value, start, end);
}

private Result<string, string> ContentScalar(string raw)
{
    nuint length = raw.Length;
    if (length >= 2 && StringByteAt(raw, 0) == 34 &&
        StringByteAt(raw, length - 1) == 34)
    {
        return Result.Ok(StringSlice(raw, 1, length - 1));
    }
    if (length > 0 && (StringByteAt(raw, 0) == 34 ||
        StringByteAt(raw, length - 1) == 34))
    {
        return Result.Err("unterminated quoted frontmatter value");
    }
    return Result.Ok(raw);
}

private bool ContentFieldsContain(List<ContentField> fields, string name)
{
    foreach (ContentField field in fields)
    {
        if (field.name == name) { return true; }
    }
    return false;
}

private Result<ContentDocument, string> ParseContentDocument(
    string path,
    string source
)
{
    nuint length = source.Length;
    nuint firstEnd = 0;
    while (firstEnd < length && StringByteAt(source, firstEnd) != 10)
    {
        firstEnd += 1;
    }
    if (ContentTrim(StringSlice(source, 0, firstEnd)) != "+++")
    {
        return Result.Err("content file must begin with `+++`");
    }

    List<ContentField> fields = new();
    nuint cursor = firstEnd < length ? firstEnd + 1 : length;
    while (cursor < length)
    {
        nuint end = cursor;
        while (end < length && StringByteAt(source, end) != 10)
        {
            end += 1;
        }
        string line = ContentTrim(StringSlice(source, cursor, end));
        if (line == "+++")
        {
            nuint bodyStart = end < length ? end + 1 : length;
            return Result.Ok(new()
            {
                path = path,
                fields = fields,
                body = StringSlice(source, bodyStart, length)
            });
        }
        if (line.Length != 0)
        {
            nuint equals = 0;
            while (equals < line.Length &&
                   StringByteAt(line, equals) != 61)
            {
                equals += 1;
            }
            if (equals == line.Length)
            {
                return Result.Err("frontmatter field requires `=`");
            }
            string name = ContentTrim(StringSlice(line, 0, equals));
            if (name.Length == 0)
            {
                return Result.Err("frontmatter field name cannot be empty");
            }
            if (ContentFieldsContain(fields, name))
            {
                return Result.Err("duplicate frontmatter field");
            }
            string raw = ContentTrim(StringSlice(
                line, equals + 1, line.Length
            ));
            string scalar = "";
            switch (ContentScalar(raw))
            {
                case Result.Err(error): { return Result.Err(error); }
                case Result.Ok(value): { scalar = value; }
            }
            fields.Add(new() { name = name, value = scalar });
        }
        cursor = end < length ? end + 1 : length;
    }
    return Result.Err("content file requires closing `+++`");
}

private Result<string, string> ContentRequiredFrom(
    List<ContentField> fields,
    string name
)
{
    foreach (ContentField field in fields)
    {
        if (field.name == name) { return Result.Ok(field.value); }
    }
    return Result.Err("missing required frontmatter field");
}

private T ContentResultOrThrow<T>(Result<T, string> result)
{
    switch (result)
    {
        case Result.Ok(value): { return value; }
        case Result.Err(error): { throw new Exception(error); }
    }
}

public Result<string, string> ContentDocument.TryRequired(
    ContentDocument self,
    string name
)
{
    return ContentRequiredFrom(self.fields, name);
}

public string ContentDocument.Required(
    ContentDocument self,
    string name
)
{
    return ContentResultOrThrow(ContentRequiredFrom(self.fields, name));
}

private Result<List<string>, string> ContentStringList(string raw)
{
    nuint length = raw.Length;
    if (length < 2 || StringByteAt(raw, 0) != 91 ||
        StringByteAt(raw, length - 1) != 93)
    {
        return Result.Err("frontmatter string list requires `[...]`");
    }
    List<string> values = new();
    nuint cursor = 1;
    while (cursor + 1 < length)
    {
        while (cursor + 1 < length &&
               (ContentSpace(StringByteAt(raw, cursor)) ||
                StringByteAt(raw, cursor) == 44))
        {
            cursor += 1;
        }
        if (cursor + 1 == length) { break; }
        if (StringByteAt(raw, cursor) != 34)
        {
            return Result.Err("frontmatter string list requires quoted values");
        }
        nuint start = cursor + 1;
        cursor = start;
        while (cursor < length && StringByteAt(raw, cursor) != 34)
        {
            cursor += 1;
        }
        if (cursor >= length)
        {
            return Result.Err("unterminated frontmatter string list value");
        }
        values.Add(StringSlice(raw, start, cursor));
        cursor += 1;
        while (cursor + 1 < length && ContentSpace(StringByteAt(raw, cursor)))
        {
            cursor += 1;
        }
        if (cursor + 1 < length && StringByteAt(raw, cursor) != 44)
        {
            return Result.Err("frontmatter string list values require `,`");
        }
    }
    return Result.Ok(values);
}

public Result<List<string>, string> ContentDocument.TryStrings(
    ContentDocument self,
    string name
)
{
    switch (ContentRequiredFrom(self.fields, name))
    {
        case Result.Err(error): { return Result.Err(error); }
        case Result.Ok(raw): { return ContentStringList(raw); }
    }
}

public List<string> ContentDocument.Strings(
    ContentDocument self,
    string name
)
{
    return ContentResultOrThrow(ContentStringList(
        ContentResultOrThrow(ContentRequiredFrom(self.fields, name))
    ));
}

public Result<ContentDocument, string> TryLoadContentDocument(string path)
{
    switch (NativeFileOpen(path, "rb"))
    {
        case Result.Err(error): { return Result.Err(error); }
        case Result.Ok(file): {
            switch (NativeFileReadAll(file))
            {
                case Result.Err(error): { return Result.Err(error); }
                case Result.Ok(source): {
                    return ParseContentDocument(path, source);
                }
            }
        }
    }
}

public ContentDocument LoadContentDocument(string path)
{
    return ContentResultOrThrow(
        ParseContentDocument(path, File.ReadAllText(path))
    );
}

public Result<List<ContentDocument>, string> TryLoadContentDirectory(
    string root,
    string suffix
)
{
    switch (DiscoverFiles(root, suffix))
    {
        case Result.Err(error): { return Result.Err(error); }
        case Result.Ok(paths): {
            List<ContentDocument> documents = new();
            foreach (string path in paths)
            {
                switch (TryLoadContentDocument(path))
                {
                    case Result.Err(error): { return Result.Err(error); }
                    case Result.Ok(document): { documents.Add(document); }
                }
            }
            return Result.Ok(documents);
        }
    }
}

public List<ContentDocument> LoadContentDirectory(
    string root,
    string suffix
)
{
    List<string> paths = ContentResultOrThrow(DiscoverFiles(root, suffix));
    List<ContentDocument> documents = new();
    foreach (string path in paths)
    {
        documents.Add(LoadContentDocument(path));
    }
    return documents;
}

public Result<List<T>, string> TryDecodeContentDirectory<T>(
    string root,
    string suffix,
    ContentDecoder<T> decoder
)
{
    switch (TryLoadContentDirectory(root, suffix))
    {
        case Result.Err(error): { return Result.Err(error); }
        case Result.Ok(documents): {
            List<T> values = new();
            foreach (ContentDocument document in documents)
            {
                switch (decoder(document))
                {
                    case Result.Err(error): { return Result.Err(error); }
                    case Result.Ok(value): { values.Add(value); }
                }
            }
            return Result.Ok(values);
        }
    }
}

public List<T> DecodeContentDirectory<T>(
    string root,
    string suffix,
    ContentValueDecoder<T> decoder
)
{
    List<ContentDocument> documents = LoadContentDirectory(root, suffix);
    List<T> values = new();
    foreach (ContentDocument document in documents)
    {
        values.Add(decoder(document));
    }
    return values;
}
