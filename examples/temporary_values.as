int main() {
    string text = "temporary clone";
    long item = [10, 20][1];

    Console.WriteLine(text);
    Console.WriteLine(item);
    Console.WriteLine(Buffer.allocate(3).len);
    return 0;
}
