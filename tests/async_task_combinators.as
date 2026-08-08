private extern Task Task.Delay(int milliseconds);

private async Task<int> FinishAfter(int milliseconds, int value)
{
    await Task.Delay(milliseconds);
    return value;
}

private async Task WorkAfter(int milliseconds)
{
    await Task.Delay(milliseconds);
}

async Task<int> main()
{
    List<Task<int>> values = new();
    values.Add(FinishAfter(40, 40));
    values.Add(FinishAfter(1, 10));
    values.Add(FinishAfter(20, 20));

    Task<int> first = await Task.WhenAny(copy(values));
    Console.WriteLine(await first);

    List<int> results = await Task.WhenAll(values);
    foreach (int result in results)
    {
        Console.WriteLine(result);
    }

    List<Task> work = new();
    work.Add(WorkAfter(2));
    work.Add(WorkAfter(1));
    await Task.WhenAll(work);
    Console.WriteLine(99);

    List<Task<int>> empty = new();
    List<int> emptyResults = await Task.WhenAll(empty);
    Console.WriteLine(emptyResults.Count);
    return 0;
}
