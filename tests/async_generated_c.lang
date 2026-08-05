private extern Task Task.Delay(int milliseconds);

private async Task<int> CalculateAsync(int value)
{
    await Task.Delay(2);
    return value + 1;
}

async Task<int> main()
{
    Task<int> leftTask = CalculateAsync(20);
    Task<int> rightTask = CalculateAsync(20);
    int left = await leftTask;
    int right = await rightTask;
    Console.WriteLine(left + right);
    return 0;
}
