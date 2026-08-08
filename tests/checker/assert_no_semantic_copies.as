private struct Resource {
    long value;

    public Resource(const ref Resource other) {
        value = other.value;
    }
}

int main() {
    assert_no_semantic_copies();
    Resource value = new() { value = 42 };
    Resource duplicate = copy(value);
    Console.WriteLine(duplicate.value);
    return 0;
}
