int main() {
    Buffer value = Buffer.allocate(8);
    if (true) {
        Buffer consumed = copy(value);
        Console.WriteLine(consumed.len);
    }
    Console.WriteLine(value.len);
    return 0;
}
