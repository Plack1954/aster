private long increment(long value) {
    return value + 1;
}

int main() {
    long value = 0;
    while (value < 1000) {
        value = increment(value);
    }
    return 0;
}
