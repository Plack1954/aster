private extern Task Task.Delay(int milliseconds);

private struct Tracked
{
    int Value;
}

~Tracked()
{
    Console.WriteLine("cleaned");
}

private async Task<int> FailAsync()
{
    Tracked value = new() { Value = 1 };
    await Task.Delay(1);
    throw new IOException("boom");
}

async Task<int> main()
{
    try
    {
        await FailAsync();
    }
    catch (IOException error)
    {
        Console.WriteLine(error.Message);
    }
    return 0;
}
