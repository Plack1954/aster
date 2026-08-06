namespace System.IO;

using Aster.Memory;
using System.Text;

// File and Directory are the .NET-shaped static API owners. Open operating-
// system resources use FileStream and DirectoryStream instead of making the
// facade types double as handles.
public struct File {}
public struct Directory {}
public struct Path {}
public using DirectoryStream = NativeHandle;
public using IoError = string;
public delegate Result<bool, IoError> LineHandler(string line);

public extern Result<NativeHandle, IoError> NativeFileOpen(
    string path,
    string mode
);

// The returned shared handle owns the temporary path. Its final release
// removes the file.
public extern Result<NativeHandle, IoError> NativeFileCreateTemporary(
    string directory
);

public extern string NativeFileTemporaryPath(NativeHandle file);

public extern Result<string, IoError> NativeFileReadAll(
    NativeHandle file
);

public extern Result<nuint, IoError> NativeFileWrite(
    NativeHandle file,
    string data
);

public extern Result<nuint, IoError> NativeFileReadInto(
    NativeHandle file,
    Span<byte> bytes
);

public extern Result<nuint, IoError> NativeFileWriteBytes(
    NativeHandle file,
    ReadOnlySpan<byte> bytes,
    nuint count
);

public extern Result<long, IoError> NativeFileSeek(
    NativeHandle file,
    long offset,
    int origin
);

public extern Result<long, IoError> NativeFileLength(NativeHandle file);

public extern Result<Unit, IoError> NativeFileFlush(NativeHandle file);

public extern void NativeFileClose(NativeHandle file);

public enum SeekOrigin
{
    Begin,
    Current,
    End,
}

// One concrete stream representation keeps dispatch explicit and predictable.
// kind 1 is a native file and kind 2 is an in-memory byte sequence.
public struct Stream
{
    int kind;
    NativeHandle? file;
    List<byte> memory;
    long position;
    bool readable;
    bool writable;
    bool disposed;
}

public using FileStream = Stream;
public using MemoryStream = Stream;

public struct BinaryReader
{
    int kind;
    NativeHandle? file;
    List<byte> memory;
    long position;
    bool disposed;
}

public struct BinaryWriter
{
    int kind;
    NativeHandle? file;
    List<byte> memory;
    long position;
    bool disposed;
}

private T FileResultOrThrow<T>(Result<T, IoError> result)
{
    switch (result)
    {
        case Result.Ok(value): { return value; }
        case Result.Err(error): { throw new IOException(error); }
    }
}

private Stream CreateFileStream(string path, string mode, bool read, bool write)
{
    Result<NativeHandle, IoError> opened = NativeFileOpen(path, mode);
    NativeHandle handle = FileResultOrThrow(opened);
    return new()
    {
        kind = 1,
        file = handle,
        memory = new(),
        position = 0,
        readable = read,
        writable = write,
        disposed = false
    };
}

public FileStream File.OpenRead(string path)
{
    return CreateFileStream(path, "rb", true, false);
}

public FileStream File.OpenWrite(string path)
{
    return CreateFileStream(path, "wb", false, true);
}

public FileStream File.Create(string path)
{
    return CreateFileStream(path, "w+b", true, true);
}

public MemoryStream MemoryStream.Create()
{
    return new()
    {
        kind = 2,
        file = null,
        memory = new(),
        position = 0,
        readable = true,
        writable = true,
        disposed = false
    };
}

public MemoryStream MemoryStream.Create(List<byte> buffer)
{
    return new()
    {
        kind = 2,
        file = null,
        memory = buffer,
        position = 0,
        readable = true,
        writable = true,
        disposed = false
    };
}

private void Stream.EnsureOpen(Stream self)
{
    if (self.disposed)
    {
        throw new InvalidOperationException("Cannot access a closed Stream.");
    }
}

public bool Stream.CanRead(Stream self)
{
    return !self.disposed && self.readable;
}

public bool Stream.CanWrite(Stream self)
{
    return !self.disposed && self.writable;
}

public bool Stream.CanSeek(Stream self)
{
    return !self.disposed && (self.kind == 1 || self.kind == 2);
}

public long Stream.Position(Stream self)
{
    self.EnsureOpen();
    return self.position;
}

public long Stream.Length(Stream self)
{
    self.EnsureOpen();
    if (self.kind == 1)
    {
        if (self.file == null)
        {
            throw new InvalidOperationException("Stream has no file handle.");
        }
        return FileResultOrThrow(NativeFileLength(self.file.Value));
    }
    return (long)self.memory.Count;
}

