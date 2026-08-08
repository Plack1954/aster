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

private long BorrowThenOwn(
    const ref CopyProbe borrowed,
    CopyProbe owned
) {
    return borrowed.generation * 10 + owned.generation;
}

private long OwnThenBorrow(
    CopyProbe owned,
    const ref CopyProbe borrowed
) {
    return owned.generation * 10 + borrowed.generation;
}

private long BorrowFieldThenOwn(
    const ref CopyProbe borrowed,
    CopyPair owned
) {
    return borrowed.generation * 10 + owned.value.generation;
}

private long OwnThenBorrowField(
    CopyPair owned,
    const ref CopyProbe borrowed
) {
    return owned.value.generation * 10 + borrowed.generation;
}

private long OwnTwice(CopyProbe first, CopyProbe second) {
    return first.generation * 10 + second.generation;
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

    CopyProbe borrowThenOwn = new() { generation = 50 };
    Console.WriteLine(BorrowThenOwn(borrowThenOwn, borrowThenOwn));

    CopyProbe ownThenBorrow = new() { generation = 60 };
    Console.WriteLine(OwnThenBorrow(ownThenBorrow, ownThenBorrow));

    CopyPair borrowFieldThenOwn = new() {
        value = new() { generation = 70 }, marker = 0
    };
    Console.WriteLine(BorrowFieldThenOwn(
        borrowFieldThenOwn.value, borrowFieldThenOwn));

    CopyPair ownThenBorrowField = new() {
        value = new() { generation = 80 }, marker = 0
    };
    Console.WriteLine(OwnThenBorrowField(
        ownThenBorrowField, ownThenBorrowField.value));

    CopyProbe ownTwice = new() { generation = 90 };
    Console.WriteLine(OwnTwice(ownTwice, ownTwice));
    return 0;
}
