private struct UniqueProjection {
    long value;

    private UniqueProjection(
        const ref UniqueProjection other) = delete;
}

private struct ProjectionBox {
    UniqueProjection value;
    long marker;
}

int main() {
    ProjectionBox box = new() {
        value = new() { value = 1 },
        marker = 2,
    };
    UniqueProjection field = box.value;

    UniqueProjection values[1] = [new() { value = 3 }];
    UniqueProjection indexed = values[0];

    (UniqueProjection deconstructed, long marker) = box;
    return 0;
}