public List<byte> Stream.Read(ref Stream self, int count)
{
    self.EnsureOpen();
    if (!self.readable)
    {
        throw new InvalidOperationException("Stream does not support reading.");
    }
    if (count < 0)
    {
        throw new ArgumentException("count cannot be negative");
    }
    List<byte> result = new();
    if (count == 0) { return result; }
    if (self.kind == 2)
    {
        long available = (long)self.memory.Count - self.position;
        int amount = available < (long)count ? (int)available : count;
        for (int index = 0; index < amount; index += 1)
        {
            result.Add(self.memory[(nuint)(self.position + (long)index)]);
        }
        self.position += (long)amount;
        return result;
    }
    if (self.file == null)
    {
        throw new InvalidOperationException("Stream has no file handle.");
    }
    Buffer buffer = Buffer.allocate((long)count);
    unsafe
    {
        Span<byte> bytes = BufferAsMutSlice(buffer);
        nuint amount = FileResultOrThrow(
            NativeFileReadInto(self.file.Value, bytes)
        );
        for (nuint index = 0; index < amount; index += 1)
        {
            result.Add(ByteSliceAt(bytes, index));
        }
        self.position += (long)amount;
    }
    return result;
}

// Reads directly into caller-owned storage and returns the number of bytes
// written. The span is borrowed only for the duration of this call.
public nuint Stream.ReadInto(ref Stream self, Span<byte> destination)
{
    self.EnsureOpen();
    if (!self.readable)
    {
        throw new InvalidOperationException("Stream does not support reading.");
    }
    if (self.kind == 2)
    {
        nuint capacity = ByteSliceLen(destination);
        long available = (long)self.memory.Count - self.position;
        if (available <= 0 || capacity == 0) { return 0; }
        nuint amount = available < (long)capacity
            ? (nuint)available : capacity;
        for (nuint index = 0; index < amount; index += 1)
        {
            ByteSliceSet(
                destination,
                index,
                self.memory[(nuint)self.position + index]
            );
        }
        self.position += (long)amount;
        return amount;
    }
    if (self.file == null)
    {
        throw new InvalidOperationException("Stream has no file handle.");
    }
    nuint amount = FileResultOrThrow(
        NativeFileReadInto(self.file.Value, destination)
    );
    self.position += (long)amount;
    return amount;
}

public int Stream.ReadByte(ref Stream self)
{
    List<byte> bytes = self.Read(1);
    return bytes.Count == 0 ? -1 : (int)bytes[0];
}

public void Stream.Write(ref Stream self, List<byte> bytes)
{
    self.EnsureOpen();
    if (!self.writable)
    {
        throw new InvalidOperationException("Stream does not support writing.");
    }
    if (self.kind == 2)
    {
        foreach (byte value in bytes)
        {
            if (self.position < (long)self.memory.Count)
            {
                self.memory.Set((nuint)self.position, value);
            }
            else
            {
                self.memory.Add(value);
            }
            self.position += 1;
        }
        return;
    }
    if (self.file == null)
    {
        throw new InvalidOperationException("Stream has no file handle.");
    }
    Buffer buffer = Buffer.allocate((long)bytes.Count);
    unsafe
    {
        Span<byte> destination = BufferAsMutSlice(buffer);
        for (nuint index = 0; index < bytes.Count; index += 1)
        {
            ByteSliceSet(destination, index, bytes[index]);
        }
        nuint written = FileResultOrThrow(
            NativeFileWriteBytes(self.file.Value, destination, bytes.Count)
        );
        self.position += (long)written;
    }
}

// Writes directly from caller-owned storage. The span is borrowed only for
// the duration of this call.
public void Stream.Write(ref Stream self, ReadOnlySpan<byte> bytes)
{
    self.EnsureOpen();
    if (!self.writable)
    {
        throw new InvalidOperationException("Stream does not support writing.");
    }
    nuint count = ByteSliceLen(bytes);
    if (self.kind == 2)
    {
        for (nuint index = 0; index < count; index += 1)
        {
            byte value = ByteSliceAt(bytes, index);
            if (self.position < (long)self.memory.Count)
            {
                self.memory.Set((nuint)self.position, value);
            }
            else
            {
                self.memory.Add(value);
            }
            self.position += 1;
        }
        return;
    }
    if (self.file == null)
    {
        throw new InvalidOperationException("Stream has no file handle.");
    }
    nuint written = FileResultOrThrow(
        NativeFileWriteBytes(self.file.Value, bytes, count)
    );
    self.position += (long)written;
}

