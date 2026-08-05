using Aster.Html;

private string marker() {
    return "Called";
}

private Html panel(bool show, string names[2]) {
    return <section>
        if (show) {
            <strong>Visible</strong>
        }
        <ul>
            foreach (string name in names) {
                <li>{name}</li>
            }
        </ul>
        {
            string scoped = "Scoped";
            <p>{scoped}</p>
        }
        {marker()}
    </section>;
}

int main() {
    string names[2] = ["Ada", "Lin"];
    Html content = panel(true, names);
    Console.WriteLine(content.ToHtmlString());
    return 0;
}
