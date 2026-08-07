using System.Collections.Generic;

private struct AssociationCopyItem {
    long value;

    public AssociationCopyItem(const ref AssociationCopyItem other) {
        value = other.value + 1;
    }
}

int main() {
    AssociationCopyItem value = new() { value = 10 };
    Dictionary<int, AssociationCopyItem> original = new();
    original.Add(1, value);
    Dictionary<int, AssociationCopyItem> copied = original;
    AssociationCopyItem originalValue = original[1];
    AssociationCopyItem copiedValue = copied[1];
    Console.WriteLine(originalValue.value);
    Console.WriteLine(copiedValue.value);

    Console.WriteLine(copied.Count);
    return 0;
}
