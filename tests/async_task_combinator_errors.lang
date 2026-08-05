private extern Task Task.Delay(int milliseconds);

private async Task<int> FailAfter(int milliseconds)
{
    await Task.Delay(milliseconds);
    throw new IOException("failed input");
}

private async Task<int> FinishAndReport(int milliseconds)
{
    await Task.Delay(milliseconds);
    Console.WriteLine("settled");
    return 7;
}

async Task<int> main()
{
    List<Task<int>> tasks = new();
    tasks.Add(FailAfter(1));
    tasks.Add(FinishAndReport(8));

    try
    {
        List<int> ignored = await Task.WhenAll(tasks);
        Console.WriteLine(ignored.Count);
    }
    catch (IOException error)
    {
        Console.WriteLine(error.Message);
    }

    List<Task<int>> empty = new();
    try
    {
        Task<int> impossible = await Task.WhenAny(empty);
        Console.WriteLine(await impossible);
    }
    catch (ArgumentException error)
    {
        Console.WriteLine(error.Message);
    }
    return 0;
}
