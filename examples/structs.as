private struct Point {
    long x;
    long y;
}

private union Status {
    Ready(long),
    Empty,
}

int main() {
    Point point = new() { x = 10, y = 20 };
    Console.WriteLine(point.x);
    Status status = Status.Ready(42);
    return 0;
}
