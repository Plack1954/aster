struct UniqueItem {
    long value;

    private UniqueItem(const ref UniqueItem other) = delete;
}

struct UniqueWrapper {
    UniqueItem item;
}

int main() {
    UniqueWrapper original = new()
    {
        item = new() { value = 1 }
    };
    UniqueWrapper invalid = original;
    return (int)invalid.item.value;
}
