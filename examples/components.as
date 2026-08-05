using Aster.Html;

private Html UserCard(string name, bool admin) {
    return <div class="user-card">
        <h2>{name}</h2>
        if (admin) {
            <strong>Administrator</strong>
        }
    </div>;
}

private Html page() {
    return <section>
        <UserCard name="Ada" admin=true />
        <UserCard name="Lin" admin=false />
    </section>;
}

int main() {
    Html output = page();
    Console.WriteLine(output.ToHtmlString());
    return 0;
}
