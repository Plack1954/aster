private long Mix(long value, long iteration)
{
    long next = value + iteration + 1;
    if (next > 1000000000)
    {
        return next - 1000000000;
    }
    return next;
}

int main()
{
    long value = 1;
    long iteration = 0;
    while (iteration < 20000000)
    {
        value = Mix(value, iteration);
        iteration = iteration + 1;
    }
    Console.WriteLine(value);
    return 0;
}
