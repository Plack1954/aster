using Aster.Html;

private Html document(string javascript) {
    return <>
        <doctype />
        <html lang="en">
            <head>
                <meta charset="utf-8" />
                <meta name="viewport" content="width=device-width" />
                <link
                    rel="preload"
                    href=Url.relative("/hero.webp")
                    as="image"
                    fetchpriority="high"
                />
                <style>.card > .title { color: aster; }</style>
                <script type="module">{javascript}</script>
            </head>
            <body itemscope=true itemtype="https://schema.org/WebPage">
                <main>
                    <figure>
                        <picture>
                            <source
                                srcset="/hero.webp"
                                type="image/webp"
                            />
                            <img
                                src=Url.relative("/hero.jpg")
                                alt="Aster grove"
                                width=800
                                height=450
                                decoding="async"
                            />
                        </picture>
                        <figcaption>
                            Measured with <var>Aster</var>
                        </figcaption>
                    </figure>
                    <form method="post" novalidate=true>
                        <fieldset>
                            <legend>Settings</legend>
                            <label for="amount">Amount</label>
                            <input
                                id="amount"
                                name="amount"
                                type="number"
                                min="1"
                                max="10"
                                step="1"
                                autofocus=true
                            />
                            <meter value=0.5 min=0.0 max=1.0>
                                50%
                            </meter>
                        </fieldset>
                    </form>
                    <table>
                        <caption>Results</caption>
                        <thead><tr><th scope="col">Name</th></tr></thead>
                        <tbody><tr><td colspan=2>Aster</td></tr></tbody>
                        <tfoot><tr><td>Done</td></tr></tfoot>
                    </table>
                    <details name="notes" open=true>
                        <summary>Notes</summary>
                        <p>Native typed HTML</p>
                    </details>
                </main>
            </body>
        </html>
    </>;
}

int main() {
    string javascript = "if (1 < 2 && 3 > 2) console.log('aster');";
    Console.WriteLine(document(javascript).ToHtmlString());
    return 0;
}
