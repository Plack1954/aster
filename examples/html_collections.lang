using Aster.Html;

private Option<Html> MaybeBadge(bool show) {
    if (show) {
        Option<Html> some =
            Option.Some(<strong>Optional</strong>);
        return some;
    }
    Option<Html> none = Option.None;
    return none;
}

int main() {
    List<Html> rows = new();
    rows.Add(<li>Ada</li>);
    rows.Add(<li>Lin</li>);

    Html document = <section>
        {MaybeBadge(true)}
        {[<h2>Array A</h2>, <h2>Array B</h2>]}
        <ul>{rows}</ul>
    </section>;
    Console.WriteLine(document.ToHtmlString());
    return 0;
}
