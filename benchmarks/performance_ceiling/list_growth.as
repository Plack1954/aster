using System.Collections.Generic;

int main()
{
    List<int> values = new();
    int index = 0;
    while (index < 2000000)
    {
        values.Add(index % 1024);
        index = index + 1;
    }

    long total = 0;
    index = 0;
    while ((nuint)index < values.Count)
    {
        total = total + (long)values[(nuint)index];
        index = index + 1;
    }
    Console.WriteLine(total);
    return 0;
}
