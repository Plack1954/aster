int main() {
    Arena arena = Arena.new();
    unsafe {
        byte* first = ArenaAlloc(arena, 128);
        byte* second = ArenaAlloc(arena, 256);
        Console.WriteLine(first);
        Console.WriteLine(second);
        ArenaReset(arena);
        byte* third = ArenaAlloc(arena, 64);
        Console.WriteLine(third);
    }
    return 0;
}
