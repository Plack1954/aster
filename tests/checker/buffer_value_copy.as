int main() {
    Buffer buffer = Buffer.allocate(16);
    Buffer other = copy(buffer);
    Console.WriteLine(buffer.len);
    return 0;
}
