using Aster.Html;

private Html Panel(string title, Html children) {
    return <section class="panel">
        <h2>{title}</h2>
        {children}
    </section>;
}

private Html page(bool showExtra, string names[2]) {
    return <Panel title="People">
        if (showExtra) {
            <strong>Featured</strong>
        }
        <ul>
            foreach (string name in names) {
                <li>{name}</li>
            }
        </ul>
    </Panel>;
}

int main() {
    string names[2] = ["Ada", "Lin"];
    Html output = page(true, names);
    Console.WriteLine(output.ToHtmlString());
    return 0;
}
