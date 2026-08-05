private sbyte AddI8(sbyte left, sbyte right) {
    return left + right;
}

private ushort AddU16(ushort left, ushort right) {
    return left + right;
}

private float half(float value) {
    return value / 2.0;
}

int main() {
    sbyte signed = AddI8(20, 22);
    ushort unsigned = AddU16(65000, 535);
    float floating = half(5.0);
    nuint index = 1;
    int values[2] = [10, 20];
    ulong maximum = 18446744073709551615;
    long minimum = -9223372036854775808;
    long wide = 42;
    byte narrowed = (byte)wide;
    float convertedFloat = (float)narrowed;
    char scalar = (char)65;

    Console.WriteLine(signed);
    Console.WriteLine(unsigned);
    Console.WriteLine(floating);
    Console.WriteLine(values[index]);
    Console.WriteLine(maximum);
    Console.WriteLine(minimum);
    Console.WriteLine(narrowed);
    Console.WriteLine(convertedFloat);
    Console.WriteLine(scalar);
    return 0;
}
