using Aster.Html;
using System.Text;

private Html Card(long index)
{
    string title = $"Item {index}";
    string body = $"Value <{index * 3}> & ready";
    return <article class="card"><h2>{title}</h2><p>{body}</p></article>;
}

int main()
{
    nuint total = 0;
    for (long i = 0; i < 100000; i++)
    {
        string rendered = Card(i).ToHtmlString();
        total += rendered.Length;
    }
    Console.WriteLine(total);
    return 0;
}
