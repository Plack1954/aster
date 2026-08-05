namespace Types.Mismatch;

using Types.TypeDep;

struct Resource {
    long id;
}

~Resource() {
    Console.WriteLine(self.id);
}

int main() {
    var local = Resource { id: 2 };
    ConsumeResource(local);
    return 0;
}
