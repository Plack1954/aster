private struct Article
{
    int Id;
    bool Published;
}

public Article MakeArticle()
{
    Article article = new()
    {
        Id = 7,
        Published = true
    };
    return article;
}

int main()
{
    Article article = MakeArticle();
    Article backup = article;
    Console.WriteLine(article.Id);
    Console.WriteLine(backup.Id);

    string title = "Aster";
    string copiedTitle = title;
    Console.WriteLine(title);
    Console.WriteLine(copiedTitle);

    var status = 0;
    Console.WriteLine(status);
    return 0;
}
