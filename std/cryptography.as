namespace System.Security.Cryptography;

using Aster.Memory;

private extern Result<Unit, string> NativeCryptoRandomFill(
    Span<byte> destination
);
private extern Result<string, string> NativeCryptoRandomHex(long byteCount);
private extern Result<string, string> NativeCryptoUuidV4();
private extern Result<Unit, string> NativeCryptoSha256(
    ReadOnlySpan<byte> source,
    Span<byte> destination
);
private extern Result<Unit, string> NativeCryptoHmacSha256(
    ReadOnlySpan<byte> key,
    ReadOnlySpan<byte> source,
    Span<byte> destination
);
private extern bool NativeCryptoFixedTimeEquals(
    ReadOnlySpan<byte> left,
    ReadOnlySpan<byte> right
);

private Unit CryptoUnitOrThrow(Result<Unit, string> result)
{
    switch (result)
    {
        case Result.Ok(value): { return value; }
        case Result.Err(error): { throw new IOException(error); }
    }
}

private string CryptoStringOrThrow(Result<string, string> result)
{
    switch (result)
    {
        case Result.Ok(value): { return value; }
        case Result.Err(error): { throw new IOException(error); }
    }
}

public void RandomNumberGenerator.Fill(Span<byte> destination)
{
    Unit ignored = CryptoUnitOrThrow(NativeCryptoRandomFill(destination));
}

public string RandomNumberGenerator.GetHexString(long byteCount)
{
    if (byteCount < 0 || byteCount > 1024)
    {
        throw new ArgumentException("byteCount must be from 0 through 1024");
    }
    return CryptoStringOrThrow(NativeCryptoRandomHex(byteCount));
}

public string RandomNumberGenerator.UuidV4()
{
    return CryptoStringOrThrow(NativeCryptoUuidV4());
}

public Buffer SHA256.HashData(ReadOnlySpan<byte> source)
{
    Buffer digest = Buffer.allocate(32);
    unsafe
    {
        Span<byte> destination = BufferAsMutSlice(digest);
        Unit ignored = CryptoUnitOrThrow(
            NativeCryptoSha256(source, destination)
        );
    }
    return digest;
}

public Buffer HMACSHA256.HashData(
    ReadOnlySpan<byte> key,
    ReadOnlySpan<byte> source
)
{
    Buffer digest = Buffer.allocate(32);
    unsafe
    {
        Span<byte> destination = BufferAsMutSlice(digest);
        Unit ignored = CryptoUnitOrThrow(
            NativeCryptoHmacSha256(key, source, destination)
        );
    }
    return digest;
}

public bool CryptographicOperations.FixedTimeEquals(
    ReadOnlySpan<byte> left,
    ReadOnlySpan<byte> right
)
{
    return NativeCryptoFixedTimeEquals(left, right);
}
