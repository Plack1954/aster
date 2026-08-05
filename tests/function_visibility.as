private long Internal(long value) {
    return value + 1;
}

public long Exported(long value) {
    return Internal(value) + 1;
}

int main() {
    Console.WriteLine(Exported(40));
    return 0;
}
