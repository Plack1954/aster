private struct Collection
{
    List<int> Values;
}

int main()
{
    List<int> original = new();
    original.Add(1);
    List<int> copied = original;
    copied.Add(2);
    Console.WriteLine(original.Count);
    Console.WriteLine(copied.Count);

    Collection first = new() { Values = original };
    Collection second = first;
    second.Values.Add(3);
    Console.WriteLine(first.Values.Count);
    Console.WriteLine(second.Values.Count);
    return 0;
}
