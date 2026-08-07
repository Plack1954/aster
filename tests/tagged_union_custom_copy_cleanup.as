private struct UnionResource {
    Arena storage;
    long id;

    public UnionResource(const ref UnionResource other) {
        storage = Arena.new();
        id = other.id + 1;
    }
}

~UnionResource() {
    Console.WriteLine(self.id);
}

int main() {
    Option<UnionResource> original = Option.Some(
        new() { storage = Arena.new(), id = 1 });
    Option<UnionResource> copied = original;
    return 0;
}
