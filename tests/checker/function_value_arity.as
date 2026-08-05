private long AddOne(long value) {
    return value + 1;
}

int main() {
    Func<long, long> operation = AddOne;
    Console.WriteLine(operation(1, 2));
    return 0;
}
