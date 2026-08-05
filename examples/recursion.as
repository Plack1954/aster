private long fibonacci(long n) {
    if (n < 2) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main() {
    Console.WriteLine(fibonacci(10));
    return 0;
}