public void Stream.WriteByte(ref Stream self, byte value)
{
    List<byte> bytes = new();
    bytes.Add(value);
    Stream.Write(ref self, bytes);
}

public long Stream.Seek(ref Stream self, long offset, SeekOrigin origin)
{
    self.EnsureOpen();
    if (self.kind == 1)
    {
        if (self.file == null)
        {
            throw new InvalidOperationException("Stream has no file handle.");
        }
        int nativeOrigin = 0;
        switch (origin)
        {
            case SeekOrigin.Begin: { nativeOrigin = 0; }
            case SeekOrigin.Current: { nativeOrigin = 1; }
            case SeekOrigin.End: { nativeOrigin = 2; }
        }
        self.position = FileResultOrThrow(
            NativeFileSeek(self.file.Value, offset, nativeOrigin)
        );
        return self.position;
    }
    long basePosition = 0;
    switch (origin)
    {
        case SeekOrigin.Begin: { basePosition = 0; }
        case SeekOrigin.Current: { basePosition = self.position; }
        case SeekOrigin.End: { basePosition = (long)self.memory.Count; }
    }
    long next = basePosition + offset;
    if (next < 0)
    {
        throw new IOException("Attempted to seek before the beginning.");
    }
    self.position = next;
    return next;
}

public void Stream.SetLength(ref Stream self, long value)
{
    self.EnsureOpen();
    if (self.kind != 2 || !self.writable)
    {
        throw new InvalidOperationException(
            "Stream does not support setting its length."
        );
    }
    if (value < 0)
    {
        throw new ArgumentException("value cannot be negative");
    }
    while ((long)self.memory.Count < value) { self.memory.Add(0); }
    if ((long)self.memory.Count > value)
    {
        self.memory.RemoveRange((nuint)value, self.memory.Count - (nuint)value);
    }
    if (self.position > value) { self.position = value; }
}

public void Stream.CopyTo(ref Stream self, ref Stream destination)
{
    self.CopyTo(ref destination, 81920);
}

public void Stream.CopyTo(
    ref Stream self,
    ref Stream destination,
    int bufferSize
)
{
    if (bufferSize <= 0)
    {
        throw new ArgumentException("bufferSize must be positive");
    }
    bool copying = true;
    while (copying)
    {
        List<byte> bytes = self.Read(bufferSize);
        if (bytes.Count == 0) { copying = false; }
        else { destination.Write(bytes); }
    }
}

public List<byte> Stream.ToArray(Stream self)
{
    self.EnsureOpen();
    if (self.kind != 2)
    {
        throw new InvalidOperationException("Stream is not memory-backed.");
    }
    return self.memory;
}

public void Stream.Flush(Stream self)
{
    self.EnsureOpen();
    if (self.kind == 1)
    {
        if (self.file == null)
        {
            throw new InvalidOperationException("Stream has no file handle.");
        }
        FileResultOrThrow(NativeFileFlush(self.file.Value));
    }
}

public void Stream.Close(ref Stream self)
{
    if (self.disposed) { return; }
    switch (self.file)
    {
        case Option.Some(file): { NativeFileClose(file); }
        case Option.None: {}
    }
    self.disposed = true;
    self.readable = false;
    self.writable = false;
}

public BinaryReader BinaryReader.Create(Stream input)
{
    return new()
    {
        kind = input.kind,
        file = input.file,
        memory = input.memory,
        position = input.position,
        disposed = input.disposed
    };
}

public BinaryWriter BinaryWriter.Create(Stream output)
{
    return new()
    {
        kind = output.kind,
        file = output.file,
        memory = output.memory,
        position = output.position,
        disposed = output.disposed
    };
}

public Stream BinaryReader.BaseStream(BinaryReader self)
{
    return new()
    {
        kind = self.kind,
        file = self.file,
        memory = self.memory,
        position = self.position,
        readable = true,
        writable = false,
        disposed = self.disposed
    };
}

public Stream BinaryWriter.BaseStream(BinaryWriter self)
{
    return new()
    {
        kind = self.kind,
        file = self.file,
        memory = self.memory,
        position = self.position,
        readable = false,
        writable = true,
        disposed = self.disposed
    };
}

