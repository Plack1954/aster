namespace System.Text.Json;

using System.Text;

public enum JsonValueKind
{
    Undefined,
    Object,
    Array,
    String,
    Number,
    True,
    False,
    Null,
}

// JsonElement is a cheap immutable view into its reference-counted UTF-8
// source. Start and End delimit the original JSON token in bytes.
public struct JsonElement
{
    string Source;
    nuint Start;
    nuint End;
    JsonValueKind ValueKind;
}

public struct JsonDocument
{
    JsonElement RootElement;
}

public struct JsonSerializer {}

private enum JsonWriterContainer
{
    Object,
    Array,
}

private struct JsonWriterFrame
{
    JsonWriterContainer Container;
    int ValueCount;
    bool ExpectsValue;
}

// A forward-only structural writer. It owns its output and rejects invalid
// object/array transitions instead of emitting malformed JSON.
public struct JsonWriter
{
    StringBuilder Output;
    List<JsonWriterFrame> Frames;
    bool RootWritten;
}

public JsonWriter JsonWriter.Create()
{
    return new()
    {
        Output = new(),
        Frames = new(),
        RootWritten = false
    };
}

private struct JsonParser
{
    string Source;
    nuint Position;
}

private void JsonFail()
{
    throw new JsonException("The JSON value could not be parsed.");
}

private bool JsonIsWhiteSpace(byte value)
{
    return value == 32 || value == 9 || value == 10 || value == 13;
}

private bool JsonIsDigit(byte value)
{
    return value >= 48 && value <= 57;
}

private int JsonHexValue(byte value)
{
    if (value >= 48 && value <= 57) { return (int)(value - 48); }
    if (value >= 65 && value <= 70) { return (int)(value - 65) + 10; }
    if (value >= 97 && value <= 102) { return (int)(value - 97) + 10; }
    JsonFail();
    return 0;
}

private void JsonSkipWhiteSpace(ref JsonParser parser)
{
    while (parser.Position < parser.Source.Length &&
           JsonIsWhiteSpace(parser.Source[parser.Position]))
    {
        parser.Position += 1;
    }
}

private uint JsonReadHex4(ref JsonParser parser)
{
    if (parser.Position + 4 > parser.Source.Length) { JsonFail(); }
    uint value = 0;
    for (int index = 0; index < 4; index++)
    {
        value = value * 16 + (uint)JsonHexValue(parser.Source[parser.Position]);
        parser.Position += 1;
    }
    return value;
}

private void JsonConsumeUtf8(ref JsonParser parser)
{
    byte first = parser.Source[parser.Position];
    nuint remaining = parser.Source.Length - parser.Position;
    int count = 0;
    uint codePoint = 0;
    uint minimum = 0;

    if (first >= 194 && first <= 223)
    {
        count = 2;
        codePoint = (uint)(first & 31);
        minimum = 128;
    }
    else if (first >= 224 && first <= 239)
    {
        count = 3;
        codePoint = (uint)(first & 15);
        minimum = 2048;
    }
    else if (first >= 240 && first <= 244)
    {
        count = 4;
        codePoint = (uint)(first & 7);
        minimum = 65536;
    }
    else
    {
        JsonFail();
    }

    if (remaining < (nuint)count) { JsonFail(); }
    for (int index = 1; index < count; index++)
    {
        byte continuation = parser.Source[parser.Position + (nuint)index];
        if ((continuation & 192) != 128) { JsonFail(); }
        codePoint = codePoint * 64 + (uint)(continuation & 63);
    }
    if (codePoint < minimum || codePoint > 1114111 ||
        (codePoint >= 55296 && codePoint <= 57343))
    {
        JsonFail();
    }
    parser.Position += (nuint)count;
}

