int main() {
    Arena arena = Arena.new();
    unsafe {
        long* pointer = ArenaAlloc(arena, 8);
        ArenaReset(arena);
        Console.WriteLine(*pointer);
    }
    return 0;
}