private List<byte> BinaryReader.ReadExactly(ref BinaryReader self, int count)
{
    if (self.disposed)
    {
        throw new InvalidOperationException("Cannot access a closed reader.");
    }
    List<byte> bytes = new();
    if (self.kind == 2)
    {
        if ((long)self.memory.Count - self.position < (long)count)
        {
            throw new IOException(
                "Unable to read beyond the end of the stream."
            );
        }
        for (int index = 0; index < count; index += 1)
        {
            bytes.Add(self.memory[(nuint)(self.position + (long)index)]);
        }
        self.position += (long)count;
        return bytes;
    }
    if (self.file == null)
    {
        throw new InvalidOperationException("Reader has no file handle.");
    }
    Buffer buffer = Buffer.allocate((long)count);
    unsafe
    {
        Span<byte> source = BufferAsMutSlice(buffer);
        nuint amount = FileResultOrThrow(
            NativeFileReadInto(self.file.Value, source)
        );
        for (nuint index = 0; index < amount; index += 1)
        {
            bytes.Add(ByteSliceAt(source, index));
        }
        self.position += (long)amount;
    }
    if (bytes.Count != (nuint)count)
    {
        throw new IOException("Unable to read beyond the end of the stream.");
    }
    return bytes;
}

public byte BinaryReader.ReadByte(ref BinaryReader self)
{
    List<byte> bytes = BinaryReader.ReadExactly(ref self, 1);
    return bytes[0];
}

public bool BinaryReader.ReadBoolean(ref BinaryReader self)
{
    return BinaryReader.ReadByte(ref self) != 0;
}

public ushort BinaryReader.ReadUInt16(ref BinaryReader self)
{
    List<byte> bytes = BinaryReader.ReadExactly(ref self, 2);
    return (ushort)((ushort)bytes[0] | ((ushort)bytes[1] << 8));
}

public short BinaryReader.ReadInt16(ref BinaryReader self)
{
    ushort raw = BinaryReader.ReadUInt16(ref self);
    return raw <= 32767 ? (short)raw : (short)((int)raw - 65536);
}

public uint BinaryReader.ReadUInt32(ref BinaryReader self)
{
    List<byte> bytes = BinaryReader.ReadExactly(ref self, 4);
    return (uint)bytes[0] |
        ((uint)bytes[1] << 8) |
        ((uint)bytes[2] << 16) |
        ((uint)bytes[3] << 24);
}

public int BinaryReader.ReadInt32(ref BinaryReader self)
{
    uint raw = BinaryReader.ReadUInt32(ref self);
    return raw <= 2147483647
        ? (int)raw : (int)((long)raw - 4294967296);
}

public ulong BinaryReader.ReadUInt64(ref BinaryReader self)
{
    List<byte> bytes = BinaryReader.ReadExactly(ref self, 8);
    ulong result = 0;
    for (int index = 0; index < 8; index += 1)
    {
        result |= (ulong)bytes[(nuint)index] << (ulong)(index * 8);
    }
    return result;
}

public long BinaryReader.ReadInt64(ref BinaryReader self)
{
    List<byte> bytes = BinaryReader.ReadExactly(ref self, 8);
    long result = 0;
    for (int index = 0; index < 7; index += 1)
    {
        result |= (long)bytes[(nuint)index] << (long)(index * 8);
    }
    result |= ((long)bytes[7] - (bytes[7] >= 128 ? 256 : 0))
        << 56;
    return result;
}

private int BinaryReader.Read7BitEncodedInt(ref BinaryReader self)
{
    int result = 0;
    int shift = 0;
    while (shift < 35)
    {
        byte current = BinaryReader.ReadByte(ref self);
        result |= (int)(current & 127) << shift;
        if ((current & 128) == 0) { return result; }
        shift += 7;
    }
    throw new FormatException("Invalid 7-bit encoded integer.");
}

public string BinaryReader.ReadString(ref BinaryReader self)
{
    int length = BinaryReader.Read7BitEncodedInt(ref self);
    return Encoding.UTF8().GetString(
        BinaryReader.ReadExactly(ref self, length)
    );
}

public List<byte> BinaryReader.ReadBytes(ref BinaryReader self, int count)
{
    if (count < 0)
    {
        throw new ArgumentException("count cannot be negative");
    }
    List<byte> result = new();
    for (int index = 0; index < count; index += 1)
    {
        if (self.kind == 2 && self.position >= (long)self.memory.Count)
            { return result; }
        List<byte> next = BinaryReader.ReadExactly(ref self, 1);
        result.Add(next[0]);
    }
    return result;
}

public void BinaryReader.Close(ref BinaryReader self)
{
    switch (self.file)
    {
        case Option.Some(file): { NativeFileClose(file); }
        case Option.None: {}
    }
    self.disposed = true;
}

