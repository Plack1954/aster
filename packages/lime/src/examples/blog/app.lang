namespace Examples.Blog.App;

using Lime;
using Aster.Html;
using System.Text;

public delegate Html PostBody();

public struct Post
{
    string title;
    string slug;
    string published;
    string publishedIso;
    string feedDate;
    string summary;
    PostBody body;
}

public struct Blog
{
    string title;
    string description;
    string baseUrl;
    List<Post> posts;
}

private Html FirstPost()
{
    return <>
        <p>This is the first post.</p>
        <p>Replace it with your own writing.</p>
    </>;
}

public Blog BlogNew()
{
    List<Post> posts = new();
    posts.Add(new()
    {
        title = "First post",
        slug = "first-post",
        published = "2 August 2026",
        publishedIso = "2026-08-02",
        feedDate = "Sun, 02 Aug 2026 00:00:00 +1000",
        summary = "The first entry.",
        body = FirstPost
    });
    return new()
    {
        title = "Blog",
        description = "Writing and notes.",
        baseUrl = "https://your-domain.example",
        posts = posts
    };
}

private Html Layout(
    Blog blog,
    string pageTitle,
    string pageDescription,
    Html content
)
{
    return <>
        <doctype />
        <html lang="en">
            <head>
                <meta charset="utf-8" />
                <meta name="viewport" content="width=device-width" />
                <meta name="description" content=pageDescription />
                <title>{pageTitle}</title>
                <link
                    rel="alternate"
                    type="application/rss+xml"
                    title=blog.title
                    href=Url.relative("/feed.xml")
                />
                <style>
                    :root {
                        color-scheme: light;
                        font-family: Georgia, serif;
                        line-height: 1.65;
                    }
                    * { box-sizing: border-box; }
                    body {
                        margin: 0;
                        color: #24211d;
                        background: #fbfaf7;
                    }
                    body > header,
                    body > main,
                    body > footer {
                        width: min(46rem, calc(100% - 2rem));
                        margin-inline: auto;
                    }
                    body > header {
                        display: flex;
                        justify-content: space-between;
                        align-items: baseline;
                        gap: 1rem;
                        padding-block: 1.5rem;
                        border-bottom: 1px solid #d9d4ca;
                    }
                    nav { display: flex; gap: 1rem; }
                    main { min-height: 65vh; padding-block: 4rem; }
                    article { max-width: 40rem; }
                    article header { margin-bottom: 2rem; }
                    h1, h2 { line-height: 1.15; }
                    h1 { font-size: clamp(2rem, 7vw, 3.75rem); }
                    a { color: #934116; text-underline-offset: 0.18em; }
                    .brand { color: inherit; font-weight: 700; }
                    .description { color: #686158; font-size: 1.1rem; }
                    .post-list {
                        list-style: none;
                        margin: 3rem 0;
                        padding: 0;
                    }
                    .post-list li {
                        padding-block: 1.5rem;
                        border-top: 1px solid #d9d4ca;
                    }
                    .post-list h2 { margin: 0 0 0.35rem; }
                    time, .summary { color: #686158; }
                    footer {
                        display: flex;
                        justify-content: space-between;
                        padding-block: 2rem;
                        border-top: 1px solid #d9d4ca;
                    }
                </style>
            </head>
            <body>
                <header>
                    <a class="brand" href=Url.relative("/")>
                        {blog.title}
                    </a>
                    <nav aria-label="Primary">
                        <a href=Url.relative("/blog/")>Posts</a>
                        <a href=Url.relative("/about/")>About</a>
                    </nav>
                </header>
                <main>{content}</main>
                <footer>
                    <span>{blog.title}</span>
                    <a href=Url.relative("/feed.xml")>RSS</a>
                </footer>
            </body>
        </html>
    </>;
}

private Html PostCard(Post post)
{
    string path = $"/blog/{post.slug}/";
    return <li>
        <article>
            <h2>
                <a href=Url.relative(path)>
                    {post.title}
                </a>
            </h2>
            <time datetime=post.publishedIso>
                {post.published}
            </time>
            <p class="summary">{post.summary}</p>
        </article>
    </li>;
}

private Html PostList(List<Post> posts)
{
    return <ul class="post-list">
        foreach (Post post in posts)
        {
            <PostCard post=post />
        }
    </ul>;
}

private Response home(Blog blog, Request request)
{
    return Response.Ok(Layout(
        blog,
        blog.title,
        blog.description,
        <>
            <h1>{blog.title}</h1>
            <p class="description">{blog.description}</p>
            <PostList posts=blog.posts />
        </>
    ));
}

private Response posts(Blog blog, Request request)
{
    return Response.Ok(Layout(
        blog,
        "Posts — Blog",
        "All posts.",
        <>
            <h1>Posts</h1>
            <PostList posts=blog.posts />
        </>
    ));
}

private Response about(Blog blog, Request request)
{
    return Response.Ok(Layout(
        blog,
        "About — Blog",
        "About the author.",
        <article>
            <h1>About</h1>
            <p>Replace this page with information about the author.</p>
        </article>
    ));
}

private Response PostResponse(List<Post> posts, string slug, Blog blog)
{
    foreach (Post post in posts)
    {
        if (post.slug == slug)
        {
            PostBody body = post.body;
            string pageTitle = $"{post.title} — {blog.title}";
            return Response.Ok(Layout(
                blog,
                pageTitle,
                post.summary,
                <article>
                    <header>
                        <h1>{post.title}</h1>
                        <time datetime=post.publishedIso>
                            {post.published}
                        </time>
                    </header>
                    {body()}
                </article>
            ));
        }
    }
    return Response.NotFound(Layout(
        blog,
        "Not found — Blog",
        "Page not found.",
        <h1>Not found</h1>
    ));
}

private Response post(Blog blog, Request request)
{
    return PostResponse(blog.posts, request.param("slug"), blog);
}

private void XmlAppend(ref StringBuilder output, string value)
{
    for (nuint index = 0; index < value.Length; index++)
    {
        byte current = StringByteAt(value, index);
        if (current == 38) { output.Append("&amp;"); }
        else if (current == 60) { output.Append("&lt;"); }
        else if (current == 62) { output.Append("&gt;"); }
        else if (current == 34) { output.Append("&quot;"); }
        else if (current == 39) { output.Append("&apos;"); }
        else { output.AppendByte(current); }
    }
}

private void AppendFeedItems(
    ref StringBuilder output,
    string baseUrl,
    List<Post> posts
)
{
    foreach (Post post in posts)
    {
        output.Append("<item><title>");
        XmlAppend(output, post.title);
        output.Append("</title><link>");
        XmlAppend(output, baseUrl);
        output.Append("/blog/");
        XmlAppend(output, post.slug);
        output.Append("/</link><guid>");
        XmlAppend(output, baseUrl);
        output.Append("/blog/");
        XmlAppend(output, post.slug);
        output.Append("/</guid><pubDate>");
        XmlAppend(output, post.feedDate);
        output.Append("</pubDate><description>");
        XmlAppend(output, post.summary);
        output.Append("</description></item>");
    }
}

private string FeedXml(Blog blog)
{
    StringBuilder output = new();
    output.Append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    output.Append("<rss version=\"2.0\"><channel><title>");
    XmlAppend(output, blog.title);
    output.Append("</title><link>");
    XmlAppend(output, blog.baseUrl);
    output.Append("/</link><description>");
    XmlAppend(output, blog.description);
    output.Append("</description>");
    AppendFeedItems(output, blog.baseUrl, blog.posts);
    output.Append("</channel></rss>");
    return output.ToString();
}

private Response feed(Blog blog, Request request)
{
    return Response.Xml(FeedXml(blog));
}

private Response robots(Blog blog, Request request)
{
    return Response.Text("User-agent: *\nAllow: /\n");
}

private Response missing(Blog blog, Request request)
{
    return Response.NotFound(Layout(
        blog,
        "Not found — Blog",
        "Page not found.",
        <h1>Not found</h1>
    ));
}

private List<string> PostPagePathsFrom(List<Post> posts)
{
    List<string> paths = new();
    foreach (Post post in posts)
    {
        paths.Add($"/blog/{post.slug}/");
    }
    return paths;
}

private List<string> PostPagePaths(Blog blog)
{
    return PostPagePathsFrom(blog.posts);
}

public Result<StatefulApp<Blog>, string> CreateApp()
{
    Blog blog = BlogNew();
    StatefulApp<Blog> app = StatefulAppNew(blog, missing);
    app.Get("/", home);
    app.Get("/blog/", posts);
    app.Get("/blog/:slug/", post, PostPagePaths);
    app.Get("/about/", about);
    app.Get("/feed.xml", feed);
    app.Get("/robots.txt", robots);
    return Result.Ok(app);
}
