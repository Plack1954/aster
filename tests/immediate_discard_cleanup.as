private struct Resource {
    long id;
}

~Resource() {
    Console.WriteLine(self.id);
}

int main() {
    _ = Resource { id: 7 };
    Console.WriteLine(8);
    return 0;
}
