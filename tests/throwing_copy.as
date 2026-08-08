private struct ThrowingCopy {
    long value;

    public ThrowingCopy(const ref ThrowingCopy other) {
        if (other.value == 2) {
            throw new IOException("copy failed");
        }
        value = other.value + 10;
    }
}

~ThrowingCopy() {
    Console.WriteLine(self.value);
}

private struct CopyPair {
    ThrowingCopy first;
    ThrowingCopy second;
}

private void Consume(ThrowingCopy value) {
    Console.WriteLine(value.value);
}

int main() {
    ThrowingCopy implicitSource = new() { value = 2 };
    try {
        Consume(implicitSource);
        Console.WriteLine(implicitSource.value);
    }
    catch (IOException error) {
        Console.WriteLine(error.Message);
    }

    CopyPair pair = new() {
        first = new() { value = 1 },
        second = new() { value = 2 }
    };
    try {
        CopyPair copied = copy(pair);
        Console.WriteLine(copied.first.value);
    }
    catch (IOException error) {
        Console.WriteLine(error.Message);
    }
    Console.WriteLine(99);
    return 0;
}
