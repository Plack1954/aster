using Aster.Html;

private Html Card(string title) {
    return <article class="card">
        <style scoped>
            .card,
            .panel:hover {
                display: grid;
            }

            .card > .title::before {
                content: "Scoped: ";
            }

            @media (width < 48rem) {
                .card { display: block; }
            }

            @keyframes reveal {
                from { opacity: 0; }
                50% { opacity: 0.5; }
                to { opacity: 1; }
            }
        </style>
        <h2 class="title">{title}</h2>
    </article>;
}

private Html Plain() {
    return <aside class="card">
        <style>.card { color: aster; }</style>
        {"Plain"}
    </aside>;
}

private Html ManyCards() {
    return <section>
        foreach (long ignored in 0..20) {
            <Card title="Repeated" />
        }
    </section>;
}

int main() {
    Html first = <Card title="Aster" />;
    Html second = <Card title="Second" />;
    Html page = <main>
        {first}
        {second}
        <ManyCards />
        <Plain />
    </main>;
    Console.WriteLine(page.ToHtmlString());
    return 0;
}
