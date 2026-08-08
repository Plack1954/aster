private struct ListCopyItem {
    long value;

    public ListCopyItem(const ref ListCopyItem other) {
        value = other.value + 1;
    }
}

int main() {
    ListCopyItem item = new() { value = 1 };
    List<ListCopyItem> original = new();
    original.Add(item);
    List<ListCopyItem> copied = copy(original);
    ListCopyItem fromOriginal = copy(original[0]);
    ListCopyItem fromCopy = copy(copied[0]);
    Console.WriteLine(fromOriginal.value);
    Console.WriteLine(fromCopy.value);
    return 0;
}