private void JsonParseStringToken(ref JsonParser parser)
{
    if (parser.Position >= parser.Source.Length ||
        parser.Source[parser.Position] != 34)
    {
        JsonFail();
    }
    parser.Position += 1;
    while (parser.Position < parser.Source.Length)
    {
        byte current = parser.Source[parser.Position];
        if (current == 34)
        {
            parser.Position += 1;
            return;
        }
        if (current < 32) { JsonFail(); }
        if (current == 92)
        {
            parser.Position += 1;
            if (parser.Position >= parser.Source.Length) { JsonFail(); }
            byte escape = parser.Source[parser.Position];
            parser.Position += 1;
            if (escape == 34 || escape == 47 || escape == 92 ||
                escape == 98 || escape == 102 || escape == 110 ||
                escape == 114 || escape == 116)
            {
                continue;
            }
            if (escape != 117) { JsonFail(); }
            uint codeUnit = JsonReadHex4(ref parser);
            if (codeUnit >= 55296 && codeUnit <= 56319)
            {
                if (parser.Position + 6 > parser.Source.Length ||
                    parser.Source[parser.Position] != 92 ||
                    parser.Source[parser.Position + 1] != 117)
                {
                    JsonFail();
                }
                parser.Position += 2;
                uint low = JsonReadHex4(ref parser);
                if (low < 56320 || low > 57343) { JsonFail(); }
            }
            else if (codeUnit >= 56320 && codeUnit <= 57343)
            {
                JsonFail();
            }
            continue;
        }
        if (current >= 128)
        {
            JsonConsumeUtf8(ref parser);
            continue;
        }
        parser.Position += 1;
    }
    JsonFail();
}

private void JsonExpectLiteral(ref JsonParser parser, string literal)
{
    if (parser.Position + literal.Length > parser.Source.Length)
    {
        JsonFail();
    }
    for (nuint index = 0; index < literal.Length; index++)
    {
        if (parser.Source[parser.Position + index] != literal[index])
        {
            JsonFail();
        }
    }
    parser.Position += literal.Length;
}

private void JsonParseNumber(ref JsonParser parser)
{
    if (parser.Source[parser.Position] == 45)
    {
        parser.Position += 1;
        if (parser.Position == parser.Source.Length) { JsonFail(); }
    }

    if (parser.Source[parser.Position] == 48)
    {
        parser.Position += 1;
        if (parser.Position < parser.Source.Length &&
            JsonIsDigit(parser.Source[parser.Position]))
        {
            JsonFail();
        }
    }
    else
    {
        if (parser.Source[parser.Position] < 49 ||
            parser.Source[parser.Position] > 57)
        {
            JsonFail();
        }
        while (parser.Position < parser.Source.Length &&
               JsonIsDigit(parser.Source[parser.Position]))
        {
            parser.Position += 1;
        }
    }

    if (parser.Position < parser.Source.Length &&
        parser.Source[parser.Position] == 46)
    {
        parser.Position += 1;
        if (parser.Position == parser.Source.Length ||
            !JsonIsDigit(parser.Source[parser.Position]))
        {
            JsonFail();
        }
        while (parser.Position < parser.Source.Length &&
               JsonIsDigit(parser.Source[parser.Position]))
        {
            parser.Position += 1;
        }
    }

    if (parser.Position < parser.Source.Length &&
        (parser.Source[parser.Position] == 69 ||
         parser.Source[parser.Position] == 101))
    {
        parser.Position += 1;
        if (parser.Position < parser.Source.Length &&
            (parser.Source[parser.Position] == 43 ||
             parser.Source[parser.Position] == 45))
        {
            parser.Position += 1;
        }
        if (parser.Position == parser.Source.Length ||
            !JsonIsDigit(parser.Source[parser.Position]))
        {
            JsonFail();
        }
        while (parser.Position < parser.Source.Length &&
               JsonIsDigit(parser.Source[parser.Position]))
        {
            parser.Position += 1;
        }
    }
}

