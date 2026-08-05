private void allocate(Arena arena) {
    unsafe {
        byte* pointer = ArenaAlloc(arena, 8);
        Console.WriteLine(pointer);
    }
}

int main() {
    Arena arena = Arena.new();
    allocate(arena);
    return 0;
}
