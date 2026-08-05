using Count = uint;
using BytePointer = byte*;
using OptionalCount = Option<Count>;

int main() {
    Count count = 42;
    OptionalCount option = Option.Some(count);
    switch (option) {
        case Option.Some(value): {
            Console.WriteLine(value);
        }
        case Option.None: {
            Console.WriteLine(0);
        }
    }
    return 0;
}
