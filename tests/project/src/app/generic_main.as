namespace App.GenericMain;

using Generic = Shared.Generics;
using First = Consumers.A;
using Second = Consumers.B;

int main() {
    Generic.Box<long> value = new Generic.Box<long> { value = 11 };
    Console.WriteLine(First.read(value) + Second.read(value));
    Console.WriteLine(First.echo(7) + Second.echo(7));
    Func<long, long> callback = First.echo;
    Console.WriteLine(callback(3));

    Generic.Maybe<long> maybe =
        Generic.Maybe.Some(5);
    switch (maybe) {
        case Generic.Maybe.Some(payload): {
            Console.WriteLine(payload);
        }
        case Generic.Maybe.None: {
            Console.WriteLine(0);
        }
    }
    return 0;
}
