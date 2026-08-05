int main()
{
    long value = 1;
    for (long i = 0; i < 50000000; i++)
    {
        value += (i % 97) + 1;
        if (value > 1000000000) { value -= 1000000000; }
    }
    Console.WriteLine(value);
    return 0;
}
