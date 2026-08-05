using Aster.Html;

private Html Card(string accent, int gap) {
    return <article class="card" --accent=accent --gap=gap>
        <style scoped>
            .card {
                color: var(--accent);
                gap: var(--gap);
            }
        </style>
        Aster
    </article>;
}

int main() {
    Console.WriteLine(<Card accent="#e45b20" gap=12 />.ToHtmlString());
    return 0;
}
