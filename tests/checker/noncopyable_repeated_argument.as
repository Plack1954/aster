private struct UniqueValue {
    long value;

    private UniqueValue(const ref UniqueValue other) = delete;
}

private void ConsumeTwice(UniqueValue first, UniqueValue second) {
}

int main() {
    UniqueValue value = new() { value = 42 };
    ConsumeTwice(value, value);
    return 0;
}