private JsonElement JsonParseValue(ref JsonParser parser, int depth)
{
    if (depth > 64) { JsonFail(); }
    JsonSkipWhiteSpace(ref parser);
    if (parser.Position >= parser.Source.Length) { JsonFail(); }

    nuint start = parser.Position;
    JsonValueKind kind = JsonValueKind.Undefined;
    byte current = parser.Source[parser.Position];

    if (current == 34)
    {
        kind = JsonValueKind.String;
        JsonParseStringToken(ref parser);
    }
    else if (current == 123)
    {
        kind = JsonValueKind.Object;
        parser.Position += 1;
        JsonSkipWhiteSpace(ref parser);
        if (parser.Position < parser.Source.Length &&
            parser.Source[parser.Position] == 125)
        {
            parser.Position += 1;
        }
        else
        {
            while (true)
            {
                JsonParseStringToken(ref parser);
                JsonSkipWhiteSpace(ref parser);
                if (parser.Position >= parser.Source.Length ||
                    parser.Source[parser.Position] != 58)
                {
                    JsonFail();
                }
                parser.Position += 1;
                JsonElement ignored = JsonParseValue(ref parser, depth + 1);
                JsonSkipWhiteSpace(ref parser);
                if (parser.Position >= parser.Source.Length) { JsonFail(); }
                if (parser.Source[parser.Position] == 125)
                {
                    parser.Position += 1;
                    break;
                }
                if (parser.Source[parser.Position] != 44) { JsonFail(); }
                parser.Position += 1;
                JsonSkipWhiteSpace(ref parser);
            }
        }
    }
    else if (current == 91)
    {
        kind = JsonValueKind.Array;
        parser.Position += 1;
        JsonSkipWhiteSpace(ref parser);
        if (parser.Position < parser.Source.Length &&
            parser.Source[parser.Position] == 93)
        {
            parser.Position += 1;
        }
        else
        {
            while (true)
            {
                JsonElement ignored = JsonParseValue(ref parser, depth + 1);
                JsonSkipWhiteSpace(ref parser);
                if (parser.Position >= parser.Source.Length) { JsonFail(); }
                if (parser.Source[parser.Position] == 93)
                {
                    parser.Position += 1;
                    break;
                }
                if (parser.Source[parser.Position] != 44) { JsonFail(); }
                parser.Position += 1;
            }
        }
    }
    else if (current == 116)
    {
        kind = JsonValueKind.True;
        JsonExpectLiteral(ref parser, "true");
    }
    else if (current == 102)
    {
        kind = JsonValueKind.False;
        JsonExpectLiteral(ref parser, "false");
    }
    else if (current == 110)
    {
        kind = JsonValueKind.Null;
        JsonExpectLiteral(ref parser, "null");
    }
    else
    {
        kind = JsonValueKind.Number;
        JsonParseNumber(ref parser);
    }

    return new()
    {
        Source = parser.Source,
        Start = start,
        End = parser.Position,
        ValueKind = kind,
    };
}

private JsonElement JsonParseComplete(string json)
{
    JsonParser parser = new()
    {
        Source = json,
        Position = 0,
    };
    JsonElement root = JsonParseValue(ref parser, 0);
    JsonSkipWhiteSpace(ref parser);
    if (parser.Position != json.Length) { JsonFail(); }
    return root;
}

private void JsonAppendCodePoint(ref StringBuilder builder, uint codePoint)
{
    if (codePoint <= 127)
    {
        builder.AppendByte((byte)codePoint);
    }
    else if (codePoint <= 2047)
    {
        builder.AppendByte((byte)(192 | (codePoint >> 6)));
        builder.AppendByte((byte)(128 | (codePoint & 63)));
    }
    else if (codePoint <= 65535)
    {
        builder.AppendByte((byte)(224 | (codePoint >> 12)));
        builder.AppendByte((byte)(128 | ((codePoint >> 6) & 63)));
        builder.AppendByte((byte)(128 | (codePoint & 63)));
    }
    else
    {
        builder.AppendByte((byte)(240 | (codePoint >> 18)));
        builder.AppendByte((byte)(128 | ((codePoint >> 12) & 63)));
        builder.AppendByte((byte)(128 | ((codePoint >> 6) & 63)));
        builder.AppendByte((byte)(128 | (codePoint & 63)));
    }
}

private string JsonDecodeString(string source, nuint start, nuint end)
{
    StringBuilder builder = new();
    JsonParser parser = new()
    {
        Source = source,
        Position = start + 1,
    };
    nuint contentEnd = end - 1;
    nuint plainStart = parser.Position;

    while (parser.Position < contentEnd)
    {
        if (source[parser.Position] != 92)
        {
            parser.Position += 1;
            continue;
        }
        if (parser.Position > plainStart)
        {
            builder.Append(source.Substring(
                plainStart, parser.Position - plainStart
            ));
        }
        parser.Position += 1;
        byte escape = source[parser.Position];
        parser.Position += 1;
        if (escape == 34) { builder.AppendByte(34); }
        else if (escape == 47) { builder.AppendByte(47); }
        else if (escape == 92) { builder.AppendByte(92); }
        else if (escape == 98) { builder.AppendByte(8); }
        else if (escape == 102) { builder.AppendByte(12); }
        else if (escape == 110) { builder.AppendByte(10); }
        else if (escape == 114) { builder.AppendByte(13); }
        else if (escape == 116) { builder.AppendByte(9); }
        else
        {
            uint codePoint = JsonReadHex4(ref parser);
            if (codePoint >= 55296 && codePoint <= 56319)
            {
                parser.Position += 2;
                uint low = JsonReadHex4(ref parser);
                codePoint = 65536 + ((codePoint - 55296) * 1024) +
                            (low - 56320);
            }
            JsonAppendCodePoint(ref builder, codePoint);
        }
        plainStart = parser.Position;
    }
    if (contentEnd > plainStart)
    {
        builder.Append(source.Substring(plainStart, contentEnd - plainStart));
    }
    return builder.ToString();
}

