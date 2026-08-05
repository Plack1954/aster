using System.Collections.Generic;

int main()
{
    Dictionary<int, int> values = new();
    values.EnsureCapacity(500000);
    for (int i = 0; i < 500000; i++) { values[i] = (i * 17) % 1000003; }
    long total = 0;
    for (int i = 0; i < 500000; i++) { total += (long)values[i]; }
    for (int i = 0; i < 500000; i += 3) { values.Remove(i); }
    Console.WriteLine(total + (long)values.Count);
    return 0;
}
