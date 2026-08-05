namespace Lime.Forms;

using Lime;
using System.IO;
using System.Text;

public struct FormField
{
    string name;
    string value;
}

public struct FormFile
{
    string name;
    string fileName;
    string contentType;
    string bytes;

    public string Name => name;
    public string FileName => fileName;
    public string ContentType => contentType;
    public long Length => (long)this.bytes.Length;
}

public struct FormCollection
{
    List<FormField> fields;
    List<FormFile> files;
}

public MemoryStream FormFile.OpenReadStream(FormFile self)
{
    List<byte> bytes = new();
    for (nuint index = 0; index < self.bytes.Length; index += 1)
    {
        bytes.Add(StringByteAt(self.bytes, index));
    }
    return MemoryStream.Create(bytes);
}

public Option<string> FormCollection.Get(FormCollection self, string name)
{
    foreach (FormField field in self.fields)
    {
        if (field.name == name) { return Option.Some(field.value); }
    }
    return Option.None;
}

public List<string> FormCollection.GetValues(
    FormCollection self,
    string name
)
{
    List<string> values = new();
    foreach (FormField field in self.fields)
    {
        if (field.name == name) { values.Add(field.value); }
    }
    return values;
}

public Option<FormFile> FormCollection.GetFile(
    FormCollection self,
    string name
)
{
    foreach (FormFile file in self.files)
    {
        if (file.name == name) { return Option.Some(file); }
    }
    return Option.None;
}

public List<FormFile> FormCollection.Files(FormCollection self)
{
    return self.files;
}

private Option<nuint> FindBytes(string source, string value, nuint start)
{
    if (value.Length == 0 || value.Length > source.Length)
    {
        return Option.None;
    }
    nuint last = source.Length - value.Length;
    nuint index = start;
    while (index <= last)
    {
        nuint offset = 0;
        while (offset < value.Length &&
            StringByteAt(source, index + offset) ==
                StringByteAt(value, offset))
        {
            offset += 1;
        }
        if (offset == value.Length) { return Option.Some(index); }
        index += 1;
    }
    return Option.None;
}

private byte FormAsciiLower(byte value)
{
    if (value >= 65 && value <= 90) { return value + 32; }
    return value;
}

private bool FormAsciiEqual(string left, string right)
{
    if (left.Length != right.Length) { return false; }
    for (nuint index = 0; index < left.Length; index += 1)
    {
        if (FormAsciiLower(StringByteAt(left, index)) !=
            FormAsciiLower(StringByteAt(right, index)))
        {
            return false;
        }
    }
    return true;
}

private bool FormMediaTypeIs(string value, string expected)
{
    nuint index = 0;
    while (index < value.Length &&
        (StringByteAt(value, index) == 32 ||
         StringByteAt(value, index) == 9))
    {
        index += 1;
    }
    if (expected.Length > value.Length - index) { return false; }
    for (nuint offset = 0; offset < expected.Length; offset += 1)
    {
        if (FormAsciiLower(StringByteAt(value, index + offset)) !=
            FormAsciiLower(StringByteAt(expected, offset)))
        {
            return false;
        }
    }
    index += expected.Length;
    while (index < value.Length &&
        (StringByteAt(value, index) == 32 ||
         StringByteAt(value, index) == 9))
    {
        index += 1;
    }
    return index == value.Length || StringByteAt(value, index) == 59;
}

private string TrimHttpWhitespace(string value)
{
    nuint start = 0;
    nuint end = value.Length;
    while (start < end &&
        (StringByteAt(value, start) == 32 ||
         StringByteAt(value, start) == 9))
    {
        start += 1;
    }
    while (end > start &&
        (StringByteAt(value, end - 1) == 32 ||
         StringByteAt(value, end - 1) == 9))
    {
        end -= 1;
    }
    return value.Substring(start, end - start);
}

private string UnquoteHeaderValue(string value)
{
    string trimmed = TrimHttpWhitespace(value);
    if (trimmed.Length >= 2 && StringByteAt(trimmed, 0) == 34 &&
        StringByteAt(trimmed, trimmed.Length - 1) == 34)
    {
        return trimmed.Substring(1, trimmed.Length - 2);
    }
    return trimmed;
}

