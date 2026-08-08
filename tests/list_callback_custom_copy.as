private struct CopyProbe {
    long generation;

    public CopyProbe(const ref CopyProbe other) {
        generation = other.generation + 1;
    }
}

private struct NestedProbe {
    CopyProbe value;
}

private delegate void Consumer<T>(T value);

private void Observe(CopyProbe value) {
    Console.WriteLine(value.generation);
}

private void ObserveNested(NestedProbe value) {
    Console.WriteLine(value.value.generation);
}

private bool IsCopied(CopyProbe value) {
    return value.generation == 11;
}

int main() {
    List<CopyProbe> values = new();
    CopyProbe value = new() { generation = 10 };
    values.Add(value);

    Consumer<CopyProbe> callback = Observe;
    values.ForEach(callback);
    Console.WriteLine(values.Exists(IsCopied));

    List<CopyProbe> matches = values.FindAll(IsCopied);
    CopyProbe match = copy(matches[0]);
    Console.WriteLine(match.generation);
    Console.WriteLine(values.FindIndex(IsCopied));
    Console.WriteLine(values.FindLastIndex(IsCopied));
    Console.WriteLine(values.TrueForAll(IsCopied));
    Console.WriteLine(values.RemoveAll(IsCopied));
    Console.WriteLine(values.Count);

    List<NestedProbe> nested = new();
    NestedProbe nestedValue = new() {
        value = new() { generation = 20 },
    };
    nested.Add(nestedValue);
    nested.ForEach(ObserveNested);
    return 0;
}
