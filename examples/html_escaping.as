using Aster.Html;

int main() {
    Html document =
        <section class="a\"&<b>">
            "quoted" & 2 < 3 > 1
            <p>&larr; Aster&apos;s entities &rarr;</p>
        </section>;
    Console.WriteLine(document.ToHtmlString());
    return 0;
}