private byte JsonHexDigit(uint value)
{
    if (value < 10) { return (byte)(48 + value); }
    return (byte)(65 + value - 10);
}

private void JsonAppendHex4(ref StringBuilder builder, uint value)
{
    builder.AppendByte(92);
    builder.AppendByte(117);
    builder.AppendByte(JsonHexDigit((value >> 12) & 15));
    builder.AppendByte(JsonHexDigit((value >> 8) & 15));
    builder.AppendByte(JsonHexDigit((value >> 4) & 15));
    builder.AppendByte(JsonHexDigit(value & 15));
}

private uint JsonDecodeUtf8CodePoint(ref JsonParser parser)
{
    byte first = parser.Source[parser.Position];
    nuint remaining = parser.Source.Length - parser.Position;
    int count = 0;
    uint codePoint = 0;
    uint minimum = 0;
    if (first >= 194 && first <= 223)
    {
        count = 2;
        codePoint = (uint)(first & 31);
        minimum = 128;
    }
    else if (first >= 224 && first <= 239)
    {
        count = 3;
        codePoint = (uint)(first & 15);
        minimum = 2048;
    }
    else if (first >= 240 && first <= 244)
    {
        count = 4;
        codePoint = (uint)(first & 7);
        minimum = 65536;
    }
    else
    {
        JsonFail();
    }
    if (remaining < (nuint)count) { JsonFail(); }
    for (int index = 1; index < count; index++)
    {
        byte continuation = parser.Source[parser.Position + (nuint)index];
        if ((continuation & 192) != 128) { JsonFail(); }
        codePoint = codePoint * 64 + (uint)(continuation & 63);
    }
    if (codePoint < minimum || codePoint > 1114111 ||
        (codePoint >= 55296 && codePoint <= 57343))
    {
        JsonFail();
    }
    parser.Position += (nuint)count;
    return codePoint;
}

private void JsonAppendEscapedString(ref StringBuilder builder, string value)
{
    builder.AppendByte(34);
    JsonParser parser = new() { Source = value, Position = 0 };
    while (parser.Position < value.Length)
    {
        byte current = value[parser.Position];
        if (current == 34 || current == 92)
        {
            builder.AppendByte(92);
            builder.AppendByte(current);
            parser.Position += 1;
        }
        else if (current == 8)
        {
            builder.Append("\\b");
            parser.Position += 1;
        }
        else if (current == 9)
        {
            builder.Append("\\t");
            parser.Position += 1;
        }
        else if (current == 10)
        {
            builder.Append("\\n");
            parser.Position += 1;
        }
        else if (current == 12)
        {
            builder.Append("\\f");
            parser.Position += 1;
        }
        else if (current == 13)
        {
            builder.Append("\\r");
            parser.Position += 1;
        }
        else if (current < 32 || current == 39 || current == 60 ||
                 current == 62 || current == 38)
        {
            JsonAppendHex4(ref builder, (uint)current);
            parser.Position += 1;
        }
        else if (current < 128)
        {
            builder.AppendByte(current);
            parser.Position += 1;
        }
        else
        {
            uint codePoint = JsonDecodeUtf8CodePoint(ref parser);
            if (codePoint <= 65535)
            {
                JsonAppendHex4(ref builder, codePoint);
            }
            else
            {
                uint adjusted = codePoint - 65536;
                JsonAppendHex4(ref builder, 55296 + (adjusted >> 10));
                JsonAppendHex4(ref builder, 56320 + (adjusted & 1023));
            }
        }
    }
    builder.AppendByte(34);
}

private string JsonSerializeString(string value)
{
    StringBuilder builder = new();
    JsonAppendEscapedString(ref builder, value);
    return builder.ToString();
}

