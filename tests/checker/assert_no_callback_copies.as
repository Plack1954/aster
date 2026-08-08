private struct Resource {
    long value;

    public Resource(const ref Resource other) {
        value = other.value;
    }
}

private void Observe(Resource value) {
    Console.WriteLine(value.value);
}

int main() {
    assert_no_semantic_copies();
    List<Resource> values = new();
    Resource value = new() { value = 42 };
    values.Add(value);
    values.ForEach(Observe);
    return 0;
}
