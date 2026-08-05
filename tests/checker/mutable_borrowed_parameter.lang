struct Counter {
    long value;
    string label;
}

private void increment(ref Counter counter) {
    counter.value = counter.value + 1;
}

int main() {
    var counter = Counter {
        value: 40,
        label: "answer",
    };
    increment(counter);
    increment(counter);
    Console.WriteLine(counter.value);
    return 0;
}
