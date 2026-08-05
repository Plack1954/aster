struct Pair<A, B> {
    A first;
    B second;
}

private Pair<int, int> MakePair(int first, int second) {
    return Pair {
        second: second,
        first: first,
    };
}

private int total(Pair<int, int> pair, int values[3]) {
    return pair.first + pair.second +
           values[0] + values[1] + values[2];
}

int main() {
    Pair<int, int> pair = MakePair(2, 3);
    pair.first = 5;

    int values[3] = [7, 11, 13];
    values[1] = 17;

    if (total(pair, values) == 45) {
        return 0;
    }
    return 1;
}
