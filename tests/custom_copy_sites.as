struct CopyProbe {
    long generation;

    public CopyProbe(const ref CopyProbe other) {
        generation = other.generation + 1;
    }
}

struct CopyBox {
    CopyProbe value;
    long marker;
}

private CopyProbe PassByValue(CopyProbe value) {
    return value;
}

private CopyProbe CopyFromReference(const ref CopyProbe value) {
    return value;
}

int main() {
    CopyProbe original = new() { generation = 0 };

    CopyProbe initialized = original;
    Console.WriteLine(initialized.generation);

    CopyProbe assigned = new() { generation = 40 };
    assigned = original;
    Console.WriteLine(assigned.generation);

    original = original;
    Console.WriteLine(original.generation);

    CopyProbe parameter = PassByValue(original);
    Console.WriteLine(parameter.generation);

    CopyProbe returned = CopyFromReference(original);
    Console.WriteLine(returned.generation);

    CopyBox box = new() { value = original, marker = 7 };
    CopyProbe field = box.value;
    Console.WriteLine(field.generation);

    CopyProbe values[1] = [original];
    CopyProbe indexed = values[0];
    Console.WriteLine(indexed.generation);

    (CopyProbe deconstructed, long marker) = box;
    Console.WriteLine(deconstructed.generation);

    foreach (CopyProbe item in values) {
        Console.WriteLine(item.generation);
    }

    List<CopyProbe> list = new();
    list.Add(original);
    CopyProbe fromList = list[0];
    Console.WriteLine(fromList.generation);
    return 0;
}
