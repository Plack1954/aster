private class Counter
{
    public Counter() { }

    public long Add(long amount)
    {
        return amount + 1;
    }
}

int main()
{
    Counter counter = new Counter();
    var operation = counter.Add;
    delete counter;
    return 0;
}
