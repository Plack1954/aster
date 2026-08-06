struct GenericItem {
    long value;

    public GenericItem(const ref GenericItem other) {
        value = other.value + 1;
    }
}

struct CopyBox<T> {
    T value;
}

int main() {
    CopyBox<GenericItem> original = CopyBox {
        value: new() { value = 7 },
    };
    CopyBox<GenericItem> copied = original;
    Console.WriteLine(original.value.value);
    Console.WriteLine(copied.value.value);
    return 0;
}
