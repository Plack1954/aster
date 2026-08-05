private long Mix(long value, long i)
{
    long next = value + i + 1;
    return next > 1000000000 ? next - 1000000000 : next;
}

int main()
{
    long value = 1;
    for (long i = 0; i < 20000000; i++) { value = Mix(value, i); }
    Console.WriteLine(value);
    return 0;
}
