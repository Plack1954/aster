delegate long Operation(long amount);

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
    Counter counter = null;
    Operation operation = counter.Add;
    return operation(1) == 2 ? 0 : 1;
}
