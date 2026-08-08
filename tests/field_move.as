private struct DropProbe
{
    string name;

    public DropProbe(const ref DropProbe other)
    {
        name = copy(other.name);
    }

}

~DropProbe()
{
    Console.WriteLine($"drop {self.name}");
}

private struct Envelope
{
    DropProbe remainder;
    string payload;
}

int main()
{
    Envelope envelope = new()
    {
        remainder = new() { name = "remainder" },
        payload = "payload"
    };
    string payload = envelope.payload;
    Console.WriteLine(payload);
    return 0;
}
