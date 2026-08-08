private struct Resource {
    long value;

    public Resource(const ref Resource other) {
        value = other.value;
    }
}

private struct Holder {
    Resource value;
}

private long Consume(Resource value) {
    return value.value;
}

int main() {
    assert_no_semantic_copies();
    Resource first = new() { value = 41 };
    Resource second = new() { value = 1 };
    Holder holder = new() {
        value = new() { value = 1 },
    };
    long left = Consume(ensure_move(first));
    long right = Consume(assert_move(second));
    long field = Consume(ensure_move(holder.value));
    Console.WriteLine(left + right + field);
    return 0;
}
