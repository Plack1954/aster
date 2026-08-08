int main()
{
    List<int> source = new();
    List<int> destination = source;
    Console.WriteLine(destination.Count);
    Console.WriteLine(source.Count);
    return 0;
}
