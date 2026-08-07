private struct ResourceValue {
    Arena storage;
    long id;

    public ResourceValue(const ref ResourceValue other) {
        storage = Arena.new();
        id = other.id + 1;
    }
}

~ResourceValue() {
    Console.WriteLine(self.id);
}

private struct ResourceWrapper {
    ResourceValue resource;
}

int main() {
    ResourceWrapper original = new()
    {
        resource = new()
        {
            storage = Arena.new(),
            id = 1
        }
    };
    ResourceWrapper copied = original;
    return 0;
}