public void BinaryWriter.Write(ref BinaryWriter self, byte value)
{
    if (self.disposed)
    {
        throw new InvalidOperationException("Cannot access a closed writer.");
    }
    if (self.kind == 2)
    {
        if (self.position < (long)self.memory.Count)
            { self.memory.Set((nuint)self.position, value); }
        else { self.memory.Add(value); }
        self.position += 1;
        return;
    }
    if (self.file == null)
    {
        throw new InvalidOperationException("Writer has no file handle.");
    }
    Buffer buffer = Buffer.allocate(1);
    unsafe
    {
        Span<byte> destination = BufferAsMutSlice(buffer);
        ByteSliceSet(destination, 0, value);
        nuint written = FileResultOrThrow(
            NativeFileWriteBytes(self.file.Value, destination, 1)
        );
        self.position += (long)written;
    }
}

public void BinaryWriter.Write(ref BinaryWriter self, bool value)
{
    BinaryWriter.Write(
        ref self,
        value ? (byte)1 : (byte)0
    );
}

public void BinaryWriter.Write(ref BinaryWriter self, ushort value)
{
    BinaryWriter.Write(ref self, (byte)(value & 255));
    BinaryWriter.Write(ref self, (byte)((value >> 8) & 255));
}

public void BinaryWriter.Write(ref BinaryWriter self, short value)
{
    BinaryWriter.Write(ref self, (byte)((value >> 0) & 255));
    BinaryWriter.Write(ref self, (byte)((value >> 8) & 255));
}

public void BinaryWriter.Write(ref BinaryWriter self, uint value)
{
    for (int shift = 0; shift < 32; shift += 8)
    {
        BinaryWriter.Write(ref self, (byte)((value >> (uint)shift) & 255));
    }
}

public void BinaryWriter.Write(ref BinaryWriter self, int value)
{
    for (int shift = 0; shift < 32; shift += 8)
    {
        BinaryWriter.Write(ref self, (byte)((value >> shift) & 255));
    }
}

public void BinaryWriter.Write(ref BinaryWriter self, ulong value)
{
    for (int shift = 0; shift < 64; shift += 8)
    {
        BinaryWriter.Write(ref self, (byte)((value >> (ulong)shift) & 255));
    }
}

public void BinaryWriter.Write(ref BinaryWriter self, long value)
{
    for (int shift = 0; shift < 64; shift += 8)
    {
        BinaryWriter.Write(ref self, (byte)((value >> (long)shift) & 255));
    }
}

private void BinaryWriter.Write7BitEncodedInt(ref BinaryWriter self, int value)
{
    uint remaining = (uint)value;
    while (remaining >= 128)
    {
        BinaryWriter.Write(ref self, (byte)((remaining & 127) | 128));
        remaining >>= 7;
    }
    BinaryWriter.Write(ref self, (byte)remaining);
}

public void BinaryWriter.Write(ref BinaryWriter self, string value)
{
    List<byte> bytes = Encoding.UTF8().GetBytes(value);
    BinaryWriter.Write7BitEncodedInt(ref self, (int)bytes.Count);
    foreach (byte value in bytes)
    {
        BinaryWriter.Write(ref self, value);
    }
}

public void BinaryWriter.Write(ref BinaryWriter self, List<byte> bytes)
{
    foreach (byte value in bytes)
    {
        BinaryWriter.Write(ref self, value);
    }
}

public void BinaryWriter.Flush(BinaryWriter self)
{
    if (self.disposed)
    {
        throw new InvalidOperationException("Cannot access a closed writer.");
    }
}

public void BinaryWriter.Close(ref BinaryWriter self)
{
    switch (self.file)
    {
        case Option.Some(file): { NativeFileClose(file); }
        case Option.None: {}
    }
    self.disposed = true;
}

public string File.ReadAllText(string path)
{
    NativeHandle file = FileResultOrThrow(NativeFileOpen(path, "rb"));
    return FileResultOrThrow(NativeFileReadAll(file));
}

public List<byte> File.ReadAllBytes(string path)
{
    FileStream stream = File.OpenRead(path);
    List<byte> result = new();
    bool reading = true;
    while (reading)
    {
        List<byte> chunk = stream.Read(65536);
        if (chunk.Count == 0) { reading = false; }
        else { result.AddRange(chunk); }
    }
    stream.Close();
    return result;
}

public void File.WriteAllText(string path, string contents)
{
    NativeHandle file = FileResultOrThrow(NativeFileOpen(path, "wb"));
    nuint written = FileResultOrThrow(NativeFileWrite(file, contents));
    if (written != contents.Length)
    {
        throw new IOException("file write did not write the complete string");
    }
}

