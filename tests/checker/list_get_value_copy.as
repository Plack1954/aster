int main() {
    List<string> values = new();
    values.Add("owned");
    string value = copy(values[0]);
    Console.WriteLine(value);
    return 0;
}
