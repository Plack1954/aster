private long RangeStart() {
    Console.WriteLine(100);
    return 2;
}

private long RangeEnd() {
    Console.WriteLine(200);
    return 7;
}

int main() {
    long total = 0;
    foreach (long index in RangeStart()..RangeEnd()) {
        if (index == 3) {
            continue;
        }
        if (index == 6) {
            break;
        }
        total += index;
    }
    Console.WriteLine(total);

    nuint limit = 4;
    nuint unsignedTotal = 0;
    foreach (nuint index in 0..limit) {
        unsignedTotal += index;
    }
    Console.WriteLine(unsignedTotal);

    foreach (long ignored in 5..2) {
        Console.WriteLine(ignored);
    }

    byte narrowStart = 254;
    byte narrowEnd = 255;
    foreach (byte value in narrowStart..narrowEnd) {
        Console.WriteLine(value);
    }
    return 0;
}
