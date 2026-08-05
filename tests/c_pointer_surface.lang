int main()
{
    Arena arena = Arena.new();
    unsafe
    {
        long* pointer = ArenaAlloc(arena, 8);
        *pointer = 42;
        Console.WriteLine(*pointer);
    }
    return 0;
}
