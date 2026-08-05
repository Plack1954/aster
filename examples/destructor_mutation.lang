struct Counter {
    long value;
}

~Counter() {
    self.value = self.value + 1;
    Console.WriteLine(self.value);
}

int main() {
    {
        Counter counter = new() { value = 41 };
    }
    return 0;
}
