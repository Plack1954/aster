private struct ThrowingProbe {
    long generation;

    public ThrowingProbe(const ref ThrowingProbe other) {
        if (other.generation == 2) {
            throw new IOException("copy failed");
        }
        generation = other.generation + 10;
    }
}

private List<ThrowingProbe> MakeValues() {
    List<ThrowingProbe> values = new();
    values.Add(new() { generation = 1 });
    values.Add(new() { generation = 2 });
    values.Add(new() { generation = 3 });
    return values;
}

private void Ignore(ThrowingProbe value) {
}

private bool NeverMatch(ThrowingProbe value) {
    return false;
}

private bool Continue(ThrowingProbe value) {
    return true;
}

private bool RemoveFirst(ThrowingProbe value) {
    return value.generation == 11;
}

int main() {
    List<ThrowingProbe> forEachValues = MakeValues();
    try {
        forEachValues.ForEach(Ignore);
    }
    catch (IOException error) {
        Console.WriteLine("ForEach");
    }

    List<ThrowingProbe> existsValues = MakeValues();
    try {
        existsValues.Exists(NeverMatch);
    }
    catch (IOException error) {
        Console.WriteLine("Exists");
    }

    List<ThrowingProbe> findAllValues = MakeValues();
    try {
        List<ThrowingProbe> matches = findAllValues.FindAll(NeverMatch);
        Console.WriteLine(matches.Count);
    }
    catch (IOException error) {
        Console.WriteLine("FindAll");
    }

    List<ThrowingProbe> findIndexValues = MakeValues();
    try {
        findIndexValues.FindIndex(NeverMatch);
    }
    catch (IOException error) {
        Console.WriteLine("FindIndex");
    }

    List<ThrowingProbe> findLastIndexValues = MakeValues();
    try {
        findLastIndexValues.FindLastIndex(NeverMatch);
    }
    catch (IOException error) {
        Console.WriteLine("FindLastIndex");
    }

    List<ThrowingProbe> trueForAllValues = MakeValues();
    try {
        trueForAllValues.TrueForAll(Continue);
    }
    catch (IOException error) {
        Console.WriteLine("TrueForAll");
    }

    List<ThrowingProbe> removeAllValues = MakeValues();
    try {
        removeAllValues.RemoveAll(RemoveFirst);
    }
    catch (IOException error) {
        Console.WriteLine("RemoveAll");
    }
    Console.WriteLine(removeAllValues.Count);
    return 0;
}
