public async Task<T> Task.FromResult<T>(T result)
{
    return result;
}

public async Task Task.CompletedTask()
{
    return;
}

async Task<int> main()
{
    await Task.CompletedTask();
    int value = await Task.FromResult((int)42);
    Console.WriteLine(value);
    return 0;
}
