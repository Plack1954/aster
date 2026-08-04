int main() {
    Arena arena = Arena.new();
    unsafe {
        const long* pointer = ArenaAlloc(arena, 8);
        *pointer = 1;
    }
    return 0;
}
