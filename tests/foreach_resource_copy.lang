private extern NativeHandle NativeHandleOpenId(long id);
private extern long NativeHandleId(NativeHandle handle);
private extern long NativeHandleDropLog();

struct ArticleResource {
    NativeHandle handle;
}

int main() {
    {
        List<ArticleResource> articles = new();
        articles.Add(ArticleResource {
            handle: NativeHandleOpenId(7),
        });

        foreach (ArticleResource article in articles)
        {
            Console.WriteLine(NativeHandleId(article.handle));
        }

        Console.WriteLine(articles.Count);
        Console.WriteLine(NativeHandleDropLog());
    }
    Console.WriteLine(NativeHandleDropLog());
    return 0;
}