private string JsonValidateNumberText(string value)
{
    JsonElement parsed = JsonParseComplete(value);
    if (parsed.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return value;
}

private void JsonWriterBeforeValue(ref JsonWriter writer)
{
    if (writer.Frames.Count == 0)
    {
        if (writer.RootWritten)
        {
            throw new InvalidOperationException(
                "A JSON writer can contain only one root value."
            );
        }
        writer.RootWritten = true;
        return;
    }

    nuint index = writer.Frames.Count - 1;
    JsonWriterFrame frame = writer.Frames[index];
    switch (frame.Container)
    {
        case JsonWriterContainer.Array: {
            if (frame.ValueCount != 0) { writer.Output.Append(","); }
            frame.ValueCount += 1;
        }
        case JsonWriterContainer.Object: {
            if (!frame.ExpectsValue)
            {
                throw new InvalidOperationException(
                    "WritePropertyName must precede an object value."
                );
            }
            frame.ExpectsValue = false;
            frame.ValueCount += 1;
        }
    }
    writer.Frames.Set(index, frame);
}

public void JsonWriter.WriteStartObject(ref JsonWriter self)
{
    JsonWriterBeforeValue(ref self);
    self.Output.Append("{");
    self.Frames.Add(new()
    {
        Container = JsonWriterContainer.Object,
        ValueCount = 0,
        ExpectsValue = false
    });
}

public void JsonWriter.WriteEndObject(ref JsonWriter self)
{
    if (self.Frames.Count == 0)
    {
        throw new InvalidOperationException("No JSON object is open.");
    }
    nuint index = self.Frames.Count - 1;
    JsonWriterFrame frame = self.Frames[index];
    if (frame.Container != JsonWriterContainer.Object || frame.ExpectsValue)
    {
        throw new InvalidOperationException(
            "The current JSON object is incomplete."
        );
    }
    self.Frames.RemoveAt(index);
    self.Output.Append("}");
}

public void JsonWriter.WriteStartArray(ref JsonWriter self)
{
    JsonWriterBeforeValue(ref self);
    self.Output.Append("[");
    self.Frames.Add(new()
    {
        Container = JsonWriterContainer.Array,
        ValueCount = 0,
        ExpectsValue = false
    });
}

public void JsonWriter.WriteEndArray(ref JsonWriter self)
{
    if (self.Frames.Count == 0)
    {
        throw new InvalidOperationException("No JSON array is open.");
    }
    nuint index = self.Frames.Count - 1;
    JsonWriterFrame frame = self.Frames[index];
    if (frame.Container != JsonWriterContainer.Array)
    {
        throw new InvalidOperationException(
            "The current JSON container is not an array."
        );
    }
    self.Frames.RemoveAt(index);
    self.Output.Append("]");
}

public void JsonWriter.WritePropertyName(
    ref JsonWriter self,
    string name
)
{
    if (self.Frames.Count == 0)
    {
        throw new InvalidOperationException(
            "JSON properties require an open object."
        );
    }
    nuint index = self.Frames.Count - 1;
    JsonWriterFrame frame = self.Frames[index];
    if (frame.Container != JsonWriterContainer.Object || frame.ExpectsValue)
    {
        throw new InvalidOperationException(
            "The JSON object is not ready for a property name."
        );
    }
    if (frame.ValueCount != 0) { self.Output.Append(","); }
    JsonAppendEscapedString(ref self.Output, name);
    self.Output.Append(":");
    frame.ExpectsValue = true;
    self.Frames.Set(index, frame);
}

private void JsonWriterWriteText(
    ref JsonWriter writer,
    string value
)
{
    JsonWriterBeforeValue(ref writer);
    writer.Output.Append(value);
}

public void JsonWriter.WriteStringValue(ref JsonWriter self, string value)
{
    JsonWriterBeforeValue(ref self);
    JsonAppendEscapedString(ref self.Output, value);
}

public void JsonWriter.WriteBooleanValue(ref JsonWriter self, bool value)
{
    JsonWriterWriteText(ref self, value ? "true" : "false");
}

public void JsonWriter.WriteNullValue(ref JsonWriter self)
{
    JsonWriterWriteText(ref self, "null");
}

public void JsonWriter.WriteNumberValue(ref JsonWriter self, sbyte value)
{ JsonWriterWriteText(ref self, value.ToString()); }
public void JsonWriter.WriteNumberValue(ref JsonWriter self, short value)
{ JsonWriterWriteText(ref self, value.ToString()); }
public void JsonWriter.WriteNumberValue(ref JsonWriter self, int value)
{ JsonWriterWriteText(ref self, value.ToString()); }
public void JsonWriter.WriteNumberValue(ref JsonWriter self, long value)
{ JsonWriterWriteText(ref self, value.ToString()); }
public void JsonWriter.WriteNumberValue(ref JsonWriter self, byte value)
{ JsonWriterWriteText(ref self, value.ToString()); }
public void JsonWriter.WriteNumberValue(ref JsonWriter self, ushort value)
{ JsonWriterWriteText(ref self, value.ToString()); }
public void JsonWriter.WriteNumberValue(ref JsonWriter self, uint value)
{ JsonWriterWriteText(ref self, value.ToString()); }
public void JsonWriter.WriteNumberValue(ref JsonWriter self, ulong value)
{ JsonWriterWriteText(ref self, value.ToString()); }
public void JsonWriter.WriteNumberValue(ref JsonWriter self, nint value)
{ JsonWriterWriteText(ref self, value.ToString()); }
public void JsonWriter.WriteNumberValue(ref JsonWriter self, nuint value)
{ JsonWriterWriteText(ref self, value.ToString()); }
public void JsonWriter.WriteNumberValue(ref JsonWriter self, float value)
{
    JsonWriterWriteText(ref self, JsonValidateNumberText(value.ToString()));
}
public void JsonWriter.WriteNumberValue(ref JsonWriter self, double value)
{
    JsonWriterWriteText(ref self, JsonValidateNumberText(value.ToString()));
}

public void JsonWriter.WriteValue(ref JsonWriter self, JsonElement value)
{
    JsonWriterWriteText(ref self, value.GetRawText());
}

public string JsonWriter.ToString(JsonWriter self)
{
    if (!self.RootWritten || self.Frames.Count != 0)
    {
        throw new InvalidOperationException(
            "The JSON writer does not contain one complete value."
        );
    }
    return self.Output.ToString();
}

// These two generic bodies are compiler synthesis points. Each concrete
// instantiation is replaced with direct, statically checked field/container
// code; the placeholders are never emitted for a supported type.
private void JsonWriteTyped<T>(ref JsonWriter writer, T value)
{
    throw new InvalidOperationException(
        "typed JSON writer was not generated"
    );
}

private T JsonReadTyped<T>(JsonElement jsonValue)
{
    throw new InvalidOperationException(
        "typed JSON reader was not generated"
    );
}

private void JsonWriteList<T>(
    ref JsonWriter writer,
    List<T> values
)
{
    writer.WriteStartArray();
    foreach (T value in values)
    {
        JsonWriteTyped(ref writer, value);
    }
    writer.WriteEndArray();
}

private List<T> JsonReadList<T>(JsonElement jsonValue)
{
    List<T> values = new();
    for (int index = 0; index < jsonValue.GetArrayLength(); index += 1)
    {
        values.Add(JsonReadTyped(jsonValue[index]));
    }
    return values;
}

private void JsonWriteDictionary<T>(
    ref JsonWriter writer,
    Dictionary<string, T> values
)
{
    writer.WriteStartObject();
    for (nuint index = 0; index < values.Count; index += 1)
    {
        writer.WritePropertyName(values.KeyAt(index));
        JsonWriteTyped(ref writer, values.ValueAt(index));
    }
    writer.WriteEndObject();
}

private Dictionary<string, T> JsonReadDictionary<T>(
    JsonElement jsonValue
)
{
    Dictionary<string, T> values = new();
    for (int index = 0; index < jsonValue.GetPropertyCount(); index += 1)
    {
        values.Add(
            jsonValue.GetPropertyName(index),
            JsonReadTyped(jsonValue.GetPropertyAt(index))
        );
    }
    return values;
}

private void JsonWriteOption<T>(
    ref JsonWriter writer,
    Option<T> value
)
{
    switch (value)
    {
        case Option.Some(present): {
            JsonWriteTyped(ref writer, present);
        }
        case Option.None: { writer.WriteNullValue(); }
    }
}

private Option<T> JsonReadOption<T>(JsonElement jsonValue)
{
    if (jsonValue.ValueKind == JsonValueKind.Null) { return Option.None; }
    return Option.Some(JsonReadTyped(jsonValue));
}

private string JsonReadRequiredString(JsonElement jsonValue)
{
    switch (jsonValue.GetString())
    {
        case Option.Some(value): { return value; }
        case Option.None: {
            throw new JsonException("Expected a JSON string value.");
        }
    }
}

private T JsonInvalidEnum<T>()
{
    throw new JsonException("JSON string is not a valid enum value.");
}

public string JsonSerializer.Serialize<T>(T value)
{
    JsonWriter writer = JsonWriter.Create();
    JsonWriteTyped(ref writer, value);
    return writer.ToString();
}

// T is inferred from the expected result type.
public T JsonSerializer.Deserialize<T>(string json)
{
    return JsonReadTyped(JsonElement.Parse(json));
}

public JsonDocument JsonDocument.Parse(string json)
{
    return new() { RootElement = JsonParseComplete(json) };
}

public JsonElement JsonElement.Parse(string json)
{
    return JsonParseComplete(json);
}

public string JsonElement.GetRawText(JsonElement self)
{
    return self.Source.Substring(self.Start, self.End - self.Start);
}

public string? JsonElement.GetString(JsonElement self)
{
    if (self.ValueKind == JsonValueKind.Null) { return null; }
    if (self.ValueKind != JsonValueKind.String) { JsonFail(); }
    return JsonDecodeString(self.Source, self.Start, self.End);
}

public int JsonElement.GetInt32(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return int.Parse(self.GetRawText());
}

public byte JsonElement.GetByte(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return byte.Parse(self.GetRawText());
}

public sbyte JsonElement.GetSByte(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return sbyte.Parse(self.GetRawText());
}

public short JsonElement.GetInt16(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return short.Parse(self.GetRawText());
}

public long JsonElement.GetInt64(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return long.Parse(self.GetRawText());
}

public ushort JsonElement.GetUInt16(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return ushort.Parse(self.GetRawText());
}

public uint JsonElement.GetUInt32(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return uint.Parse(self.GetRawText());
}

public ulong JsonElement.GetUInt64(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return ulong.Parse(self.GetRawText());
}

public nint JsonElement.GetNInt(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return nint.Parse(self.GetRawText());
}

public nuint JsonElement.GetNUInt(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return nuint.Parse(self.GetRawText());
}

public float JsonElement.GetSingle(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return float.Parse(self.GetRawText());
}

public double JsonElement.GetDouble(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Number) { JsonFail(); }
    return double.Parse(self.GetRawText());
}

