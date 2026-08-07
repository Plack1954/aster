private struct Box<T> {
    T value;
}

int main() {
    Box<string> original = Box {
        value: "owned",
    };
    Box<string> invalid = original;
    Box<string> invalidAgain = original;
    return 0;
}
