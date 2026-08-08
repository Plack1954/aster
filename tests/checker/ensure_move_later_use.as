private struct Resource {
    long value;

    public Resource(const ref Resource other) {
        value = other.value;
    }
}

private long Consume(Resource value) {
    return value.value;
}

int main() {
    Resource value = new() { value = 42 };
    long result = Consume(ensure_move(value));
    Console.WriteLine(value.value);
    Console.WriteLine(result);
    return 0;
}
