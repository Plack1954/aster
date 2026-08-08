private struct Resource {
    long value;

    public Resource(const ref Resource other) {
        value = other.value + 1000;
    }
}

private struct Pair {
    Resource left;
    Resource right;
}

private long Consume(Resource value) {
    return value.value;
}

int main() {
    assert_no_semantic_copies();
    Pair pair = new() {
        left = new() { value = 20 },
        right = new() { value = 22 },
    };
    long fieldLeft = Consume(ensure_move(pair.left));
    long fieldRight = Consume(ensure_move(pair.right));

    Resource items[2] = [
        new() { value = 30 },
        new() { value = 12 },
    ];
    long indexLeft = Consume(ensure_move(items[0]));
    long indexRight = Consume(ensure_move(items[1]));

    Resource dynamicItems[2] = [
        new() { value = 10 },
        new() { value = 32 },
    ];
    long selected = 1;
    long dynamic = Consume(ensure_move(dynamicItems[selected]));

    Resource reset[1] = [new() { value = 40 }];
    long beforeReset = Consume(ensure_move(reset[0]));
    reset[0] = new Resource { value = 2 };
    long afterReset = Consume(ensure_move(reset[0]));
    Console.WriteLine(fieldLeft + fieldRight);
    Console.WriteLine(indexLeft + indexRight);
    Console.WriteLine(dynamic + 10);
    Console.WriteLine(beforeReset + afterReset);
    return 0;
}
