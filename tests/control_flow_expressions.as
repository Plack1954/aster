private union Choice {
    Number(long),
    Empty,
}

private long choose(bool flag) {
    return flag ? 10 : 20;
}

private long chained(long value) {
    long result = value == 1 ? 30 : value == 2 ? 40 : 50;
    return result;
}

private long inspect(Choice choice) {
    return choice switch {
        Choice.Number(value) => value,
        Choice.Empty => 0
    };
}

int main() {
    Console.WriteLine(choose(true));
    Console.WriteLine(choose(false));
    Console.WriteLine(chained(2));
    Console.WriteLine(inspect(Choice.Number(7)));
    Console.WriteLine(inspect(Choice.Empty));
    return 0;
}
