using Aster.Html;
using System.Text;

private struct Article {
    string title;
}

private Html ArticleCard(Article article) {
    return <article><h2>{article.title}</h2></article>;
}

int main() {
    Article article = new() {
        title = "Aster & HTML",
    };
    Console.WriteLine(<ArticleCard article=article />.ToHtmlString());
    Console.WriteLine(article.title);
    return 0;
}
