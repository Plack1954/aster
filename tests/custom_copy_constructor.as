private struct CountedValue {
    long value;

    public CountedValue(const ref CountedValue other) {
        value = other.value + 1;
    }
}

private long Inspect(CountedValue value) {
    return value.value;
}

int main() {
    CountedValue original = new() { value = 41 };
    CountedValue copied = original;
    original = original;
    Console.WriteLine(copied.value);
    Console.WriteLine(original.value);
    Console.WriteLine(Inspect(original));
    CountedValue explicitCopy = new CountedValue(original);
    Console.WriteLine(explicitCopy.value);
    return 0;
}
