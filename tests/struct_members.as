struct Example {
    int value;

    public Example(int value) {
        this.value = value;
    }

    public int Double() {
        return value * 2;
    }

    public void Increment() {
        value += 1;
    }

    public int Doubled => value * 2;
    public int Tripled {
        get {
            return value * 3;
        }
    }
    public static int Answer => 42;

    public static int Square(int number) {
        return number * number;
    }
}

int main() {
    Example example = new() { value = 21 };
    Console.WriteLine(example.Double());
    Console.WriteLine(example.Doubled);
    Console.WriteLine(example.Tripled);
    Console.WriteLine(Example.Answer);
    Console.WriteLine(Example.Square(6));
    example.Increment();
    Console.WriteLine(example.Doubled);
    Example constructed = new(7);
    Console.WriteLine(constructed.Doubled);
    return 0;
}
