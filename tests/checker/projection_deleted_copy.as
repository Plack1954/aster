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
    UniqueProjection field = copy(box.value);

    UniqueProjection values[1] = [new() { value = 3 }];
    UniqueProjection indexed = copy(values[0]);

    ProjectionBox copiedBox = copy(box);
    (UniqueProjection deconstructed, long marker) = copiedBox;
    return 0;
}
