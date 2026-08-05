using Aster.Html;

int main() {
    Html stylesheet = <style>
        .card {
            color: aster;
    </style>;
    Console.WriteLine(stylesheet.ToHtmlString());
    return 0;
}
