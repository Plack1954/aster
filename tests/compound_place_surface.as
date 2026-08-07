private struct Counter {
    int Value;
}

private int SelectedIndex() {
    Console.WriteLine(99);
    return 1;
}

int main() {
    Counter counter = new() { Value = 10 };
    counter.Value += 5;
    Console.WriteLine(counter.Value);

    int values[3] = [1, 2, 3];
    values[SelectedIndex()] += 5;
    Console.WriteLine(values[1]);

    counter.Value *= 2;
    counter.Value /= 3;
    counter.Value %= 6;
    counter.Value -= 1;
    counter.Value <<= 2;
    counter.Value >>= 1;
    counter.Value |= 1;
    counter.Value &= 6;
    counter.Value ^= 3;
    Console.WriteLine(counter.Value);
    return 0;
}
