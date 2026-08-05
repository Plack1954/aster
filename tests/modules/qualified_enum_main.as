namespace Qualified.EnumMain;

using Qualified.EnumDep;

private long read(Qualified.Enums.Status status) {
    switch (status) {
        case Qualified.Enums.Status.Ready(value): {
            return value;
        }
        case Qualified.Enums.Status.Empty: {
            return 0;
        }
    }
}

int main() {
    Qualified.Enums.Status ready =
        Qualified.Enums.Status.Ready(42);
    Qualified.Enums.Status empty =
        Qualified.Enums.Status.Empty;
    Console.WriteLine(read(ready));
    Console.WriteLine(read(empty));
    return 0;
}
