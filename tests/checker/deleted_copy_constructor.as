private struct UniqueValue {
    long value;

    private UniqueValue(const ref UniqueValue other) = delete;
}

int main() {
    UniqueValue original = new() { value = 42 };
    UniqueValue copied = copy(original);
    return (int)copied.value;
}
