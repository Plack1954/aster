namespace Robustness.Checker;

struct Box<T> {
    T value;
}

private long Read(const ref Box<long> box) {
    return box.value;
}

int main() {
    Box<long> box = new() { value = 42 };
    return Read(box);
}
