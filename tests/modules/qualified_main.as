namespace Isolation.QualifiedMain;

using Isolation.Dep;

int main() {
    Console.WriteLine(Dep.choose(true));
    Console.WriteLine(Isolation.Dep.choose(false));
    return 0;
}
