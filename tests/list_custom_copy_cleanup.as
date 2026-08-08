private struct ListResource {
    Arena storage;
    long id;

    public ListResource(const ref ListResource other) {
        storage = Arena.new();
        id = other.id + 1;
    }
}

~ListResource() {
    Console.WriteLine(self.id);
}

int main() {
    ListResource item = new() {
        storage = Arena.new(),
        id = 1
    };
    List<ListResource> original = new();
    original.Add(item);
    List<ListResource> copied = copy(original);
    return 0;
}
