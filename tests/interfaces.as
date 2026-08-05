using System;

delegate long Reader();

interface IValue
{
    long Value();
    long Number { get; }
}

interface IAdvanced : IValue
{
    long Twice();
}

interface IAlso
{
    long Value();
}

interface IWritable
{
    long Number { get; set; }
}

sealed class Box : IWritable
{
    private long Storage;

    public Box(long value)
    {
        Storage = value;
    }

    public long Number
    {
        get { return Storage; }
        set { Storage = value; }
    }
}

class Base : IAdvanced, IAlso
{
    public Base() { }

    public virtual long Value() { return 3; }
    public virtual long Number => 4;
    public long Twice() { return 6; }

    ~Base()
    {
        Console.WriteLine(100);
    }
}

sealed class Derived : Base
{
    public Derived() { }

    public override long Value() { return 7; }
    public override long Number => 8;

    ~Derived()
    {
        Console.WriteLine(200);
    }
}

int main()
{
    Derived derived = new Derived();
    IAdvanced advanced = derived;
    IValue value = advanced;
    IAlso also = derived;

    Console.WriteLine(value.Value());
    Console.WriteLine(value.Number);
    Console.WriteLine(advanced.Number);
    Console.WriteLine(advanced.Twice());
    Console.WriteLine(also.Value());

    Reader reader = advanced.Value;
    Console.WriteLine(reader());

    Box box = new Box(1);
    IWritable writable = box;
    writable.Number = 9;
    Console.WriteLine(writable.Number);
    delete writable;

    delete value;
    return 0;
}
