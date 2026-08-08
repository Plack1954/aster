using Aster.Html;

private Html Render(string value, int iteration) {
    return <main><h1>Template benchmark</h1><p>{value}</p><span>{iteration}</span></main>;
}

int main() {
    string value = "A&B <tag> / unicode: Melbourne";
    long bytes = 0;
    for (int warmup = 0; warmup < 1000; warmup++) {
        string output = Render(value, warmup).ToHtmlString();
        bytes += (long)output.Length;
    }
    bytes = 0;
    for (int iteration = 0; iteration < 250000; iteration++) {
        string output = Render(value, iteration).ToHtmlString();
        bytes += (long)output.Length;
    }
    Console.WriteLine(bytes);
    return 0;
}
