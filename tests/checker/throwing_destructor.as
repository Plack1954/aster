private struct InvalidDrop {
    long value;
}

~InvalidDrop() {
    throw new IOException("destructors cannot throw");
}

int main() {
    InvalidDrop value = new() { value = 1 };
    return 0;
}
