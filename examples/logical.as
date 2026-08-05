private bool touch(long marker) {
    Console.WriteLine(marker);
    return true;
}

int main() {
    if (false && touch(1)) {
        Console.WriteLine(1);
    }
    if (true || touch(2)) {
        Console.WriteLine(3);
    }
    if (true && touch(4)) {
        Console.WriteLine(5);
    }
    if (false || touch(6)) {
        Console.WriteLine(7);
    }

    Console.WriteLine(8);
    return 0;
}
