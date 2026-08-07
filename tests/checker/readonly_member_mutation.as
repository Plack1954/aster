private struct Counter {
    int value;

    public readonly void InvalidIncrement() {
        value += 1;
    }
}

int main() {
    return 0;
}
