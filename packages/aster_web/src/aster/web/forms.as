namespace Aster.Web.Forms;

using Aster.Web;
using Aster.Memory;
using System.IO;
using System.Text;

public struct FormOptions
{
    long MemoryBufferThreshold;
    long MultipartBodyLengthLimit;
    long MultipartFileLengthLimit;
    long ValueLengthLimit;
    long MultipartHeadersLengthLimit;
    int MaxPartCount;
    int MaxFileCount;
    int MaxFieldCount;
    string TemporaryDirectory;
}

public FormOptions FormOptions()
{
    return new()
    {
        MemoryBufferThreshold = 65536,
        MultipartBodyLengthLimit = 134217728,
        MultipartFileLengthLimit = 134217728,
        ValueLengthLimit = 1048576,
        MultipartHeadersLengthLimit = 16384,
        MaxPartCount = 1024,
        MaxFileCount = 128,
        MaxFieldCount = 1024,
        TemporaryDirectory = "."
    };
}

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
    NativeHandle? temporaryFile;
    long length;

    public string Name => name;
    public string FileName => fileName;
    public string ContentType => contentType;
    public long Length => this.length;
    public bool IsBuffered => this.temporaryFile == null;
}

public struct FormCollection
{
    List<FormField> fields;
    List<FormFile> files;
}

