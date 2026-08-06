using Aster.Memory;
using System.IO;

int main()
{
    Buffer sourceBuffer = Buffer.allocate(6);
    Buffer destinationBuffer = Buffer.allocate(6);
    Buffer smallBuffer = Buffer.allocate(2);
    unsafe
    {
        Span<byte> source = BufferAsMutSlice(sourceBuffer);
        Span<byte> destination = BufferAsMutSlice(destinationBuffer);
        Span<byte> small = BufferAsMutSlice(smallBuffer);

        ByteSliceFill(source, 7);
        ByteSliceSet(source, 0, 1);
        ByteSliceSet(source, 1, 2);
        ByteSliceSet(source, 4, 8);
        ByteSliceSet(source, 5, 9);
        if (ByteSliceIndexOf(source, 8) != 4 ||
            ByteSliceIndexOf(source, 99) != -1)
        {
            return 1;
        }

        ReadOnlySpan<byte> prefix = ByteSliceRange(source, 0, 2);
        ReadOnlySpan<byte> suffix = ByteSliceRange(source, 4, 2);
        if (!ByteSliceStartsWith(source, prefix) ||
            !ByteSliceEndsWith(source, suffix))
        {
            return 2;
        }

        ByteSliceClear(destination);
        ByteSliceCopyTo(source, destination);
        if (!ByteSliceSequenceEqual(source, destination) ||
            ByteSliceTryCopyTo(source, small))
        {
            return 3;
        }

        Span<byte> overlapping = ByteSliceRangeMut(destination, 1, 4);
        ByteSliceCopyTo(ByteSliceRange(destination, 0, 4), overlapping);
        if (ByteSliceAt(destination, 0) != 1 ||
            ByteSliceAt(destination, 1) != 1 ||
            ByteSliceAt(destination, 2) != 2)
        {
            return 4;
        }

        MemoryStream memory = MemoryStream.Create();
        memory.Write(source);
        if (memory.Position() != 6 || memory.Length() != 6)
        {
            return 5;
        }
        memory.Seek(0, SeekOrigin.Begin);
        ByteSliceClear(destination);
        if (memory.ReadInto(destination) != 6 ||
            !ByteSliceSequenceEqual(source, destination))
        {
            return 6;
        }
        memory.Seek(99, SeekOrigin.Begin);
        if (memory.ReadInto(destination) != 0) { return 7; }

        FileStream file = File.Create("byte_native_streams.bin");
        file.Write(source);
        file.Seek(0, SeekOrigin.Begin);
        ByteSliceClear(destination);
        if (file.ReadInto(destination) != 6 ||
            !ByteSliceSequenceEqual(source, destination))
        {
            return 8;
        }
        file.Close();
        File.Delete("byte_native_streams.bin");
    }
    return 0;
}
