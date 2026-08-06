struct InvalidCopy {
    long value;

    public InvalidCopy(const ref InvalidCopy other) {
        other.value = 99;
        value = other.value;
    }
}

int main() {
    InvalidCopy value = new() { value = 1 };
    InvalidCopy copied = value;
    return (int)copied.value;
}
