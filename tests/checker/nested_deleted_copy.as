private struct UniqueItem {
    long value;

    private UniqueItem(const ref UniqueItem other) = delete;
}

private struct UniqueWrapper {
    UniqueItem item;
}

int main() {
    UniqueWrapper original = new()
    {
        item = new() { value = 1 }
    };
    UniqueWrapper invalid = copy(original);
    return (int)invalid.item.value;
}
