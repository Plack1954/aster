using Aster.Memory;
using System.Security.Cryptography;
using System.Text;

int main()
{
    Buffer first = Buffer.allocate(32);
    Buffer second = Buffer.allocate(32);
    unsafe
    {
        RandomNumberGenerator.Fill(BufferAsMutSlice(first));
        RandomNumberGenerator.Fill(BufferAsMutSlice(second));
        if (CryptographicOperations.FixedTimeEquals(
            BufferAsSlice(first), BufferAsSlice(second)))
        {
            return 1;
        }
    }
    string randomHex = RandomNumberGenerator.GetHexString(32);
    string uuid = RandomNumberGenerator.UuidV4();
    if (randomHex.Length != 64 || uuid.Length != 36 || !uuid.Contains("-"))
    {
        return 2;
    }

    Buffer digest = SHA256.HashData(StringAsByteSlice("abc"));
    unsafe
    {
        ReadOnlySpan<byte> bytes = BufferAsSlice(digest);
        if (ByteSliceAt(bytes, 0) != 186 || ByteSliceAt(bytes, 1) != 120 ||
            ByteSliceAt(bytes, 30) != 21 || ByteSliceAt(bytes, 31) != 173)
        {
            return 3;
        }
    }
    Buffer signature = HMACSHA256.HashData(
        StringAsByteSlice("key"),
        StringAsByteSlice("The quick brown fox jumps over the lazy dog")
    );
    unsafe
    {
        ReadOnlySpan<byte> bytes = BufferAsSlice(signature);
        if (ByteSliceAt(bytes, 0) != 247 || ByteSliceAt(bytes, 1) != 188 ||
            ByteSliceAt(bytes, 30) != 60 || ByteSliceAt(bytes, 31) != 216)
        {
            return 4;
        }
        if (!CryptographicOperations.FixedTimeEquals(bytes, bytes))
        {
            return 5;
        }
    }
    return 0;
}
