namespace Aster.Memory;

public using ByteError = string;

// These views do not extend Buffer lifetime. The caller keeps the Buffer
// alive for the complete use of the returned span.
public extern ReadOnlySpan<byte> BufferAsSlice(Buffer buffer);

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

// Returned ranges are non-owning views. The programmer must keep the source
// storage alive for as long as the returned span is used.
public extern ReadOnlySpan<byte> ByteSliceRange(
    ReadOnlySpan<byte> bytes,
    nuint start,
    nuint length
);

public extern Span<byte> ByteSliceRangeMut(
    Span<byte> bytes,
    nuint start,
    nuint length
);

public extern void ByteSliceCopyTo(
    ReadOnlySpan<byte> source,
    Span<byte> destination
);

public extern bool ByteSliceTryCopyTo(
    ReadOnlySpan<byte> source,
    Span<byte> destination
);

public extern void ByteSliceFill(Span<byte> bytes, byte value);

public extern void ByteSliceClear(Span<byte> bytes);

public extern bool ByteSliceSequenceEqual(
    ReadOnlySpan<byte> left,
    ReadOnlySpan<byte> right
);

// Returns -1 when the byte is absent.
public extern long ByteSliceIndexOf(
    ReadOnlySpan<byte> bytes,
    byte value
);

public extern bool ByteSliceStartsWith(
    ReadOnlySpan<byte> bytes,
    ReadOnlySpan<byte> prefix
);

public extern bool ByteSliceEndsWith(
    ReadOnlySpan<byte> bytes,
    ReadOnlySpan<byte> suffix
);

// This view borrows its source storage and must not outlive it.
public extern ReadOnlySpan<byte> StringAsByteSlice(const ref string bytes);

// Copies the half-open byte range into one owned string. string storage is
// UTF-8-oriented, but this low-level operation does not validate encoding.
public extern Result<string, ByteError> ByteSliceToString(
    ReadOnlySpan<byte> bytes,
    nuint start,
    nuint end
);
