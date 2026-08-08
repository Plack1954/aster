private struct Resource {
    long value;

    public Resource(const ref Resource other) {
        value = other.value;
    }
}

private struct Pair {
    Resource left;
    Resource right;
}

private long Consume(Resource value) {
    return value.value;
}

private long Inspect(const ref Pair value) {
    return value.right.value;
}

int main() {
    Pair pair = new() {
        left = new() { value = 20 },
        right = new() { value = 22 },
    };
    long moved = Consume(ensure_move(pair.left));
    return moved + Inspect(pair);
}
