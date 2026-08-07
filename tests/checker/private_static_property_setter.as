private class Counter
{
    public static long Count { get; private set; }
}

int main()
{
    Counter.Count = 1;
    return 0;
}
