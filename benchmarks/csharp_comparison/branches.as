int main()
{
    long total = 0;
    for (long i = 0; i < 50000000; i++)
    {
        long kind = i % 8;
        if (kind == 0) { total += 3; }
        else if (kind == 1) { total -= 2; }
        else if (kind == 2) { total += i % 17; }
        else if (kind == 3) { total ^= kind; }
        else if (kind == 4) { total += 11; }
        else if (kind == 5) { total -= i % 5; }
        else if (kind == 6) { total += 7; }
        else { total -= 1; }
    }
    Console.WriteLine(total);
    return 0;
}