public void File.WriteAllBytes(string path, List<byte> bytes)
{
    FileStream stream = File.Create(path);
    stream.Write(bytes);
    stream.Close();
}

public List<string> File.ReadAllLines(string path)
{
    return FileResultOrThrow(ReadLinesBuffered(path, 65536));
}

public Result<nuint, IoError> CopyFileBuffered(
    string sourcePath,
    string destinationPath,
    long bufferSize
) {
    if (bufferSize <= 0) {
        return Result.Err("buffer size must be positive");
    }
    NativeHandle source = FileResultOrThrow(NativeFileOpen(sourcePath, "rb"));
    NativeHandle destination =
        FileResultOrThrow(NativeFileOpen(destinationPath, "wb"));
    Buffer buffer = Buffer.allocate(bufferSize);
    nuint total = 0;
    bool reading = true;

    while (reading) {
        unsafe {
            Span<byte> bytes = BufferAsMutSlice(buffer);
            nuint count =
                FileResultOrThrow(NativeFileReadInto(source, bytes));
            if (count == 0) {
                reading = false;
            } else {
                nuint written = FileResultOrThrow(NativeFileWriteBytes(
                    destination,
                    bytes,
                    count,
                ));
                total = total + written;
            }
        }
    }
    return Result.Ok(total);
}

public Result<List<string>, IoError> ReadLinesBuffered(
    string path,
    long bufferSize
) {
    if (bufferSize <= 0) {
        return Result.Err("buffer size must be positive");
    }
    NativeHandle file = FileResultOrThrow(NativeFileOpen(path, "rb"));
    Buffer buffer = Buffer.allocate(bufferSize);
    List<string> lines = new();
    StringBuilder line = new();
    nuint lineLength = 0;
    bool reading = true;
    bool skipLineFeed = false;

    while (reading) {
        unsafe {
            Span<byte> bytes = BufferAsMutSlice(buffer);
            nuint count = FileResultOrThrow(NativeFileReadInto(file, bytes));
            if (count == 0) {
                reading = false;
            } else {
                nuint cursor = 0;
                nuint segmentStart = 0;
                if (skipLineFeed)
                {
                    if (ByteSliceAt(bytes, 0) == 10)
                    {
                        cursor = 1;
                        segmentStart = 1;
                    }
                    skipLineFeed = false;
                }
                while (cursor < count) {
                    byte current = ByteSliceAt(bytes, cursor);
                    if (current == 10 || current == 13) {
                        string segment =
                            FileResultOrThrow(ByteSliceToString(
                                bytes,
                                segmentStart,
                                cursor,
                            ));
                        line.Append(segment);
                        string completed =
                            line.ToString();
                        lines.Add(completed);
                        line = new();
                        lineLength = 0;
                        if (current == 13)
                        {
                            if (cursor + 1 < count &&
                                ByteSliceAt(bytes, cursor + 1) == 10)
                            {
                                cursor = cursor + 1;
                            }
                            else if (cursor + 1 == count)
                            {
                                skipLineFeed = true;
                            }
                        }
                        segmentStart = cursor + 1;
                    }
                    cursor = cursor + 1;
                }
                if (segmentStart < count) {
                    string segment =
                        FileResultOrThrow(ByteSliceToString(
                            bytes,
                            segmentStart,
                            count,
                        ));
                    line.Append(segment);
                    lineLength =
                        lineLength + (count - segmentStart);
                }
            }
        }
    }
    if (lineLength > 0) {
        string completed =
            line.ToString();
        lines.Add(completed);
    }
    return Result.Ok(lines);
}