private Option<string> HeaderParameter(string value, string parameter)
{
    nuint cursor = 0;
    while (cursor < value.Length)
    {
        while (cursor < value.Length &&
            (StringByteAt(value, cursor) == 32 ||
             StringByteAt(value, cursor) == 9 ||
             StringByteAt(value, cursor) == 59))
        {
            cursor += 1;
        }
        nuint nameStart = cursor;
        while (cursor < value.Length &&
            StringByteAt(value, cursor) != 61 &&
            StringByteAt(value, cursor) != 59)
        {
            cursor += 1;
        }
        nuint nameEnd = cursor;
        while (nameEnd > nameStart &&
            (StringByteAt(value, nameEnd - 1) == 32 ||
             StringByteAt(value, nameEnd - 1) == 9))
        {
            nameEnd -= 1;
        }
        if (cursor < value.Length && StringByteAt(value, cursor) == 61)
        {
            cursor += 1;
            nuint valueStart = cursor;
            nuint valueEnd = cursor;
            if (cursor < value.Length && StringByteAt(value, cursor) == 34)
            {
                valueStart = cursor;
                cursor += 1;
                while (cursor < value.Length &&
                    StringByteAt(value, cursor) != 34)
                {
                    cursor += 1;
                }
                if (cursor >= value.Length)
                {
                    throw new FormatException(
                        $"Unterminated quoted form header parameter in {value}."
                    );
                }
                cursor += 1;
                valueEnd = cursor;
            }
            else
            {
                while (cursor < value.Length &&
                    StringByteAt(value, cursor) != 59)
                {
                    cursor += 1;
                }
                valueEnd = cursor;
            }
            string foundName = value.Substring(
                nameStart, nameEnd - nameStart
            );
            if (FormAsciiEqual(foundName, parameter))
            {
                return Option.Some(UnquoteHeaderValue(
                    value.Substring(
                        valueStart,
                        valueEnd - valueStart
                    )
                ));
            }
        }
        while (cursor < value.Length &&
            StringByteAt(value, cursor) != 59)
        {
            cursor += 1;
        }
    }
    return Option.None;
}

private Option<string> PartHeader(string headers, string name)
{
    nuint cursor = 0;
    while (cursor < headers.Length)
    {
        nuint end = cursor;
        while (end < headers.Length &&
            !(end + 1 < headers.Length &&
              StringByteAt(headers, end) == 13 &&
              StringByteAt(headers, end + 1) == 10))
        {
            end += 1;
        }
        nuint colon = cursor;
        while (colon < end && StringByteAt(headers, colon) != 58)
        {
            colon += 1;
        }
        if (colon < end && FormAsciiEqual(
            headers.Substring(
                cursor, colon - cursor
            ), name))
        {
            return Option.Some(TrimHttpWhitespace(
                headers.Substring(
                    colon + 1, end - colon - 1
                )
            ));
        }
        cursor = end < headers.Length ? end + 2 : headers.Length;
    }
    return Option.None;
}

private FormCollection ParseUrlEncodedForm(string body)
{
    List<FormField> fields = new();
    List<FormFile> files = new();
    nuint pairStart = 0;
    while (pairStart < body.Length)
    {
        nuint pairEnd = pairStart;
        while (pairEnd < body.Length &&
            StringByteAt(body, pairEnd) != 38)
        {
            pairEnd += 1;
        }
        nuint separator = pairStart;
        while (separator < pairEnd &&
            StringByteAt(body, separator) != 61)
        {
            separator += 1;
        }
        nuint valueStart = separator < pairEnd ? separator + 1 : pairEnd;
        string name = "";
        string value = "";
        switch (UrlDecode(body.Substring(
            pairStart, separator - pairStart
        )))
        {
            case Result.Ok(decoded): { name = decoded; }
            case Result.Err(error): { throw new FormatException(error); }
        }
        switch (UrlDecode(body.Substring(
            valueStart, pairEnd - valueStart
        )))
        {
            case Result.Ok(decoded): { value = decoded; }
            case Result.Err(error): { throw new FormatException(error); }
        }
        fields.Add(new() { name = name, value = value });
        pairStart = pairEnd + 1;
    }
    return new() { fields = fields, files = files };
}

