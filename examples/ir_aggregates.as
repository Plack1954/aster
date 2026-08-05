struct Pair {
    long first;
    long second;
}

private long total(Pair pair) {
    return pair.first + pair.second;
}

int main() {
    Pair pair = new() {
        second = 2,
        first = 1,
    };
    Pair copy = pair;
    copy.first = 9;
    Console.WriteLine(pair.first);
    Console.WriteLine(total(copy));

    long values[3] = [3, 4, 5];
    long copiedValues[3] = values;
    copiedValues[1] = 8;
    Console.WriteLine(values[1]);
    Console.WriteLine(copiedValues[1]);
    return 0;
}
