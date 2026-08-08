private struct CopyProbe {
    long generation;

    public CopyProbe(const ref CopyProbe other) {
        generation = other.generation + 1;
    }
}

private struct CopyBox {
    CopyProbe value;
    long marker;
}

private CopyProbe PassByValue(CopyProbe value) {
    return value;
}

private CopyProbe CopyFromReference(const ref CopyProbe value) {
    return copy(value);
}

int main() {
    CopyProbe original = new() { generation = 0 };

    CopyProbe initialized = copy(original);
    Console.WriteLine(initialized.generation);

    CopyProbe assigned = new() { generation = 40 };
    assigned = copy(original);
    Console.WriteLine(assigned.generation);

    original = copy(original);
    Console.WriteLine(original.generation);

    CopyProbe parameter = PassByValue(copy(original));
    Console.WriteLine(parameter.generation);

    CopyProbe returned = CopyFromReference(original);
    Console.WriteLine(returned.generation);

    CopyBox box = new() { value = copy(original), marker = 7 };
    CopyProbe field = copy(box.value);
    Console.WriteLine(field.generation);

    CopyProbe values[1] = [copy(original)];
    CopyProbe indexed = copy(values[0]);
    Console.WriteLine(indexed.generation);

    CopyBox deconstructionSource = copy(box);
    (CopyProbe deconstructed, long marker) = deconstructionSource;
    Console.WriteLine(deconstructed.generation);

    foreach (CopyProbe item in values) {
        Console.WriteLine(item.generation);
    }

    List<CopyProbe> list = new();
    list.Add(copy(original));
    CopyProbe fromList = copy(list[0]);
    Console.WriteLine(fromList.generation);
    return 0;
}
