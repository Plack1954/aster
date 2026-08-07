private delegate T Transformer<T>(T value);

private int Increment(int value) {
    return value + 1;
}

int main() {
    Transformer<int> transform = Increment;
    return transform(41) == 42 ? 0 : 1;
}
