private struct CountedCopy {
    long generation;

    public CountedCopy(const ref CountedCopy other) {
        generation = other.generation + 1;
    }
}

~CountedCopy() {
    Console.WriteLine(self.generation);
}

private struct CountedBox {
    CountedCopy value;
    long marker;
}

int main() {
    {
        CountedCopy source = new() { generation = 1 };
        CountedCopy assigned = source;
        source = source;

        CountedBox box = new() { value = source, marker = 1 };
        CountedCopy field = box.value;
        CountedCopy values[1] = [source];
        CountedCopy indexed = values[0];
        Console.WriteLine(99);
    }
    return 0;
}
