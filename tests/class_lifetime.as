using System;

private class Counter
{
    private long Value;
    private string Name;

    public Counter(long value, string name)
    {
        Value = value;
        Name = name;
    }

    public long Increment()
    {
        Value = Value + 1;
        return Value;
    }

    ~Counter()
    {
        Console.WriteLine(Name);
    }
}

int main()
{
    Counter counter = new Counter(40, "destroyed");
    Counter alias = counter;
    Console.WriteLine(alias.Increment());
    delete counter;
    return 0;
}
