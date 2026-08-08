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
    Resource items[2] = [
        new() { value = 20 },
        new() { value = 22 },
    ];
    long first = Consume(ensure_move(items[0]));
    Console.WriteLine(items[0].value);
    Console.WriteLine(first);
    return 0;
}
