private struct Point {
    long x;
    long y;
}

private Point Point.offset(Point self, long amount) {
    self.x = self.x + amount;
    self.y = self.y + amount;
    return self;
}

int main() {
    var point = Point { x: 20, y: 22 };
    var shifted = point.offset(10);
    Console.WriteLine(shifted.x + shifted.y);
    return 0;
}
