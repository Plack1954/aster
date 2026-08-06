struct OptionItem {
    long value;

    private OptionItem(const ref OptionItem other) = delete;
}

int main() {
    Option<OptionItem> original = Option.Some(
        new() { value = 1 });
    Option<OptionItem> invalid = original;
    return 0;
}
