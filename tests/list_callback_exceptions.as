private struct CopyProbe {
    long generation;

    public CopyProbe(const ref CopyProbe other) {
        if (other.generation == 2) {
            throw new IOException("copy failed");
        }
        generation = other.generation + 10;
    }
}

private void ObserveCopy(CopyProbe value) {
    Console.WriteLine(value.generation);
}

private struct CopyLeaf {
    string text;
    bool fail;

    public CopyLeaf(const ref CopyLeaf other) {
        text = copy(other.text);
        fail = other.fail;
        if (fail) {
            throw new IOException("nested copy failed");
        }
    }
}

private struct CopyPair {
    CopyLeaf first;
    CopyLeaf second;
}

private void IgnorePair(CopyPair value) {
}

private void ObserveThenThrow(long value) {
    Console.WriteLine(value);
    if (value == 2) {
        throw new IOException("callback failed");
    }
}

private bool PredicateThenThrow(long value) {
    if (value == 2) {
        throw new IOException("predicate failed");
    }
    return false;
}

private bool TrueThenThrow(long value) {
    if (value == 2) {
        throw new IOException("predicate failed");
    }
    return true;
}

private bool FindOrThrow(string value) {
    if (value == "boom") {
        throw new IOException("find failed");
    }
    return value == "keep";
}

private bool RemoveOrThrow(string value) {
    if (value == "boom") {
        throw new IOException("remove failed");
    }
    return value == "remove";
}

int main() {
    List<CopyProbe> copies = new();
    copies.Add(new() { generation = 1 });
    copies.Add(new() { generation = 2 });
    copies.Add(new() { generation = 3 });
    try {
        copies.ForEach(ObserveCopy);
    }
    catch (IOException error) {
        Console.WriteLine(error.Message);
    }
    Console.WriteLine(copies.Count);

    List<CopyPair> pairs = new();
    pairs.Add(new() {
        first = new() { text = "first", fail = false },
        second = new() { text = "second", fail = true },
    });
    try {
        pairs.ForEach(IgnorePair);
    }
    catch (IOException error) {
        Console.WriteLine(error.Message);
    }

    List<long> numbers = new();
    numbers.Add(1);
    numbers.Add(2);
    numbers.Add(3);
    try {
        numbers.ForEach(ObserveThenThrow);
    }
    catch (IOException error) {
        Console.WriteLine(error.Message);
    }
    Console.WriteLine(99);
    try {
        numbers.Exists(PredicateThenThrow);
    }
    catch (IOException error) {
        Console.WriteLine(error.Message);
    }
    try {
        numbers.TrueForAll(TrueThenThrow);
    }
    catch (IOException error) {
        Console.WriteLine(error.Message);
    }
    try {
        numbers.FindIndex(PredicateThenThrow);
    }
    catch (IOException error) {
        Console.WriteLine(error.Message);
    }
    try {
        numbers.FindLastIndex(PredicateThenThrow);
    }
    catch (IOException error) {
        Console.WriteLine(error.Message);
    }

    List<string> findValues = new();
    findValues.Add("keep");
    findValues.Add("boom");
    findValues.Add("tail");
    try {
        List<string> matches = findValues.FindAll(FindOrThrow);
        Console.WriteLine(matches.Count);
    }
    catch (IOException error) {
        Console.WriteLine(error.Message);
    }
    Console.WriteLine(findValues.Count);

    List<string> removeValues = new();
    removeValues.Add("remove");
    removeValues.Add("keep");
    removeValues.Add("boom");
    removeValues.Add("tail");
    try {
        removeValues.RemoveAll(RemoveOrThrow);
    }
    catch (IOException error) {
        Console.WriteLine(error.Message);
    }
    Console.WriteLine(removeValues.Count);
    Console.WriteLine(removeValues[0]);
    Console.WriteLine(removeValues[1]);
    Console.WriteLine(removeValues[2]);
    return 0;
}
