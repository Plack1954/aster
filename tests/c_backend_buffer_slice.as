using Aster.Memory;

int main() {
    Buffer buffer = Buffer.allocate(17);
    unsafe {
        Span<byte> bytes = BufferAsMutSlice(buffer);
        ReadOnlySpan<byte> view = bytes;
        Console.WriteLine(ByteSliceLen(view));
        long total = 0;
        foreach (byte value in bytes) {
            total += (long)value;
        }
        Console.WriteLine(total);
    }
    return 0;
}
