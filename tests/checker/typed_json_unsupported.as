using System.Text.Json;

int main()
{
    Dictionary<int, int> values = new();
    string json = JsonSerializer.Serialize(values);
    return json.Length == 0 ? 0 : 1;
}