public bool JsonElement.GetBoolean(JsonElement self)
{
    if (self.ValueKind == JsonValueKind.True) { return true; }
    if (self.ValueKind == JsonValueKind.False) { return false; }
    JsonFail();
    return false;
}

public JsonElement JsonElement.Clone(JsonElement self)
{
    // The source string is immutable and reference counted, so every Aster
    // JsonElement already has the lifetime guarantee .NET Clone provides.
    return self;
}

public int JsonElement.GetArrayLength(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Array) { JsonFail(); }
    JsonParser parser = new()
    {
        Source = self.Source,
        Position = self.Start + 1,
    };
    JsonSkipWhiteSpace(ref parser);
    if (parser.Source[parser.Position] == 93) { return 0; }
    int count = 0;
    while (true)
    {
        JsonElement ignored = JsonParseValue(ref parser, 1);
        count += 1;
        JsonSkipWhiteSpace(ref parser);
        if (parser.Source[parser.Position] == 93) { return count; }
        parser.Position += 1;
    }
    return count;
}

// Item is the .NET metadata name for an indexer. Aster lowers value[index]
// to this ordinary member, so the source surface remains value[index].
public JsonElement JsonElement.Item(JsonElement self, int index)
{
    if (self.ValueKind != JsonValueKind.Array || index < 0) { JsonFail(); }
    JsonParser parser = new()
    {
        Source = self.Source,
        Position = self.Start + 1,
    };
    JsonSkipWhiteSpace(ref parser);
    int current = 0;
    while (parser.Source[parser.Position] != 93)
    {
        JsonElement value = JsonParseValue(ref parser, 1);
        if (current == index) { return value; }
        current += 1;
        JsonSkipWhiteSpace(ref parser);
        if (parser.Source[parser.Position] == 93) { break; }
        parser.Position += 1;
        JsonSkipWhiteSpace(ref parser);
    }
    JsonFail();
    return new()
    {
        Source = self.Source,
        Start = 0,
        End = 0,
        ValueKind = JsonValueKind.Undefined,
    };
}

