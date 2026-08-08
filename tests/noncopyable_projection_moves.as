private struct Unique {
    long value;

    private Unique(const ref Unique other) = delete;
}

private struct Pair {
    Unique left;
    Unique right;
}

private long Consume(Unique value) {
    return value.value;
}

int main() {
    Pair pair = new() {
        left = new() { value = 20 },
        right = new() { value = 22 },
    };
    long fieldLeft = Consume(pair.left);
    long fieldRight = Consume(pair.right);

    Unique items[2] = [
        new() { value = 30 },
        new() { value = 12 },
    ];
    long indexLeft = Consume(items[0]);
    long indexRight = Consume(items[1]);
    Console.WriteLine(fieldLeft + fieldRight);
    Console.WriteLine(indexLeft + indexRight);
    return 0;
}
