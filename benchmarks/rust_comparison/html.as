using Aster.Html;

private Html Card(long id, string title, bool active) {
    <article class="card" data-id=$"{id}">
        <h2>{title}</h2>
        <p>Customer #{id} is {active ? "active" : "paused"}.</p>
    </article>
}

int main() {
    Console.WriteLine(
        <Card id=0 title="A&B <Aster>" active=true />
    .ToHtmlString());

    nuint total = 0;
    long index = 0;
    bool active = true;
    while (index < 200000) {
        String rendered =
            <Card
                id=index
                title="A&B <Aster>"
                active=active
            />
        .ToHtmlString();
        total = total + TextLen(rendered);
        active = !active;
        index = index + 1;
    }
    Console.WriteLine(total);
    return 0;
}
