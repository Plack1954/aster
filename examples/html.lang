using Aster.Html;

private Html page() {
    return <section>
        <div>
            <img src=Url.relative("image.jpg") alt="Example & image" />
            <div>
                <h2>Section Title</h2>
                <p>{"Some <simple> text goes here."}</p>
                <a href=Url.fragment("")>
                    Learn more
                </a>
            </div>
        </div>
    </section>;
}

int main() {
    Html pageHtml = page();
    Console.WriteLine(pageHtml.ToHtmlString());
    return 0;
}
