private extern Task Task.Delay(int milliseconds);
private extern Task Task.Delay(
    int milliseconds,
    CancellationToken cancellationToken
);

async Task<int> main()
{
    CancellationTokenSource source = new();
    CancellationToken token = source.Token;
    if (token.IsCancellationRequested) { Console.WriteLine("true"); }
    else { Console.WriteLine("false"); }

    Task delayed = Task.Delay(1000, token);
    source.Cancel();
    if (source.IsCancellationRequested) { Console.WriteLine("true"); }
    else { Console.WriteLine("false"); }

    try
    {
        await delayed;
        Console.WriteLine("not canceled");
    }
    catch (OperationCanceledException error)
    {
        Console.WriteLine(error.Message);
    }

    try
    {
        token.ThrowIfCancellationRequested();
    }
    catch (OperationCanceledException error)
    {
        Console.WriteLine(error.Message);
    }

    CancellationToken none = CancellationToken.None;
    if (none.IsCancellationRequested) { Console.WriteLine("true"); }
    else { Console.WriteLine("false"); }
    await Task.Delay(1, none);
    Console.WriteLine("completed");

    CancellationTokenSource combinedSource = new();
    CancellationToken combinedToken = combinedSource.Token;
    List<Task> work = new();
    work.Add(Task.Delay(1000, combinedToken));
    work.Add(Task.Delay(1));
    combinedSource.Cancel();
    try
    {
        await Task.WhenAll(work);
        Console.WriteLine("not combined canceled");
    }
    catch (OperationCanceledException error)
    {
        Console.WriteLine("combined canceled");
    }
    return 0;
}
