int main() {
    List<string> values = new();
    string first = "Ada";
    string second = "Lin";
    values.Add(first);
    values.Add(second);

    foreach (string value in values) {
        Console.WriteLine(value);
        break;
    }
    return 0;
}
