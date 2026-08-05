int main() {
    byte flags = 0b1100;

    Console.WriteLine((long)(flags & 0b1010));
    Console.WriteLine((long)(flags | 0b0011));
    Console.WriteLine((long)(flags ^ 0b1010));
    Console.WriteLine((long)(~flags));

    flags &= 0b1110;
    flags ^= 0b0110;
    flags |= 0b0001;
    flags <<= 1;
    flags >>= 2;
    Console.WriteLine((long)flags);

    sbyte signed = -16;
    Console.WriteLine((long)(~signed));

    Console.WriteLine((long)(0b0011 | 0b0100 & 0b0110));
    return 0;
}
