using System;

private class Counter
{
    private long Changes;
    private string StoredName;

    public long Value { get; private set; }
    public string Name { get { return copy(StoredName); } }
    public long Doubled => Value * 2;

    public long Limited
    {
        get { return Value; }
        set
        {
            if (value < 0) {
                Value = 0;
            } else {
                Value = value;
            }
            Changes += 1;
        }
    }

    public long ChangeCount
    {
        get { return Changes; }
    }

    public static long Meaning
    {
        get { return 42; }
    }

    public static long Sink
    {
        set { Console.WriteLine(value); }
    }

    public Counter(long initial, string name)
    {
        Value = initial;
        StoredName = name;
        Changes = 0;
    }

    public void Raise()
    {
        Value += 1;
    }
}

private struct Point
{
    public long X { get; set; }

    public Point(long x)
    {
        X = x;
    }
}

int main()
{
    Counter counter = new Counter(5, "Ada");
    Console.WriteLine(counter.Value);
    counter.Limited = -3;
    counter.Limited += 7;
    counter.Raise();
    Console.WriteLine(counter.Value);
    Console.WriteLine(counter.Doubled);
    Console.WriteLine(counter.ChangeCount);
    Console.WriteLine(counter.Name);
    Console.WriteLine(Counter.Meaning);
    Counter.Sink = 9;
    Point point = new Point(1);
    point.X += 3;
    Console.WriteLine(point.X);
    delete counter;
    return 0;
}
