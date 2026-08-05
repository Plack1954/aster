int main()
{
    long value = 1;
    long iteration = 0;
    while (iteration < 50000000)
    {
        value = value + (iteration % 97) + 1;
        if (value > 1000000000)
        {
            value = value - 1000000000;
        }
        iteration = iteration + 1;
    }
    Console.WriteLine(value);
    return 0;
}
