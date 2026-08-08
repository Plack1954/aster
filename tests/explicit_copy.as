private struct CopyProbe
{
    long generation;

    public CopyProbe(const ref CopyProbe other)
    {
        generation = other.generation + 1;
    }
}

private struct TextBox
{
    string value;
}

int main()
{
    CopyProbe original = new() { generation = 0 };
    CopyProbe moved = original;
    Console.WriteLine(moved.generation);

    CopyProbe duplicated = copy(moved);
    Console.WriteLine(moved.generation);
    Console.WriteLine(duplicated.generation);

    List<int> values = new();
    values.Add(1);
    List<int> independent = copy(values);
    independent.Add(2);
    Console.WriteLine(values.Count);
    Console.WriteLine(independent.Count);

    List<int> transferred = values;
    transferred.Add(3);
    Console.WriteLine(transferred.Count);

    values = new();
    values.Add(4);
    Console.WriteLine(values.Count);

    TextBox box = new() { value = "field" };
    string fieldCopy = copy(box.value);
    Console.WriteLine(box.value);
    Console.WriteLine(fieldCopy);

    Option<string> optional = Option.Some("payload");
    string payloadCopy = copy(optional.Value);
    Console.WriteLine(optional.Value);
    Console.WriteLine(payloadCopy);
    return 0;
}
