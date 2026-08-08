private struct SelfCopy {
    long value;

    public SelfCopy(const ref SelfCopy other) {
        Console.WriteLine(500);
        value = other.value;
    }
}

private struct SelfBox {
    SelfCopy item;
}

int main() {
    SelfCopy value = new() { value = 7 };
    value = value;

    SelfBox box = new() { item = new() { value = 8 } };
    box.item = box.item;

    SelfCopy values[1] = [new() { value = 9 }];
    values[0] = values[0];

    Console.WriteLine(value.value);
    Console.WriteLine(box.item.value);
    Console.WriteLine(values[0].value);
    return 0;
}
