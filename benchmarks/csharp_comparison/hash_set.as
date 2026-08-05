using System.Collections.Generic;

int main()
{
    HashSet<int> values = new();
    values.EnsureCapacity(1000000);
    for (int i = 0; i < 1000000; i++) { values.Add(i); }
    long found = 0;
    for (int i = 0; i < 2000000; i++)
    {
        if (values.Contains(i % 1250000)) { found += 1; }
    }
    for (int i = 0; i < 1000000; i += 4) { values.Remove(i); }
    Console.WriteLine(found + (long)values.Count);
    return 0;
}
