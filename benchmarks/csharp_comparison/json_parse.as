using System.Text.Json;

int main()
{
    string source = "{\"name\":\"Aster\",\"count\":42,\"ready\":true,\"items\":[1,2,3,4,5]}";
    long total = 0;
    for (int i = 0; i < 50000; i++)
    {
        JsonDocument document = JsonDocument.Parse(source);
        JsonElement root = document.RootElement;
        total += (long)root.GetProperty("count").GetInt32();
        total += (long)root.GetProperty("items").GetArrayLength();
        if (root.GetProperty("ready").GetBoolean()) { total += 1; }
    }
    Console.WriteLine(total);
    return 0;
}
