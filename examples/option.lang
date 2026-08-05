private int ValueOrZero(Option<int> option) {
    switch (option) {
        case Option.Some(value): {
            return value;
        }
        case Option.None: {
            return 0;
        }
    }
}

int main() {
    Option<int> present = Option.Some(42);
    Option<int> absent = Option.None;
    Console.WriteLine(ValueOrZero(present));
    Console.WriteLine(ValueOrZero(absent));
    return 0;
}
