private extern Task Task.Delay(int milliseconds);

private struct Tracked
{
    int Value;
}

~Tracked()
{
    Console.WriteLine("dropped");
}

async Task<int> main()
{
    Tracked kept = new() { Value = 42 };
    int dead = 7;
    Console.WriteLine(dead);
    await Task.Delay(1);
    Console.WriteLine(kept.Value);
    return 0;
}
