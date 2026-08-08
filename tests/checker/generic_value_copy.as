private struct Box<T> {
    T value;
}

int main() {
    Box<string> original = Box {
        value: "owned",
    };
    Box<string> invalid = copy(original);
    Box<string> invalidAgain = copy(original);
    return 0;
}
