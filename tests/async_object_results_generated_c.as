private extern Task Task.Delay(int milliseconds);

using Aster.Html;

private struct PageResponse
{
    int StatusCode;
    string ContentType;
    List<string> Headers;
    Html Body;
}

private async Task<string> LoadTitleAsync()
{
    await Task.Delay(1);
    return "Aster async";
}

private async Task<List<int>> LoadNumbersAsync()
{
    await Task.Delay(1);
    List<int> values = new();
    values.Add(10);
    values.Add(20);
    return values;
}

private async Task<Html> LoadHtmlAsync(string title)
{
    await Task.Delay(1);
    return <article><h1>{title}</h1></article>;
}

private async Task<PageResponse> LoadResponseAsync()
{
    string title = await LoadTitleAsync();
    List<string> headers = new();
    headers.Add("cache-control: public");
    Html body = await LoadHtmlAsync(title);
    return new()
    {
        StatusCode = 200,
        ContentType = "text/html",
        Headers = headers,
        Body = body
    };
}

async Task<int> main()
{
    Task<string> titleTask = LoadTitleAsync();
    string titleFirst = await copy(titleTask);
    string titleSecond = await titleTask;
    Console.WriteLine(titleFirst);
    Console.WriteLine(titleSecond);

    Task<List<int>> numbersTask = LoadNumbersAsync();
    List<int> firstNumbers = await copy(numbersTask);
    firstNumbers.Add(30);
    List<int> secondNumbers = await numbersTask;
    Console.WriteLine(firstNumbers.Count);
    Console.WriteLine(secondNumbers.Count);

    Task<PageResponse> responseTask = LoadResponseAsync();
    PageResponse first = await copy(responseTask);
    first.Headers.Add("x-first: yes");
    PageResponse second = await responseTask;
    Console.WriteLine(first.StatusCode);
    Console.WriteLine(first.Headers.Count);
    Console.WriteLine(second.Headers.Count);
    Console.WriteLine(first.ContentType);
    Console.WriteLine(first.Body.ToHtmlString());
    Console.WriteLine(second.Body.ToHtmlString());
    return 0;
}
