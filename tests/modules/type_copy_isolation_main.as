namespace Types.CloneIsolation;

using Types.Dep;

private struct Resource {
    long id;
}

int main() {
    Resource local = Resource { id: 42 };
    Resource copy = local;
    Console.WriteLine(copy.id);
    return 0;
}
