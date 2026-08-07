private struct Article
{
    int Views;
}

private struct ArticleSet
{
    List<Article> Items;
}

int main()
{
    List<Article> articles = new();
    articles.Add(new() { Views = 1 });

    foreach (Article article in articles)
    {
        article.Views = 99;
        Console.WriteLine(article.Views);
    }

    foreach (Article article in articles)
    {
        Console.WriteLine(article.Views);
    }

    Article fixedArticles[1] = [new() { Views = 2 }];
    foreach (Article article in fixedArticles)
    {
        article.Views = 88;
        Console.WriteLine(article.Views);
    }
    Console.WriteLine(fixedArticles[0].Views);

    ArticleSet set = new() { Items = articles };
    foreach (Article article in set.Items)
    {
        Console.WriteLine(article.Views);
    }
    return 0;
}
