struct Point {
    long x;
    long y;
}

struct Resource {
    long id;
}

~Resource() {
    Console.WriteLine(self.id);
}

struct Box {
    Resource item;
}

int main() {
    var point = new Point { x = 1, y = 2 };
    point.x = 10;
    Console.WriteLine(point.x);

    long values[3] = [1, 2, 3];
    values[1] = 9;
    Console.WriteLine(values[1]);

    var box = new Box {
        item = new Resource { id = 1 },
    };
    box.item = new Resource { id = 2 };

    return 0;
}
