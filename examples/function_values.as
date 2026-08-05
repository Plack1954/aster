private long AddOne(long value) {
    return value + 1;
}

delegate long Operation(long value);

private long apply(long value, Operation operation) {
    return operation(value);
}

private T ApplyT<T>(T value, Func<T, T> operation) {
    return operation(value);
}

int main() {
    Operation operation = AddOne;
    Console.WriteLine(operation(4));
    Console.WriteLine(apply(9, AddOne));
    long genericResult = ApplyT(20, AddOne);
    Console.WriteLine(genericResult);
    return 0;
}
