delegate long Operation(long amount);

class Counter
{
    public Counter() { }

    private long Add(long amount)
    {
        return amount + 1;
    }
}

int main()
{
    Counter counter = new Counter();
    Operation operation = counter.Add;
    delete counter;
    return 0;
}
