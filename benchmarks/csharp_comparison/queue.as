using System.Collections.Generic;

int main()
{
    Queue<int> values = new();
    values.EnsureCapacity(2000000);
    for (int i = 0; i < 2000000; i++) { values.Enqueue(i % 1024); }
    long total = 0;
    while (values.Count != 0) { total += (long)values.Dequeue(); }
    Console.WriteLine(total);
    return 0;
}
