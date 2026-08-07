private struct Pair<A, B> {
    A first;
    B second;
}

private struct Box<T> {
    T value;
}

private union Maybe<T> {
    None,
    Some(T),
}

private void ConsumeBox(Box<string> value) {
    Console.WriteLine("owned box");
}

int main() {
    Pair<long, string> pair = new() {
        first = 42,
        second = "answer",
    };
    Console.WriteLine(pair.first);

    Box<long> numbers = new() { value = 7 };
    Box<long> numbersCopy = numbers;
    Console.WriteLine(numbersCopy.value);

    Box<string> box = new() {
        value = "resource",
    };
    Box<string> boxCopy = box;
    ConsumeBox(box);
    ConsumeBox(boxCopy);

    Maybe<long> maybe = Maybe.Some(9);
    switch (maybe) {
        case Maybe.Some(value): {
            Console.WriteLine(value);
        }
        case Maybe.None: {
            Console.WriteLine(0);
        }
    }
    return 0;
}
