namespace Types.Main;

using Types.TypeDep;

struct Resource {
    long id;
}

~Resource() {
    Console.WriteLine(self.id + 100);
}

int main() {
    var foreign = MakeResource(1);
    var local = Resource { id: 2 };
    return 0;
}
