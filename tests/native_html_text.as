using Aster.Html;

private Html page(string title, bool show) {
    return <main>
        <h1>{title}</h1>
        <p>Benchmark content & escaping.</p>
        <p>Comparison: 2 < 3 > 1.</p>
        <p>Hello <strong>Aster</strong> world.</p>
        <p>Hello, {title}. You have {2} open issues.</p>
        <p>
            Multiple
            words.
        </p>
        // Formatting comments are Aster trivia, not output.
        <p>#1 — okay?</p>
        <p>Unicode: λ 😀 café.</p>
        <p>if only static prose remains text.</p>
        if (show) {
            <span>Shown</span>
        }
        <code>{"if (condition) { ... }"}</code>
        <p>"quotes" remain text; {title}#1.</p>
        <script>{"if (1 < 2 && 3 > 2) { console.log(\"aster\"); }"}</script>
    </main>;
}

int main() {
    Console.WriteLine(page("A&B", true).ToHtmlString());
    return 0;
}
