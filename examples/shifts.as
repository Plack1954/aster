private void AcceptNested(Result<Option<long>, Option<long>> value) {
    Console.WriteLine(0);
}

int main() {
    sbyte small = 3;
    Console.WriteLine(small << 2);

    long negative = -8;
    Console.WriteLine(negative >> 2);

    byte bits = 128;
    Console.WriteLine(bits >> 7);

    long precedence = 1 + 2 << 2;
    Console.WriteLine(precedence);

    Result<Option<long>, Option<long>> nested =
        Result.Ok(Option.Some(42));
    switch (nested) {
        case Result.Ok(value): {
            switch (value) {
                case Option.Some(number): { Console.WriteLine(number); }
                case Option.None: { Console.WriteLine(0); }
            }
        }
        case Result.Err(error): { Console.WriteLine(0); }
    }
    return 0;
}
