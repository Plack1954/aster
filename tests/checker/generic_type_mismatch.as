private struct Box<T> {
    T value;
}

int main() {
    Box<string> text = Box {
        value: "text",
    };
    Box<long> number = text;
    return 0;
}
