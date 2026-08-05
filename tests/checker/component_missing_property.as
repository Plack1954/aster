using Aster.Html;

private Html Card(string title) {
    return <h2>{title}</h2>;
}

int main() {
    Html card = <Card />;
    return 0;
}
