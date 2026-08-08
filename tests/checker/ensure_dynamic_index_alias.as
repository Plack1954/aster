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
    long selected = 0;
    long moved = Consume(ensure_move(items[selected]));
    return moved + Consume(items[1]);
}
