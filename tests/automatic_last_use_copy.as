private struct CopyProbe {
    long generation;

    public CopyProbe(const ref CopyProbe other) {
        generation = other.generation + 1;
    }
}

private struct CopyPair {
    CopyProbe value;
    long marker;
}

private CopyProbe PassByValue(CopyProbe value) {
    return value;
}

private CopyProbe CopyFromBorrow(const ref CopyProbe value) {
    return value;
}

int main() {
    CopyProbe source = new() { generation = 0 };
    CopyProbe copied = source;
    Console.WriteLine(copied.generation);
    Console.WriteLine(source.generation);

    CopyProbe moved = copied;
    Console.WriteLine(moved.generation);

    CopyProbe argument = new() { generation = 10 };
    CopyProbe result = PassByValue(argument);
    Console.WriteLine(result.generation);
    Console.WriteLine(argument.generation);

    CopyProbe branchSource = new() { generation = 20 };
    if (true) {
        CopyProbe branchCopy = branchSource;
        Console.WriteLine(branchCopy.generation);
    }
    Console.WriteLine(branchSource.generation);

    CopyProbe borrowedSource = new() { generation = 30 };
    CopyProbe borrowedResult = CopyFromBorrow(borrowedSource);
    Console.WriteLine(borrowedResult.generation);
    Console.WriteLine(borrowedSource.generation);

    List<CopyProbe> items = new();
    items.Add(borrowedSource);
    CopyProbe indexed = items[0];
    Console.WriteLine(indexed.generation);
    Console.WriteLine(borrowedSource.generation);

    CopyPair pair = new() {
        value = new() { generation = 40 },
        marker = 7
    };
    (CopyProbe part, long marker) = pair;
    Console.WriteLine(part.generation);
    Console.WriteLine(pair.value.generation);
    return 0;
}