public int JsonElement.GetPropertyCount(JsonElement self)
{
    if (self.ValueKind != JsonValueKind.Object) { JsonFail(); }
    JsonParser parser = new()
    {
        Source = self.Source,
        Position = self.Start + 1,
    };
    JsonSkipWhiteSpace(ref parser);
    if (parser.Source[parser.Position] == 125) { return 0; }
    int count = 0;
    while (true)
    {
        JsonParseStringToken(ref parser);
        JsonSkipWhiteSpace(ref parser);
        parser.Position += 1;
        JsonElement ignored = JsonParseValue(ref parser, 1);
        count += 1;
        JsonSkipWhiteSpace(ref parser);
        if (parser.Source[parser.Position] == 125) { return count; }
        parser.Position += 1;
        JsonSkipWhiteSpace(ref parser);
    }
    return count;
}

public string JsonElement.GetPropertyName(JsonElement self, int index)
{
    if (self.ValueKind != JsonValueKind.Object || index < 0) { JsonFail(); }
    JsonParser parser = new()
    {
        Source = self.Source,
        Position = self.Start + 1,
    };
    JsonSkipWhiteSpace(ref parser);
    int current = 0;
    while (parser.Source[parser.Position] != 125)
    {
        nuint nameStart = parser.Position;
        JsonParseStringToken(ref parser);
        nuint nameEnd = parser.Position;
        JsonSkipWhiteSpace(ref parser);
        parser.Position += 1;
        JsonElement ignored = JsonParseValue(ref parser, 1);
        if (current == index)
        {
            return JsonDecodeString(parser.Source, nameStart, nameEnd);
        }
        current += 1;
        JsonSkipWhiteSpace(ref parser);
        if (parser.Source[parser.Position] == 125) { break; }
        parser.Position += 1;
        JsonSkipWhiteSpace(ref parser);
    }
    JsonFail();
    return "";
}

