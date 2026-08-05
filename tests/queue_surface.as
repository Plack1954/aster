using System.Collections.Generic;

int main()
{
    Queue<int> numbers = new();
    if (numbers.Count != 0 || numbers.Capacity != 0) { return 1; }
    if (numbers.EnsureCapacity(8) < 8) { return 2; }

    for (int i = 0; i < 8; i++)
    {
        numbers.Enqueue(i);
    }
    if (numbers.Peek() != 0 || numbers.Count != 8) { return 3; }
    for (int i = 0; i < 5; i++)
    {
        if (numbers.Dequeue() != i) { return 4; }
    }
    for (int i = 8; i < 16; i++)
    {
        numbers.Enqueue(i);
    }
    if (numbers.Count != 11 || numbers.Peek() != 5) { return 5; }
    for (int i = 5; i < 16; i++)
    {
        if (numbers.Dequeue() != i) { return 6; }
    }
    if (numbers.Count != 0) { return 7; }
    int queued = -1;
    if (numbers.TryDequeue(out queued) || queued != 0) { return 15; }
    numbers.Enqueue(41);
    numbers.Enqueue(42);
    if (!numbers.TryPeek(out queued) || queued != 41 || numbers.Count != 2)
    {
        return 16;
    }
    if (!numbers.TryDequeue(out queued) || queued != 41 ||
        numbers.Count != 1)
    {
        return 17;
    }
    if (numbers.Dequeue() != 42) { return 18; }

    Queue<string> names = new();
    names.Enqueue("Aster");
    names.Enqueue("Lime");
    string queuedName = "unset";
    if (!names.TryPeek(out queuedName) || queuedName != "Aster")
    {
        return 19;
    }
    Queue<string> copied = names;
    string first = names.Dequeue();
    if (first != "Aster" || names.Peek() != "Lime") { return 8; }
    if (copied.Dequeue() != "Aster" || copied.Dequeue() != "Lime")
    {
        return 9;
    }

    for (int i = 0; i < 100; i++)
    {
        numbers.Enqueue(i);
    }
    for (int i = 0; i < 75; i++)
    {
        if (numbers.Dequeue() != i) { return 10; }
    }
    numbers.TrimExcess();
    if (numbers.Capacity != numbers.Count || numbers.Peek() != 75)
    {
        return 11;
    }
    for (int i = 75; i < 100; i++)
    {
        if (numbers.Dequeue() != i) { return 12; }
    }
    numbers.Enqueue(200);
    numbers.Clear();
    if (numbers.Count != 0) { return 13; }
    numbers.TrimExcess();
    if (numbers.Capacity != 0) { return 14; }

    names.Clear();
    copied.Clear();
    return 0;
}
