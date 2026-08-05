using Aster.Html;

struct PageData
{
    List<int> Values;
}

private Buffer MakeBuffer()
{
    Buffer result = Buffer.allocate(64);
    return result;
}

private List<int> MakeList()
{
    List<int> result = new();
    result.Add(7);
    return result;
}

private Html MakeHtml()
{
    Html result = <strong>Aster</strong>;
    return result;
}

private PageData MakeStruct()
{
    PageData result = new() { Values = MakeList() };
    return result;
}

int main()
{
    Buffer buffer = MakeBuffer();
    List<int> values = MakeList();
    Html html = MakeHtml();
    PageData data = MakeStruct();
    Console.WriteLine(values.Count);
    Console.WriteLine(html.ToHtmlString());
    Console.WriteLine(data.Values.Count);
    return 0;
}
