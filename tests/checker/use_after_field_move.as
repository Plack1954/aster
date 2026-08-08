private struct Envelope
{
    string label;
    List<int> values;
}

int main()
{
    Envelope envelope = new() { label = "owned", values = new() };
    List<int> values = envelope.values;
    Console.WriteLine(values.Count);
    Console.WriteLine(envelope.label);
    return 0;
}
