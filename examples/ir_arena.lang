int main() {
    Arena arena = Arena.new();
    unsafe {
        long* first = ArenaAlloc(arena, 8);
        *first = 1;
        ArenaReset(arena);

        long* second = ArenaAlloc(arena, 8);
        *second = 2;
        Console.WriteLine(*second);
    }
    return 0;
}
