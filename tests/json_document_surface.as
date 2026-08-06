using System.Text.Json;

private bool Rejects(string json)
{
    try
    {
        JsonDocument document = JsonDocument.Parse(json);
    }
    catch (Exception error)
    {
        return true;
    }
    return false;
}

private bool JsonWriterRejectsInvalidOrder()
{
    try
    {
        JsonWriter writer = JsonWriter.Create();
        writer.WriteStartObject();
        writer.WriteStringValue("missing-name");
    }
    catch (Exception error)
    {
        return true;
    }
    return false;
}

int main()
{
    string source = "{\"name\":\"Aster\\nLang\",\"count\":42,\"large\":9223372036854775807,\"ratio\":1.25e2,\"ready\":true,\"missing\":null,\"items\":[1,{\"ok\":false},3],\"escaped\":\"\\u0041\\uD83D\\uDE00\",\"duplicate\":1,\"duplicate\":2}";

    JsonDocument document = JsonDocument.Parse(source);
    JsonElement root = document.RootElement;
    if (root.ValueKind != JsonValueKind.Object) { return 1; }
    if (root.GetPropertyCount() != 10) { return 2; }

    string? name = root.GetProperty("name").GetString();
    if (name == null || name.Value != "Aster\nLang") { return 3; }
    if (root.GetProperty("count").GetInt32() != 42) { return 4; }
    if (root.GetProperty("count").GetByte() != 42 ||
        root.GetProperty("count").GetSByte() != 42 ||
        root.GetProperty("count").GetInt16() != 42 ||
        root.GetProperty("count").GetUInt16() != 42 ||
        root.GetProperty("count").GetUInt32() != 42 ||
        root.GetProperty("count").GetUInt64() != 42)
        { return 23; }
    if (root.GetProperty("large").GetInt64() != 9223372036854775807)
        { return 5; }
    if (root.GetProperty("ratio").GetDouble() != 125.0) { return 6; }
    if (root.GetProperty("ratio").GetSingle() != 125.0) { return 24; }
    if (!root.GetProperty("ready").GetBoolean()) { return 7; }

    JsonElement missing = root.GetProperty("missing");
    if (missing.ValueKind != JsonValueKind.Null) { return 8; }
    if (missing.GetString() != null) { return 9; }

    JsonElement items = root.GetProperty("items");
    if (items.ValueKind != JsonValueKind.Array || items.GetArrayLength() != 3)
        { return 10; }
    if (items.GetRawText() != "[1,{\"ok\":false},3]") { return 11; }
    if (items[0].GetInt32() != 1) { return 20; }
    if (items[1].GetProperty("ok").GetBoolean()) { return 21; }
    if (items[2].GetInt32() != 3) { return 22; }

    string? escaped = root.GetProperty("escaped").GetString();
    if (escaped == null || escaped.Value != "A😀") { return 12; }
    if (root.GetProperty("duplicate").GetInt32() != 2) { return 13; }
    JsonElement foundProperty = JsonElement.Parse("null");
    if (!root.TryGetProperty("count", out foundProperty) ||
        foundProperty.GetInt32() != 42)
        { return 33; }
    if (root.TryGetProperty("not-there", out foundProperty) ||
        foundProperty.ValueKind != JsonValueKind.Undefined)
        { return 34; }
    JsonElement cloned = root.GetProperty("duplicate").Clone();
    if (cloned.GetInt32() != 2) { return 25; }

    JsonElement scalar = JsonElement.Parse("  -12.5  ");
    if (scalar.ValueKind != JsonValueKind.Number ||
        scalar.GetRawText() != "-12.5" || scalar.GetDouble() != -12.5)
        { return 14; }

    if (!Rejects("")) { return 15; }
    if (!Rejects("[1,]")) { return 16; }
    if (!Rejects("{\"x\":01}")) { return 17; }
    if (!Rejects("\"\\uD800\"")) { return 18; }
    if (!Rejects("true false")) { return 19; }

    if (JsonSerializer.Serialize("A\n\"<&'😀") !=
        "\"A\\n\\\"\\u003C\\u0026\\u0027\\uD83D\\uDE00\"")
        { return 26; }
    string? absent = null;
    if (JsonSerializer.Serialize(absent) != "null") { return 27; }
    string? present = "text";
    if (JsonSerializer.Serialize(present) != "\"text\"") { return 28; }
    if (JsonSerializer.Serialize(true) != "true") { return 29; }
    if (JsonSerializer.Serialize(42) != "42") { return 30; }
    if (JsonSerializer.Serialize(1.25) != "1.25") { return 31; }
    if (JsonSerializer.Serialize(root.GetProperty("items")) !=
        "[1,{\"ok\":false},3]")
        { return 32; }

    JsonWriter writer = JsonWriter.Create();
    writer.WriteStartObject();
    writer.WritePropertyName("message");
    writer.WriteStringValue("A\n\"<&'😀");
    writer.WritePropertyName("items");
    writer.WriteStartArray();
    writer.WriteNumberValue(42);
    writer.WriteBooleanValue(true);
    writer.WriteNullValue();
    writer.WriteValue(JsonElement.Parse("{\"nested\":1}"));
    writer.WriteEndArray();
    writer.WriteEndObject();
    string written = writer.ToString();
    if (written !=
        "{\"message\":\"A\\n\\\"\\u003C\\u0026\\u0027\\uD83D\\uDE00\",\"items\":[42,true,null,{\"nested\":1}]}"
    ) { return 35; }
    if (JsonDocument.Parse(written).RootElement.GetProperty("items")
        .GetArrayLength() != 4) { return 36; }
    if (!JsonWriterRejectsInvalidOrder()) { return 37; }
    return 0;
}