public MemoryStream FormFile.OpenReadStream(FormFile self)
{
    switch (self.temporaryFile)
    {
        case Option.Some(file): {
            return File.OpenRead(NativeFileTemporaryPath(file));
        }
        case Option.None: {
            MemoryStream stream = MemoryStream.Create();
            stream.Write(StringAsByteSlice(self.bytes));
            stream.Seek(0, SeekOrigin.Begin);
            return stream;
        }
    }
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

private void ValidateFormOptions(FormOptions options)
{
    if (options.MemoryBufferThreshold < 0 ||
        options.MultipartBodyLengthLimit < 0 ||
        options.MultipartFileLengthLimit < 0 ||
        options.ValueLengthLimit < 0 ||
        options.MultipartHeadersLengthLimit < 0 ||
        options.MaxPartCount < 0 || options.MaxFileCount < 0 ||
        options.MaxFieldCount < 0 || options.TemporaryDirectory.Length == 0)
    {
        throw new ArgumentException("FormOptions limits are invalid.");
    }
}

private Option<nuint> FindMultipartBytes(
    ReadOnlySpan<byte> source,
    string value,
    nuint start
)
{
    nuint sourceLength = ByteSliceLen(source);
    if (value.Length == 0 || value.Length > sourceLength ||
        start > sourceLength - value.Length)
    {
        return Option.None;
    }
    nuint last = sourceLength - value.Length;
    for (nuint index = start; index <= last; index += 1)
    {
        nuint offset = 0;
        while (offset < value.Length &&
            ByteSliceAt(source, index + offset) ==
                StringByteAt(value, offset))
        {
            offset += 1;
        }
        if (offset == value.Length) { return Option.Some(index); }
    }
    return Option.None;
}

private Option<nuint> FindMultipartBoundary(
    ReadOnlySpan<byte> source,
    string marker,
    nuint start
)
{
    nuint cursor = start;
    nuint sourceLength = ByteSliceLen(source);
    while (cursor < sourceLength)
    {
        switch (FindMultipartBytes(source, marker, cursor))
        {
            case Option.None: { return Option.None; }
            case Option.Some(found): {
                nuint after = found + marker.Length;
                if (after + 1 < sourceLength &&
                    ((ByteSliceAt(source, after) == 45 &&
                      ByteSliceAt(source, after + 1) == 45) ||
                     (ByteSliceAt(source, after) == 13 &&
                      ByteSliceAt(source, after + 1) == 10)))
                {
                    return Option.Some(found);
                }
                cursor = found + 1;
            }
        }
    }
    return Option.None;
}

private string MultipartString(
    ReadOnlySpan<byte> source,
    nuint start,
    nuint end
)
{
    switch (ByteSliceToString(source, start, end))
    {
        case Result.Ok(value): { return value; }
        case Result.Err(error): { throw new FormatException(error); }
    }
}

private NativeHandle SpillMultipartFile(
    ReadOnlySpan<byte> body,
    nuint start,
    nuint end,
    string directory
)
{
    switch (NativeFileCreateTemporary(directory))
    {
        case Result.Ok(temporary): {
            FileStream output = File.Create(
                NativeFileTemporaryPath(temporary)
            );
            try
            {
                nuint cursor = start;
                while (cursor < end)
                {
                    nuint count = end - cursor;
                    if (count > 65536) { count = 65536; }
                    output.Write(ByteSliceRange(body, cursor, count));
                    cursor += count;
                }
                output.Flush();
            }
            finally
            {
                output.Close();
            }
            return temporary;
        }
        case Result.Err(error): { throw new IOException(error); }
    }
}

private FormCollection ParseMultipartForm(
    ReadOnlySpan<byte> body,
    string contentType,
    FormOptions options
)
{
    ValidateFormOptions(options);
    nuint bodyLength = ByteSliceLen(body);
    if ((long)bodyLength > options.MultipartBodyLengthLimit)
    {
        throw new InvalidOperationException(
            "Multipart body exceeds the configured limit."
        );
    }
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
    if (bodyLength < marker.Length)
    {
        throw new FormatException("Multipart body is missing its boundary.");
    }
    for (nuint index = 0; index < marker.Length; index += 1)
    {
        if (ByteSliceAt(body, index) != StringByteAt(marker, index))
        {
            throw new FormatException(
                "Multipart body is missing its boundary."
            );
        }
    }

    List<FormField> fields = new();
    List<FormFile> files = new();
    nuint cursor = marker.Length;
    int partCount = 0;
    int fileCount = 0;
    int fieldCount = 0;
    bool complete = false;
    while (!complete)
    {
        if (cursor + 1 < bodyLength &&
            ByteSliceAt(body, cursor) == 45 &&
            ByteSliceAt(body, cursor + 1) == 45)
        {
            complete = true;
            continue;
        }
        partCount += 1;
        if (partCount > options.MaxPartCount)
        {
            throw new InvalidOperationException(
                "Multipart form contains too many parts."
            );
        }
        if (cursor + 1 >= bodyLength ||
            ByteSliceAt(body, cursor) != 13 ||
            ByteSliceAt(body, cursor + 1) != 10)
        {
            throw new FormatException("Malformed multipart boundary.");
        }
        cursor += 2;
        nuint headerEnd = 0;
        switch (FindMultipartBytes(body, "\r\n\r\n", cursor))
        {
            case Option.Some(value): { headerEnd = value; }
            case Option.None: {
                throw new FormatException("Multipart headers are incomplete.");
            }
        }
        if ((long)(headerEnd - cursor) >
            options.MultipartHeadersLengthLimit)
        {
            throw new InvalidOperationException(
                "Multipart headers exceed the configured limit."
            );
        }
        string headers = MultipartString(body, cursor, headerEnd);
        nuint contentStart = headerEnd + 4;
        nuint contentEnd = 0;
        switch (FindMultipartBoundary(body, nextMarker, contentStart))
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
        nuint contentLength = contentEnd - contentStart;
        switch (HeaderParameter(disposition, "filename"))
        {
            case Option.Some(fileName): {
                fileCount += 1;
                if (fileCount > options.MaxFileCount ||
                    (long)contentLength >
                        options.MultipartFileLengthLimit)
                {
                    throw new InvalidOperationException(
                        "Multipart file exceeds the configured limits."
                    );
                }
                string partContentType = "application/octet-stream";
                switch (PartHeader(headers, "Content-Type"))
                {
                    case Option.Some(value): { partContentType = value; }
                    case Option.None: {}
                }
                string bytes = "";
                NativeHandle? temporaryFile = null;
                if ((long)contentLength <= options.MemoryBufferThreshold)
                {
                    bytes = MultipartString(
                        body, contentStart, contentEnd
                    );
                }
                else
                {
                    temporaryFile = SpillMultipartFile(
                        body, contentStart, contentEnd,
                        options.TemporaryDirectory
                    );
                }
                files.Add(new()
                {
                    name = name,
                    fileName = fileName,
                    contentType = partContentType,
                    bytes = bytes,
                    temporaryFile = temporaryFile,
                    length = (long)contentLength
                });
            }
            case Option.None: {
                fieldCount += 1;
                if (fieldCount > options.MaxFieldCount ||
                    (long)contentLength > options.ValueLengthLimit)
                {
                    throw new InvalidOperationException(
                        "Multipart field exceeds the configured limits."
                    );
                }
                fields.Add(new()
                {
                    name = name,
                    value = MultipartString(
                        body, contentStart, contentEnd
                    )
                });
            }
        }
        cursor = contentEnd + 2 + marker.Length;
    }
    return new() { fields = fields, files = files };
}

public FormCollection Request.ReadForm(Request self)
{
    return self.ReadForm(FormOptions());
}

public FormCollection Request.ReadForm(Request self, FormOptions options)
{
    string contentType = self.ContentType;
    string body = self.Body;
    if (FormMediaTypeIs(contentType, "application/x-www-form-urlencoded"))
    {
        return ParseUrlEncodedForm(body);
    }
    if (FormMediaTypeIs(contentType, "multipart/form-data"))
    {
        return ParseMultipartForm(self.BodyBytes(), contentType, options);
    }
    throw new InvalidOperationException(
        "The request does not contain form data."
    );
}
