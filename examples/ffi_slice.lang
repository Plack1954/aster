private extern nuint NativeFillBytes(Span<byte> bytes, byte value);

int main() {
    Buffer buffer = Buffer.allocate(16);
    unsafe {
        Span<byte> bytes = BufferAsMutSlice(buffer);
        Console.WriteLine(NativeFillBytes(bytes, 42));
    }
    return 0;
}
