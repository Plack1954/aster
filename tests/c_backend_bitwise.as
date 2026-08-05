int main() {
    byte flags = 0b1100;
    if ((flags & 0b1010) != 0b1000) {
        return 1;
    }
    if ((flags | 0b0011) != 0b1111) {
        return 2;
    }
    if ((flags ^ 0b1010) != 0b0110) {
        return 3;
    }
    if (~flags != 0b11110011) {
        return 4;
    }

    flags &= 0b1110;
    flags ^= 0b0110;
    flags |= 0b0001;
    if (flags != 11) {
        return 5;
    }

    sbyte signed = -16;
    if (~signed != 15) {
        return 6;
    }
    if ((0b0011 | 0b0100 & 0b0110) != 0b0111) {
        return 7;
    }
    Console.WriteLine(1);
    return 0;
}
