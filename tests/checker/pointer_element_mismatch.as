private void consume(long* pointer) {
}

int main() {
    Arena arena = Arena.new();
    unsafe {
        byte* pointer = ArenaAlloc(arena, 8);
        consume(pointer);
    }
    return 0;
}