public JsonElement JsonElement.GetPropertyAt(JsonElement self, int index)
{
    if (self.ValueKind != JsonValueKind.Object || index < 0) { JsonFail(); }
    JsonParser parser = new()
    {
        Source = self.Source,
        Position = self.Start + 1,
    };
    JsonSkipWhiteSpace(ref parser);
    int current = 0;
    while (parser.Source[parser.Position] != 125)
    {
        JsonParseStringToken(ref parser);
        JsonSkipWhiteSpace(ref parser);
        parser.Position += 1;
        JsonElement value = JsonParseValue(ref parser, 1);
        if (current == index) { return value; }
        current += 1;
        JsonSkipWhiteSpace(ref parser);
        if (parser.Source[parser.Position] == 125) { break; }
        parser.Position += 1;
        JsonSkipWhiteSpace(ref parser);
    }
    JsonFail();
    return new()
    {
        Source = self.Source,
        Start = 0,
        End = 0,
        ValueKind = JsonValueKind.Undefined,
    };
}

public bool JsonElement.TryGetProperty(
    JsonElement self,
    string propertyName,
    out JsonElement value
)
{
    if (self.ValueKind != JsonValueKind.Object) { JsonFail(); }
    JsonParser parser = new()
    {
        Source = self.Source,
        Position = self.Start + 1,
    };
    JsonSkipWhiteSpace(ref parser);
    bool found = false;
    value = new()
    {
        Source = self.Source,
        Start = 0,
        End = 0,
        ValueKind = JsonValueKind.Undefined,
    };
    while (parser.Source[parser.Position] != 125)
    {
        nuint nameStart = parser.Position;
        JsonParseStringToken(ref parser);
        nuint nameEnd = parser.Position;
        JsonSkipWhiteSpace(ref parser);
        parser.Position += 1;
        JsonElement propertyValue = JsonParseValue(ref parser, 1);
        if (JsonDecodeString(parser.Source, nameStart, nameEnd) == propertyName)
        {
            value = propertyValue;
            found = true;
        }
        JsonSkipWhiteSpace(ref parser);
        if (parser.Source[parser.Position] == 125) { break; }
        parser.Position += 1;
        JsonSkipWhiteSpace(ref parser);
    }
    return found;
}

public JsonElement JsonElement.GetProperty(
    JsonElement self,
    string propertyName
)
{
    JsonElement value = new()
    {
        Source = self.Source,
        Start = 0,
        End = 0,
        ValueKind = JsonValueKind.Undefined,
    };
    if (!self.TryGetProperty(propertyName, out value)) { JsonFail(); }
    return value;
}
