using System.Text;

private struct Article {
    string title;
}

int main() {
    List<Article> articles = new();
    articles.Add(new Article {
        title = "First",
    });
    articles.Add(new Article {
        title = "Second",
    });

    foreach (Article article in articles)
    {
        Console.WriteLine(article.title);
    }

    Console.WriteLine(articles.Count);
    return 0;
}
