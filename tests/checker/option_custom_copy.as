struct OptionItem {
    long value;

    public OptionItem(const ref OptionItem other) {
        value = other.value + 1;
    }
}

int main() {
    Option<OptionItem> original = Option.Some(
        new() { value = 1 });
    Option<OptionItem> invalid = original;
    return 0;
}
