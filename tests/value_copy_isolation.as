private struct Collection
{
    List<int> Values;
}

int main()
{
    List<int> original = new();
    original.Add(1);
    List<int> copied = copy(original);
    copied.Add(2);
    Console.WriteLine(original.Count);
    Console.WriteLine(copied.Count);

    Collection first = new() { Values = copy(original) };
    Collection second = copy(first);
    second.Values.Add(3);
    Console.WriteLine(first.Values.Count);
    Console.WriteLine(second.Values.Count);
    return 0;
}
