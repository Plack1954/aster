using Aster.Html;

private Html card(bool showBadge) {
    string label = "owned & safe";
    return <section class="a\"&<b>">
        <h2>{label}</h2>
        if (showBadge) {
            <strong>Administrator</strong>
        }
        {Html.UnsafeRaw("<em>trusted</em>")}
        <a href=Url.fragment("")>Open</a>
    </section>;
}

private Html choose(bool early) {
    return <div>
        if (early) {
            return <p>early</p>;
        }
        <p>late</p>
    </div>;
}

int main() {
    Html original = card(true);
    Html duplicate = copy(original);
    Console.WriteLine(duplicate.ToHtmlString());
    Console.WriteLine(original.ToHtmlString());
    Console.WriteLine(card(false).ToHtmlString());
    Console.WriteLine(choose(true).ToHtmlString());
    Console.WriteLine(choose(false).ToHtmlString());
    return 0;
}
