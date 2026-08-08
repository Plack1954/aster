private struct Box<T> {
    T value;
}

private void invalid(Box<Arena> arenaBox) {
    Box<Arena> copied = copy(arenaBox);
}

int main() {
    return 0;
}
