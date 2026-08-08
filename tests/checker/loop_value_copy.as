int main() {
    Buffer value = Buffer.allocate(8);
    while (true) {
        Buffer consumed = copy(value);
        Console.WriteLine(consumed.len);
    }
    return 0;
}
