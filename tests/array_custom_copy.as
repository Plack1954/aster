private struct ArrayItem {
    long value;

    public ArrayItem(const ref ArrayItem other) {
        value = other.value + 1;
    }
}

int main() {
    ArrayItem original[2] = [
        new() { value = 1 },
        new() { value = 2 }
    ];
    ArrayItem copied[2] = copy(original);
    Console.WriteLine(original[0].value);
    Console.WriteLine(original[1].value);
    Console.WriteLine(copied[0].value);
    Console.WriteLine(copied[1].value);
    return 0;
}
