namespace Aster.Memory;

public using ByteError = string;

public extern nuint ByteSliceLen(ReadOnlySpan<byte> bytes);

public extern byte ByteSliceAt(
    ReadOnlySpan<byte> bytes,
    nuint index
);

public extern void ByteSliceSet(
    Span<byte> bytes,
    nuint index,
    byte value
);

// Copies the half-open byte range into one owned string. string storage is
// UTF-8-oriented, but this low-level operation does not validate encoding.
public extern Result<string, ByteError> ByteSliceToString(
    ReadOnlySpan<byte> bytes,
    nuint start,
    nuint end
);
