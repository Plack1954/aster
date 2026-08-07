private struct Box<T> {
    T value;
}

private T identity<T>(T value) {
    return value;
}

private T SumTo<T>(T value) {
    if (value == 0) {
        return value;
    }
    return value + SumTo(value - 1);
}

private T unbox<T>(Box<T> value) {
    return value.value;
}

int main() {
    long number = identity(42);
    Console.WriteLine(number);

    string text = "generic";
    string returned = identity(text);
    Console.WriteLine(returned);

    string item = "boxed";
    Box<string> boxed = new() { value = item };
    Box<string> returnedBox = identity(boxed);
    Console.WriteLine("boxed");

    long total = SumTo(5);
    Console.WriteLine(total);

    Box<long> numberBox = new() { value = 8 };
    Console.WriteLine(unbox(numberBox));
    return 0;
}
