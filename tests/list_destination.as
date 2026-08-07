using System.Text;

private union Badge
{
    None,
    Text(string),
}

private struct Article
{
    string title;
    List<string> tags;
    Badge badge;
    int views;
}

int main()
{
    List<Article> articles = new();
    articles.Add(new()
    {
        title = "Direct",
        tags = new(),
        badge = Badge.None,
        views = 7
    });

    List<string> tags = new();
    tags.Add("aster");
    Article reusable = new()
    {
        title = "Reusable",
        tags = tags,
        badge = Badge.Text("Featured"),
        views = 11
    };
    articles.Add(reusable);

    Console.WriteLine(reusable.title);
    Console.WriteLine(reusable.tags.Count);
    Console.WriteLine(articles.Count);
    foreach (Article article in articles)
    {
        Console.WriteLine(article.title);
        Console.WriteLine(article.tags.Count);
        Console.WriteLine(article.views);
    }
    return 0;
}
