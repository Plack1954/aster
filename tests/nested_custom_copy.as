struct InnerValue {
    long value;

    public InnerValue(const ref InnerValue other) {
        value = other.value + 1;
    }
}

struct MiddleValue {
    InnerValue inner;
}

struct OuterValue {
    MiddleValue middle;
}

int main() {
    OuterValue original = new()
    {
        middle = new()
        {
            inner = new() { value = 41 }
        }
    };
    OuterValue copied = original;
    Console.WriteLine(original.middle.inner.value);
    Console.WriteLine(copied.middle.inner.value);
    return 0;
}
