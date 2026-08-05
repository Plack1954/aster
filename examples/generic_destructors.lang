struct Tracked {
    long id;
}

~Tracked() {
    Console.WriteLine(self.id);
}

struct Wrapper<T> {
    T item;
}

int main() {
    {
        Tracked firstItem = new() { id = 1 };
        Wrapper<Tracked> first = new() {
            item = firstItem,
        };
        Tracked secondItem = new() { id = 2 };
        Wrapper<Tracked> second = new() {
            item = secondItem,
        };
    }
    return 0;
}
