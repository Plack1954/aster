using Aster.Html;

int main() {
    Html document =
        <img
            src=Url.relative("image.jpg")
            alt="image"
        >
            not allowed
        </img>;
    Console.WriteLine(document.ToHtmlString());
    return 0;
}