public Result<nuint, IoError> ForEachLineBuffered(
    string path,
    long bufferSize,
    LineHandler handler
) {
    if (bufferSize <= 0) {
        return Result.Err("buffer size must be positive");
    }
    NativeHandle file = try NativeFileOpen(path, "rb");
    Buffer buffer = Buffer.allocate(bufferSize);
    StringBuilder line = new();
    nuint lineLength = 0;
    nuint lineCount = 0;
    bool reading = true;
    bool skipLineFeed = false;

    while (reading) {
        unsafe {
            Span<byte> bytes = BufferAsMutSlice(buffer);
            nuint count = try NativeFileReadInto(file, bytes);
            if (count == 0) {
                reading = false;
            } else {
                nuint cursor = 0;
                nuint segmentStart = 0;
                if (skipLineFeed)
                {
                    if (ByteSliceAt(bytes, 0) == 10)
                    {
                        cursor = 1;
                        segmentStart = 1;
                    }
                    skipLineFeed = false;
                }
                while (cursor < count) {
                    byte current = ByteSliceAt(bytes, cursor);
                    if (current == 10 || current == 13) {
                        string segment =
                            try ByteSliceToString(
                                bytes,
                                segmentStart,
                                cursor,
                            );
                        line.Append(segment);
                        string completed =
                            line.ToString();
                        lineCount = lineCount + 1;
                        bool keepReading = try handler(completed);
                        if (!keepReading) {
                            return Result.Ok(lineCount);
                        }
                        line = new();
                        lineLength = 0;
                        if (current == 13)
                        {
                            if (cursor + 1 < count &&
                                ByteSliceAt(bytes, cursor + 1) == 10)
                            {
                                cursor = cursor + 1;
                            }
                            else if (cursor + 1 == count)
                            {
                                skipLineFeed = true;
                            }
                        }
                        segmentStart = cursor + 1;
                    }
                    cursor = cursor + 1;
                }
                if (segmentStart < count) {
                    string segment =
                        try ByteSliceToString(
                            bytes,
                            segmentStart,
                            count,
                        );
                    line.Append(segment);
                    lineLength =
                        lineLength + (count - segmentStart);
                }
            }
        }
    }
    if (lineLength > 0) {
        string completed =
            line.ToString();
        lineCount = lineCount + 1;
        bool ignored = try handler(completed);
    }
    return Result.Ok(lineCount);
}

// Directory and path operations share the .NET-style System.IO namespace.

public extern Result<DirectoryStream, string> NativeDirectoryOpen(
    string path
);

public extern Result<string, string> NativeDirectoryNext(
    DirectoryStream directory
);

public using FilesystemError = string;

public extern Result<bool, FilesystemError> NativePathExists(
    string path
);

public extern Result<bool, FilesystemError> NativePathIsFile(
    string path
);

public extern Result<bool, FilesystemError> NativePathIsDirectory(
    string path
);

public extern string NativePathCombine(string path1, string path2);
public extern string NativePathJoin(string path1, string path2);
public extern string NativePathGetFileName(string path);
public extern string NativePathGetFileNameWithoutExtension(string path);
public extern string NativePathGetExtension(string path);
public extern bool NativePathIsPathFullyQualified(string path);
private extern bool NativePathIsRoot(string path);
public extern string NativePathGetDirectoryName(string path);
public extern string NativePathGetPathRoot(string path);
private extern string NativePathChangeExtension(
    string path,
    string extension,
    bool removeExtension
);
public extern string NativeDirectoryGetCurrentDirectory();
public extern void NativeDirectorySetCurrentDirectory(string path);
private extern string NativeEnvironmentNewLine();

public extern Result<Unit, FilesystemError> NativeCreateDirectory(
    string path
);

public extern Result<Unit, FilesystemError> NativeRenamePath(
    string source,
    string destination
);

public extern Result<Unit, FilesystemError> NativeRemoveFile(
    string path
);

public extern Result<Unit, FilesystemError> NativeRemoveDirectory(
    string path
);

public bool File.Exists(string path)
{
    switch (NativePathIsFile(path))
    {
        case Result.Ok(exists): { return exists; }
        case Result.Err(error): { return false; }
    }
}

public void File.Copy(string sourceFileName, string destFileName)
{
    if (FileResultOrThrow(NativePathExists(destFileName)))
    {
        throw new IOException("destination file already exists");
    }
    nuint ignored = FileResultOrThrow(
        CopyFileBuffered(sourceFileName, destFileName, 65536)
    );
}

public void File.Copy(
    string sourceFileName,
    string destFileName,
    bool overwrite
)
{
    if (!overwrite && FileResultOrThrow(NativePathExists(destFileName)))
    {
        throw new IOException("destination file already exists");
    }
    nuint ignored = FileResultOrThrow(
        CopyFileBuffered(sourceFileName, destFileName, 65536)
    );
}

public void File.Move(string sourceFileName, string destFileName)
{
    if (FileResultOrThrow(NativePathExists(destFileName)))
    {
        throw new IOException("destination path already exists");
    }
    Unit ignored = FileResultOrThrow(
        NativeRenamePath(sourceFileName, destFileName)
    );
}

public void File.Delete(string path)
{
    if (!File.Exists(path))
    {
        return;
    }
    Unit ignored = FileResultOrThrow(NativeRemoveFile(path));
}

public bool Directory.Exists(string path)
{
    switch (NativePathIsDirectory(path))
    {
        case Result.Ok(exists): { return exists; }
        case Result.Err(error): { return false; }
    }
}

