using System;

private class Counter
{
    private static long Value = 3;
    public static long Direct;
    public static readonly long Seed = 9;

    public static long Count { get; private set; }

    public static long Current => Value;

    public static void Add(long amount)
    {
        Value += amount;
        Count += 1;
        Touch();
    }

    private static void Touch()
    {
        Value += 1;
    }
}

int main()
{
    Counter.Direct = 7;
    Console.WriteLine(Counter.Direct);
    Console.WriteLine(Counter.Seed);
    Console.WriteLine(Counter.Current);
    Counter.Add(4);
    Counter.Add(5);
    Console.WriteLine(Counter.Current);
    Console.WriteLine(Counter.Count);
    return 0;
}
