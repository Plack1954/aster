using System.Collections.Generic;

int main()
{
    Stack<int> numbers = new();
    if (numbers.Count != 0 || numbers.Capacity != 0) { return 1; }
    if (numbers.EnsureCapacity(8) < 8) { return 2; }

    for (int i = 0; i < 256; i++)
    {
        numbers.Push(i);
    }
    if (numbers.Count != 256 || numbers.Peek() != 255) { return 3; }
    for (int i = 255; i >= 128; i--)
    {
        if (numbers.Pop() != i) { return 4; }
    }
    if (numbers.Count != 128 || numbers.Peek() != 127) { return 5; }
    numbers.TrimExcess();
    if (numbers.Capacity != numbers.Count) { return 6; }
    numbers.TrimExcess(200);
    if (numbers.Capacity != 200 || numbers.Count != 128) { return 7; }
    for (int i = 127; i >= 0; i--)
    {
        if (numbers.Pop() != i) { return 8; }
    }
    int stacked = -1;
    if (numbers.TryPop(out stacked) || stacked != 0) { return 12; }
    numbers.Push(41);
    numbers.Push(42);
    if (!numbers.TryPeek(out stacked) || stacked != 42 || numbers.Count != 2)
    {
        return 13;
    }
    if (!numbers.TryPop(out stacked) || stacked != 42 || numbers.Count != 1)
    {
        return 14;
    }
    if (numbers.Pop() != 41) { return 15; }

    Stack<string> names = new();
    names.Push("Aster");
    names.Push("Pear");
    string stackedName = "unset";
    if (!names.TryPeek(out stackedName) || stackedName != "Pear")
    {
        return 16;
    }
    Stack<string> copied = names;
    string top = names.Pop();
    if (top != "Pear" || names.Peek() != "Aster") { return 9; }
    if (copied.Pop() != "Pear" || copied.Pop() != "Aster") { return 10; }

    numbers.Push(42);
    numbers.Clear();
    numbers.TrimExcess();
    if (numbers.Count != 0 || numbers.Capacity != 0) { return 11; }
    names.Clear();
    copied.Clear();
    return 0;
}
