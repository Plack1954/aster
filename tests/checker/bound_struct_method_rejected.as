delegate long Operation(long amount);

struct Counter
{
    long value;

    public Counter(long initial)
    {
        value = initial;
    }

    public long Add(long amount)
    {
        return value + amount;
    }
}

int main()
{
    Counter counter = new Counter(1);
    Operation operation = counter.Add;
    return operation(2) == 3 ? 0 : 1;
}
