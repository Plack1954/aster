private struct Point {
    long x;
    long y;
}

private long Point.sum(Point self, long offset) {
    return self.x + self.y + offset;
}

private struct Counter {
    long value;
}

~Counter() {
}

private void Counter.Add(ref Counter self, long amount) {
    self.value = self.value + amount;
}

private long Counter.read(Counter self) {
    return self.value;
}

private long Counter.consume(Counter self) {
    self.value = self.value + 1;
    return self.value;
}

private long IncrementTwice(long value) {
    value += 1;
    value += 1;
    return value;
}

int main() {
    var point = new Point { x = 10, y = 20 };
    Console.WriteLine(point.sum(12));

    List<long> values = new();
    values.Add(7);
    values.Add(9);
    Console.WriteLine(values.Count);
    Console.WriteLine(values[1]);

    StringBuilder builder = new();
    builder.Append("method");
    builder.Append(" calls");
    var output = (builder).ToString();
    Console.WriteLine(output);

    var counter = new Counter { value = 39 };
    counter.Add(2);
    Console.WriteLine(counter.read());
    Console.WriteLine((counter).consume());
    Console.WriteLine(IncrementTwice(40));
    return 0;
}
