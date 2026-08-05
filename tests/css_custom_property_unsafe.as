using Aster.Html;

private Html Card(string accent) {
    return <article --accent=accent>Aster</article>;
}

int main() {
    Console.WriteLine(<Card accent="red;display:none" />.ToHtmlString());
    return 0;
}