public void Directory.CreateDirectory(string path)
{
    if (Directory.Exists(path))
    {
        return;
    }
    Unit ignored = FileResultOrThrow(NativeCreateDirectory(path));
}

public void Directory.Move(string sourceDirName, string destDirName)
{
    if (FileResultOrThrow(NativePathExists(destDirName)))
    {
        throw new IOException("destination path already exists");
    }
    Unit ignored = FileResultOrThrow(
        NativeRenamePath(sourceDirName, destDirName)
    );
}

public void Directory.Delete(string path)
{
    Unit ignored = FileResultOrThrow(NativeRemoveDirectory(path));
}

public string Directory.GetCurrentDirectory()
{
    return NativeDirectoryGetCurrentDirectory();
}

public void Directory.SetCurrentDirectory(string path)
{
    NativeDirectorySetCurrentDirectory(path);
}

public string Path.Combine(string path1, string path2)
{
    return NativePathCombine(path1, path2);
}

public string Path.Combine(string path1, string path2, string path3)
{
    return NativePathCombine(NativePathCombine(path1, path2), path3);
}

public string Path.Combine(
    string path1,
    string path2,
    string path3,
    string path4
)
{
    return NativePathCombine(
        NativePathCombine(NativePathCombine(path1, path2), path3),
        path4
    );
}

public string Path.Join(string path1, string path2)
{
    return NativePathJoin(path1, path2);
}

public string Path.Join(string path1, string path2, string path3)
{
    return NativePathJoin(NativePathJoin(path1, path2), path3);
}

public string Path.Join(
    string path1,
    string path2,
    string path3,
    string path4
)
{
    return NativePathJoin(
        NativePathJoin(NativePathJoin(path1, path2), path3),
        path4
    );
}

public string Path.GetFileName(string path)
{
    return NativePathGetFileName(path);
}

public string Path.GetFileNameWithoutExtension(string path)
{
    return NativePathGetFileNameWithoutExtension(path);
}

public string Path.GetExtension(string path)
{
    return NativePathGetExtension(path);
}

public bool Path.IsPathFullyQualified(string path)
{
    return NativePathIsPathFullyQualified(path);
}

public string? Path.GetDirectoryName(string path)
{
    if (path.Length == 0 || NativePathIsRoot(path))
    {
        return null;
    }
    return NativePathGetDirectoryName(path);
}

public string? Path.GetPathRoot(string path)
{
    if (path.Length == 0)
    {
        return null;
    }
    return NativePathGetPathRoot(path);
}

public string Path.ChangeExtension(string path, string? extension)
{
    if (extension == null)
    {
        return NativePathChangeExtension(path, "", true);
    }
    return NativePathChangeExtension(path, extension.Value, false);
}

private List<string> Directory.GetEntries(string path, int kind)
{
    DirectoryStream directory = FileResultOrThrow(
        NativeDirectoryOpen(path)
    );
    List<string> entries = new();
    bool reading = true;
    while (reading)
    {
        switch (NativeDirectoryNext(directory))
        {
            case Result.Ok(name):
            {
                string entry = Path.Combine(path, name);
                if (kind == 0 ||
                    (kind == 1 && File.Exists(entry)) ||
                    (kind == 2 && Directory.Exists(entry)))
                {
                    entries.Add(entry);
                }
            }
            case Result.Err(error):
            {
                if (error != "end of directory")
                {
                    throw new IOException(error);
                }
                reading = false;
            }
        }
    }
    return entries;
}

public List<string> Directory.GetFileSystemEntries(string path)
{
    return Directory.GetEntries(path, 0);
}

public List<string> Directory.GetFiles(string path)
{
    return Directory.GetEntries(path, 1);
}

public List<string> Directory.GetDirectories(string path)
{
    return Directory.GetEntries(path, 2);
}

private string File.LinesToText(List<string> contents)
{
    string newLine = NativeEnvironmentNewLine();
    StringBuilder builder = new();
    foreach (string line in contents)
    {
        builder.Append(line);
        builder.Append(newLine);
    }
    return builder.ToString();
}

public void File.WriteAllLines(string path, List<string> contents)
{
    File.WriteAllText(path, File.LinesToText(contents));
}

public void File.AppendAllText(string path, string contents)
{
    NativeHandle file = FileResultOrThrow(NativeFileOpen(path, "ab"));
    nuint written = FileResultOrThrow(NativeFileWrite(file, contents));
    if (written != contents.Length)
    {
        throw new IOException("file append did not write the complete string");
    }
}

public void File.AppendAllLines(string path, List<string> contents)
{
    File.AppendAllText(path, File.LinesToText(contents));
}
