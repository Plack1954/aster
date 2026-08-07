private struct Box<T> {
    T value;
}

private void invalid(Box<Arena> arenaBox) {
    Box<Arena> copy = arenaBox;
}

int main() {
    return 0;
}