private FormCollection ParseMultipartForm(
    string body,
    string contentType
)
{
    string boundary = "";
    switch (HeaderParameter(contentType, "boundary"))
    {
        case Option.Some(value): { boundary = value; }
        case Option.None: {
            throw new FormatException("Multipart boundary is missing.");
        }
    }
    if (boundary.Length == 0 || boundary.Length > 200)
    {
        throw new FormatException("Multipart boundary is invalid.");
    }

    string marker = $"--{boundary}";
    string nextMarker = $"\r\n--{boundary}";
    if (body.Length < marker.Length ||
        body.Substring(0, marker.Length) != marker)
    {
        throw new FormatException("Multipart body is missing its boundary.");
    }

    List<FormField> fields = new();
    List<FormFile> files = new();
    nuint cursor = marker.Length;
    int partCount = 0;
    bool complete = false;
    while (!complete)
    {
        if (cursor + 1 < body.Length &&
            StringByteAt(body, cursor) == 45 &&
            StringByteAt(body, cursor + 1) == 45)
        {
            complete = true;
            continue;
        }
        if (cursor + 1 >= body.Length ||
            StringByteAt(body, cursor) != 13 ||
            StringByteAt(body, cursor + 1) != 10)
        {
            throw new FormatException("Malformed multipart boundary.");
        }
        cursor += 2;
        nuint headerEnd = 0;
        switch (FindBytes(body, "\r\n\r\n", cursor))
        {
            case Option.Some(value): { headerEnd = value; }
            case Option.None: {
                throw new FormatException("Multipart headers are incomplete.");
            }
        }
        string headers = body.Substring(
            cursor, headerEnd - cursor
        );
        nuint contentStart = headerEnd + 4;
        nuint contentEnd = 0;
        switch (FindBytes(body, nextMarker, contentStart))
        {
            case Option.Some(value): { contentEnd = value; }
            case Option.None: {
                throw new FormatException("Multipart part is incomplete.");
            }
        }

        string disposition = "";
        switch (PartHeader(headers, "Content-Disposition"))
        {
            case Option.Some(value): { disposition = value; }
            case Option.None: {
                throw new FormatException(
                    "Multipart part has no Content-Disposition header."
                );
            }
        }
        if (!FormMediaTypeIs(disposition, "form-data"))
        {
            throw new FormatException(
                "Multipart Content-Disposition must be form-data."
            );
        }
        string name = "";
        switch (HeaderParameter(disposition, "name"))
        {
            case Option.Some(value): { name = value; }
            case Option.None: {
                throw new FormatException("Multipart part name is missing.");
            }
        }
        string bytes = body.Substring(
            contentStart, contentEnd - contentStart
        );
        switch (HeaderParameter(disposition, "filename"))
        {
            case Option.Some(fileName): {
                string partContentType = "application/octet-stream";
                switch (PartHeader(headers, "Content-Type"))
                {
                    case Option.Some(value): { partContentType = value; }
                    case Option.None: {}
                }
                files.Add(new()
                {
                    name = name,
                    fileName = fileName,
                    contentType = partContentType,
                    bytes = bytes
                });
            }
            case Option.None: {
                fields.Add(new() { name = name, value = bytes });
            }
        }
        partCount += 1;
        if (partCount > 1024)
        {
            throw new InvalidOperationException(
                "Multipart form contains too many parts."
            );
        }
        cursor = contentEnd + 2 + marker.Length;
    }
    return new() { fields = fields, files = files };
}

public FormCollection Request.ReadForm(Request self)
{
    string contentType = self.ContentType;
    string body = self.Body;
    if (FormMediaTypeIs(contentType, "application/x-www-form-urlencoded"))
    {
        return ParseUrlEncodedForm(body);
    }
    if (FormMediaTypeIs(contentType, "multipart/form-data"))
    {
        return ParseMultipartForm(body, contentType);
    }
    throw new InvalidOperationException(
        "The request does not contain form data."
    );
}
