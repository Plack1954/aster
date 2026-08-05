using Aster.Html;

element Html notice {
    string tone;
    Html children;
}

int main() {
    Html document = <notice tone="warning">
        <strong>Careful</strong>
    </notice>;
    string output = document.ToHtmlString();
    Console.WriteLine(output);
    return 0;
}
