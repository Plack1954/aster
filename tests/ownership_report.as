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
    Resource moved = new() { value = 20 };
    Resource retained = new() { value = 22 };
    long first = Consume(moved);
    long second = Consume(retained);
    Console.WriteLine(retained.value);
    Resource explicit = copy(retained);
    Console.WriteLine(first + second + explicit.value);
    return 0;
}
