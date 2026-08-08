private struct UniqueListItem {
    long value;

    private UniqueListItem(const ref UniqueListItem other) = delete;
}

int main() {
    List<UniqueListItem> original = new();
    List<UniqueListItem> invalid = copy(original);
    return 0;
}
