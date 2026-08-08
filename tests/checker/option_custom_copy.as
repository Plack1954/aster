private struct OptionItem {
    long value;

    private OptionItem(const ref OptionItem other) = delete;
}

int main() {
    Option<OptionItem> original = Option.Some(
        new() { value = 1 });
    Option<OptionItem> invalid = copy(original);
    return 0;
}
