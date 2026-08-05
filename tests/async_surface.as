private extern Task<int> LoadNumberAsync();

private async Task<int> AddOneAsync()
{
    int value = await LoadNumberAsync();
    return value + 1;
}

private async Task PrintLaterAsync()
{
    await LoadNumberAsync();
}

int main()
{
    return 0;
}
