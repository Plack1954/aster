private struct ResourceValue {
    long id;
    Arena storage;

    public ResourceValue(const ref ResourceValue other) {
        id = other.id + 1;
        storage = Arena.new();
    }
}

~ResourceValue() {
    Console.WriteLine(self.id);
}

int main() {
    ResourceValue first = new() {
        id = 1,
        storage = Arena.new(),
    };
    ResourceValue second = copy(first);
    Console.WriteLine(first.id);
    return 0;
}
