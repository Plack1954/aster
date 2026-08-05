using System.Collections.Generic;

int main()
{
    List<int> values = new();
    values.EnsureCapacity(2000000);
    for (int i = 0; i < 2000000; i++) { values.Add(i % 1024); }
    long total = 0;
    for (int pass = 0; pass < 8; pass++)
    {
        for (nuint i = 0; i < values.Count; i++)
        {
            total += (long)values[i];
        }
    }
    Console.WriteLine(total);
    return 0;
}
