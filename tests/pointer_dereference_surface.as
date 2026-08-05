private long* SelectedPointer(long* pointer) {
    Console.WriteLine(99);
    return pointer;
}

int main() {
    Arena arena = Arena.new();
    unsafe {
        long* pointer = ArenaAlloc(arena, 8);
        *pointer = 40;
        *SelectedPointer(pointer) += 2;
        Console.WriteLine(*pointer);

        const long* readable = ArenaAlloc(arena, 8);
        Console.WriteLine(*readable);
    }
    return 0;
}
