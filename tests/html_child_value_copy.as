using Aster.Html;

int main() {
    Html heading = <h2>Title</h2>;
    Html page = <section>{copy(heading)}</section>;
    Console.WriteLine(heading.ToHtmlString());
    Console.WriteLine(page.ToHtmlString());
    return 0;
}
