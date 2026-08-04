using System.IO;

int main()
{
    MemoryStream memory = MemoryStream.Create();
    List<byte> initial = new();
    initial.Add(1);
    initial.Add(2);
    initial.Add(3);
    memory.Write(initial);
    if (memory.Length() != 3 || memory.Position() != 3) { return 1; }
    memory.Seek(0, SeekOrigin.Begin);
    if (memory.ReadByte() != 1) { return 2; }
    List<byte> remaining = memory.Read(8);
    if (remaining.Count != 2) { return 2; }

    memory.Seek(0, SeekOrigin.Begin);
    MemoryStream copied = MemoryStream.Create();
    memory.CopyTo(ref copied, 2);
    List<byte> copiedBytes = copied.ToArray();
    if (copiedBytes.Count != 3 || copiedBytes[2] != 3) { return 3; }

    BinaryWriter writer = BinaryWriter.Create(MemoryStream.Create());
    writer.Write(true);
    writer.Write((short)-1234);
    writer.Write((int)-123456789);
    writer.Write((long)-123456789012345);
    writer.Write((uint)4000000000);
    writer.Write("λime 😀");
    MemoryStream encoded = writer.BaseStream();
    encoded.Seek(0, SeekOrigin.Begin);

    BinaryReader reader = BinaryReader.Create(encoded);
    if (!reader.ReadBoolean() || reader.ReadInt16() != -1234 ||
        reader.ReadInt32() != -123456789 ||
        reader.ReadInt64() != -123456789012345 ||
        reader.ReadUInt32() != 4000000000 ||
        reader.ReadString() != "λime 😀") { return 4; }

    List<byte> fileBytes = new();
    fileBytes.Add(0);
    fileBytes.Add(255);
    fileBytes.Add(128);
    File.WriteAllBytes("streams_binary_surface.bin", fileBytes);
    List<byte> loaded = File.ReadAllBytes("streams_binary_surface.bin");
    if (loaded.Count != 3 || loaded[0] != 0 || loaded[1] != 255 ||
        loaded[2] != 128) { return 5; }

    FileStream file = File.OpenRead("streams_binary_surface.bin");
    if (!file.CanRead() || file.CanWrite() || !file.CanSeek() ||
        file.Length() != 3 || file.ReadByte() != 0)
        { return 6; }
    file.Seek(-1, SeekOrigin.End);
    if (file.ReadByte() != 128) { return 7; }
    file.Close();
    if (file.CanRead()) { return 8; }

    return 0;
}
