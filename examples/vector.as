int main() {
    List<string> values = new();
    string first = "Ada";
    string second = "Lin";
    values.Add(first);
    values.Add(second);
    Console.WriteLine(values.Count);

    foreach (string value in values) {
        Console.WriteLine(value);
    }
    return 0;
}
