private String record(long index, bool active) {
    return $"customer-{index}:active={active}:balance={index * 3}";
}

int main() {
    Console.WriteLine(record(0, true));

    nuint total = 0;
    long index = 0;
    bool active = true;
    while (index < 300000) {
        String value = record(index, active);
        total = total + TextLen(value);
        active = !active;
        index = index + 1;
    }
    Console.WriteLine(total);
    return 0;
}
