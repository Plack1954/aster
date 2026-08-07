private struct Counter {
    long value;
    string label;
}

private void increment(ref Counter counter) {
    counter.value = counter.value + 1;
}

int main() {
    var counter = Counter {
        value: 0,
        label: "fixed",
    };
    increment(counter);
    return 0;
}
