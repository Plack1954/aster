private int Choose(int value)
{
    return value + 1;
}

private string Choose(string value)
{
    return value;
}

private int Add(int left, int right)
{
    return left + right;
}

private int Add(int left, int middle, int right)
{
    return left + middle + right;
}

delegate int IntChooser(int value);

private T First<T>(T value)
{
    return value;
}

private T First<T>(T value, T ignored)
{
    return value;
}

private int Change(Counter value)
{
    return value.Base + 1;
}

private int Change(ref Counter value)
{
    value.Base += 2;
    return value.Base;
}

struct Counter
{
    int Base;
}

private int Counter.Add(Counter self, int value)
{
    return self.Base + value;
}

private int Counter.Add(Counter self, int left, int right)
{
    return self.Base + left + right;
}

int main()
{
    Counter counter = new()
    {
        Base = 10
    };
    Console.WriteLine(Choose(4));
    Console.WriteLine(Choose("aster"));
    Console.WriteLine(Add(2, 3));
    Console.WriteLine(Add(2, 3, 4));
    Console.WriteLine(counter.Add(5));
    Console.WriteLine(counter.Add(5, 6));
    string text = "Aster";
    Console.WriteLine(text.Length);
    Console.WriteLine(text[1]);
    List<int> values = new();
    values.Add(7);
    values.Add(9);
    Console.WriteLine(values.Count);
    Console.WriteLine(values[1]);
    IntChooser chooser = Choose;
    Console.WriteLine(chooser(8));
    Console.WriteLine(First(12));
    Console.WriteLine(First(13, 14));
    Counter changed = new()
    {
        Base = 20
    };
    Console.WriteLine(Change(changed));
    Console.WriteLine(Change(ref changed));
    Console.WriteLine(changed.Base);
    return 0;
}
