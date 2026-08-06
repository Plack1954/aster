struct DeepNumber {
    Arena storage;
    long* value;

    public DeepNumber(long initial) {
        storage = Arena.new();
        unsafe {
            value = ArenaAlloc(storage, 8);
            *value = initial;
        }
    }

    public DeepNumber(const ref DeepNumber other) {
        storage = Arena.new();
        unsafe {
            value = ArenaAlloc(storage, 8);
            *value = *other.value;
        }
    }
}

int main() {
    DeepNumber source = new DeepNumber(10);
    DeepNumber copied = source;
    unsafe {
        *copied.value = 20;
        Console.WriteLine(*source.value);
        Console.WriteLine(*copied.value);
    }
    return 0;
}
