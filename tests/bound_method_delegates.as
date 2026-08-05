using System;
using System.Collections.Generic;

delegate long Operation(long amount);
delegate void Consumer(long value);

class Counter
{
    private long Value;

    public Counter(long initial)
    {
        Value = initial;
    }

    public long Add(long amount)
    {
        Value += amount;
        return Value;
    }

    public long Add(string ignored)
    {
        return Value;
    }

    private long AddTwice(long amount)
    {
        Value += amount * 2;
        return Value;
    }

    public Operation TwiceDelegate()
    {
        return this.AddTwice;
    }
}

class Total
{
    private long Value;

    public Total()
    {
        Value = 0;
    }

    public void Include(long value)
    {
        Value += value;
    }

    public long Read()
    {
        return Value;
    }
}

int main()
{
    Counter counter = new Counter(10);
    Operation add = counter.Add;
    Operation copied = add;
    Console.WriteLine(add(2));
    Console.WriteLine(copied(3));

    Operation twice = counter.TwiceDelegate();
    Console.WriteLine(twice(4));

    Total total = new Total();
    Consumer include = total.Include;
    List<long> values = new();
    values.Add(1);
    values.Add(2);
    values.Add(3);
    values.ForEach(include);
    Console.WriteLine(total.Read());

    delete total;
    delete counter;
    return 0;
}
